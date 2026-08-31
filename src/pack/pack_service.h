#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"
#include "pack/pack.h"
#include "pack/pack_types.h"
#include "pack/pack_plan_internal.h"
#include "pack/packer.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace pack {

class PackService final {
public:
  PackService() = default;

  auto packGroups(PackPlan const& plan) -> eh::Result<std::vector<fs::path>>;

private:
  Packer packer_;

  auto packGroupsCompact(PackPlan const& plan) -> eh::Result<std::vector<fs::path>>;
  auto packGroupsFull(PackPlan const& plan) -> eh::Result<std::vector<fs::path>>;
};

}  // namespace pack
