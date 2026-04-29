#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"
#include "pack/pack_types.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace pack {

class Packer;  // forward declaration

class PackService final {
public:
  explicit PackService(Packer& packer);

  static auto buildGroupOrdinalRanges(std::vector<std::vector<fs::path>> const& groups)
    -> std::vector<FileOrdinalRange>;
  static auto
  buildGroupOrdinalRanges(std::vector<std::vector<PackFileEntry>> const& groups)
    -> std::vector<FileOrdinalRange>;
  static auto
  appendOrdinalRangeSuffix(std::string_view fileName, FileOrdinalRange const& range)
    -> std::string;
  static auto defaultZipNameForIndex(std::size_t index) -> std::string;
  static auto defaultProgressLabelForZipName(std::string_view zipName) -> std::string;
  static auto resolveZipNameForIndex(PackPlan const& plan, std::size_t index)
    -> std::string;
  static auto resolveProgressLabelForIndex(PackPlan const& plan, std::size_t index)
    -> std::string;

  static auto
  selectPackPlanIndexes(PackPlan const& plan, std::span<std::size_t const> indexes)
    -> PackPlan;

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
  Packer& packer_;

  static auto makeSubsetZipNameResolver(
    std::function<std::string(std::size_t)> const& originalResolver,
    std::shared_ptr<std::vector<std::size_t>> const& selectedIndexes
  ) -> std::function<std::string(std::size_t)>;
  static auto makeSubsetProgressLabelResolver(
    std::function<std::string(std::size_t)> const& originalResolver,
    std::shared_ptr<std::vector<std::size_t>> const& selectedIndexes
  ) -> std::function<std::string(std::size_t)>;

  auto packGroupsCompact(PackPlan const& plan) -> eh::Result<std::vector<fs::path>>;
  auto packGroupsFull(PackPlan const& plan) -> eh::Result<std::vector<fs::path>>;
};

}  // namespace pack
