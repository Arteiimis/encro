#include "tagger/onnx_tagger.h"

#include "tagger/mapping.h"
#include "tagger/onnx_runtime.h"
#include "tagger/preprocess.h"

#include <onnxruntime_cxx_api.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace tagger {

struct OnnxTagger::SessionState {
  explicit SessionState(fs::path const& modelPath)
    : model(modelPath, "encro-tagger", "tagger") { }

  OnnxModel model;
};

OnnxTagger::OnnxTagger(
  fs::path modelPath,
  fs::path const& vocabPath,
  std::optional<fs::path> ffmpegPath
)
  : modelPath_(std::move(modelPath)), ffmpegPath_(std::move(ffmpegPath)) {
  auto vocabRes = loadVocabulary(vocabPath);
  if (!vocabRes) { throw std::runtime_error(vocabRes.error()); }
  vocabulary_ = std::move(*vocabRes);

  // The session (and its provider chain) is OnnxModel's job; both engines
  // share that plumbing (design D7).
  session_ = std::make_unique<SessionState>(modelPath_);
  providerName_ = session_->model.providerName();
}

OnnxTagger::~OnnxTagger() = default;

auto OnnxTagger::classify(fs::path const& path) -> eh::Result<TaggerOutput> {
  auto const pixels =
    runPreprocess(ffmpegPath_.value_or(fs::path{"ffmpeg"}), path, InputKind::Tagger);
  if (!pixels) { return std::unexpected(pixels.error()); }

  auto inputFloats = toInputFloats(*pixels);
  auto const shape = std::array<std::int64_t, 4>{1, kInputEdge, kInputEdge, 3};
  auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
  auto inputTensor = Ort::Value::CreateTensor<
    float
  >(memoryInfo, inputFloats.data(), inputFloats.size(), shape.data(), shape.size());

  auto& session = session_->model.session();
  auto const inputName =
    session.GetInputNameAllocated(0, Ort::AllocatorWithDefaultOptions{});
  auto const outputName =
    session.GetOutputNameAllocated(0, Ort::AllocatorWithDefaultOptions{});
  char const* const names[] = {inputName.get()};
  char const* const outNames[] = {outputName.get()};

  auto outputs =
    session.Run(Ort::RunOptions{nullptr}, names, &inputTensor, 1, outNames, 1);
  if (outputs.empty() || !outputs.front().IsTensor()) {
    return eh::makeError("tagger produced no tensor output for {}", path.string());
  }

  auto const scores = outputs.front().GetTensorMutableData<float>();
  auto const count = outputs.front().GetTensorTypeAndShapeInfo().GetElementCount();
  return mapOutputs({scores, count}, vocabulary_, kEngineConfidenceFloor);
}

}  // namespace tagger
