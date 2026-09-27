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

}  // namespace media
