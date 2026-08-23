#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string_view>

#if defined(_WIN32)
  #include <windows.h>
#endif

namespace workdirs {

namespace fs = std::filesystem;

inline constexpr auto kEncroDirName = ".encro";
inline constexpr auto kScratchDirName = "scratch";
inline constexpr auto kSegmentsDirName = "segments";

// Per-run transient scratch root on the OS temp volume. Swept at startup;
// see design D2/D3.
auto scratchDir() -> fs::path;

// Creates the scratch root if missing. Best-effort: writers create it lazily
// before spawning ffmpeg, whose failures surface at the caller anyway.
void ensureScratchDir();

// Marks a directory with the Windows Hidden attribute. No-op on POSIX.
// Idempotent: only calls SetFileAttributesW when the attribute is missing.
inline void setHiddenAttribute(fs::path const& path) {
#if defined(_WIN32)
  auto const wide = path.wstring();
  auto const attrs = ::GetFileAttributesW(wide.c_str());
  if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_HIDDEN) == 0) {
    ::SetFileAttributesW(wide.c_str(), attrs | FILE_ATTRIBUTE_HIDDEN);
  }
#else
  (void)path;
#endif
}

// Applies the Hidden attribute to the nearest `.encro` ancestor of `path`,
// i.e. the work dir that was just created as part of the chain. No-op when
// the path has no `.encro` component (e.g. an explicit --state-file dir).
inline void setHiddenOnEncroDir(fs::path const& path) {
  auto current = path;
  while (current.filename() != kEncroDirName) {
    auto const parent = current.parent_path();
    if (parent == current) { return; }  // filesystem root; no .encro ancestor
    current = parent;
  }
  setHiddenAttribute(current);
}

// Creates `<workRoot>\.encro\` and applies the Hidden attribute on Windows.
// Idempotent on re-runs.
auto ensureEncroDir(fs::path const& workRoot) -> eh::Result<fs::path>;

auto encroDir(fs::path const& workRoot) -> fs::path;

// `<workRoot>\.encro\segments\<hash>\` — resume data for one task.
auto segmentsDir(fs::path const& workRoot, std::string_view taskHash) -> fs::path;

// `<workRoot>\.encro\compress_q<N>\` — picture compression cache.
auto compressCacheDir(fs::path const& workRoot, int quality) -> fs::path;

// `<workRoot>\.encro\job-state.json` — default job-state file location.
auto jobStateFile(fs::path const& workRoot) -> fs::path;

// Deletes stale entries under the scratch dir older than 24 hours. Best-effort:
// failures are ignored, and fresh files (written by live runs) are never
// touched because ffmpeg/probe writers keep updating mtimes continuously.
void sweepScratchDir();

// Resolves the work root (the directory holding `.encro\`) from the run
// configuration. Anchor rule (design D1):
//   1. explicit --output
//   2. video webp without --output → `<input root>\encoded_webp`
//   3. single input directory → itself; single input file → its parent
//   4. multiple inputs → lowest common ancestor
//   5. empty inputs or no common ancestor (cross-drive) → error (never temp)
auto resolveWorkRoot(appctx::AppConfig const& config) -> eh::Result<fs::path>;

}  // namespace workdirs
