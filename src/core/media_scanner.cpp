#include "core/media_scanner.h"

namespace media {

namespace {

constexpr auto kDirectoryOptions = fs::directory_options::skip_permission_denied;

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
  auto collectEntry = [&](fs::directory_entry const& entry) {
    auto ec = std::error_code{};
    if (!entry.is_regular_file(ec) || ec) { return; }
    if (extensionMatches(entry.path(), extensions)) {
      matches.emplace_back(entry.path());
    }
  };

  if (recursive) {
    auto ec = std::error_code{};
    for (
      auto const& entry: fs::recursive_directory_iterator(dirPath, kDirectoryOptions, ec)
    ) {
      if (ec) { break; }
      collectEntry(entry);
    }
  } else {
    auto ec = std::error_code{};
    for (auto const& entry: fs::directory_iterator(dirPath, kDirectoryOptions, ec)) {
      if (ec) { break; }
      collectEntry(entry);
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
  auto ec = std::error_code{};
  if (fs::is_regular_file(root, ec) && !ec) {
    if (extensionMatches(root, extensions)) { return {root}; }
    return {};
  }

  ec.clear();
  if (!fs::is_directory(root, ec) || ec) { return {}; }

  return scanDir(root, extensions, recursive);
}

}  // namespace media
