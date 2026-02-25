#pragma once

#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace media {

auto scanByExtensions(
  fs::path const& root,
  std::span<std::string_view const> extensions,
  bool recursive
) -> std::vector<fs::path>;

}  // namespace media
