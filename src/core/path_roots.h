#pragma once

#include <filesystem>
#include <optional>

namespace pathroots {

namespace fs = std::filesystem;

inline auto normalizeInputRootDir(fs::path const& inputPath) -> fs::path {
  return fs::is_directory(inputPath) ? inputPath : inputPath.parent_path();
}

inline auto commonAncestorPath(fs::path const& lhs, fs::path const& rhs)
  -> std::optional<fs::path> {
  auto const normalizedLhs = lhs.lexically_normal();
  auto const normalizedRhs = rhs.lexically_normal();

  auto result = fs::path{};
  auto lhsIt = normalizedLhs.begin();
  auto rhsIt = normalizedRhs.begin();
  while (lhsIt != normalizedLhs.end()
         && rhsIt != normalizedRhs.end()
         && *lhsIt == *rhsIt) {
    result /= *lhsIt;
    ++lhsIt;
    ++rhsIt;
  }

  if (result.empty()) { return std::nullopt; }
  return result.lexically_normal();
}

}  // namespace pathroots
