#include "organize/organize_command.h"

#include "cmd/cmd.h"
#include "core/display_text.h"
#include "core/progress.h"
#include "infra/console_width.h"
#include "infra/stop_signal.h"
#include "infra/terminal.h"
#include "organize/pipeline.h"
#include "tagger/engine_factory.h"
#include "tagger/model_store.h"
#include "utils/utils.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace organize {

namespace {

using terminal::MessageKind;

// The one provider notice (spec "Execution provider selection"). Both engines
// negotiate their provider on their own, so a differing identity provider is
// named in the same line rather than in a second one.
void notifyProviders(
  tagger::TaggerEngine const& engine,
  tagger::FeatureEngine const& features
) {
  auto const provider = engine.providerName();
  auto const identityProvider = features.providerName();
  if (provider == identityProvider) {
    terminal::println(MessageKind::Info, "onnxruntime: {}", provider);
    return;
  }
  terminal::println(
    MessageKind::Info,
    "onnxruntime: {} (identity: {})",
    provider,
    identityProvider
  );
}

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
  auto const missing = tagger::missingFiles(modelDir, files);
  if (missing.empty()) { return true; }

  terminal::messageln(
    MessageKind::Error,
    "models not found in {}",
    displaytext::pathToUtf8String(modelDir)
  );
  for (auto const& name: missing) {
    terminal::eprintln(MessageKind::Plain, "  missing: {}", name);
  }
  terminal::eprintln(
    MessageKind::Info,
    "  -> rerun with --download-models to fetch them, or place the files "
    "manually in the model directory"
  );
  return false;
}

}  // namespace

// The --download-models action: fetch whatever the manifest is missing, and
// report the failure the downloader produced.
bool downloadModels(fs::path const& modelDir) {
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
    return false;
  }
  return true;
}

// The two engines the pipeline runs. Both load a model file and fail the same
// way, so they share one build path (design D7); a null member means its
// build error sits in `errors`, printed by the caller after the loading
// spinner is erased (bars are cleared before any failure diagnostic).
struct Engines {
  std::unique_ptr<tagger::TaggerEngine> tagger;
  std::unique_ptr<tagger::FeatureEngine> features;
  std::vector<std::string> errors;

  explicit operator bool() const { return tagger != nullptr && features != nullptr; }
};

auto makeEngines(fs::path const& modelDir, std::optional<fs::path> const& ffmpegPath)
  -> Engines {
  auto build = [](auto&& make, std::vector<std::string>& errors) -> decltype(make()) {
    try {
      return make();
    } catch (std::exception const& error) {
      errors.emplace_back(error.what());
      return nullptr;
    }
  };
  auto errors = std::vector<std::string>{};
  auto engines = Engines{
    .tagger =
      build([&] { return tagger::makeTaggerEngine(modelDir, ffmpegPath); }, errors),
    .features =
      build([&] { return tagger::makeFeatureEngine(modelDir, ffmpegPath); }, errors),
  };
  engines.errors = std::move(errors);
  return engines;
}

int runOrganizeCommand(CmdParseResult const& cmd) {
  auto const modelDir = resolveModelDir(cmd);
  auto const fakeEngine = tagger::fakeTaggerRequested();

  if (cmd.organizeDownloadModels && !fakeEngine && !downloadModels(modelDir)) {
    return 1;
  }
  if (!fakeEngine && !requireModels(modelDir)) { return 1; }

  // The fetch-only form: no directory means --download-models was the whole
  // request, so nothing is organized (design D2). The presence check above
  // still ran — a model file that exists at the wrong size is missing from
  // the downloader's own "already present" rule and caught only by it.
  if (!cmd.organizeDir.has_value()) { return 0; }

  auto const ffmpegPath = cmd.ffmpegPath.has_value()
    ? std::optional<fs::path>{fs::path{*cmd.ffmpegPath}}
    : std::nullopt;

  // The engine build is the run's silent spot (CUDA DLL loads, two session
  // creations); an indeterminate spinner covers it and is erased before any
  // output follows, per the bar lifecycle. The fake-engine path loads
  // nothing, so it never starts the spinner.
  auto spinner = progress::ProgressContext{};
  if (!fakeEngine) {
    auto const barIndex = spinner.addBar("Loading models");
    spinner.setIndeterminate(barIndex, true);
  }
  auto engines = makeEngines(modelDir, ffmpegPath);
  spinner.eraseBars();
  if (!engines) {
    for (auto const& error: engines.errors) {
      terminal::messageln(MessageKind::Error, "{}", error);
    }
    return 1;
  }
  if (!fakeEngine) { notifyProviders(*engines.tagger, *engines.features); }

  auto options = Options{
    .root = fs::path{cmd.organizeDir.value_or(".")},
    .recursive = cmd.recursive,
    .minConfidence = cmd.organizeMinConfidence.value_or(0.35),
    .modelDir = modelDir,
    .dryRun = cmd.dryRun,
    .recluster = cmd.organizeRecluster,
    .ffmpegPath = ffmpegPath,
    .maxJobs = cmd.maxJobs.value_or(4),
    .identityTau = cmd.organizeIdentityTau.value_or(kCombinedTau),
  };

  auto progress = progress::ProgressContext{};
  auto const runResult =
    runOrganize(options, *engines.tagger, *engines.features, &progress);
  if (!runResult) {
    terminal::messageln(MessageKind::Error, "{}", runResult.error());
    return 1;
  }

  // A stop request aborted the run: one notice naming the stage, the
  // cancellation exit code, and no partial report.
  if (runResult->canceled) {
    progress.eraseBars();
    terminal::messageln(MessageKind::Warning, "Organize canceled by user.");
    return stopsignal::kCanceledExitCode;
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
  terminal::print(
    MessageKind::Plain,
    "{}",
    renderReport(*runResult, consolewidth::resolveColumns())
  );
  return 0;
}

}  // namespace organize
