#pragma once

#include "core/job_state.h"
#include "pack/pack_types.h"

#include <optional>
#include <vector>

namespace archiveplan {

struct PreparedPackExecution {
  std::optional<pack::PackPlan> pendingPlan;
  std::vector<std::string> pendingActionIds;
};

auto prepareResumablePackExecution(jobstate::Store& store, pack::PackPlan const& plan)
  -> PreparedPackExecution;

}  // namespace archiveplan
