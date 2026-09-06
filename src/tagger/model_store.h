// Model/runtime file acquisition (task 2.4, design D7): a pinned manifest,
// streaming download with checksum verification, Hugging Face -> hf-mirror
// fallback with HF_ENDPOINT override, and gated cuDNN self-install.
#pragma once

#include "core/error_handle.h"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace tagger {

// One downloadable file: `urlPath` is appended to the resolved endpoint host.
struct RemoteFile {
  std::string logical;      // stable name in logs/progress
  std::string urlPath;      // e.g. /SmilingWolf/wd-vit-tagger-v3/resolve/main/model.onnx
  std::uintmax_t size = 0;  // 0 = unknown (size check skipped)
  std::string sha256;       // empty = not pinned yet (acceptance pins it)
};

// Endpoint hosts (design D7). HF_ENDPOINT overrides the primary.
auto primaryEndpoint() -> std::string;
auto mirrorEndpoint() -> std::string;

// wd-vit-tagger-v3 model + vocabulary; sha256 values are pinned during the
// real-machine acceptance (task 6.2).
auto modelFiles() -> std::vector<RemoteFile>;

// The pinned cuDNN 9 CUDA-12 archive (NVIDIA login-free CDN).
auto cudnnArchive() -> RemoteFile;

// Default model directory: ~/.encro/models.
auto defaultModelDir() -> fs::path;

// Where self-installed runtime DLLs live: %LOCALAPPDATA%\encro\lib.
auto encroLibDir() -> fs::path;

// True when an NVIDIA driver is present (nvcuda.dll loads).
bool hasNvidiaDriver();

// True when every file in `files` exists under `dir` (size-checked when the
// manifest pins a size).
bool allFilesPresent(fs::path const& dir, std::vector<RemoteFile> const& files);

// Downloads `file` to dir/<filename>: primary host first, mirror fallback,
// .part streaming, checksum verify (when pinned), bounded retry on mismatch,
// atomic rename. Returns the installed path.
auto downloadFile(
  fs::path const& dir,
  RemoteFile const& file,
  std::string const& primary,
  std::string const& mirror
) -> eh::Result<fs::path>;

// Installs the pinned cuDNN archive into `libDir` (extract bin/*.dll via
// libzippp). No-op success when the driver is absent; returns false then.
// `driverPresent` defaults to the live probe (tests inject it).
auto ensureCudnn(fs::path const& libDir, bool driverPresent = hasNvidiaDriver())
  -> eh::Result<bool>;

// Full --download-models action: fetch missing model files, then cuDNN when
// an NVIDIA driver is present. `report` receives per-file progress lines.
auto downloadMissing(
  fs::path const& modelDir,
  std::function<void(std::string_view)> const& report
) -> eh::Result<void>;

}  // namespace tagger
