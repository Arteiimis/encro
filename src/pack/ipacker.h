#pragma once

#include "core/error_handle.h"
#include "core/progress.h"
#include "pack/pack_types.h"
#include "pack/packer_types.h"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pack {

class IPacker {
public:
  virtual ~IPacker() = default;

  virtual eh::Result<void> packFilesToZip(
    std::vector<PackFileEntry> const& entries,
    std::filesystem::path const& zipFilePath,
    progress::ProgressContext& progressCtx,
    std::string_view progressText
  ) = 0;

  virtual eh::Result<void> packFilesToZip(
    std::vector<PackFileEntry> const& entries,
    std::filesystem::path const& zipFilePath,
    pack::detail::PackEntryProgressCallback onEntryPacked,
    std::atomic<std::size_t>* finalizingCount
  ) = 0;

  virtual eh::Result<PackPlan> buildDirectoryPackPlan(
    std::filesystem::path const& dirPath,
    std::filesystem::path const& zipFileDir,
    std::uintmax_t maxGroupSize,
    bool recursive,
    bool forceNameConflictHandling,
    std::optional<std::size_t> maxParallelJobs,
    std::optional<std::filesystem::path> excludedPath = std::nullopt
  ) = 0;
};

}  // namespace pack
