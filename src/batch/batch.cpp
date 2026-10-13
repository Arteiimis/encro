#include "batch/batch.h"

#include "app/pipeline.h"
#include "core/collision_naming.h"
#include "core/media_scanner.h"
#include "infra/stop_signal.h"
#include "infra/terminal.h"
#include "picture/picture_process.h"
#include "video/video_info.h"

#include <algorithm>
#include <array>
#include <system_error>

using enum terminal::MessageKind;

namespace batch {

namespace {

bool hasMediaFile(
  fs::path const& dir,
  std::span<std::string_view const> extensions,
  bool recursive
) {
  auto const scanRes = media::scanByExtensions(dir, extensions, recursive);
  return scanRes && !scanRes->matches.empty();
}

// Video files the scan would keep for the given config: the webp size gate
// lives in the video module, so the pre-check cannot drift from it.
template<class Keep>
bool hasKeptVideo(fs::path const& dir, bool recursive, Keep const& keep) {
  auto const scanRes =
    media::scanByExtensions(dir, videoinfo::videoExtensions(), recursive);
  return scanRes && std::ranges::any_of(scanRes->matches, keep);
}

bool hasAnyRegularFile(fs::path const& dir) {
  // The scanner's own idiom: the ec-taking constructor plus an in-loop check
  // keeps a missing or unreadable root from throwing.
  auto ec = std::error_code{};
  for (
    auto const& entry: fs::recursive_directory_iterator{
      dir,
      fs::directory_options::skip_permission_denied,
      ec
    }
  ) {
    if (ec) { return false; }
    auto entryEc = std::error_code{};
    if (entry.is_regular_file(entryEc) && !entryEc) { return true; }
  }
  return false;
}

// One row style per outcome: the label and its role always travel together.
struct OutcomeStyle {
  std::string_view label;
  terminal::Role role;
};

constexpr auto kOutcomeStyles = std::array{
  OutcomeStyle{"done", terminal::Role::Good},
  OutcomeStyle{"failed", terminal::Role::Bad},
  OutcomeStyle{"skipped", terminal::Role::Warn},
};

auto outcomeStyle(TaskOutcome outcome) -> OutcomeStyle {
  return kOutcomeStyles[static_cast<std::size_t>(outcome)];
}

}  // namespace

auto enumerateSubfolders(fs::path const& root, fs::path const& packedRoot)
  -> eh::Result<std::vector<fs::path>> {
  auto const packedKey = collisionnaming::stablePathString(packedRoot);
  auto subfolders = std::vector<fs::path>{};

  auto ec = std::error_code{};
  for (
    auto const& entry:
    fs::directory_iterator{root, fs::directory_options::skip_permission_denied, ec}
  ) {
    if (ec) { break; }
    auto const& path = entry.path();
    if (media::isHiddenEntry(path)) { continue; }
    if (collisionnaming::stablePathString(path) == packedKey) { continue; }
    if (!entry.is_directory(ec)) { continue; }
    subfolders.push_back(path);
  }
  if (ec) {
    return eh::makeError("Failed to scan batch root {}: {}", root.string(), ec.message());
  }

  std::ranges::sort(subfolders, collisionnaming::stablePathLess);
  return subfolders;
}

bool hasProcessableMedia(appctx::AppConfig const& config, fs::path const& dir) {
  if (config.packOnly) { return hasAnyRegularFile(dir); }

  if (config.processType == "video") {
    return hasKeptVideo(dir, config.recursive, [&config](fs::path const& file) {
      return videoinfo::keepsForEncoding(config, file);
    });
  }

  if (hasMediaFile(dir, pictureExtensions(), config.recursive)) { return true; }
  if (!config.videoWebp) { return false; }
  return hasKeptVideo(dir, config.recursive, [](fs::path const& file) {
    return videoinfo::keepsForConversion(file);
  });
}

auto resolvePackedRoot(
  fs::path const& root,
  std::optional<fs::path> const& outputOverride
) -> fs::path {
  return outputOverride.value_or(root / "packed");
}

auto subConfigFor(BatchRequest const& request, fs::path const& subfolder)
  -> appctx::AppConfig {
  auto config = request.baseConfig;
  config.inputPath = subfolder;
  config.inputPaths.clear();
  config.outputPath.reset();
  config.stateFilePath.reset();
  config.packOutputDir = request.packedRoot / subfolder.filename();
  return config;
}

auto runTasks(BatchRequest const& request) -> eh::Result<std::vector<SubTaskResult>> {
  auto const subfoldersRes = enumerateSubfolders(request.root, request.packedRoot);
  if (!subfoldersRes) { return eh::makeError("{}", subfoldersRes.error()); }

  auto results = std::vector<SubTaskResult>{};
  results.reserve(subfoldersRes->size());
  // One confirmation for the whole batch: the first subfolder that reaches a
  // prompt stores its answer here and every later subfolder reuses it.
  auto consent = std::optional<bool>{};
  auto declined = false;

  for (auto const& subfolder: subfoldersRes.value()) {
    auto result = SubTaskResult{.subfolder = subfolder};

    if (stopsignal::isStopRequested()) {
      terminal::println(Info, "Skipping {}: stop requested.", terminal::path(subfolder));
      result.exitCode = stopsignal::kCanceledExitCode;
      results.push_back(std::move(result));
      continue;
    }

    if (declined) {
      terminal::println(
        Info,
        "Skipping {}: confirmation declined.",
        terminal::path(subfolder)
      );
      results.push_back(std::move(result));
      continue;
    }

    if (!hasProcessableMedia(request.baseConfig, subfolder)) {
      terminal::println(
        Info,
        "Skipping {}: no processable media.",
        terminal::path(subfolder)
      );
      results.push_back(std::move(result));
      continue;
    }

    auto ctx = appctx::AppContext{};
    ctx.config = subConfigFor(request, subfolder);
    ctx.toolchain = request.toolchain;
    ctx.runtime.promptConsent = &consent;

    auto const runRes = pipeline::run(ctx);
    if (!runRes) {
      result.outcome = TaskOutcome::Failed;
      result.error = runRes.error();
      terminal::messageln(
        Error,
        "Subfolder {} failed: {}",
        terminal::path(subfolder),
        result.error
      );
    } else {
      result.exitCode = runRes.value();
      if (result.exitCode == stopsignal::kCanceledExitCode) {
        result.outcome = TaskOutcome::Skipped;
      } else if (result.exitCode == 0 && consent.has_value() && !consent.value()) {
        // The workflow stopped at the confirmation: it printed its decline
        // notice and did no work, so this subfolder and every remaining one are
        // skipped. The exit code stays the prompt-abort zero, so a decline is
        // never reported as a cancellation or a failure.
        result.outcome = TaskOutcome::Skipped;
        declined = true;
      } else if (result.exitCode == 0) {
        result.outcome = TaskOutcome::Done;
      } else {
        result.outcome = TaskOutcome::Failed;
      }
    }
    results.push_back(std::move(result));
  }

  return results;
}

int batchExitCode(std::span<SubTaskResult const> results) {
  // Result-based, not flag-based: a stop that arrives after every subfolder
  // finished is not a canceled batch.
  if (std::ranges::any_of(results, [](SubTaskResult const& result) {
        return result.exitCode == stopsignal::kCanceledExitCode;
      })) {
    return stopsignal::kCanceledExitCode;
  }

  auto const failed = std::ranges::any_of(results, [](SubTaskResult const& result) {
    return result.outcome == TaskOutcome::Failed;
  });
  return failed ? 1 : 0;
}

void printSummary(std::span<SubTaskResult const> results) {
  auto done = std::size_t{0};
  auto failed = std::size_t{0};
  auto skipped = std::size_t{0};

  for (auto const& result: results) {
    auto const style = outcomeStyle(result.outcome);
    switch (result.outcome) {
      case TaskOutcome::Done   : ++done; break;
      case TaskOutcome::Failed : ++failed; break;
      case TaskOutcome::Skipped: ++skipped; break;
    }
    terminal::println(
      Plain,
      "  {}  {}",
      terminal::withRole(style.role, style.label),
      terminal::path(result.subfolder)
    );
  }

  terminal::println(
    Summary,
    "{} {}",
    terminal::withRole(terminal::outcomeVerbRole(failed, skipped), "Processed"),
    terminal::summaryCounts(done, results.size(), "subfolders", failed, skipped)
  );
}

auto runBatch(BatchRequest const& request) -> eh::Result<int> {
  auto const resultsRes = runTasks(request);
  if (!resultsRes) { return eh::makeError("{}", resultsRes.error()); }
  auto const& results = resultsRes.value();

  if (results.empty()) {
    terminal::println(
      Info,
      "No subfolders to process under {}.",
      terminal::path(request.root)
    );
  } else {
    printSummary(results);
  }

  return batchExitCode(results);
}

}  // namespace batch
