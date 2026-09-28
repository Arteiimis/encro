// Shared ONNX Runtime plumbing for the model engines (design D7): the
// CUDA/cuDNN search paths a self-installed runtime needs, and the
// execution-provider chain with its one-line notice name.
//
// Windows-only, like the ORT binaries themselves: xmake removes every
// src/tagger/onnx_*.cpp from other platforms, so no other target may include
// this header.
#pragma once

#include <onnxruntime_cxx_api.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace tagger {

// An ORT environment plus a session created with the provider chain (CUDA,
// then DirectML, then CPU). The environment must outlive its sessions, so
// both live here.
class OnnxModel {
public:
  // `logLabel` names the environment in ORT's logs; `modelKind` names the
  // model in a load failure ("cannot load <kind> model <path>: <reason>").
  OnnxModel(fs::path const& modelPath, char const* logLabel, std::string_view modelKind);

  OnnxModel(OnnxModel const&) = delete;
  auto operator=(OnnxModel const&) -> OnnxModel& = delete;
  ~OnnxModel();

  auto session() const -> Ort::Session& { return *session_; }

  // "cuda", "dml" or "cpu": the provider that accepted the model.
  auto providerName() const -> std::string const& { return providerName_; }

private:
  std::unique_ptr<Ort::Env> env_;
  std::unique_ptr<Ort::Session> session_;
  std::string providerName_ = "cpu";
};

}  // namespace tagger
