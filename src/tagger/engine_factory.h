// Test seam for e2e runs (the fake_media_tool pattern, design D9): when
// ENCRO_FAKE_TAGGER names a JSON fixture file, makeTaggerEngine returns an
// env-driven fake instead of the ONNX engine, so e2e runs exercise the full
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

}  // namespace tagger
