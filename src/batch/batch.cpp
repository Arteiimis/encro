#include "batch/batch.h"

#include "app/pipeline.h"
#include "core/collision_naming.h"
#include "core/media_scanner.h"
#include "infra/stop_signal.h"
#include "infra/terminal.h"
#include "picture/picture_process.h"
#include "video/video_info.h"

#include <algorithm>
#include <system_error>

using enum terminal::MessageKind;

namespace batch {

namespace {

// The picture set plus the video set, for a picture run that converts videos:
// a video-only subfolder is a task there.
auto conversionMediaExtensions() -> std::vector<std::string_view> {
  auto extensions = std::vector<std::string_view>{};
  auto const pics = pictureExtensions();
  auto const vids = videoinfo::videoExtensions();
  extensions.reserve(pics.size() + vids.size());
  extensions.insert(extensions.end(), pics.begin(), pics.end());
  extensions.insert(extensions.end(), vids.begin(), vids.end());
  return extensions;
}

auto hasMediaFile(
  fs::path const& dir,
  std::span<std::string_view const> extensions,
  bool recursive
) -> bool {
  auto const scanRes = media::scanByExtensions(dir, extensions, recursive);
  return scanRes && !scanRes->matches.empty();
}

auto hasAnyRegularFile(fs::path const& dir) -> bool {
  auto ec = std::error_code{};
  auto it = fs::recursive_directory_iterator{
    dir,
    fs::directory_options::skip_permission_denied,
    ec
  };
  auto const end = fs::recursive_directory_iterator{};
  for (; it != end; it.increment(ec)) {
    if (ec) { return false; }
    auto entryEc = std::error_code{};
    if (it->is_regular_file(entryEc) && !entryEc) { return true; }
  }
  return false;
}

auto outcomeLabel(TaskOutcome outcome) -> std::string_view {
  switch (outcome) {
    case TaskOutcome::Done   : return "done";
    case TaskOutcome::Failed : return "failed";
    case TaskOutcome::Skipped: return "skipped";
  }
  return "skipped";
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

auto hasProcessableMedia(appctx::AppConfig const& config, fs::path const& dir) -> bool {
  if (config.packOnly) { return hasAnyRegularFile(dir); }

  if (config.processType == "video") {
    return hasMediaFile(dir, videoinfo::videoExtensions(), config.recursive);
  }

  if (config.videoWebp) {
    auto const extensions = conversionMediaExtensions();
    return hasMediaFile(dir, extensions, config.recursive);
  }

  return hasMediaFile(dir, pictureExtensions(), config.recursive);
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

  for (auto const& subfolder: subfoldersRes.value()) {
    auto result = SubTaskResult{.subfolder = subfolder};

    if (stopsignal::isStopRequested()) {
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
      if (result.exitCode == 0) {
        result.outcome = TaskOutcome::Done;
      } else if (result.exitCode == stopsignal::kCanceledExitCode) {
        result.outcome = TaskOutcome::Skipped;
      } else {
        result.outcome = TaskOutcome::Failed;
      }
    }
    results.push_back(std::move(result));
  }

  return results;
}

auto batchExitCode(std::span<SubTaskResult const> results) -> int {
  if (stopsignal::isStopRequested()) { return stopsignal::kCanceledExitCode; }

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
    auto const role = [&]() -> terminal::Role {
      switch (result.outcome) {
        case TaskOutcome::Done   : ++done; return terminal::Role::Good;
        case TaskOutcome::Failed : ++failed; return terminal::Role::Bad;
        case TaskOutcome::Skipped: ++skipped; return terminal::Role::Warn;
      }
      return terminal::Role::Default;
    }();
    terminal::println(
      Plain,
      "  {}  {}",
      terminal::withRole(role, outcomeLabel(result.outcome)),
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
