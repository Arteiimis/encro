#include "core/archive_plan.h"

#include <memory>
#include <numeric>

namespace archiveplan {

namespace {

struct PackExecutionState {
  jobstate::Store* store = nullptr;
  std::vector<jobstate::ActionRecord> mergedActions;
  std::vector<std::size_t> pendingIndexes;

  auto actionIdForSubsetIndex(std::size_t subsetIndex) const -> std::string const& {
    return mergedActions[pendingIndexes.at(subsetIndex)].id;
  }
};

auto buildPlanIndexes(std::size_t groupCount) -> std::vector<std::size_t> {
  auto indexes = std::vector<std::size_t>(groupCount);
  std::iota(indexes.begin(), indexes.end(), std::size_t{0});
  return indexes;
}

auto buildArchiveActions(pack::PackPlan const& plan, std::span<std::size_t const> indexes)
  -> std::vector<jobstate::ActionRecord> {
  auto actions = std::vector<jobstate::ActionRecord>{};
  actions.reserve(indexes.size());

  for (auto const index: indexes) {
    auto const zipName = pack::resolveZipNameForIndex(plan, index);
    auto const label = pack::resolveProgressLabelForIndex(plan, index);
    auto members = std::vector<std::filesystem::path>{};
    members.reserve(plan.groups[index].size());
    for (auto const& entry: plan.groups[index]) { members.push_back(entry.sourcePath); }
    actions.push_back(
      jobstate::makeArchiveAction(plan.outputDir / zipName, members, label)
    );
  }

  return actions;
}

}  // namespace

auto prepareResumablePackExecution(jobstate::Store& store, pack::PackPlan const& plan)
  -> PreparedPackExecution {
  auto const allIndexes = buildPlanIndexes(plan.groups.size());
  auto executionState = std::make_shared<PackExecutionState>();
  executionState->store = &store;
  executionState->mergedActions =
    store.mergeActions(buildArchiveActions(plan, allIndexes));

  auto pendingActionIds = std::vector<std::string>{};
  executionState->pendingIndexes.reserve(executionState->mergedActions.size());
  pendingActionIds.reserve(executionState->mergedActions.size());
  for (auto index = std::size_t{0}; index < executionState->mergedActions.size();
       ++index) {
    if (!jobstate::needsExecution(executionState->mergedActions[index])) { continue; }
    executionState->pendingIndexes.push_back(index);
    pendingActionIds.push_back(executionState->mergedActions[index].id);
  }

  if (executionState->pendingIndexes.empty()) {
    return PreparedPackExecution{.pendingPlan = std::nullopt, .pendingActionIds = {}};
  }

  auto pendingPlan = pack::selectPackPlanIndexes(plan, executionState->pendingIndexes);
  pendingPlan.onGroupStart = [executionState](std::size_t subsetIndex) {
    executionState->store->markRunning(
      executionState->actionIdForSubsetIndex(subsetIndex)
    );
  };
  pendingPlan.onGroupSuccess =
    [executionState](std::size_t subsetIndex, std::filesystem::path const&) {
      executionState->store->markSucceeded(
        executionState->actionIdForSubsetIndex(subsetIndex)
      );
    };
  pendingPlan.onGroupFailure =
    [executionState](std::size_t subsetIndex, std::string const& error) {
      executionState->store->markFailed(
        executionState->actionIdForSubsetIndex(subsetIndex),
        error
      );
    };

  return PreparedPackExecution{
    .pendingPlan = std::move(pendingPlan),
    .pendingActionIds = std::move(pendingActionIds)
  };
}

}  // namespace archiveplan
