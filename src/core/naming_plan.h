// Grouped-candidate naming: the grouping and collision-order contract shared by
// the video output planner and the picture zip-entry planner. Each flow keeps
// its own candidate key and its own two names, so the sort order and the
// group/unique dispatch are stated once instead of twice.
#pragma once

#include "core/collision_naming.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

namespace core {

namespace fs = std::filesystem;

// Groups `inputs` by the candidate key `candidateKey` derives, then names every
// input. A group of one takes `uniqueName` unless `forceConflictNaming` asks
// for the conflict name anyway; a colliding group is sorted by
// `collisionnaming::stablePathString` first, so the result does not depend on
// directory iteration order.
template<class Ty>
auto planNamesByCandidate(
  std::span<fs::path const> inputs,
  std::function<fs::path(fs::path const& input)> const& candidateKey,
  bool forceConflictNaming,
  std::function<Ty(fs::path const& input, fs::path const& candidate)> const& uniqueName,
  std::function<Ty(fs::path const& input, fs::path const& candidate)> const& conflictName
) -> std::unordered_map<fs::path, Ty> {
  auto grouped = std::unordered_map<fs::path, std::vector<fs::path>>{};
  grouped.reserve(inputs.size());
  for (auto const& input: inputs) { grouped[candidateKey(input)].push_back(input); }

  auto planned = std::unordered_map<fs::path, Ty>{};
  planned.reserve(inputs.size());
  for (auto const& [candidate, group]: grouped) {
    if (group.size() == 1 && !forceConflictNaming) {
      planned[group.front()] = uniqueName(group.front(), candidate);
      continue;
    }

    auto sortedGroup = group;
    std::ranges::sort(sortedGroup, [](fs::path const& lhs, fs::path const& rhs) {
      return collisionnaming::stablePathString(lhs)
        < collisionnaming::stablePathString(rhs);
    });
    for (auto const& input: sortedGroup) {
      planned[input] = conflictName(input, candidate);
    }
  }
  return planned;
}

}  // namespace core
