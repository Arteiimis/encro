#include "utils/utils.h"

#include "infra/terminal.h"
#include "infra/stop_signal.h"
#include "logging/log_tags.h"
#include "logging/logging.h"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
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
#include <optional>
#include <variant>

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::UTILS_SUBPROCESS);

using enum terminal::MessageKind;

namespace {

namespace asio = boost::asio;

// ── exec2 core: sync-over-async coroutine ──
// The public exec2 overloads block the calling thread; internally the whole
// run is a single asio coroutine driven by a function-local io_context
// (co_spawn + run() on the caller). Frames never outlive the call, so
// captures are safe. Note for crash forensics: coroutine frames appear as
// bare coroutine_handle::resume entries in stack traces.

struct ReadEof { };
struct ProcessExit { };
struct StopRequested { };

constexpr auto kTerminateWaitTimeout = std::chrono::milliseconds{500};

struct ProcessReadState {
  std::string output_;
  std::atomic<bool> callbacksEnabled_{true};
};

auto readAllInto(
  std::shared_ptr<asio::readable_pipe> pipe,
  std::shared_ptr<ProcessReadState> state,
  std::function<void(std::string_view)> const& onLine
) -> asio::awaitable<void> {
  auto pendingLine = std::string{};
  auto buffer = std::array<char, 4096>{};

  for (;;) {
    auto ec = boost::system::error_code{};
    auto const count = co_await pipe->async_read_some(
      asio::buffer(buffer),
      asio::redirect_error(asio::use_awaitable, ec)
    );
    if (ec || count == 0) { break; }

    auto const chunk = std::string_view{buffer.data(), count};
    state->output_.append(chunk);
    pendingLine.append(chunk);

    auto newlinePos = pendingLine.find('\n');
    while (newlinePos != std::string::npos) {
      auto line = pendingLine.substr(0, newlinePos);
      if (!line.empty() && line.back() == '\r') { line.pop_back(); }
      if (state->callbacksEnabled_.load(std::memory_order_acquire) && onLine) {
        onLine(line);
      }
      pendingLine.erase(0, newlinePos + 1);
      newlinePos = pendingLine.find('\n');
    }
  }
}

auto waitExit(boost::process::v2::process& process) -> asio::awaitable<ProcessExit> {
  // Timer poll instead of process.async_wait: on Windows the async wait's
  // cancellation is not reliable, and a cancelled-but-unfinished wait would
  // pin ctx.run() forever. 20 ms granularity matches the legacy poll loop.
  auto const executor = co_await asio::this_coro::executor;
  auto timer = asio::steady_timer{executor};
  while (process.running()) {
    timer.expires_after(std::chrono::milliseconds{20});
    auto ec = boost::system::error_code{};
    co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
  }
  co_return ProcessExit{};
}

auto stopWait() -> asio::awaitable<StopRequested> {
  // Timer poll on the flag: asio's windows::object_handle wait is not
  // cancellable, so awaiting the stop event here could pin ctx.run() forever
  // when this operand loses the race. 20 ms stop latency, as in the legacy
  // implementation; monitor/spinner keep the instant event wake.
  auto const executor = co_await asio::this_coro::executor;
  auto timer = asio::steady_timer{executor};
  while (!stopsignal::isStopRequested()) {
    timer.expires_after(std::chrono::milliseconds{20});
    auto ec = boost::system::error_code{};
    co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
  }
  co_return StopRequested{};
}

// Polls the child until exit or the grace deadline. Timer-based co_await
// leaves no pending async operation behind at co_return, so the caller's
// frame can be destroyed safely.
auto waitExitWithin(boost::process::v2::process& process, std::chrono::milliseconds grace)
  -> asio::awaitable<bool> {
  auto const executor = co_await asio::this_coro::executor;
  auto timer = asio::steady_timer{executor};
  auto const deadline = std::chrono::steady_clock::now() + grace;
  for (;;) {
    if (!process.running()) { co_return true; }
    if (std::chrono::steady_clock::now() >= deadline) { co_return false; }
    timer.expires_after(std::chrono::milliseconds{20});
    auto ec = boost::system::error_code{};
    co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
  }
}

// Terminates the child on a stop request. Returns the child's real exit code
// when it had already exited naturally (terminate raced the exit — the
// legacy stop-after-exit contract); returns nullopt once terminated, or
// detached after the grace period expired.
auto terminateOnStop(
  std::shared_ptr<boost::process::v2::process> process,
  std::string const& cmd
) -> asio::awaitable<std::optional<int>> {
  auto terminated = false;
  try {
    if (process->running()) {
      LOG_INFO("Terminating child process due to stop request: {}", cmd);
      process->terminate();
      terminated = true;
    }
  } catch (std::exception const& ex) {
    LOG_WARN(
      "Failed to terminate child process on stop request: {} ({})",
      cmd,
      ex.what()
    );
  }
  if (!terminated && !process->running()) { co_return process->exit_code(); }
  if (!co_await waitExitWithin(*process, kTerminateWaitTimeout)) {
    LOG_WARN(
      "Child process did not exit within {} ms after terminate, detaching "
      "handle: {}",
      kTerminateWaitTimeout.count(),
      cmd
    );
    process->detach();
  }
  co_return std::nullopt;
}

// NOLINTNEXTLINE(readability-function-size): linear 3-way coroutine race; branches are 3-9 lines each
auto runProcess(
  std::string cmd,
  std::function<void(std::string_view)> onLine,
  bool mergeStdErr
) -> asio::awaitable<ExecResult> {
  namespace bp = boost::process::v2;
  using namespace boost::asio::experimental::awaitable_operators;

  LOG_DEBUG("Executing command: {}", cmd);

  auto const executor = co_await asio::this_coro::executor;

  auto command = bp::shell{boost::string_view{cmd.data(), cmd.size()}};

  // One pipe for the child's output; stdout and stderr share its write end so
  // merged output keeps its natural interleaving.
  auto pipeReader = asio::readable_pipe{executor};
  auto pipeWriter = asio::writable_pipe{executor};
  asio::connect_pipe(pipeReader, pipeWriter);
  auto const writeEnd = pipeWriter.native_handle();

  auto stdio = mergeStdErr ? bp::process_stdio{.out = writeEnd, .err = writeEnd}
                           : bp::process_stdio{.out = writeEnd, .err = nullptr};

#if defined(_WIN32)
  // Windows CreateProcess resolves the exe from the command line when the
  // application name is empty, so keep the stock shell exe() resolution.
  auto process = std::make_shared<
    bp::process
  >(executor, command.exe(), command.args(), std::move(stdio));
#else
  // boost::process v2's posix find_executable cannot resolve absolute paths
  // (boost::filesystem appends instead of replacing), leaving an empty exe and
  // execve("") ENOENT; the parsed argv[0] token is the correct program name.
  auto const* exeToken = command.argv()[0];
  auto exePath = bp::environment::find_executable(exeToken);
  if (exePath.empty()) { exePath = exeToken; }
  auto process =
    std::make_shared<bp::process>(executor, exePath, command.args(), std::move(stdio));
#endif
  auto const capturedPid = static_cast<int>(process->id());

  // The parent must not keep a write end open, or the reader never sees EOF.
  auto pipeCloseEc = boost::system::error_code{};
  // NOLINTNEXTLINE(bugprone-unused-return-value): asio close(ec) returns void via BOOST_ASIO_SYNC_OP_VOID
  pipeWriter.close(pipeCloseEc);

  auto pipeShared = std::make_shared<asio::readable_pipe>(std::move(pipeReader));
  auto state = std::make_shared<ProcessReadState>();

  auto readAll = [pipeShared, state, onLine]() -> asio::awaitable<ReadEof> {
    co_await readAllInto(pipeShared, state, onLine);
    co_return ReadEof{};
  };
  auto waitExitOp = [process]() -> asio::awaitable<ProcessExit> {
    co_return co_await waitExit(*process);
  };
  auto stopOp = []() -> asio::awaitable<StopRequested> { co_return co_await stopWait(); };

  // The losing awaitables are cancelled when one wins; every pending op here
  // (pipe read, timers) supports cancellation, so no handler outlives this
  // frame and ctx.run() is guaranteed to drain.
  auto outcome = co_await (readAll() || waitExitOp() || stopOp());

  if (std::holds_alternative<ReadEof>(outcome)) {
    // EOF first: no more output will arrive. Wait for the child, honoring
    // a stop request that may arrive meanwhile.
    auto second = co_await (waitExitOp() || stopOp());
    if (std::holds_alternative<StopRequested>(second)) {
      state->callbacksEnabled_.store(false, std::memory_order_release);
      if (
        auto const exitCode = co_await terminateOnStop(process, cmd); exitCode.has_value()
      ) {
        co_return ExecResult{exitCode.value(), state->output_, capturedPid};
      }
      co_return ExecResult{stopsignal::kCanceledExitCode, state->output_, capturedPid};
    }
    co_return ExecResult{process->exit_code(), state->output_, capturedPid};
  }

  if (std::holds_alternative<ProcessExit>(outcome)) {
    // Exit first: drain the remaining buffered output to EOF. A grandchild
    // holding the write end blocks here, as in the legacy implementation.
    // Yield one event-loop turn so the cancelled read's abort completion
    // clears the pipe before a new read is issued (one outstanding read
    // per pipe).
    co_await asio::post(executor, asio::use_awaitable);
    co_await readAllInto(pipeShared, state, onLine);
    co_return ExecResult{process->exit_code(), state->output_, capturedPid};
  }

  // Stop first: suppress further callbacks, terminate, and return the
  // partial output accumulated so far. terminateOnStop reports the real exit
  // code when the child had already exited in the same poll window.
  state->callbacksEnabled_.store(false, std::memory_order_release);
  if (
    auto const exitCode = co_await terminateOnStop(process, cmd); exitCode.has_value()
  ) {
    co_return ExecResult{exitCode.value(), state->output_, capturedPid};
  }
  co_return ExecResult{stopsignal::kCanceledExitCode, state->output_, capturedPid};
}

auto exec2Impl(
  std::string_view cmd,
  std::function<void(std::string_view)> const* onLine,
  bool mergeStdErr
) -> ExecResult {
  auto ctx = asio::io_context{};
  auto lineCallback =
    onLine != nullptr ? *onLine : std::function<void(std::string_view)>{};
  auto result = asio::co_spawn(
    ctx,
    runProcess(std::string{cmd}, std::move(lineCallback), mergeStdErr),
    asio::use_future
  );
  ctx.run();
  return result.get();
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
