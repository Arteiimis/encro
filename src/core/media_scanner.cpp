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

struct WalkOptions {
  bool recursive = false;
  // scanByExtensions drops hidden entries; packing keeps them.
  bool skipHiddenEntries = false;
  std::span<std::string_view const> skippedDirNames = {};
};

// The single tolerant walk every directory consumer shares: an unreadable
// entry or a mid-walk failure becomes a warning instead of an exception, and a
// directory the caller excluded is never descended into.
template<class TyCollect>
void walkDirectory(
  fs::path const& dirPath,
  WalkOptions const& options,
  ScanResult& out,
  TyCollect const& collect
) {
  auto noteWalkFailure = [&](std::error_code const& ec) {
    out.warnings.emplace_back(
      std::format("Failed while scanning {}: {}", dirPath.string(), ec.message())
    );
  };
  auto shouldDescend = [&](fs::directory_entry const& entry) {
    auto const& path = entry.path();
    if (options.skipHiddenEntries && isHiddenEntry(path)) { return false; }
    auto const name = path.filename().string();
    return std::ranges::find(options.skippedDirNames, name)
      == options.skippedDirNames.end();
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
      if (entry.is_directory() && !shouldDescend(entry)) {
        // Skipped directories (.encro, legacy .compress_tmp*, hidden work
        // dirs) are not descended into; their contents are not collected.
        it.disable_recursion_pending();
        continue;
      }
      collect(entry);
    }
    return;
  }

  auto ec = std::error_code{};
  for (auto const& entry: fs::directory_iterator(dirPath, kDirectoryOptions, ec)) {
    if (ec) {
      noteWalkFailure(ec);
      break;
    }
    collect(entry);
  }
}

void scanDir(
  fs::path const& dirPath,
  std::span<std::string_view const> extensions,
  bool recursive,
  ScanResult& out
) {
  walkDirectory(
    dirPath,
    WalkOptions{.recursive = recursive, .skipHiddenEntries = true},
    out,
    [&](fs::directory_entry const& entry) {
      if (isHiddenEntry(entry.path())) { return; }
      auto ec = std::error_code{};
      if (!entry.is_regular_file(ec) || ec) { return; }
      if (extensionMatches(entry.path(), extensions)) {
        out.matches.emplace_back(entry.path());
      }
    }
  );
}

}  // namespace

bool extensionMatches(
  fs::path const& filePath,
  std::span<std::string_view const> extensions
) {
  auto const ext = filePath.extension().string();
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
  auto const name = path.filename().string();
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
      root.string(),
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
      root.string(),
      ec ? ec.message() : std::string_view{"not a directory"}
    );
  }

  auto result = ScanResult{};
  walkDirectory(
    root,
    WalkOptions{.recursive = recursive, .skippedDirNames = skippedDirNames},
    result,
    [&](fs::directory_entry const& entry) {
      auto entryEc = std::error_code{};
      if (!entry.is_regular_file(entryEc) || entryEc) { return; }
      result.matches.emplace_back(entry.path());
    }
  );
  return result;
}

}  // namespace media
