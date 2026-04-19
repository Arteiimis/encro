#include "utils/utils.h"

#include "infra/stop_signal.h"

#include <boost/lexical_cast.hpp>
#include <boost/process/v1.hpp>
#include <boost/uuid.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <iostream>
#include <print>
#include <thread>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <atomic>
  #include <future>
  #include <memory>
#endif

namespace {

auto exec2Impl(
  std::string_view cmd,
  std::function<void(std::string_view)> const* onLine,
  bool mergeStdErr
) -> ExecResult {
  namespace bp = boost::process::v1;
  using namespace std::chrono_literals;

  constexpr auto kTerminateWaitTimeout = 500ms;
#if !defined(_WIN32)
  constexpr auto kReaderWaitTimeout = 250ms;
#endif

  spdlog::debug("Executing command: {}", cmd);

#if defined(_WIN32)
  auto onLineCopy = onLine != nullptr ? *onLine : std::function<void(std::string_view)>{};
  auto outputPipe = bp::pipe{};
  auto process = mergeStdErr
    ? bp::child(cmd.data(), bp::std_out > outputPipe, bp::std_err > outputPipe)
    : bp::child(cmd.data(), bp::std_out > outputPipe, bp::std_err > bp::null);

  auto closePipeSink = [&] {
    auto const sink = outputPipe.native_sink();
    if (sink == INVALID_HANDLE_VALUE || sink == nullptr) { return; }
    ::CloseHandle(sink);
    outputPipe.assign_sink(INVALID_HANDLE_VALUE);
  };

  auto closePipeSource = [&] {
    auto const source = outputPipe.native_source();
    if (source == INVALID_HANDLE_VALUE || source == nullptr) { return; }
    ::CloseHandle(source);
    outputPipe.assign_source(INVALID_HANDLE_VALUE);
  };

  auto result = std::string{};
  auto pendingLine = std::string{};

  auto appendChunk = [&](std::string_view chunk, bool allowCallbacks) {
    result.append(chunk);
    pendingLine.append(chunk);

    auto newlinePos = pendingLine.find('\n');
    while (newlinePos != std::string::npos) {
      auto line = pendingLine.substr(0, newlinePos);
      if (!line.empty() && line.back() == '\r') { line.pop_back(); }
      if (allowCallbacks && onLineCopy) { onLineCopy(line); }
      pendingLine.erase(0, newlinePos + 1);
      newlinePos = pendingLine.find('\n');
    }
  };

  auto drainAvailableOutput = [&](bool allowCallbacks) {
    auto buffer = std::array<char, 4096>{};
    auto totalRead = std::size_t{0};

    while (true) {
      auto const source = outputPipe.native_source();
      if (source == INVALID_HANDLE_VALUE || source == nullptr) { return totalRead; }

      auto available = DWORD{0};
      if (!::PeekNamedPipe(source, nullptr, 0, nullptr, &available, nullptr)) {
        auto const error = ::GetLastError();
        if (
          error == ERROR_BROKEN_PIPE
          || error == ERROR_NO_DATA
          || error == ERROR_INVALID_HANDLE
        ) {
          return totalRead;
        }
        spdlog::debug(
          "PeekNamedPipe failed for {} with error {}",
          cmd,
          static_cast<unsigned long>(error)
        );
        return totalRead;
      }

      if (available == 0) { return totalRead; }

      auto const toRead = std::min<std::size_t>(buffer.size(), available);
      auto const readCount = outputPipe.read(buffer.data(), static_cast<int>(toRead));
      if (readCount <= 0) { return totalRead; }

      totalRead += static_cast<std::size_t>(readCount);
      appendChunk(
        std::string_view{buffer.data(), static_cast<std::size_t>(readCount)},
        allowCallbacks
      );
    }
  };

  auto waitForExitUntil = [&](std::chrono::steady_clock::time_point deadline) {
    while (process.running() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(20ms);
    }
    return !process.running();
  };

  closePipeSink();

  while (process.running()) {
    drainAvailableOutput(true);

    if (!stopsignal::isStopRequested()) {
      std::this_thread::sleep_for(20ms);
      continue;
    }

    try {
      if (process.running()) {
        spdlog::info("Terminating child process due to stop request: {}", cmd);
        process.terminate();
      }
    } catch (std::exception const& ex) {
      spdlog::warn(
        "Failed to terminate child process on stop request: {} ({})",
        cmd,
        ex.what()
      );
    }

    closePipeSource();

    if (!waitForExitUntil(std::chrono::steady_clock::now() + kTerminateWaitTimeout)) {
      spdlog::warn(
        "Child process did not exit within {} ms after terminate, detaching handle: {}",
        kTerminateWaitTimeout.count(),
        cmd
      );
      process.detach();
    }

    return {stopsignal::kCanceledExitCode, result};
  }

  process.wait();
  while (drainAvailableOutput(true) > 0) { }
  closePipeSource();

  return {process.exit_code(), result};
#else

  auto pipeStream = std::make_shared<bp::ipstream>();
  auto callbackEnabled = std::make_shared<std::atomic<bool>>(true);
  auto onLineCopy = onLine != nullptr ? *onLine : std::function<void(std::string_view)>{};
  auto process = mergeStdErr
    ? bp::child(cmd.data(), bp::std_out > *pipeStream, bp::std_err > *pipeStream)
    : bp::child(cmd.data(), bp::std_out > *pipeStream, bp::std_err > bp::null);
  auto terminatedByStop = std::atomic<bool>{false};
  auto outputPromise = std::promise<std::string>{};
  auto outputFuture = outputPromise.get_future();

  auto pipeReader = std::thread([pipeStream,
                                 callbackEnabled,
                                 onLineCopy = std::move(onLineCopy),
                                 outputPromise = std::move(outputPromise)]() mutable {
    auto line = std::string{};
    auto result = std::string{};

    try {
      while (std::getline(*pipeStream, line)) {
        if (callbackEnabled->load(std::memory_order_acquire) && onLineCopy) {
          onLineCopy(line);
        }
        std::format_to(std::back_inserter(result), "{}\n", line);
      }

      outputPromise.set_value(std::move(result));
    } catch (...) {
      try {
        outputPromise.set_exception(std::current_exception());
      } catch (...) { }
    }
  });

  auto closePipe = [&] {
    if (!pipeStream->is_open()) { return; }

    try {
      pipeStream->close();
    } catch (std::exception const& ex) {
      spdlog::debug("Failed to close command output pipe for {}: {}", cmd, ex.what());
    }
  };

  auto joinReader = [&] {
    auto result = outputFuture.get();
    if (pipeReader.joinable()) { pipeReader.join(); }
    return result;
  };

  auto waitForExitUntil = [&](std::chrono::steady_clock::time_point deadline) {
    while (process.running() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(20ms);
    }
    return !process.running();
  };

  while (process.running()) {
    if (!stopsignal::isStopRequested()) {
      std::this_thread::sleep_for(20ms);
      continue;
    }

    try {
      if (process.running()) {
        spdlog::info("Terminating child process due to stop request: {}", cmd);
        process.terminate();
        terminatedByStop.store(true, std::memory_order_release);
      }
    } catch (std::exception const& ex) {
      spdlog::warn(
        "Failed to terminate child process on stop request: {} ({})",
        cmd,
        ex.what()
      );
    }

    callbackEnabled->store(false, std::memory_order_release);
    closePipe();

    if (!waitForExitUntil(std::chrono::steady_clock::now() + kTerminateWaitTimeout)) {
      spdlog::warn(
        "Child process did not exit within {} ms after terminate, detaching handle: {}",
        kTerminateWaitTimeout.count(),
        cmd
      );
      process.detach();
    }

    if (outputFuture.wait_for(kReaderWaitTimeout) == std::future_status::ready) {
      return {stopsignal::kCanceledExitCode, joinReader()};
    }

    spdlog::warn(
      "Command output reader did not finish within {} ms after stop request; returning "
      "partial output: {}",
      kReaderWaitTimeout.count(),
      cmd
    );
    if (pipeReader.joinable()) { pipeReader.detach(); }
    return {stopsignal::kCanceledExitCode, {}};
  }

  process.wait();
  closePipe();

  return {process.exit_code(), joinReader()};
#endif
}

}  // namespace

auto exec2(std::string_view cmd) -> ExecResult {
  return exec2Impl(cmd, nullptr, true);
}

auto exec2(std::string_view cmd, std::function<void(std::string_view)> const& onLine)
  -> ExecResult {
  return exec2Impl(cmd, &onLine, true);
}

auto exec2(std::string_view cmd, bool mergeStdErr) -> ExecResult {
  return exec2Impl(cmd, nullptr, mergeStdErr);
}

auto exec2(
  std::string_view cmd,
  std::function<void(std::string_view)> const& onLine,
  bool mergeStdErr
) -> ExecResult {
  return exec2Impl(cmd, &onLine, mergeStdErr);
}

bool readUserIpt(bool yesToAll, std::string_view prompt) {
  if (yesToAll) { return true; }

  if (!prompt.empty()) { std::print("{}", prompt); }

  auto response = 'n';
  auto input = std::string{};
  std::getline(std::cin, input);
  if (!input.empty()) { std::istringstream(input) >> response; }

  return response == 'y' || response == 'Y';
}

auto findFFprobe(std::optional<fs::path> const& installDir) -> std::optional<fs::path> {
  auto const systemFFprobeAvailable = exec2("ffprobe -version").exitCode == 0;

  if (!installDir.has_value() && systemFFprobeAvailable) { return fs::path{"ffprobe"}; }

  if (!installDir.has_value() || !fs::is_directory(installDir.value())) {
    return std::nullopt;
  }

  auto pathIter = fs::recursive_directory_iterator{installDir.value()};

  for (auto const& entry: pathIter) {
    if (entry.is_regular_file() && entry.path().filename() == "ffprobe") {
      auto cmd = std::format("\"{}\" -version", entry.path().string());
      if (exec2(cmd).exitCode == 0) { return entry.path(); }
    }
  }

  return std::nullopt;
}

auto findFFmpeg(std::optional<fs::path> const& installDir) -> std::optional<fs::path> {
  auto const systemFFmpegAvailable = exec2("ffmpeg -version").exitCode == 0;

  if (!installDir.has_value() && systemFFmpegAvailable) { return fs::path{"ffmpeg"}; }

  if (!installDir.has_value() || !fs::is_directory(installDir.value())) {
    return std::nullopt;
  }

  auto pathIter = fs::recursive_directory_iterator{installDir.value()};

  for (auto const& entry: pathIter) {
    if (entry.is_regular_file() && entry.path().filename() == "ffmpeg") {
      auto cmd = std::format("\"{}\" -version", entry.path().string());
      if (exec2(cmd).exitCode == 0) { return entry.path(); }
    }
  }

  return std::nullopt;
}

std::string getUUID() {
  return boost::lexical_cast<std::string>(boost::uuids::random_generator{}());
}

auto getParamStr(
  boost::program_options::variables_map const& vm,
  std::string_view paramName
) -> std::string {
  return boost::trim_copy(vm.at(paramName.data()).as<std::string>());
}
