#include "tagger/onnx_tagger.h"

#include "tagger/mapping.h"
#include "infra/env.h"
#include "tagger/preprocess.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <utility>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#endif

namespace tagger {

namespace {

// CUDA/cuDNN DLL resolution: prepend the encro lib dir (self-installed
// cuDNN) and the model dir to the process PATH before session creation, and
// register them with AddDllDirectory. PATH prepend is what dependent-DLL
// lookup consults; AddDllDirectory is the modern belt-and-suspenders.
auto ensureGpuRuntimePaths(fs::path const& modelDir) -> void {
#if defined(_WIN32)
  auto const libDir = []() -> fs::path {
    auto const localAppData = processenv::readEnvVar("LOCALAPPDATA");
    return fs::path{localAppData.value_or("")} / "encro" / "lib";
  }();

  for (auto const& dir: {libDir, modelDir}) {
    auto ec = std::error_code{};
    if (dir.empty() || !fs::exists(dir, ec) || ec) { continue; }
    AddDllDirectory(dir.c_str());

    auto currentPath = processenv::readEnvVar("PATH").value_or("");
    if (currentPath.find(dir.string()) != std::string::npos) { continue; }
    auto const newPath = dir.string() + ";" + currentPath;
    _putenv_s("PATH", newPath.c_str());
  }
#else
  (void)modelDir;
#endif
}

}  // namespace

struct OnnxTagger::SessionState {
  Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "encro-tagger"};
  Ort::Session session{nullptr};
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

  ensureGpuRuntimePaths(modelPath_.parent_path());

  // Single session-creation seam (design D1): try CUDA EP, fall back to CPU
  // with exactly one notice-worthy outcome recorded in providerName_.
  session_ = std::make_unique<SessionState>();
  auto makeOptions = []() {
    auto options = Ort::SessionOptions{};
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    options.SetIntraOpNumThreads(2);
    return options;
  };

  auto created = false;
  try {
    auto cudaOptions = makeOptions();
    cudaOptions.AppendExecutionProvider("CUDAExecutionProvider");
    session_->session = Ort::Session{session_->env, modelPath_.c_str(), cudaOptions};
    created = true;
    providerName_ = "cuda";
  } catch (Ort::Exception const&) {
    // Provider DLLs missing, no CUDA driver, or EP init failed: CPU it is.
    providerName_ = "cpu";
  }
  if (!created) {
    try {
      session_->session = Ort::Session{session_->env, modelPath_.c_str(), makeOptions()};
    } catch (Ort::Exception const& error) {
      throw std::runtime_error(
        std::format("cannot load tagger model {}: {}", modelPath_.string(), error.what())
      );
    }
  }
}

OnnxTagger::~OnnxTagger() = default;

auto OnnxTagger::classify(fs::path const& path) -> eh::Result<TaggerOutput> {
  auto const pixels = runPreprocess(ffmpegPath_.value_or(fs::path{"ffmpeg"}), path);
  if (!pixels) { return std::unexpected(pixels.error()); }

  auto inputFloats = toInputFloats(*pixels);
  auto const shape = std::array<std::int64_t, 4>{1, kInputEdge, kInputEdge, 3};
  auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
  auto inputTensor = Ort::Value::CreateTensor<
    float
  >(memoryInfo, inputFloats.data(), inputFloats.size(), shape.data(), shape.size());

  auto const inputName =
    session_->session.GetInputNameAllocated(0, Ort::AllocatorWithDefaultOptions{});
  auto const outputName =
    session_->session.GetOutputNameAllocated(0, Ort::AllocatorWithDefaultOptions{});
  char const* const names[] = {inputName.get()};
  char const* const outNames[] = {outputName.get()};

  auto outputs =
    session_->session.Run(Ort::RunOptions{nullptr}, names, &inputTensor, 1, outNames, 1);
  if (outputs.empty() || !outputs.front().IsTensor()) {
    return eh::makeError("tagger produced no tensor output for {}", path.string());
  }

  auto const scores = outputs.front().GetTensorMutableData<float>();
  auto const count = outputs.front().GetTensorTypeAndShapeInfo().GetElementCount();
  return mapOutputs({scores, count}, vocabulary_, kEngineConfidenceFloor);
}

}  // namespace tagger
