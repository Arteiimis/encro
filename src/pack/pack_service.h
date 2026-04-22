#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace pack {

struct PackRunResult {
  int exitCode = 0;
  std::vector<fs::path> zippedFiles;
};

struct PackFileEntry {
  fs::path sourcePath;
  std::string zipEntryName;

  auto operator==(PackFileEntry const&) const -> bool = default;
};

struct FileOrdinalRange {
  std::size_t first = 0;
  std::size_t last = 0;
  std::size_t count = 0;
};

struct PackPlan {
  std::vector<std::vector<PackFileEntry>> groups;
  fs::path outputDir;
  std::function<std::string(std::size_t)> zipNameForIndex;
  std::function<std::string(std::size_t)> progressLabelForIndex;
  std::function<void(std::size_t)> onGroupStart;
  std::function<void(std::size_t, fs::path const&)> onGroupSuccess;
  std::function<void(std::size_t, std::string const&)> onGroupFailure;
  std::optional<std::size_t> maxParallelJobs;
  bool removeOnFailure = false;
};

auto buildGroupOrdinalRanges(std::vector<std::vector<fs::path>> const& groups)
  -> std::vector<FileOrdinalRange>;

auto buildGroupOrdinalRanges(std::vector<std::vector<PackFileEntry>> const& groups)
  -> std::vector<FileOrdinalRange>;

auto appendOrdinalRangeSuffix(std::string_view fileName, FileOrdinalRange const& range)
  -> std::string;

auto defaultZipNameForIndex(std::size_t index) -> std::string;

auto defaultProgressLabelForZipName(std::string_view zipName) -> std::string;

auto resolveZipNameForIndex(PackPlan const& plan, std::size_t index) -> std::string;

auto resolveProgressLabelForIndex(PackPlan const& plan, std::size_t index) -> std::string;

auto selectPackPlanIndexes(PackPlan const& plan, std::span<std::size_t const> indexes)
  -> PackPlan;

auto runPackPlan(appctx::AppContext& ctx, PackPlan const& plan)
  -> eh::Result<PackRunResult>;

auto packGroups(PackPlan const& plan) -> eh::Result<std::vector<fs::path>>;

}  // namespace pack
