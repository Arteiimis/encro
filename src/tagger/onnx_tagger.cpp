#include "tagger/onnx_tagger.h"

#include "infra/crash_runtime.h"
#include "tagger/mapping.h"
#include "infra/env.h"
#include "tagger/preprocess.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iterator>
#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

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

// CUDA/cuDNN DLL resolution. The CUDA runtime (cudart/cublas/cufft) comes
// from a toolkit install (scoop sets CUDA_PATH and adds its bin to the USER
// PATH in the registry), cuDNN is self-installed into the encro lib dir.
// Reading the registry user PATH directly keeps GPU acceleration working
// even from terminals opened before the toolkit was installed.
// Reads a HKCU\Environment value. Scoop and installers store PATH as
// REG_EXPAND_SZ, so any type is accepted and %VAR% references are expanded;
// a stale terminal's process PATH never sees a freshly installed toolkit,
// which is why the registry is consulted directly.
auto registryEnvironmentValue(std::wstring const& name) -> std::string {
#if defined(_WIN32)
  auto buffer = std::array<wchar_t, 32767>{};
  auto size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
  if (
    RegGetValueW(
      HKEY_CURRENT_USER,
      L"Environment",
      name.c_str(),
      RRF_RT_ANY,
      nullptr,
      buffer.data(),
      &size
    )
    != ERROR_SUCCESS
  ) {
    return {};
  }
  auto raw = std::wstring{buffer.data()};
  auto expanded = std::array<wchar_t, 32767>{};
  auto const grown = ExpandEnvironmentStringsW(
    raw.c_str(),
    expanded.data(),
    static_cast<DWORD>(expanded.size())
  );
  if (grown == 0 || grown > expanded.size()) {
    raw = std::wstring{};
  } else {
    raw = std::wstring{expanded.data()};
  }
  return std::string{std::filesystem::path{raw}.string()};
#else
  (void)name;
  return {};
#endif
}

auto containsCudart(fs::path const& dir) -> bool {
  auto ec = std::error_code{};
  if (dir.empty() || !fs::exists(dir, ec) || ec) { return false; }
  return fs::exists(dir / "cudart64_12.dll", ec) && !ec;
}

auto ensureGpuRuntimePaths(fs::path const& modelDir) -> void {
#if defined(_WIN32)
  auto const libDir = []() -> fs::path {
    auto const localAppData = processenv::readEnvVar("LOCALAPPDATA");
    return fs::path{localAppData.value_or("")} / "encro" / "lib";
  }();

  // Candidate CUDA dirs: the registry CUDA_PATH bin, plus every registry/
  // process PATH entry that actually holds the CUDA runtime DLLs. The
  // registry is consulted because a freshly installed toolkit is invisible
  // to terminals opened before the install.
  auto candidates = std::vector<fs::path>{libDir, modelDir};
  auto const cudaPath = registryEnvironmentValue(L"CUDA_PATH");
  if (!cudaPath.empty()) { candidates.emplace_back(fs::path{cudaPath} / "bin"); }
  auto const cudaPathEnv = processenv::readEnvVar("CUDA_PATH");
  if (cudaPathEnv.has_value()) {
    candidates.emplace_back(fs::path{*cudaPathEnv} / "bin");
  }
  auto pathSources = std::vector<std::string>{
    registryEnvironmentValue(L"Path"),
    processenv::readEnvVar("PATH").value_or("")
  };
  for (auto const& source: pathSources) {
    auto stream = std::istringstream{source};
    auto entry = std::string{};
    while (std::getline(stream, entry, ';')) {
      if (entry.empty()) { continue; }
      candidates.emplace_back(entry);
    }
  }

  auto prepended = std::string{};
  for (auto const& dir: candidates) {
    auto ec = std::error_code{};
    if (dir.empty() || !fs::exists(dir, ec) || ec) { continue; }
    if (!containsCudart(dir) && dir != libDir && dir != modelDir) { continue; }
    AddDllDirectory(dir.c_str());
    prepended += dir.string() + ";";
  }
  if (!prepended.empty()) {
    auto currentPath = processenv::readEnvVar("PATH").value_or("");
    _putenv_s("PATH", (prepended + currentPath).c_str());
  }

  // Preload the CUDA provider exactly like ORT's dynamic loader would, but
  // with a full path: ORT's own bare-name LoadLibrary resolves against the
  // current directory (altered search) and fails in encro's process. An
  // already-loaded module satisfies ORT's later LoadLibrary by name.
  auto const exeDir = []() -> fs::path {
    auto buffer = std::array<wchar_t, 1024>{};
    auto const len =
      GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return fs::path{std::wstring{buffer.data(), len}}.parent_path();
  }();
  // Crash-report work must not run while this thread may hold the loader
  // lock: the dbgeng stack capture deadlocks there. DLL-init exceptions pass
  // through first chance instead of hanging the process.
  auto const dllLoadZone = crash::ScopedDllLoadZone{};
  for (
    auto const* name:
    {L"onnxruntime_providers_shared.dll", L"onnxruntime_providers_cuda.dll"}
  ) {
    auto const fullPath = exeDir / name;
    // Intentionally not freed: the resident module is what makes ORT's
    // later by-name LoadLibrary resolve to the CUDA provider.
    LoadLibraryExW(fullPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
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

  // Provider chain: CUDA first, DirectML second (compiled into the Windows
  // ORT build and runs on any DX12 GPU), CPU always available. Each attempt
  // either succeeds or throws; the winner is recorded for the notice line.
  session_ = std::make_unique<SessionState>();
  auto makeOptions = []() {
    auto options = Ort::SessionOptions{};
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    options.SetIntraOpNumThreads(2);
    // Keep provider-assignment chatter (Memcpy warnings) off the console.
    options.SetLogSeverityLevel(3);
    return options;
  };

  struct ProviderAttempt {
    char const* name;
    std::function<void(Ort::SessionOptions&)> configure;
  };
  auto attempts = std::vector<ProviderAttempt>{
    {"cuda",
     [](Ort::SessionOptions& options) {
       options.AppendExecutionProvider_CUDA(OrtCUDAProviderOptions{});
     }},
    {"dml", [](Ort::SessionOptions& options) {
       options.AppendExecutionProvider("DmlExecutionProvider");
     }},
  };
  auto created = false;
  for (auto const& attempt: attempts) {
    try {
      auto options = makeOptions();
      attempt.configure(options);
      session_->session = Ort::Session{session_->env, modelPath_.c_str(), options};
      created = true;
      providerName_ = attempt.name;
      break;
    } catch (Ort::Exception const&) {
      // Provider init failed (DLLs missing, no driver, unsupported GPU):
      // fall through to the next provider; the single provider notice comes
      // from providerName_ at the end (design D1).
      providerName_ = "cpu";
    }
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
