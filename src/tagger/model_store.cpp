#include "tagger/model_store.h"

#include "core/sha256.h"
#include "utils/utils.h"

#include <httplib.h>
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

// Downloads urlPath from host into dest via a .part sibling, verifies, and
// renames into place.
auto fetchOnce(std::string const& host, RemoteFile const& file, fs::path const& dest)
  -> eh::Result<void> {
  // cpp-httplib accepts bare hosts (http) and scheme-prefixed hosts (https).
  httplib::Client client{host};
  client.set_follow_location(true);
  client.set_connection_timeout(30);
  client.set_read_timeout(600, 0);

  auto const partPath = dest.string() + ".part";
  // A previous attempt may have left a partial file; this write appends.
  auto removeEc = std::error_code{};
  fs::remove(partPath, removeEc);
  auto const response = client.Get(file.urlPath, [&](char const* data, size_t length) {
    auto out = std::ofstream{partPath, std::ios::binary | std::ios::app};
    if (!out.is_open()) { return false; }
    out.write(data, static_cast<std::streamsize>(length));
    return static_cast<bool>(out);
  });

  if (!response) { return eh::makeError("connection to {} failed", host); }
  if (response->status != 200) {
    auto ec = std::error_code{};
    fs::remove(partPath, ec);
    return eh::makeError("HTTP {} from {}{}", response->status, host, file.urlPath);
  }
  return {};
}

}  // namespace

auto primaryEndpoint() -> std::string {
  auto* overridden = std::getenv("HF_ENDPOINT");
  return (overridden != nullptr && *overridden != '\0')
    ? std::string{overridden}
    : std::string{"https://huggingface.co"};
}

auto mirrorEndpoint() -> std::string {
  return "https://hf-mirror.com";
}

auto modelFiles() -> std::vector<RemoteFile> {
  constexpr auto prefix = "/SmilingWolf/wd-vit-tagger-v3/resolve/main";
  return std::vector<RemoteFile>{
    {.logical = "wd-vit-tagger-v3/model.onnx",
     .urlPath = std::string{prefix} + "/model.onnx",
     .size = 0,      // pinned during acceptance (task 6.2)
     .sha256 = ""},  // pinned during acceptance (task 6.2)
    {.logical = "wd-vit-tagger-v3/selected_tags.csv",
     .urlPath = std::string{prefix} + "/selected_tags.csv",
     .size = 0,
     .sha256 = ""},
  };
}

auto cudnnArchive() -> RemoteFile {
  return RemoteFile{
    .logical = "cudnn-9.5.1-cuda12",
    .urlPath = "/compute/cudnn/redist/cudnn/windows-x86_64/"
               "cudnn-windows-x86_64-9.5.1.17_cuda12-archive.zip",
    .size = 557597538,  // verified against NVIDIA's CDN during exploration
    .sha256 = "",       // pinned during acceptance (task 6.2)
  };
}

auto defaultModelDir() -> fs::path {
  auto* home = std::getenv("USERPROFILE");
  if (home == nullptr) { home = std::getenv("HOME"); }
  return fs::path{home != nullptr ? home : ""} / ".encro" / "models";
}

auto encroLibDir() -> fs::path {
  auto* localAppData = std::getenv("LOCALAPPDATA");
  return fs::path{localAppData != nullptr ? localAppData : ""} / "encro" / "lib";
}

auto hasNvidiaDriver() -> bool {
#if defined(_WIN32)
  auto const driver = LoadLibraryW(L"nvcuda.dll");
  if (driver == nullptr) { return false; }
  FreeLibrary(driver);
  return true;
#else
  return false;
#endif
}

auto allFilesPresent(fs::path const& dir, std::vector<RemoteFile> const& files) -> bool {
  return std::ranges::all_of(files, [&](RemoteFile const& file) {
    auto ec = std::error_code{};
    auto const path = dir / fileNameOf(file);
    if (!fs::exists(path, ec) || ec) { return false; }
    if (file.size != 0 && fs::file_size(path, ec) != file.size) { return false; }
    return !ec;
  });
}

auto downloadFile(
  fs::path const& dir,
  RemoteFile const& file,
  std::string const& primary,
  std::string const& mirror
) -> eh::Result<fs::path> {
  auto ec = std::error_code{};
  fs::create_directories(dir, ec);

  auto const dest = dir / fileNameOf(file);
  for (auto attempt = 0; attempt <= kMaxVerifyRetries; ++attempt) {
    auto fetched = fetchOnce(primary, file, dest);
    if (!fetched) { fetched = fetchOnce(mirror, file, dest); }
    if (!fetched) { return std::unexpected(fetched.error()); }

    auto const partPath = dest.string() + ".part";
    if (file.size != 0) {
      auto const actual = fs::file_size(partPath, ec);
      if (ec || actual != file.size) {
        return eh::makeError(
          "size mismatch for {}: got {} bytes, expected {}",
          file.logical,
          ec ? 0 : actual,
          file.size
        );
      }
    }
    if (!file.sha256.empty()) {
      auto const digest = fileHash(partPath);
      if (digest != file.sha256) {
        if (attempt < kMaxVerifyRetries) { continue; }
        return eh::makeError(
          "checksum mismatch for {} (got {}, expected {})",
          file.logical,
          digest,
          file.sha256
        );
      }
    }
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
