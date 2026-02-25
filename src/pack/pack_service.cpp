#include "pack/pack_service.h"

#include "core/parallel.h"
#include "core/progress.h"
#include "pack/packer.h"

#include <format>
#include <mutex>

namespace pack {

auto packGroupsParallel(PackPlan const& plan) -> eh::Result<std::vector<fs::path>> {
  if (plan.groups.empty()) { return std::vector<fs::path>{}; }

  fs::create_directories(plan.outputDir);

  auto progressCtx = progress::ProgressContext{};
  auto packResults = std::vector<eh::Result<void>>(plan.groups.size());
  auto zippedFiles = std::vector<fs::path>(plan.groups.size());
  auto resultMtx = std::mutex{};

  auto _ = progress::CursorGuard{};
  parallel::runIndexedTasks(
    plan.groups.size(),
    plan.groups.size(),
    [&](std::size_t index) {
      auto const zipName = plan.zipNameForIndex
                           ? plan.zipNameForIndex(index)
                           : std::format("part{}.zip", index + 1);
      auto const zipPath = plan.outputDir / zipName;
      auto const label = plan.progressLabelForIndex
                         ? plan.progressLabelForIndex(index)
                         : std::format("Packing: {}", zipName);

      auto const packRes =
        packFilesToZip(plan.groups[index], zipPath, progressCtx, label);

      auto lock = std::scoped_lock{resultMtx};
      if (!packRes) {
        if (plan.removeOnFailure) {
          auto ec = std::error_code{};
          fs::remove(zipPath, ec);
        }
        packResults[index] = packRes;
        return;
      }

      packResults[index] = {};
      zippedFiles[index] = zipPath;
    }
  );

  for (auto const& res: packResults) {
    if (!res) { return eh::makeError("{}", res.error()); }
  }

  return zippedFiles;
}

}  // namespace pack
