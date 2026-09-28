// Test seam for e2e runs (the fake_media_tool pattern, design D9): when
// ENCRO_FAKE_TAGGER names a JSON fixture file, the factories return
// env-driven fakes instead of the ONNX engines, so e2e runs exercise the full
// CLI and pipeline offline.
#pragma once

#include "tagger/tagger.h"

#include <filesystem>
#include <memory>
#include <optional>

namespace fs = std::filesystem;

namespace tagger {

// True when ENCRO_FAKE_TAGGER is set (model management is bypassed too).
bool fakeTaggerRequested();

// Builds the production OnnxTagger (throws std::runtime_error on load
// failure) or the env-driven fake. ffmpegPath feeds preprocessing.
auto makeTaggerEngine(fs::path const& modelDir, std::optional<fs::path> const& ffmpegPath)
  -> std::unique_ptr<TaggerEngine>;

// Builds the identity feature engine: the same env fixture’s `identity` field
// when ENCRO_FAKE_TAGGER is set, the ONNX extractor on Windows, and the
// unsupported-platform arm where the ORT package does not exist (design D7).
auto makeFeatureEngine(
  fs::path const& modelDir,
  std::optional<fs::path> const& ffmpegPath
) -> std::unique_ptr<FeatureEngine>;

}  // namespace tagger
