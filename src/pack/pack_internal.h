#pragma once
#include "pack/pack_plan_internal.h"
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace pack::internal {

// Assigns the running "part ordinal" range to each group (empty groups get
// an empty range); shared by the plan builders over both row types.
template<class GroupRow>
auto buildGroupOrdinalRanges(std::vector<std::vector<GroupRow>> const& groups)
  -> std::vector<FileOrdinalRange> {
  auto ranges = std::vector<FileOrdinalRange>{};
  ranges.reserve(groups.size());

  auto nextOrdinal = std::size_t{1};
  for (auto const& group: groups) {
    if (group.empty()) {
      ranges.push_back({});
      continue;
    }

    auto const first = nextOrdinal;
    auto const last = first + group.size() - 1;
    ranges.push_back({first, last, group.size()});
    nextOrdinal = last + 1;
  }

  return ranges;
}

auto appendOrdinalRangeSuffix(std::string_view fileName, FileOrdinalRange const& range)
  -> std::string;
auto defaultProgressLabelForZipName(std::string_view zipName) -> std::string;
auto resolveZipNameForIndex(PackPlan const& plan, std::size_t index) -> std::string;
auto selectPackPlanIndexes(PackPlan const& plan, std::span<std::size_t const> indexes)
  -> PackPlan;

}  // namespace pack::internal
