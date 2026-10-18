#include "core/path_utf8.h"
#include "core/media_scanner.h"

#include "logging/log_tags.h"
#include "logging/logging.h"

#include <algorithm>
#include <format>
#include <functional>

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::CORE_SCAN);

namespace media {

namespace {

constexpr auto kDirectoryOptions = fs::directory_options::skip_permission_denied;

// ASCII-only fold: A-Z to a-z, every other byte unchanged (no locale).
constexpr char asciiLower(char ch) {
  return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
}

auto shouldDescend(fs::directory_entry const& entry, WalkOptions const& options) {
  auto const& path = entry.path();
  if (options.skipHiddenEntries && isHiddenEntry(path)) { return false; }
  auto const name = pathutf8::fromPath(path.filename());
  return std::ranges::find(options.skippedDirNames, name)
    == options.skippedDirNames.end();
}

}  // namespace

void walkDirectory(
  fs::path const& dirPath,
  WalkOptions const& options,
  std::function<bool(fs::directory_entry const&)> const& onEntry,
  std::function<void(std::error_code const&)> const& onFailure
) {
  auto noteWalkFailure = [&](std::error_code const& ec) {
    if (onFailure) { onFailure(ec); }
  };

  if (options.recursive) {
    auto ec = std::error_code{};
    auto it = fs::recursive_directory_iterator(dirPath, kDirectoryOptions, ec);
    auto const end = fs::recursive_directory_iterator{};
    for (; it != end; it.increment(ec)) {
      if (ec) {
        noteWalkFailure(ec);
        break;
      }
      auto const& entry = *it;
      if (entry.is_directory() && !shouldDescend(entry, options)) {
        // Skipped directories (.encro, legacy .compress_tmp*, hidden work
        // dirs) are not descended into; their contents are not collected.
        it.disable_recursion_pending();
        continue;
      }
      if (!onEntry(entry)) { break; }
    }
    return;
  }

  auto ec = std::error_code{};
  for (auto const& entry: fs::directory_iterator(dirPath, kDirectoryOptions, ec)) {
    if (ec) {
      noteWalkFailure(ec);
      break;
    }
    if (!onEntry(entry)) { break; }
  }
}

namespace {

void scanDir(
  fs::path const& dirPath,
  std::span<std::string_view const> extensions,
  bool recursive,
  ScanResult& out
) {
  walkDirectory(
    dirPath,
    WalkOptions{.recursive = recursive, .skipHiddenEntries = true},
    [&](fs::directory_entry const& entry) {
      if (isHiddenEntry(entry.path())) { return true; }
      auto ec = std::error_code{};
      if (!entry.is_regular_file(ec) || ec) { return true; }
      if (extensionMatches(entry.path(), extensions)) {
        out.matches.emplace_back(entry.path());
      }
      return true;
    },
    [&](std::error_code const& ec) {
      out.warnings.emplace_back(
        std::format(
          "Failed while scanning {}: {}",
          pathutf8::fromPath(dirPath),
          ec.message()
        )
      );
    }
  );
}

}  // namespace

bool extensionMatches(
  fs::path const& filePath,
  std::span<std::string_view const> extensions
) {
  auto const ext = pathutf8::fromPath(filePath.extension());
  return std::ranges::any_of(extensions, [&ext](std::string_view candidate) {
    return std::ranges::equal(
      candidate,
      ext,
      std::ranges::equal_to{},
      asciiLower,
      asciiLower
    );
  });
}

bool isHiddenEntry(fs::path const& path) {
  auto const name = pathutf8::fromPath(path.filename());
  return !name.empty() && name.front() == '.';
}

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
      pathutf8::fromPath(root),
      ec ? ec.message() : std::string_view{"not a directory"}
    );
  }

  auto result = ScanResult{};
  scanDir(root, extensions, recursive, result);
  return result;
}

auto listFiles(
  fs::path const& root,
  bool recursive,
  std::span<std::string_view const> skippedDirNames
) -> eh::Result<ScanResult> {
  auto ec = std::error_code{};
  if (!fs::is_directory(root, ec) || ec) {
    return eh::makeError(
      "Input path is not a readable directory: {} ({})",
      pathutf8::fromPath(root),
      ec ? ec.message() : std::string_view{"not a directory"}
    );
  }

  auto result = ScanResult{};
  walkDirectory(
    root,
    WalkOptions{.recursive = recursive, .skippedDirNames = skippedDirNames},
    [&](fs::directory_entry const& entry) {
      auto entryEc = std::error_code{};
      if (!entry.is_regular_file(entryEc) || entryEc) { return true; }
      result.matches.emplace_back(entry.path());
      return true;
    },
    [&](std::error_code const& ec) {
      result.warnings.emplace_back(
        std::format(
          "Failed while scanning {}: {}",
          pathutf8::fromPath(root),
          ec.message()
        )
      );
    }
  );
  return result;
}

}  // namespace media
