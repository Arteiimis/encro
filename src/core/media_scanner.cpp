#include "core/media_scanner.h"

#include "logging/log_tags.h"
#include "logging/logging.h"

#include <algorithm>
#include <format>

DEFINE_LOGGER(logtags::CORE_SCAN);

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
  bool recursive,
  ScanResult& out
) -> void {
  auto collectEntry = [&](fs::directory_entry const& entry) {
    auto ec = std::error_code{};
    if (!entry.is_regular_file(ec) || ec) { return; }
    if (extensionMatches(entry.path(), extensions)) {
      out.matches.emplace_back(entry.path());
    }
  };
  auto noteScanFailure = [&](std::error_code const& ec) {
    out.warnings.emplace_back(
      std::format("Failed while scanning {}: {}", dirPath.string(), ec.message())
    );
  };

  if (recursive) {
    auto ec = std::error_code{};
    for (
      auto const& entry: fs::recursive_directory_iterator(dirPath, kDirectoryOptions, ec)
    ) {
      if (ec) {
        noteScanFailure(ec);
        break;
      }
      collectEntry(entry);
    }
  } else {
    auto ec = std::error_code{};
    for (auto const& entry: fs::directory_iterator(dirPath, kDirectoryOptions, ec)) {
      if (ec) {
        noteScanFailure(ec);
        break;
      }
      collectEntry(entry);
    }
  }
}

}  // namespace

auto scanByExtensions(
  fs::path const& root,
  std::span<std::string_view const> extensions,
  bool recursive
) -> eh::Result<ScanResult> {
  auto ec = std::error_code{};
  if (fs::is_regular_file(root, ec) && !ec) {
    auto result = ScanResult{};
    if (extensionMatches(root, extensions)) { result.matches.emplace_back(root); }
    return result;
  }

  ec.clear();
  if (!fs::is_directory(root, ec) || ec) {
    return eh::makeError(
      "Input path is not a readable directory: {} ({})",
      root.string(),
      ec ? ec.message() : std::string_view{"not a directory"}
    );
  }

  auto result = ScanResult{};
  scanDir(root, extensions, recursive, result);
  return result;
}

}  // namespace media
