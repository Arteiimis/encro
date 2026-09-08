#include "utils/utils.h"

#include "infra/terminal.h"
#include "infra/stop_signal.h"
#include "logging/log_tags.h"
#include "logging/logging.h"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp>  // IWYU pragma: keep
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
#include <boost/uuid.hpp>  // IWYU pragma: keep

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>
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
struct StderrEof { };
struct ProcessExit { };
struct StopRequested { };

constexpr auto kTerminateWaitTimeout = std::chrono::milliseconds{500};

struct ProcessReadState {
  std::string output_;
  std::string stderr_;  // merging-off children only
  std::atomic<bool> callbacksEnabled_{true};
};

auto readAllInto(
  std::shared_ptr<asio::readable_pipe> pipe,
  std::shared_ptr<ProcessReadState> state,
  std::function<void(std::string_view)> const& onLine,
  bool intoStderr = false
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
    if (intoStderr) {
      state->stderr_.append(chunk);
      continue;
    }
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

// PATH lookup for a bare tool token: bpv2's posix find_executable resolves
// neither absolute paths (boost::filesystem appends instead of replacing)
// nor bare names on this platform.
auto findOnPath(fs::path const& token) -> fs::path {
  auto const* pathEnv = std::getenv("PATH");
  if (pathEnv == nullptr) { return {}; }
  auto const view = std::string_view{pathEnv};
  auto start = size_t{0};
  while (start <= view.size()) {
    auto const end = view.find(':', start);
    auto const dir = view.substr(
      start,
      end == std::string_view::npos ? std::string_view::npos : end - start
    );
    if (!dir.empty()) {
      auto candidate = fs::path{dir} / token;
      auto ec = std::error_code{};
      if (fs::is_regular_file(candidate, ec) && !ec) { return candidate; }
    }
    if (end == std::string_view::npos) { break; }
    start = end + 1;
  }
  return {};
}

// Resolves the shell-parsed executable token to a spawnable path, or an
// empty path when the tool does not exist. bpv2's posix find_executable
// cannot resolve absolute paths (see findOnPath above), so a PATH miss falls
// back to the verbatim token — and the stat check turns a missing tool into
// an exit code (sh's 127) instead of an ENOENT thrown out of the launcher.
auto resolveExecutableToken(boost::process::v2::shell const& command) -> fs::path {
  auto const& rawToken = command.argv()[0];
  auto const token = fs::path{rawToken};
  auto const raw = token.native();
  auto const pathLike = raw.find('/') != decltype(raw)::npos
    || raw.find(fs::path::preferred_separator) != decltype(raw)::npos;
  auto const found = boost::process::v2::environment::find_executable(rawToken);
  auto exePath = found.empty() ? fs::path{} : fs::path{found.native()};
#if !defined(_WIN32)
  if (exePath.empty() && !pathLike) { exePath = findOnPath(token); }
#endif
  if (exePath.empty()) { exePath = token; }
  auto ec = std::error_code{};
  if (fs::exists(exePath, ec) && !ec) { return exePath; }

  // The shell parse splits an unquoted path at its first space, while the
  // Windows launcher still resolves the tool via the whitespace-extension
  // search. Mirror that: the first argv-prefix join naming an existing file
  // wins, so bare spaced paths keep working (quoteToolPath stays unquoted on
  // Windows because some configs carry compound commands).
  auto const argv = command.argv();
  auto joined = fs::path{};
  for (auto i = 0; argv[i] != nullptr; ++i) {
    if (i) { joined += fs::path::value_type{' '}; }
    joined += fs::path{argv[i]};
    if (fs::is_regular_file(joined, ec) && !ec) { return joined; }
  }
  return {};
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

  auto const exePath = resolveExecutableToken(command);
  if (exePath.empty()) { co_return ExecResult{127, "", {}}; }

  // One pipe for the child's output; stdout and stderr share its write end so
  // merged output keeps its natural interleaving.
  auto pipeReader = asio::readable_pipe{executor};
  auto pipeWriter = asio::writable_pipe{executor};
  asio::connect_pipe(pipeReader, pipeWriter);
  auto const writeEnd = pipeWriter.native_handle();

  auto stderrReader = asio::readable_pipe{executor};
  auto stderrWriter = asio::writable_pipe{executor};
  if (!mergeStdErr) { asio::connect_pipe(stderrReader, stderrWriter); }

  auto stdio = mergeStdErr
    ? bp::process_stdio{.out = writeEnd, .err = writeEnd}
    : bp::process_stdio{.out = writeEnd, .err = stderrWriter.native_handle()};

#if defined(_WIN32)
  // Windows CreateProcess resolves the exe from the command line when the
  // application name is empty, so keep the stock shell exe() resolution.
  auto process = std::make_shared<
    bp::process
  >(executor, command.exe(), command.args(), std::move(stdio));
#else
  // bpv2's posix filesystem::path is boost::filesystem (windows uses
  // std::filesystem), so convert the resolved path back before spawning;
  // native() carries the posix bytes through unchanged.
  auto process = std::make_shared<
    bp::process
  >(executor, bp::filesystem::path{exePath.native()}, command.args(), std::move(stdio));
#endif
  auto const capturedPid = static_cast<int>(process->id());

  // The parent must not keep a write end open, or the reader never sees EOF.
  auto pipeCloseEc = boost::system::error_code{};
  // NOLINTNEXTLINE(bugprone-unused-return-value): asio close(ec) returns void via BOOST_ASIO_SYNC_OP_VOID
  pipeWriter.close(pipeCloseEc);
  if (!mergeStdErr) { stderrWriter.close(pipeCloseEc); }

  auto pipeShared = std::make_shared<asio::readable_pipe>(std::move(pipeReader));
  auto stderrPipeShared = std::make_shared<asio::readable_pipe>(std::move(stderrReader));
  auto state = std::make_shared<ProcessReadState>();

  auto readAll = [pipeShared, state, onLine]() -> asio::awaitable<ReadEof> {
    co_await readAllInto(pipeShared, state, onLine);
    co_return ReadEof{};
  };
  auto readAllStderr = [stderrPipeShared, state]() -> asio::awaitable<StderrEof> {
    co_await readAllInto(stderrPipeShared, state, {}, true);
    co_return StderrEof{};
  };
  auto waitExitOp = [process]() -> asio::awaitable<ProcessExit> {
    co_return co_await waitExit(*process);
  };
  auto stopOp = []() -> asio::awaitable<StopRequested> { co_return co_await stopWait(); };
  auto makeResult = [&](int code) {
    return ExecResult{code, state->output_, capturedPid, state->stderr_};
  };

  // The losing awaitables are cancelled when one wins; every pending op here
  // (pipe read, timers) supports cancellation, so no handler outlives this
  // frame and ctx.run() is guaranteed to drain.
  // Both races share the ReadEof/ProcessExit/StopRequested outcomes; the
  // merging-off race adds the stderr reader with its own EOF tag.
  using RaceOutcome = std::variant<ReadEof, StderrEof, ProcessExit, StopRequested>;
  auto runRace = [&]() -> asio::awaitable<RaceOutcome> {
    if (!mergeStdErr) {
      co_return co_await (readAll() || readAllStderr() || waitExitOp() || stopOp());
    }
    // Merging on: no stderr pipe; map the 3-way race onto the outcome set.
    auto alt = co_await (readAll() || waitExitOp() || stopOp());
    co_return std::visit([](auto const& v) -> RaceOutcome { return v; }, alt);
  };
  auto const outcome = co_await runRace();

  if (
    std::holds_alternative<ReadEof>(outcome) || std::holds_alternative<StderrEof>(outcome)
  ) {
    // A pipe hit EOF; drain the other one fully (an already-EOF read returns
    // immediately). Wait for the child, honoring a stop request that may
    // arrive meanwhile.
    co_await readAllInto(pipeShared, state, onLine);
    if (!mergeStdErr) { co_await readAllInto(stderrPipeShared, state, {}, true); }
    auto second = co_await (waitExitOp() || stopOp());
    if (std::holds_alternative<StopRequested>(second)) {
      state->callbacksEnabled_.store(false, std::memory_order_release);
      if (
        auto const exitCode = co_await terminateOnStop(process, cmd); exitCode.has_value()
      ) {
        co_return makeResult(exitCode.value());
      }
      co_return makeResult(stopsignal::kCanceledExitCode);
    }
    co_return makeResult(process->exit_code());
  }

  if (std::holds_alternative<ProcessExit>(outcome)) {
    // Exit first: drain the remaining buffered output to EOF. A grandchild
    // holding the write end blocks here, as in the legacy implementation.
    // Yield one event-loop turn so the cancelled read's abort completion
    // clears the pipe before a new read is issued (one outstanding read
    // per pipe).
    co_await asio::post(executor, asio::use_awaitable);
    co_await readAllInto(pipeShared, state, onLine);
    if (!mergeStdErr) { co_await readAllInto(stderrPipeShared, state, {}, true); }
    co_return makeResult(process->exit_code());
  }

  // Stop first: suppress further callbacks, terminate, and return the
  // partial output accumulated so far. terminateOnStop reports the real exit
  // code when the child had already exited in the same poll window.
  state->callbacksEnabled_.store(false, std::memory_order_release);
  if (
    auto const exitCode = co_await terminateOnStop(process, cmd); exitCode.has_value()
  ) {
    co_return makeResult(exitCode.value());
  }
  co_return makeResult(stopsignal::kCanceledExitCode);
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

// Extracts a one-line failure reason for a failed child: the first line the
// classifier accepts (when given) - else the last non-empty line - from the
// separate stderr text when it carries anything, else from the retained
// merged-output capture; trimmed, capped at ~200 chars, with an "exit code
// N" fallback when neither carries a line.
auto extractFailureReason(
  std::string_view capturedOutput,
  std::string_view stderrText,
  int exitCode,
  std::function<bool(std::string_view)> const& acceptedLine
) -> std::string {
  auto const& source = !stderrText.empty() ? stderrText : capturedOutput;

  constexpr auto kWhitespace = " \t\r\n";

  constexpr auto kMaxReasonLength = std::size_t{200};
  auto trim = [](std::string_view text) {
    auto const begin = text.find_first_not_of(kWhitespace);
    if (begin == std::string_view::npos) { return std::string_view{}; }
    auto const end = text.find_last_not_of(kWhitespace);
    return text.substr(begin, end - begin + 1);
  };

  auto accepted = std::string{};
  auto lastNonEmpty = std::string_view{};
  auto pending = std::string_view{source};
  while (!pending.empty()) {
    auto const newlinePos = pending.find('\n');
    auto const line = pending.substr(0, newlinePos);
    pending = newlinePos == std::string_view::npos ? std::string_view{}
                                                   : pending.substr(newlinePos + 1);
    auto const trimmed = trim(line);
    if (trimmed.empty()) { continue; }
    lastNonEmpty = trimmed;
    if (accepted.empty() && acceptedLine && acceptedLine(trimmed)) {
      accepted = std::string{trimmed};
    }
  }

  auto reason = !accepted.empty() ? accepted : std::string{lastNonEmpty};
  if (reason.empty()) { reason = std::format("exit code {}", exitCode); }
  if (reason.size() > kMaxReasonLength) { reason.resize(kMaxReasonLength); }
  return reason;
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
bool probeTool(fs::path const& toolPath) {
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
