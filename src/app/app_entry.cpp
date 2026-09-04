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
  terminal::println(Hint, "Run encro -h for help (or -hh for all options).");
}

int failWithHint(
  prelude::StartupContext const& startup,
  std::string const& message,
  bool showHelpHint = false,
  appctx::AppContext* ctx = nullptr
) {
  if (startup.cmd.verbose) {
    LOG_ERROR("{}", message);
  } else {
    terminal::println(Error, "Error: {}", message);
    LOG_ERROR("{}", message);
  }
  logging::printLogHint();
  // End-of-run summary before the drain below, so it lands in the log.
  logging::logRunSummary(buildSummary(ctx, "failed"));
  // Drain the async queue so the error reaches the console echo and the log file
  // before the process exits.
  logging::shutdown();
  if (showHelpHint) { printHelpHint(); }
  return 1;
}

auto handleParseAndHelp(prelude::StartupContext const& startup) -> std::optional<int> {
  auto const& cmd = startup.cmd;

  if (cmd.error.has_value()) {
    return failWithHint(
      startup,
      std::format("Invalid arguments: {}", cmd.error.value()),
      true
    );
  }

  if (cmd.help) {
    std::cout << cmd.helpText;
    return 0;
  }

  if (cmd.version) {
    terminal::println(Version, "encro v1.6 (build: {})", compileTimestamp());
    return 0;
  }

  return std::nullopt;
}

auto buildAppConfig(prelude::StartupContext const& startup)
  -> std::optional<appctx::AppConfig> {
  auto configRes = cmd::buildConfig(startup.cmd);
  if (!configRes) {
    failWithHint(startup, configRes.error(), true);
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
    failWithHint(
      startup,
      std::format("Tool check failed: {}", toolRes.error()),
      false,
      &ctx
    );
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
    return failWithHint(
      startup,
      std::format("Preview failed: {}", configRes.error()),
      false,
      &ctx
    );
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
    return failWithHint(
      startup,
      std::format("Preview failed: {}", runRes.error()),
      false,
      &ctx
    );
  }

  auto const exitCode = runRes.value();
  if (exitCode != 0 && exitCode != stopsignal::kCanceledExitCode) {
    logging::printLogHint();
  }
  auto const status = exitCode == 0 ? "success" : "failed";
  logging::logRunSummary(buildSummary(&ctx, status));
  logging::shutdown();
  return exitCode;
}

int runAppPipeline(appctx::AppContext& ctx, prelude::StartupContext const& startup) {
  auto runRes = pipeline::run(ctx);
  if (!runRes) {
    return failWithHint(
      startup,
      std::format("Pipeline failed: {}", runRes.error()),
      false,
      &ctx
    );
  }

  // Success, Ctrl-C, or any other non-zero pipeline exit all reach here; today
  // these paths return without draining, so the summary must be followed by an
  // explicit shutdown.
  auto const exitCode = runRes.value();
  if (exitCode != 0 && exitCode != stopsignal::kCanceledExitCode) {
    logging::printLogHint();
  }
  auto const status = [exitCode] {
    if (exitCode == stopsignal::kCanceledExitCode) { return "interrupted"; }
    return exitCode == 0 ? "success" : "failed";
  }();
  logging::logRunSummary(buildSummary(&ctx, status));
  logging::shutdown();
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

int run(int argc, char* argv[]) {
  stopsignal::installHandler();
  stopsignal::reset();

  gRunStartedAt = std::chrono::steady_clock::now();

  auto const introLine = helpIntroLine();
  auto const startup = prelude::initStartup(argc, argv, introLine);

  if (auto const earlyExit = handleParseAndHelp(startup); earlyExit.has_value()) {
    return earlyExit.value();
  }

  if (startup.cmd.preview) { return runPreview(startup); }

  // Subcommand bodies report their own errors and return non-zero; they bypass
  // failWithHint, so the log hint is attached here on failure.
  if (startup.cmd.config) {
    auto const exitCode = cmd::runConfigCommand(startup.cmd);
    if (exitCode != 0) { logging::printLogHint(); }
    return exitCode;
  }
  if (startup.cmd.completion) {
    auto const exitCode = cmd::runCompletionCommand(startup.cmd);
    if (exitCode != 0) { logging::printLogHint(); }
    return exitCode;
  }

  auto config = buildAppConfig(startup);
  if (!config.has_value()) { return 1; }

  auto ctx = appctx::AppContext{.config = std::move(config.value())};

  if (!ensureToolchainReady(ctx, startup)) { return 1; }

  return runAppPipeline(ctx, startup);
}

}  // namespace appentry
