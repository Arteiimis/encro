// Concrete TaggerEngine over onnxruntime: single-seam session creation
// (CUDA EP with CPU fallback, design D1), preprocessing hand-off, sigmoid
// output mapping through the vocabulary (task 2.3).
#pragma once

#include "tagger/preprocess.h"
#include "tagger/tagger.h"
#include "tagger/vocabulary.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace fs = std::filesystem;

namespace tagger {

// Minimum reported confidence; pairs below are dropped from the output.
inline constexpr auto kEngineConfidenceFloor = 0.01;

class OnnxTagger final: public TaggerEngine {
public:
  OnnxTagger(fs::path modelPath, fs::path vocabPath, std::optional<fs::path> ffmpegPath);

  ~OnnxTagger() override;

  auto classify(fs::path const& path) -> eh::Result<TaggerOutput> override;

  // "cuda" or "cpu" — the pipeline prints this as the one provider notice.
  auto providerName() const -> std::string const& { return providerName_; }

private:
  fs::path modelPath_;
  std::optional<fs::path> ffmpegPath_;
  Vocabulary vocabulary_;
  std::string providerName_ = "cpu";
  // Ort::Session is move-only and the header must stay Ort-free, so the
  // session lives behind the pimpl firewall.
  struct SessionState;
  std::unique_ptr<SessionState> session_;
};

}  // namespace tagger
