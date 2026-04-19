#include "core/media_scanner.h"

namespace media {

namespace {

bool extensionMatches(
  fs::path const& filePath,
  std::span<std::string_view const> extensions
) {
  auto const ext = filePath.extension().string();
  return std::ranges::contains(extensions, std::string_view{ext});
}

auto scanDir(
  fs::path const& dirPath,
  std::span<std::string_view const> extensions,
  bool recursive
) -> std::vector<fs::path> {
  auto matches = std::vector<fs::path>{};

  if (recursive) {
    for (auto const& entry: fs::recursive_directory_iterator(dirPath)) {
      if (!entry.is_regular_file()) { continue; }
      if (extensionMatches(entry.path(), extensions)) {
        matches.emplace_back(entry.path());
      }
    }
  } else {
    for (auto const& entry: fs::directory_iterator(dirPath)) {
      if (!entry.is_regular_file()) { continue; }
      if (extensionMatches(entry.path(), extensions)) {
        matches.emplace_back(entry.path());
      }
    }
  }

  return matches;
}

}  // namespace

auto scanByExtensions(
  fs::path const& root,
  std::span<std::string_view const> extensions,
  bool recursive
) -> std::vector<fs::path> {
  if (fs::is_regular_file(root)) {
    if (extensionMatches(root, extensions)) { return {root}; }
    return {};
  }

  if (!fs::is_directory(root)) { return {}; }

  return scanDir(root, extensions, recursive);
}

}  // namespace media
