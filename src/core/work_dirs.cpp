#include "core/work_dirs.h"

#include "core/path_roots.h"

#if defined(_WIN32)
  #include <process.h>
#else
  #include <unistd.h>
#endif

#include <chrono>
#include <format>
#include <span>
#include <string>
#include <system_error>

namespace workdirs {

namespace fs = std::filesystem;

namespace {

// Path component naming this process's scratch root. The pid alone is not
// enough (a reused pid would inherit a previous run's leftovers), so the tag
// also carries a clock reading taken once per process. The static keeps the
// value stable for the whole run: every caller of scratchDir() must land in the
// directory that ensureScratchDir() created.
// ponytail: pid+clock nonce; add a boot-time seed if pid reuse ever collides.
auto scratchRunTag() -> std::string {
  static auto const tag = [] {
#if defined(_WIN32)
    auto const pid = static_cast<long long>(::_getpid());
#else
    auto const pid = static_cast<long long>(::getpid());
#endif
    auto const startedAt = std::chrono::system_clock::now().time_since_epoch().count();
    return std::format("{}-{}", pid, startedAt);
  }();
  return tag;
}

// Parent of every per-process scratch root; the sweep target.
auto scratchParentDir() -> fs::path {
  return fs::temp_directory_path() / "encro" / kScratchDirName;
}

}  // namespace

auto scratchDir() -> fs::path {
  return scratchParentDir() / scratchRunTag();
}

void ensureScratchDir() {
  auto ec = std::error_code{};
  fs::create_directories(scratchDir(), ec);
}

void sweepScratchDir() {
  using namespace std::chrono;
  constexpr auto kStaleAfter = 24h;
  auto const dir = scratchParentDir();
  auto ec = std::error_code{};
  if (!fs::is_directory(dir, ec) || ec) { return; }
  auto const now = fs::file_time_type::clock::now();
  for (
    auto const& entry:
    fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)
  ) {
    auto entryEc = std::error_code{};
    auto const mtime = entry.last_write_time(entryEc);
    if (entryEc) { continue; }
    if (mtime < now - kStaleAfter) { fs::remove_all(entry.path(), entryEc); }
  }
}

auto encroDir(fs::path const& workRoot) -> fs::path {
  return workRoot / kEncroDirName;
}

auto ensureEncroDir(fs::path const& workRoot) -> eh::Result<fs::path> {
  auto dir = encroDir(workRoot);
  auto ec = std::error_code{};
  fs::create_directories(dir, ec);
  if (ec) {
    return eh::makeError(
      "Failed to create work directory: {} ({})",
      dir.string(),
      ec.message()
    );
  }
  setHiddenAttribute(dir);
  return dir;
}

auto segmentsDir(fs::path const& workRoot, std::string_view taskHash) -> fs::path {
  return workRoot / kEncroDirName / kSegmentsDirName / taskHash;
}

auto compressCacheDir(fs::path const& workRoot, int quality) -> fs::path {
  return workRoot / kEncroDirName / std::format("compress_q{}", quality);
}

auto jobStateFile(fs::path const& workRoot) -> fs::path {
  return workRoot / kEncroDirName / "job-state.json";
}

namespace {

auto lowestCommonAncestor(std::span<fs::path const> paths) -> std::optional<fs::path> {
  if (paths.empty()) { return std::nullopt; }
  auto result = std::optional<fs::path>{paths.front()};
  for (auto const& path: paths) {
    if (!result.has_value()) { return std::nullopt; }
    result = pathroots::commonAncestorPath(*result, path);
  }
  return result;
}

}  // namespace

auto resolveWorkRoot(appctx::AppConfig const& config) -> eh::Result<fs::path> {
  if (config.outputPath.has_value()) { return *config.outputPath; }

  auto const isWebpVideo = config.processType == "video" && config.outputFormat == "webp";

  if (!config.inputPath.empty()) {
    auto root = pathroots::normalizeInputRootDir(config.inputPath);
    if (isWebpVideo) { root /= "encoded_webp"; }
    return root;
  }

  if (
    auto const ancestor = lowestCommonAncestor(config.inputPaths); ancestor.has_value()
  ) {
    // A lowest common ancestor that is a filesystem root (e.g. "/" on POSIX,
    // a drive root on Windows) is not a usable work root: nothing meaningful
    // lives there and it is usually not writable.
    if (*ancestor != ancestor->root_path()) {
      return isWebpVideo ? *ancestor / "encoded_webp" : *ancestor;
    }
  }

  if (isWebpVideo) {
    return eh::makeError(
      "Failed to resolve work root for webp encoding: inputs have no common "
      "ancestor directory. Pass --output/-o."
    );
  }
  return eh::makeError(
    "Failed to resolve work root: inputs have no common ancestor directory "
    "(e.g. inputs on different drives). Pass --output/-o."
  );
}

}  // namespace workdirs
