#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace collisionnaming {

namespace fs = std::filesystem;

inline auto stablePathString(fs::path const& path) -> std::string {
  auto normalized = path.lexically_normal().generic_string();
  std::ranges::transform(normalized, normalized.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return normalized;
}

inline auto fnv1a32(std::string_view text) -> std::uint32_t {
  auto hash = std::uint32_t{2166136261u};
  for (auto const ch: text) {
    hash ^= static_cast<unsigned char>(ch);
    hash *= 16777619u;
  }
  return hash;
}

inline auto shortPathHash(fs::path const& path) -> std::string {
  return std::format("{:08x}", fnv1a32(stablePathString(path)));
}

inline auto sanitizeLabel(std::string_view text) -> std::string {
  auto sanitized = std::string{};
  sanitized.reserve(text.size());

  auto lastWasSeparator = false;
  for (auto const ch: text) {
    if (std::isalnum(static_cast<unsigned char>(ch))) {
      sanitized.push_back(static_cast<char>(std::tolower(ch)));
      lastWasSeparator = false;
      continue;
    }

    if (!lastWasSeparator) {
      sanitized.push_back('_');
      lastWasSeparator = true;
    }
  }

  while (!sanitized.empty() && sanitized.front() == '_') {
    sanitized.erase(sanitized.begin());
  }
  while (!sanitized.empty() && sanitized.back() == '_') { sanitized.pop_back(); }

  return sanitized;
}

inline auto relativeParentPath(
  std::optional<fs::path> const& sourceRootDir,
  fs::path const& inputPath
) -> std::optional<fs::path> {
  if (!sourceRootDir.has_value()) { return std::nullopt; }

  auto const relativePath =
    inputPath.parent_path().lexically_relative(sourceRootDir.value());
  if (relativePath.empty() || relativePath == fs::path{"."}) { return std::nullopt; }

  return relativePath;
}

inline auto relativeParentPath(fs::path const& rootDir, fs::path const& inputPath)
  -> std::optional<fs::path> {
  return relativeParentPath(std::optional<fs::path>{rootDir}, inputPath);
}

inline auto buildCollisionGroupLabel(
  std::optional<fs::path> const& sourceRootDir,
  fs::path const& inputPath
) -> std::string {
  auto label = std::string{};
  if (
    auto const relativePath = relativeParentPath(sourceRootDir, inputPath);
    relativePath.has_value()
  ) {
    label = sanitizeLabel(relativePath->generic_string());
  } else if (!inputPath.parent_path().filename().empty()) {
    label = sanitizeLabel(inputPath.parent_path().filename().string());
  }

  if (label.empty() && inputPath.has_extension()) {
    auto const extension = inputPath.extension().string();
    auto const extensionView =
      std::string_view{extension}.substr(extension.starts_with('.') ? 1 : 0);
    label = sanitizeLabel(extensionView);
  }

  if (label.empty()) { label = "src"; }

  return label;
}

inline auto buildCollisionGroupLabel(fs::path const& rootDir, fs::path const& inputPath)
  -> std::string {
  return buildCollisionGroupLabel(std::optional<fs::path>{rootDir}, inputPath);
}

inline auto buildCollisionGroupIdentity(
  std::optional<fs::path> const& sourceRootDir,
  fs::path const& inputPath
) -> fs::path {
  if (
    auto const relativePath = relativeParentPath(sourceRootDir, inputPath);
    relativePath.has_value()
  ) {
    return relativePath.value();
  }

  return inputPath.parent_path();
}

inline auto
buildCollisionGroupIdentity(fs::path const& rootDir, fs::path const& inputPath)
  -> fs::path {
  return buildCollisionGroupIdentity(std::optional<fs::path>{rootDir}, inputPath);
}

inline auto buildCollisionGroupPrefix(
  std::optional<fs::path> const& sourceRootDir,
  fs::path const& inputPath
) -> std::string {
  return std::format(
    "{}__{}",
    buildCollisionGroupLabel(sourceRootDir, inputPath),
    shortPathHash(buildCollisionGroupIdentity(sourceRootDir, inputPath))
  );
}

inline auto buildCollisionGroupPrefix(fs::path const& rootDir, fs::path const& inputPath)
  -> std::string {
  return buildCollisionGroupPrefix(std::optional<fs::path>{rootDir}, inputPath);
}

inline auto buildConflictHandledFlatName(
  std::optional<fs::path> const& sourceRootDir,
  fs::path const& inputPath,
  std::string_view stem,
  std::string_view extension
) -> std::string {
  return std::format(
    "{}__{}__{}{}",
    buildCollisionGroupPrefix(sourceRootDir, inputPath),
    stem,
    shortPathHash(inputPath),
    extension
  );
}

inline auto buildConflictHandledFlatName(
  fs::path const& rootDir,
  fs::path const& inputPath,
  std::string_view stem,
  std::string_view extension
) -> std::string {
  return buildConflictHandledFlatName(
    std::optional<fs::path>{rootDir},
    inputPath,
    stem,
    extension
  );
}

}  // namespace collisionnaming
