// Concrete FeatureEngine over onnxruntime: the identity model's feature
// extractor (design D3/D7) — a 384x384 stretched frame in, one embedding out,
// scaled to unit length for the similarity contract.
#pragma once

#include "tagger/preprocess.h"
#include "tagger/tagger.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace fs = std::filesystem;

namespace tagger {

class OnnxFeatureEngine final: public FeatureEngine {
public:
  OnnxFeatureEngine(fs::path const& modelPath, std::optional<fs::path> ffmpegPath);

  ~OnnxFeatureEngine() override;

  auto extract(fs::path const& path) -> eh::Result<std::vector<float>> override;

  // "cuda", "dml" or "cpu" — the pipeline prints this as its provider notice.
  auto providerName() const -> std::string override { return providerName_; }

private:
  std::optional<fs::path> ffmpegPath_;
  std::string providerName_ = "cpu";
  // Ort::Session is move-only and the header must stay Ort-free, so the
  // session lives behind the pimpl firewall (as in onnx_tagger.h).
  struct Model;
  std::unique_ptr<Model> model_;
};

}  // namespace tagger
