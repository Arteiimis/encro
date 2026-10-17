#pragma once

#include "core/error_handle.h"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace media {

struct ScanResult {
  std::vector<fs::path> matches;
  std::vector<std::string> warnings;
};

// True when `filePath`'s extension matches one of `extensions`. Letters are
// compared with an ASCII case fold (A-Z matches a-z); every other byte,
// including non-ASCII, must match exactly. The fold never consults the
// process locale.
bool extensionMatches(
  fs::path const& filePath,
  std::span<std::string_view const> extensions
);

// True for entries whose filename starts with a dot (e.g. ".encro") — the
// program's own hidden work entries; never scanned as input.
bool isHiddenEntry(fs::path const& path);

// Scans `root` (a file or directory) for entries whose extension matches.
// Returns an error when `root` is not a readable directory (missing, not a
// directory, permission denied) — never a silently empty result.
// Mid-walk iteration failures are collected in ScanResult::warnings instead
// of silently truncating the result set.
auto scanByExtensions(
  fs::path const& root,
  std::span<std::string_view const> extensions,
  bool recursive
) -> eh::Result<ScanResult>;

// Lists the regular files under `root`, skipping any directory whose name is
// in `skippedDirNames` (e.g. the program's own ".encro" work directory) and
// everything inside it. Same tolerance as scanByExtensions: an unreadable
// entry or a mid-walk failure is a warning, never an exception; `root` itself
// being unreadable is an error. Unlike a media scan, hidden files are kept —
// packing a user tree must keep its .gitignore.
auto listFiles(
  fs::path const& root,
  bool recursive,
  std::span<std::string_view const> skippedDirNames = {}
) -> eh::Result<ScanResult>;

}  // namespace media
