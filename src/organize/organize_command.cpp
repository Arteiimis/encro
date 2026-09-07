#include "organize/organize_command.h"

#include "cmd/cmd.h"
#include "core/display_text.h"
#include "core/progress.h"
#include "infra/terminal.h"
#include "organize/pipeline.h"
#include "tagger/engine_factory.h"
#include "tagger/model_store.h"
#include "utils/utils.h"

#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;

namespace organize {

namespace {

using terminal::MessageKind;

auto resolveModelDir(CmdParseResult const& cmd) -> fs::path {
  if (cmd.organizeModelDir.has_value() && !cmd.organizeModelDir->empty()) {
    return fs::path{*cmd.organizeModelDir};
  }
  return tagger::defaultModelDir();
}

// Fail fast when model files are missing (spec "Model and runtime file
// management"): list what is absent and both remedies before any scan.
bool requireModels(fs::path const& modelDir) {
  auto const files = tagger::modelFiles();
  if (tagger::allFilesPresent(modelDir, files)) { return true; }

  terminal::messageln(
    MessageKind::Error,
    "models not found in {}",
    displaytext::pathToUtf8String(modelDir)
  );
  for (auto const& file: files) {
    auto const name = file.urlPath.substr(file.urlPath.find_last_of('/') + 1);
    if (!fs::exists(modelDir / name)) {
      terminal::eprintln(MessageKind::Plain, "  missing: {}", name);
    }
  }
  terminal::eprintln(
    MessageKind::Info,
    "  -> rerun with --download-models to fetch them, or place the files "
    "manually in the model directory"
  );
  return false;
}

}  // namespace

int runOrganizeCommand(CmdParseResult const& cmd) {
  auto const modelDir = resolveModelDir(cmd);
  auto const fakeEngine = tagger::fakeTaggerRequested();

  if (cmd.organizeDownloadModels && !fakeEngine) {
    terminal::println(
      MessageKind::Info,
      "fetching missing model files into {}",
      displaytext::pathToUtf8String(modelDir)
    );
    auto const downloaded = tagger::downloadMissing(modelDir, [](std::string_view line) {
      terminal::println(MessageKind::Plain, "{}", line);
    });
    if (!downloaded) {
      terminal::messageln(MessageKind::Error, "{}", downloaded.error());
      return 1;
    }
  }

  if (!fakeEngine && !requireModels(modelDir)) { return 1; }

  auto engine = [&]() -> std::unique_ptr<tagger::TaggerEngine> {
    try {
      return tagger::makeTaggerEngine(
        modelDir,
        cmd.ffmpegPath.has_value() ? std::optional<fs::path>{fs::path{*cmd.ffmpegPath}}
                                   : std::nullopt
      );
    } catch (std::exception const& error) {
      terminal::messageln(MessageKind::Error, "{}", error.what());
      return nullptr;
    }
  }();
  if (engine == nullptr) { return 1; }

  if (!fakeEngine) {
    // The one provider notice (spec "Execution provider selection").
    terminal::println(MessageKind::Info, "onnxruntime: {}", engine->providerName());
  }

  auto options = Options{
    .root = fs::path{cmd.organizeDir.value_or(".")},
    .recursive = cmd.recursive,
    .minConfidence = cmd.organizeMinConfidence.value_or(0.35),
    .modelDir = modelDir,
    .dryRun = cmd.dryRun,
    .recluster = cmd.organizeRecluster,
    .downloadModels = cmd.organizeDownloadModels,
    .ffmpegPath = cmd.ffmpegPath.has_value()
      ? std::optional<fs::path>{fs::path{*cmd.ffmpegPath}}
      : std::nullopt,
    .maxJobs = cmd.maxJobs.value_or(4),
  };

  auto progress = progress::ProgressContext{};
  auto const runResult = runOrganize(options, *engine, &progress);
  if (!runResult) {
    terminal::messageln(MessageKind::Error, "{}", runResult.error());
    return 1;
  }

  if (runResult->scanned == 0) {
    terminal::println(
      MessageKind::Info,
      "no images found in {}",
      displaytext::pathToUtf8String(options.root)
    );
    return 0;
  }

  progress.eraseBars();
  terminal::print(MessageKind::Plain, "{}", renderReport(*runResult));
  return 0;
}

}  // namespace organize
