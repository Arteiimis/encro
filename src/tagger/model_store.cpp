#include "tagger/model_store.h"

#include "core/sha256.h"
#include "infra/crash_runtime.h"
#include "infra/env.h"
#include "utils/utils.h"

#include <libzippp/libzippp.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <string_view>
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

// Bounded re-download attempts when a checksum mismatches.
constexpr auto kMaxVerifyRetries = 2;

auto fileNameOf(RemoteFile const& file) -> std::string {
  auto const slash = file.urlPath.find_last_of('/');
  return slash == std::string::npos ? file.urlPath : file.urlPath.substr(slash + 1);
}

auto fileHash(fs::path const& path) -> std::string {
  auto file = std::ifstream{path, std::ios::binary};
  if (!file.is_open()) { return {}; }
  auto const bytes = std::string{std::istreambuf_iterator<char>{file}, {}};
  return core::sha256Hex(bytes);
}

// Downloads urlPath from endpoint into dest via a .part sibling, using the
// platform curl: Windows ships one since Win10 1803 and Linux distros have
// it, so TLS and the trust store stay the OS's own (no static OpenSSL in the
// binary). -f turns HTTP errors into exit codes, -L follows endpoint
// redirects, -sS keeps only the error line, --retry rides out transient
// drops. ponytail: output is captured, so no live progress bar -- the
// downloadMissing report lines are the progress; inherit stderr instead of
// capturing if that ever feels blind.
auto fetchOnce(std::string const& endpoint, RemoteFile const& file, fs::path const& dest)
  -> eh::Result<void> {
  auto const partPath = dest.string() + ".part";
  // A previous attempt may have left a partial file; start each attempt
  // fresh so a corrupt or foreign partial can never verify by accident.
  auto removeEc = std::error_code{};
  fs::remove(partPath, removeEc);
  auto const command = std::format(
    "curl -fsSL --retry 3 --connect-timeout 30 -o \"{}\" \"{}\"",
    partPath,
    endpoint + file.urlPath
  );
  auto const result = exec2(command, true);
  if (result.exitCode != 0) {
    auto ec = std::error_code{};
    fs::remove(partPath, ec);
    return eh::makeError(
      "download from {} failed (curl exit {}): {}",
      endpoint,
      result.exitCode,
      result.output
    );
  }
  return {};
}

}  // namespace

auto primaryEndpoint() -> std::string {
  auto const overridden = processenv::readNonEmptyEnvVar("HF_ENDPOINT");
  return overridden.value_or("https://huggingface.co");
}

auto mirrorEndpoint() -> std::string {
  return "https://hf-mirror.com";
}

auto modelFiles() -> std::vector<RemoteFile> {
  constexpr auto prefix = "/SmilingWolf/wd-vit-tagger-v3/resolve/main";
  return std::vector<RemoteFile>{
    {.logical = "wd-vit-tagger-v3/model.onnx",
     .urlPath = std::string{prefix} + "/model.onnx",
     .size = 378536310,
     .sha256 = "35f23693620b668f4d53fd3c62bf65e40af739bc52c7eb0fbc49258b58d065b6"},
    {.logical = "wd-vit-tagger-v3/selected_tags.csv",
     .urlPath = std::string{prefix} + "/selected_tags.csv",
     .size = 308468,
     .sha256 = "298633d94d0031d2081c0893f29c82eab7f0df00b08483ba8f29d1e979441217"},
  };
}

auto cudnnArchive() -> RemoteFile {
  return RemoteFile{
    .logical = "cudnn-9.5.1-cuda12",
    .urlPath = "/compute/cudnn/redist/cudnn/windows-x86_64/"
               "cudnn-windows-x86_64-9.5.1.17_cuda12-archive.zip",
    .size = 557597538,
    .sha256 =
      "3a4cecc8b6d6aa7f6777620e6f2c129b76be635357c4506f2c4ccdbe0e2a1641",  // acceptance-pinned
  };
}

auto defaultModelDir() -> fs::path {
  auto const home = processenv::readEnvVar("USERPROFILE");
  auto const homeDir = home.has_value() ? home : processenv::readEnvVar("HOME");
  return fs::path{homeDir.value_or("")} / ".encro" / "models";
}

auto encroLibDir() -> fs::path {
  auto const localAppData = processenv::readEnvVar("LOCALAPPDATA");
  return fs::path{localAppData.value_or("")} / "encro" / "lib";
}

bool hasNvidiaDriver() {
#if defined(_WIN32)
  // ponytail: guards our own load window; DLL init inside third-party library
  // calls (no load site of ours) stays the ceiling — bounded helper-thread
  // capture is the upgrade path if it ever bites.
  auto const dllLoadZone = crash::ScopedDllLoadZone{};
  auto const driver = LoadLibraryW(L"nvcuda.dll");
  if (driver == nullptr) { return false; }
  FreeLibrary(driver);
  return true;
#else
  return false;
#endif
}

bool allFilesPresent(fs::path const& dir, std::vector<RemoteFile> const& files) {
  return std::ranges::all_of(files, [&](RemoteFile const& file) {
    auto ec = std::error_code{};
    auto const path = dir / fileNameOf(file);
    if (!fs::exists(path, ec) || ec) { return false; }
    if (file.size != 0 && fs::file_size(path, ec) != file.size) { return false; }
    return !ec;
  });
}

namespace {

enum class VerifyOutcome {
  Accept,
  Mismatch,
  Reject
};

// Size/checksum verification of a fetched .part file. Size mismatches and
// unpinned-but-wrong states are fatal (Reject); a checksum mismatch with
// attempts left is retryable (Mismatch).
auto verifyDownload(fs::path const& dest, RemoteFile const& file, bool retrying)
  -> std::pair<VerifyOutcome, std::string> {
  auto ec = std::error_code{};
  auto const partPath = dest.string() + ".part";
  if (file.size != 0) {
    auto const actual = fs::file_size(partPath, ec);
    if (ec || actual != file.size) {
      return {
        VerifyOutcome::Reject,
        std::format(
          "size mismatch for {}: got {} bytes, expected {}",
          file.logical,
          ec ? 0 : actual,
          file.size
        )
      };
    }
  }
  if (!file.sha256.empty()) {
    auto const digest = fileHash(partPath);
    if (digest != file.sha256) {
      if (!retrying) {
        return {
          VerifyOutcome::Reject,
          std::format(
            "checksum mismatch for {} (got {}, expected {})",
            file.logical,
            digest,
            file.sha256
          )
        };
      }
      return {VerifyOutcome::Mismatch, ""};
    }
  }
  return {VerifyOutcome::Accept, ""};
}

}  // namespace

auto downloadFile(
  fs::path const& dir,
  RemoteFile const& file,
  std::string const& primary,
  std::string const& mirror
) -> eh::Result<fs::path> {
  auto ec = std::error_code{};
  fs::create_directories(dir, ec);

  auto dest = dir / fileNameOf(file);
  for (auto attempt = 0; attempt <= kMaxVerifyRetries; ++attempt) {
    auto fetched = fetchOnce(primary, file, dest);
    if (!fetched) { fetched = fetchOnce(mirror, file, dest); }
    if (!fetched) { return std::unexpected(fetched.error()); }

    auto const [outcome, errorText] =
      verifyDownload(dest, file, attempt < kMaxVerifyRetries);
    if (outcome == VerifyOutcome::Reject) { return eh::makeError("{}", errorText); }
    if (outcome == VerifyOutcome::Mismatch) { continue; }
    auto renameEc = std::error_code{};
    fs::rename(dest.string() + ".part", dest, renameEc);
    if (renameEc) {
      return eh::makeError("cannot finalize download of {}", file.logical);
    }
    return dest;
  }
  return eh::makeError("download failed: {}", file.logical);
}

auto ensureCudnn(fs::path const& libDir, bool driverPresent) -> eh::Result<bool> {
  if (!driverPresent) { return false; }

  auto ec = std::error_code{};
  auto const marker = libDir / "cudnn64_9.dll";
  if (fs::exists(marker, ec) && !ec) { return true; }

  auto const archive = cudnnArchive();
  auto const zipPath = downloadFile(
    defaultModelDir(),
    archive,
    "https://developer.download.nvidia.com",
    "https://developer.download.nvidia.com"
  );
  if (!zipPath) { return std::unexpected(zipPath.error()); }

  auto zip = libzippp::ZipArchive{zipPath->string()};
  if (!zip.open()) {
    return eh::makeError("cannot open cuDNN archive: {}", zipPath->string());
  }
  fs::create_directories(libDir, ec);
  auto extracted = std::size_t{0};
  for (auto const& entry: zip.getEntries()) {
    auto const name = entry.getName();
    if (entry.isDirectory()) { continue; }
    if (name.find("bin/") == std::string::npos || !name.ends_with(".dll")) { continue; }
    auto const slash = name.find_last_of('/');
    auto const dest = libDir / name.substr(slash + 1);
    auto const bytes = entry.readAsText();
    auto out = std::ofstream{dest, std::ios::binary};
    if (!out.is_open()) { return eh::makeError("cannot write {}", dest.string()); }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    ++extracted;
  }
  zip.close();
  if (extracted == 0) { return eh::makeError("no DLLs found in cuDNN archive"); }
  return true;
}

auto downloadMissing(
  fs::path const& modelDir,
  std::function<void(std::string_view)> const& report
) -> eh::Result<void> {
  auto const models = modelFiles();
  if (!allFilesPresent(modelDir, models)) {
    for (auto const& file: models) {
      auto const dest = modelDir / fileNameOf(file);
      auto ec = std::error_code{};
      if (fs::exists(dest, ec) && !ec) { continue; }
      if (report) { report(std::format("downloading {}", file.logical)); }
      auto const installed =
        downloadFile(modelDir, file, primaryEndpoint(), mirrorEndpoint());
      if (!installed) { return std::unexpected(installed.error()); }
      if (report) { report(std::format("installed {}", installed->string())); }
    }
  }

  auto const cudnn = ensureCudnn(encroLibDir());
  if (!cudnn) { return std::unexpected(cudnn.error()); }
  if (cudnn.value() && report) { report("installed cuDNN runtime DLLs"); }
  return {};
}

}  // namespace tagger
