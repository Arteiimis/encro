#pragma once
#include "pack/pack_plan_internal.h"
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace pack::internal {

auto buildGroupOrdinalRanges(std::vector<std::vector<fs::path>> const& groups)
  -> std::vector<FileOrdinalRange>;
auto buildGroupOrdinalRanges(std::vector<std::vector<PackFileEntry>> const& groups)
  -> std::vector<FileOrdinalRange>;
auto appendOrdinalRangeSuffix(std::string_view fileName, FileOrdinalRange const& range)
  -> std::string;
auto resolveZipNameForIndex(PackPlan const& plan, std::size_t index) -> std::string;
auto resolveProgressLabelForIndex(PackPlan const& plan, std::size_t index) -> std::string;
auto selectPackPlanIndexes(PackPlan const& plan, std::span<std::size_t const> indexes)
  -> PackPlan;

}  // namespace pack::internal
