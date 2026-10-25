#include "batch/batch.h"
#include "core/path_utf8.h"

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

std::size_t countMediaFiles(
  fs::path const& dir,
  std::span<std::string_view const> extensions,
  bool recursive
) {
  auto const scanRes = media::scanByExtensions(dir, extensions, recursive);
  return scanRes ? scanRes->matches.size() : 0;
}

// Video files the scan would keep for the given config: the webp size gate
// lives in the video module, so the pre-check cannot drift from it.
template<class Keep>
std::size_t countKeptVideos(fs::path const& dir, bool recursive, Keep const& keep) {
  auto const scanRes =
    media::scanByExtensions(dir, videoinfo::videoExtensions(), recursive);
  if (!scanRes) { return 0; }
  return static_cast<std::size_t>(std::ranges::count_if(scanRes->matches, keep));
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

// One enumerated subfolder with the entry count it contributes to the run.
struct SubfolderScope {
  fs::path subfolder;
  std::size_t entries;
};

// The entry count's noun follows the run's work: a picture run that converts
// videos counts both kinds as one number, so it needs a noun covering both.
auto scopeNoun(appctx::AppConfig const& config) -> std::string_view {
  if (config.processType == "video") { return "video(s)"; }
  return config.videoWebp ? "item(s)" : "picture(s)";
}

// States what the batch's single confirmation covers. The counts are the ones
// the scans found, so a probe or the pipeline's own video filter may still drop
// entries before they are encoded: the block promises scope, never work. Every
// line is an `Info`, so quiet mode drops the block whole — a `Plain` row would
// slip through the gate and leave orphan rows above the prompt.
void printScopeBlock(std::span<SubfolderScope const> scopes, std::string_view noun) {
  auto processable = std::size_t{0};
  auto total = std::size_t{0};
  for (auto const& scope: scopes) {
    if (scope.entries == 0) { continue; }
    ++processable;
    total += scope.entries;
  }
  if (processable == 0) { return; }

  terminal::println(
    Info,
    "Batch: {} {} in {} of {} subfolder(s)",
    terminal::count(total),
    noun,
    terminal::count(processable),
    terminal::count(scopes.size())
  );
  for (auto const& scope: scopes) {
    if (scope.entries == 0) { continue; }
    terminal::println(
      Info,
      "  {}  {}",
      terminal::count(scope.entries),
      terminal::path(scope.subfolder)
    );
  }
  terminal::println(Info, "The confirmation below covers every subfolder in this batch.");
}

}  // namespace

auto enumerateSubfolders(fs::path const& root, fs::path const& packedRoot)
  -> eh::Result<std::vector<fs::path>> {
  auto const packedKey = collisionnaming::pathIdentityString(packedRoot);
  auto subfolders = std::vector<fs::path>{};

  auto ec = std::error_code{};
  for (
    auto const& entry:
    fs::directory_iterator{root, fs::directory_options::skip_permission_denied, ec}
  ) {
    if (ec) { break; }
    auto const& path = entry.path();
    if (media::isHiddenEntry(path)) { continue; }
    if (collisionnaming::pathIdentityString(path) == packedKey) { continue; }
    if (!entry.is_directory(ec)) { continue; }
    subfolders.push_back(path);
  }
  if (ec) {
    return eh::makeError(
      "Failed to scan batch root {}: {}",
      pathutf8::fromPath(root),
      ec.message()
    );
  }

  std::ranges::sort(subfolders, collisionnaming::stablePathLess);
  return subfolders;
}

std::size_t countProcessableMedia(appctx::AppConfig const& config, fs::path const& dir) {
  if (config.packOnly) { return hasAnyRegularFile(dir) ? 1 : 0; }

  if (config.processType == "video") {
    return countKeptVideos(dir, config.recursive, [&config](fs::path const& file) {
      return videoinfo::keepsForEncoding(config, file);
    });
  }

  auto const pictures = countMediaFiles(dir, pictureExtensions(), config.recursive);
  if (!config.videoWebp) { return pictures; }
  return pictures + countKeptVideos(dir, config.recursive, [](fs::path const& file) {
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
  auto subfoldersRes = enumerateSubfolders(request.root, request.packedRoot);
  if (!subfoldersRes) { return eh::makeError("{}", subfoldersRes.error()); }

  // One scan per subfolder answers both questions the driver has: what will the
  // confirmation cover, and does this subfolder have anything to do.
  auto scopes = std::vector<SubfolderScope>{};
  scopes.reserve(subfoldersRes->size());
  for (auto& subfolder: subfoldersRes.value()) {
    auto const entries = countProcessableMedia(request.baseConfig, subfolder);
    scopes.push_back(SubfolderScope{std::move(subfolder), entries});
  }

  // The block exists to disambiguate the single confirmation below, so a run
  // that asks nothing keeps its output byte-identical: an already-requested
  // stop leaves no answer to read either. Pack-only additionally keeps its
  // presence-only pre-check instead of paying for a tree walk.
  if (
    !request.baseConfig.packOnly
    && !request.baseConfig.yesToAll
    && !request.baseConfig.dryRun
    && !stopsignal::isStopRequested()
  ) {
    printScopeBlock(scopes, scopeNoun(request.baseConfig));
  }

  auto results = std::vector<SubTaskResult>{};
  results.reserve(scopes.size());
  // One confirmation for the whole batch: the first subfolder that reaches a
  // prompt stores its answer here and every later subfolder reuses it.
  auto consent = std::optional<bool>{};

  for (auto const& scope: scopes) {
    auto const& subfolder = scope.subfolder;
    auto result = SubTaskResult{.subfolder = subfolder};
    auto const skip = [&](std::string_view reason) {
      terminal::println(Info, "Skipping {}: {}", terminal::path(subfolder), reason);
      results.push_back(std::move(result));
    };

    if (stopsignal::isStopRequested()) {
      result.exitCode = stopsignal::kCanceledExitCode;
      skip("stop requested.");
      continue;
    }

    // A declined confirmation (\(consent == false\)) ends the batch: the subfolder
    // that asked and every later one are skipped, without being failed.
    if (consent == false) {
      skip("confirmation declined.");
      continue;
    }

    if (scope.entries == 0) {
      skip("no processable media.");
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
      } else if (result.exitCode == 0 && consent == false) {
        // The workflow stopped at the confirmation: it printed its decline
        // notice and did no work, so this subfolder and every remaining one are
        // skipped. The exit code stays the prompt-abort zero, so a decline is
        // never reported as a cancellation or a failure.
        result.outcome = TaskOutcome::Skipped;
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
