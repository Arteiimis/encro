#pragma once

#include "infra/stop_signal.h"

#include <catch2/catch_test_macros.hpp>
#include <libzippp/libzippp.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

struct TempDir {
  fs::path path;

  TempDir() {
    path = fs::temp_directory_path();
    path /= std::format(
      "video_encoder_tests_{}",
      std::chrono::steady_clock::now().time_since_epoch().count()
    );
    fs::create_directories(path);
  }

  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

namespace testutils {

struct ScopedStopSignalReset {
  ScopedStopSignalReset() { stopsignal::reset(); }

  ~ScopedStopSignalReset() { stopsignal::reset(); }
};

inline auto writeTextFile(fs::path const& filePath, std::string_view content = "x")
  -> void {
  auto const parentPath = filePath.parent_path();
  if (!parentPath.empty()) { fs::create_directories(parentPath); }

  auto out = std::ofstream{filePath, std::ios::binary};
  REQUIRE(out.is_open());
  out << content;
}

inline auto writeFile(fs::path const& filePath, std::string_view content = "x") -> void {
  writeTextFile(filePath, content);
}

inline auto touchFile(fs::path const& filePath) -> void {
  writeTextFile(filePath);
}

inline auto stripCollisionSafePrefix(std::string_view entryName) -> std::string_view {
  constexpr auto kFlatEntryPrefix = std::string_view{"1000__"};
  return entryName.starts_with(kFlatEntryPrefix)
    ? entryName.substr(kFlatEntryPrefix.size())
    : entryName;
}

inline auto hasCollisionSafePrefix(
  std::string_view entryName,
  std::string_view dirLabel,
  std::string_view stem
) -> bool {
  auto const normalized = stripCollisionSafePrefix(entryName);
  return normalized.starts_with(std::format("{}__", dirLabel))
    && normalized.find(std::format("__{}__", stem)) != std::string_view::npos;
}

inline auto collisionGroupPrefix(std::string_view entryName) -> std::string {
  auto const normalized = stripCollisionSafePrefix(entryName);
  auto const lastSep = normalized.rfind("__");
  if (lastSep == std::string_view::npos) { return std::string{normalized}; }

  auto const stemSep = normalized.rfind("__", lastSep - 1);
  if (stemSep == std::string_view::npos) {
    return std::string{normalized.substr(0, lastSep)};
  }

  return std::string{normalized.substr(0, stemSep)};
}

inline auto listRegularFiles(fs::path const& dirPath) -> std::vector<fs::path> {
  auto files = std::vector<fs::path>{};
  if (!fs::exists(dirPath)) { return files; }

  for (auto const& entry: fs::directory_iterator{dirPath}) {
    if (entry.is_regular_file()) { files.push_back(entry.path()); }
  }

  std::ranges::sort(files);
  return files;
}

inline auto listZipRegularEntryNames(fs::path const& zipPath)
  -> std::vector<std::string> {
  auto zip = libzippp::ZipArchive{zipPath.string()};
  zip.open(libzippp::ZipArchive::ReadOnly);

  auto entryNames = std::vector<std::string>{};
  auto const entries = zip.getEntries();
  entryNames.reserve(entries.size());
  for (auto const& entry: entries) {
    if (entry.getName().ends_with('/')) { continue; }
    entryNames.emplace_back(entry.getName());
  }

  std::ranges::sort(entryNames);
  zip.close();
  return entryNames;
}

}  // namespace testutils
