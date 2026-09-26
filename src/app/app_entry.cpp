#include "app/app_entry.h"

#include "cmd/completion_command.h"
#include "cmd/config_builder.h"
#include "cmd/config_command.h"
#include "core/app_context.h"
#include "core/job_state.h"
#include "app/pipeline.h"
#include "app/prelude.h"
#include "infra/terminal.h"
#include "infra/stop_signal.h"
#include "infra/toolchain.h"
#include "organize/organize_command.h"
#include "preview/preview_process.h"

#include "logging/log_tags.h"
#include "logging/logging.h"
#include "logging/setup.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

using enum terminal::MessageKind;

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::APP_ENTRY);

namespace {

// Run start time for the end-of-run summary's elapsed_ms.
static auto gRunStartedAt = std::chrono::steady_clock::time_point{};

// ── End-of-run summary (D6) ────────────────────────────────────────────────
// jobId/task counts come from the job-state store when it is active; the
// log path and level_counts are attached by the formatter itself.

auto buildSummary(appctx::AppContext const* ctx, std::string status)
  -> logging::SummaryData {
  auto data = logging::SummaryData{
    .status = std::move(status),
    .elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - gRunStartedAt
    )
                   .count(),
  };
  if (ctx != nullptr) {
    if (auto const* store = ctx->runtime.jobState.get(); store != nullptr) {
      data.jobId = store->currentJobId();
      auto const tasks = store->tasks();
      data.tasksTotal = tasks.size();
      data.tasksFailed = static_cast<std::size_t>(
        std::ranges::count_if(tasks, [](jobstate::TaskRecord const& task) {
          return task.status == jobstate::TaskStatus::Failed;
        })
      );
    }
  }
  return data;
}

int parseDecimal(std::string_view text) {
  auto value = 0;
  for (char const ch: text) {
    if (ch == ' ') { continue; }
    value = value * 10 + (ch - '0');
  }
  return value;
}

int monthNumber(std::string_view month) {
  using namespace std::literals;
  constexpr auto months = std::array{
    "Jan"sv,
    "Feb"sv,
    "Mar"sv,
    "Apr"sv,
    "May"sv,
    "Jun"sv,
    "Jul"sv,
    "Aug"sv,
    "Sep"sv,
    "Oct"sv,
    "Nov"sv,
    "Dec"sv,
  };

  for (auto index = std::size_t{0}; index < months.size(); ++index) {
    if (months[index] == month) { return static_cast<int>(index) + 1; }
  }

  return 0;
}

auto compileTimestamp() -> std::string {
  constexpr auto buildDate = std::string_view{__DATE__};
  constexpr auto buildTime = std::string_view{__TIME__};

  auto const year = parseDecimal(buildDate.substr(7, 4));
  auto const month = monthNumber(buildDate.substr(0, 3));
  auto const day = parseDecimal(buildDate.substr(4, 2));

  return std::format("{:04d}-{:02d}-{:02d} {}", year, month, day, buildTime);
}

void printHelpHint() {
  terminal::messageln(Hint, "Run encro -h for help (or -hh for all options).");
}

// Reports a failure and returns its exit code; the caller's `run` funnel does
// the teardown (log hint, summary, drain).
int failRun(std::string const& message, bool showHelpHint = false) {
  // The clean error line prints in every verbosity mode; the echo never
  // replaces it (verbose-levels D4).
  terminal::messageln(Error, "{}", message);
  LOG_ERROR("{}", message);
  if (showHelpHint) { printHelpHint(); }
  return 1;
}

auto handleParseAndHelp(prelude::StartupContext const& startup) -> std::optional<int> {
  auto const& cmd = startup.cmd;

  if (cmd.error.has_value()) {
    return failRun(std::format("Invalid arguments: {}", cmd.error.value()), true);
  }

  if (cmd.help) {
    std::cout << cmd.helpText();
    return 0;
  }

  if (cmd.version) {
    terminal::println(Plain, "encro v1.6 (build: {})", compileTimestamp());
    return 0;
  }

  return std::nullopt;
}

auto buildAppConfig(prelude::StartupContext const& startup)
  -> std::optional<appctx::AppConfig> {
  auto configRes = cmd::buildConfig(startup.cmd);
  if (!configRes) {
    failRun(configRes.error(), true);
    return std::nullopt;
  }

  auto config = std::move(configRes.value());
  prelude::logConfigSummary(config);
  return config;
}

bool ensureToolchainReady(
  appctx::AppContext& ctx,
  prelude::StartupContext const& startup
) {
  if (ctx.config.packOnly) { return true; }

  auto const toolRes = toolchain::resolve(ctx.config, ctx.toolchain);
  if (!toolRes) {
    failRun(std::format("Tool check failed: {}", toolRes.error()), false);
    return false;
  }

  return true;
}

// Preview runs before buildAppConfig (which hard-fails without an input
// path) and skips job-state setup entirely.
int runPreview(prelude::StartupContext const& startup) {
  auto ctx = appctx::AppContext{};

  // Reuse the standard config builder so encode flags (--video-codec,
  // --crf, --preset, --min-vmaf, ...) apply to preview windows too; the
  // preview input fills the required input slot and gets validated as a
  // file, matching the pre-existing "Preview input does not exist" check.
  auto cmd = startup.cmd;
  if (cmd.previewOriginal.has_value()) { cmd.input = cmd.previewOriginal; }
  auto const configRes = cmd::buildConfig(cmd);
  if (!configRes) {
    return failRun(std::format("Preview failed: {}", configRes.error()), false);
  }
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): guarded by the !configRes check above; expected's operator bool is not recognized
  ctx.config = *configRes;
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): guarded by the has_value() check above
  ctx.config.inputPath = fs::path{startup.cmd.previewOriginal.value()};

  if (startup.cmd.ffmpegPath.has_value()) {
    ctx.config.ffmpegInstallDir = fs::path{startup.cmd.ffmpegPath.value()};
  }

  if (!ensureToolchainReady(ctx, startup)) { return 1; }

  auto options = preview::PreviewOptions{.original = ctx.config.inputPath};
  if (startup.cmd.previewEncoded.has_value()) {
    options.encoded = fs::path{startup.cmd.previewEncoded.value()};
  }
  if (startup.cmd.previewOutput.has_value()) {
    options.output = fs::path{startup.cmd.previewOutput.value()};
  }
  if (startup.cmd.previewStart.has_value()) {
    options.startSeconds = startup.cmd.previewStart;
  }
  if (startup.cmd.previewDuration.has_value()) {
    options.durationSeconds = startup.cmd.previewDuration;
  }
  options.noOpen = startup.cmd.previewNoOpen;

  auto const runRes = preview::run(ctx, options);
  if (!runRes) {
    return failRun(std::format("Preview failed: {}", runRes.error()), false);
  }

  return runRes.value();
}

int runAppPipeline(appctx::AppContext& ctx, prelude::StartupContext const& startup) {
  auto runRes = pipeline::run(ctx);
  if (!runRes) {
    return failRun(std::format("Pipeline failed: {}", runRes.error()), false);
  }

  return runRes.value();
}

}  // namespace

namespace appentry {

auto helpIntroLine() -> std::string {
  return std::format(
    "encro: Universal video encoder/converter/packer | build: {}",
    compileTimestamp()
  );
}

// `0` stays "success" even when a stop arrives in the last instant; any other
// non-zero exit is "interrupted" while a stop is pending (a stop-aborted run
// ends with the cancellation exit code, so this covers a failure that
// coincides with one) and "failed" otherwise.
auto runStatus(int exitCode, bool stopRequested) -> std::string {
  if (exitCode == 0) { return "success"; }
  if (exitCode == stopsignal::kCanceledExitCode || stopRequested) {
    return "interrupted";
  }
  return "failed";
}

int run(int argc, char* argv[]) {
  stopsignal::installHandler();
  stopsignal::reset();

  gRunStartedAt = std::chrono::steady_clock::now();

  auto const introLine = helpIntroLine();
  auto const startup = prelude::initStartup(argc, argv, introLine);

  auto ctx = appctx::AppContext{};
  auto exitCode = 0;

  if (auto const earlyExit = handleParseAndHelp(startup); earlyExit.has_value()) {
    exitCode = earlyExit.value();
  } else if (startup.cmd.preview) {
    exitCode = runPreview(startup);
  } else if (startup.cmd.organize) {
    exitCode = organize::runOrganizeCommand(startup.cmd);
  } else if (startup.cmd.config) {
    exitCode = cmd::runConfigCommand(startup.cmd);
  } else if (startup.cmd.completion) {
    exitCode = cmd::runCompletionCommand(startup.cmd);
  } else if (auto config = buildAppConfig(startup); config.has_value()) {
    ctx.config = std::move(config.value());
    exitCode = ensureToolchainReady(ctx, startup) ? runAppPipeline(ctx, startup) : 1;
  } else {
    exitCode = 1;
  }

  // The single teardown funnel: every dispatch branch above falls through here.
  if (startup.loggingActive) {
    if (exitCode != 0 && exitCode != stopsignal::kCanceledExitCode) {
      logging::printLogHint();
    }
    logging::logRunSummary(
      buildSummary(&ctx, runStatus(exitCode, stopsignal::isStopRequested()))
    );
    logging::shutdown();
  }

  return exitCode;
}

}  // namespace appentry
