#include "tagger/onnx_features.h"

#include "tagger/mapping.h"
#include "tagger/onnx_runtime.h"

#include <onnxruntime_cxx_api.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

namespace tagger {

struct OnnxFeatureEngine::Model {
  Model(fs::path const& modelPath, char const* label, std::string_view what)
    : onnx(modelPath, label, what) { }

  OnnxModel onnx;
};

OnnxFeatureEngine::OnnxFeatureEngine(
  fs::path const& modelPath,
  std::optional<fs::path> ffmpegPath
)
  : ffmpegPath_(std::move(ffmpegPath)) {
  model_ = std::make_unique<Model>(modelPath, "encro-identity", "identity");
  providerName_ = model_->onnx.providerName();
}

OnnxFeatureEngine::~OnnxFeatureEngine() = default;

auto OnnxFeatureEngine::extract(fs::path const& path) -> eh::Result<std::vector<float>> {
  auto const pixels =
    runPreprocess(ffmpegPath_.value_or(fs::path{"ffmpeg"}), path, InputKind::Identity);
  if (!pixels) { return std::unexpected(pixels.error()); }

  // NCHW, because this model's own transform emits planar channels.
  auto inputFloats = toIdentityInput(*pixels);
  auto const shape = std::array<std::int64_t, 4>{1, 3, kIdentityEdge, kIdentityEdge};
  auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
  auto inputTensor = Ort::Value::CreateTensor<
    float
  >(memoryInfo, inputFloats.data(), inputFloats.size(), shape.data(), shape.size());

  auto& session = model_->onnx.session();
  auto const inputName =
    session.GetInputNameAllocated(0, Ort::AllocatorWithDefaultOptions{});
  auto const outputName =
    session.GetOutputNameAllocated(0, Ort::AllocatorWithDefaultOptions{});
  char const* const names[] = {inputName.get()};
  char const* const outNames[] = {outputName.get()};

  auto outputs =
    session.Run(Ort::RunOptions{nullptr}, names, &inputTensor, 1, outNames, 1);
  if (outputs.empty() || !outputs.front().IsTensor()) {
    return eh::makeError(
      "identity model produced no tensor output for {}",
      path.string()
    );
  }

  auto const count = outputs.front().GetTensorTypeAndShapeInfo().GetElementCount();
  auto const* scores = outputs.front().GetTensorMutableData<float>();
  auto feature = std::vector<float>{scores, scores + count};
  // Unit length is the contract every consumer of this vector reads
  // (cluster.h cosineSimilarity), not a property of the model.
  normalizeFeature(feature);
  return feature;
}

}  // namespace tagger
