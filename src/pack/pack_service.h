#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"
#include "pack/pack.h"
#include "pack/pack_types.h"
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

  auto runPackPlan(appctx::AppContext& ctx, PackPlan const& plan)
    -> eh::Result<PackRunResult>;
  auto packGroups(PackPlan const& plan) -> eh::Result<std::vector<fs::path>>;

  auto packAllFilesInDirectory(
    std::filesystem::path const& dirPath,
    std::filesystem::path const& zipFileDir,
    std::uintmax_t maxGroupSize = kDefaultMaxArchiveGroupSize,
    bool recursive = true,
    bool forceNameConflictHandling = false,
    std::optional<std::size_t> maxParallelJobs = std::nullopt
  ) -> eh::Result<void>;
  auto runDirectoryPackWorkflow(
    appctx::AppContext& ctx,
    std::filesystem::path const& dirPath
  ) -> eh::Result<int>;

private:
  Packer packer_;

  auto packGroupsCompact(PackPlan const& plan) -> eh::Result<std::vector<fs::path>>;
  auto packGroupsFull(PackPlan const& plan) -> eh::Result<std::vector<fs::path>>;
};

}  // namespace pack
