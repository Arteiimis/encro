#include "utils/utils.h"

#include "infra/terminal.h"
#include "infra/stop_signal.h"
#include "logging/log_tags.h"
#include "logging/logging.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/asio/writable_pipe.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/shell.hpp>
#include <boost/process/v2/stdio.hpp>
#include <boost/uuid.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <thread>

DEFINE_LOGGER(logtags::UTILS_SUBPROCESS);

using enum terminal::MessageKind;

namespace {

auto exec2Impl(
  std::string_view cmd,
  std::function<void(std::string_view)> const* onLine,
  bool mergeStdErr
) -> ExecResult {
  namespace bp = boost::process::v2;
  namespace asio = boost::asio;
  using namespace std::chrono_literals;

  constexpr auto kTerminateWaitTimeout = 500ms;
  constexpr auto kReaderWaitTimeout = 250ms;
  constexpr auto kPollInterval = 20ms;

  LOG_DEBUG("Executing command: {}", cmd);

  auto ctx = asio::io_context{};
  auto command = bp::shell{boost::string_view{cmd.data(), cmd.size()}};

  // One pipe for the child's output; stdout and stderr share its write end so
  // merged output keeps its natural interleaving.
  auto pipeReader = asio::readable_pipe{ctx};
  auto pipeWriter = asio::writable_pipe{ctx};
  asio::connect_pipe(pipeReader, pipeWriter);
  auto const writeEnd = pipeWriter.native_handle();

  auto stdio = mergeStdErr ? bp::process_stdio{.out = writeEnd, .err = writeEnd}
                           : bp::process_stdio{.out = writeEnd, .err = nullptr};

#if defined(_WIN32)
  // Windows CreateProcess resolves the exe from the command line when the
  // application name is empty, so keep the stock shell exe() resolution.
  auto process = bp::process{ctx, command.exe(), command.args(), std::move(stdio)};
#else
  // boost::process v2's posix find_executable cannot resolve absolute paths
  // (boost::filesystem appends instead of replacing), leaving an empty exe and
  // execve("") ENOENT; the parsed argv[0] token is the correct program name.
  auto const* exeToken = command.argv()[0];
  auto exePath = bp::environment::find_executable(exeToken);
  if (exePath.empty()) { exePath = exeToken; }
  auto process = bp::process{ctx, exePath, command.args(), std::move(stdio)};
#endif
  auto const capturedPid = static_cast<int>(process.id());

  // The parent must not keep a write end open, or the reader never sees EOF.
  boost::system::error_code pipeCloseEc;
  pipeWriter.close(pipeCloseEc);

  auto onLineCopy = onLine != nullptr ? *onLine : std::function<void(std::string_view)>{};
  auto callbackEnabled = std::make_shared<std::atomic<bool>>(true);
  // Heap-held promise/pipe: the reader thread may outlive this function on the
  // stop-request detach path; stack storage would be use-after-return.
  auto outputPromise = std::make_shared<std::promise<std::string>>();
  auto outputFuture = outputPromise->get_future();
  auto pipeReaderShared = std::make_shared<asio::readable_pipe>(std::move(pipeReader));

  auto pipeReaderThread = std::thread(
    [&, pipeReaderShared, outputPromise, onLineCopy = std::move(onLineCopy)]() mutable {
    auto result = std::string{};
    auto pendingLine = std::string{};
    auto buffer = std::array<char, 4096>{};

    try {
      for (;;) {
        boost::system::error_code ec;
        auto const count = pipeReaderShared->read_some(asio::buffer(buffer), ec);
        if (ec || count == 0) { break; }

        auto const chunk = std::string_view{buffer.data(), count};
        result.append(chunk);
        pendingLine.append(chunk);

        auto newlinePos = pendingLine.find('\n');
        while (newlinePos != std::string::npos) {
          auto line = pendingLine.substr(0, newlinePos);
          if (!line.empty() && line.back() == '\r') { line.pop_back(); }
          if (callbackEnabled->load(std::memory_order_acquire) && onLineCopy) {
            onLineCopy(line);
          }
          pendingLine.erase(0, newlinePos + 1);
          newlinePos = pendingLine.find('\n');
        }
      }

      outputPromise->set_value(std::move(result));
    } catch (...) {
      try {
        outputPromise->set_exception(std::current_exception());
      } catch (...) { }
    }
  });

  auto closePipeReader = [&] {
    boost::system::error_code ec;
    pipeReaderShared->close(ec);
  };

  auto joinReader = [&] {
    auto const result = outputFuture.get();
    if (pipeReaderThread.joinable()) { pipeReaderThread.join(); }
    return result;
  };

  auto waitForExitUntil = [&](std::chrono::steady_clock::time_point deadline) {
    while (process.running() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(kPollInterval);
    }
    return !process.running();
  };

  while (process.running()) {
    if (!stopsignal::isStopRequested()) {
      std::this_thread::sleep_for(kPollInterval);
      continue;
    }

    try {
      if (process.running()) {
        LOG_INFO("Terminating child process due to stop request: {}", cmd);
        process.terminate();
      }
    } catch (std::exception const& ex) {
      LOG_WARN(
        "Failed to terminate child process on stop request: {} ({})",
        cmd,
        ex.what()
      );
      // terminate can race the child's natural exit (same 20 ms poll); fall
      // through to the normal exit path instead of a spurious cancellation.
      if (!process.running()) { break; }
    }

    callbackEnabled->store(false, std::memory_order_release);
    closePipeReader();

    if (!waitForExitUntil(std::chrono::steady_clock::now() + kTerminateWaitTimeout)) {
      LOG_WARN(
        "Child process did not exit within {} ms after terminate, detaching handle: {}",
        kTerminateWaitTimeout.count(),
        cmd
      );
      process.detach();
    }

    if (outputFuture.wait_for(kReaderWaitTimeout) == std::future_status::ready) {
      return {stopsignal::kCanceledExitCode, joinReader(), capturedPid};
    }

    LOG_WARN(
      "Command output reader did not finish within {} ms after stop request; returning "
      "partial output: {}",
      kReaderWaitTimeout.count(),
      cmd
    );
    if (pipeReaderThread.joinable()) { pipeReaderThread.detach(); }
    return {stopsignal::kCanceledExitCode, {}, capturedPid};
  }

  process.wait();
  // Join the reader before closing: the child has exited, so its write end is
  // gone and the pipe hits EOF on its own; closing first races the reader
  // thread and can drop output the kernel still holds in the pipe buffer.
  auto const output = joinReader();
  closePipeReader();

  return {process.exit_code(), output, capturedPid};
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
  if (stopsignal::isStopRequested()) { return false; }
  if (yesToAll) { return true; }

  auto const promptShown = !prompt.empty();
  if (promptShown) { terminal::print(Prompt, "{}", prompt); }

  auto response = 'y';
  auto input = std::string{};
  if (!std::getline(std::cin, input)) {
    std::cin.clear();
    if (promptShown) { terminal::write(terminal::Stream::Stdout, "", true); }
    return false;
  }
  if (stopsignal::isStopRequested()) { return false; }
  if (!input.empty()) { std::istringstream(input) >> response; }

  return response == 'y' || response == 'Y';
}

// exec2 resolves the quoted exe token itself (posix find_executable cannot
// handle absolute paths), so quotes are safe on both platforms here.
auto probeTool(fs::path const& toolPath) -> bool {
  auto const cmd = std::format("\"{}\" -version", toolPath.string());
  return exec2(cmd).exitCode == 0;
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
      if (probeTool(entry.path())) { return entry.path(); }
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
      if (probeTool(entry.path())) { return entry.path(); }
    }
  }

  return std::nullopt;
}

std::string getUUID() {
  return boost::lexical_cast<std::string>(boost::uuids::random_generator{}());
}
