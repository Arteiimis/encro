#pragma once

#include "pack/ipacker.h"
#include "pack/pack_types.h"
#include "core/error_handle.h"
#include "core/progress.h"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pack::test {

class MockPacker final: public IPacker {
public:
  struct PackFilesToZipCall {
    std::vector<PackFileEntry> entries;
    std::filesystem::path zipFilePath;
    progress::ProgressContext* progressCtx = nullptr;
    std::string progressText;
    pack::detail::PackEntryProgressCallback onEntryPacked;
    std::atomic<std::size_t>* finalizingCount = nullptr;
    bool isCompact = false;
  };

  struct BuildPlanCall {
    std::filesystem::path dirPath;
    std::filesystem::path zipFileDir;
    std::uintmax_t maxGroupSize = 0;
    bool recursive = false;
    bool forceNameConflictHandling = false;
    std::optional<std::size_t> maxParallelJobs;
    std::optional<std::filesystem::path> excludedPath;
  };

  std::vector<PackFilesToZipCall> packFilesToZipCalls;
  std::vector<BuildPlanCall> buildPlanCalls;
  eh::Result<void> packFilesToZipResult = {};
  eh::Result<PackPlan> buildPlanResult = PackPlan{};

  eh::Result<void> packFilesToZip(
    std::vector<PackFileEntry> const& entries,
    std::filesystem::path const& zipFilePath,
    progress::ProgressContext& progressCtx,
    std::string_view progressText
  ) override {
    packFilesToZipCalls.push_back({
      .entries = entries,
      .zipFilePath = zipFilePath,
      .progressCtx = &progressCtx,
      .progressText = std::string{progressText},
      .isCompact = false,
    });
    return packFilesToZipResult;
  }

  eh::Result<void> packFilesToZip(
    std::vector<PackFileEntry> const& entries,
    std::filesystem::path const& zipFilePath,
    pack::detail::PackEntryProgressCallback onEntryPacked,
    std::atomic<std::size_t>* finalizingCount
  ) override {
    packFilesToZipCalls.push_back({
      .entries = entries,
      .zipFilePath = zipFilePath,
      .onEntryPacked = onEntryPacked,
      .finalizingCount = finalizingCount,
      .isCompact = true,
    });
    return packFilesToZipResult;
  }

  eh::Result<PackPlan> buildDirectoryPackPlan(
    std::filesystem::path const& dirPath,
    std::filesystem::path const& zipFileDir,
    std::uintmax_t maxGroupSize,
    bool recursive,
    bool forceNameConflictHandling,
    std::optional<std::size_t> maxParallelJobs,
    std::optional<std::filesystem::path> excludedPath
  ) override {
    buildPlanCalls.push_back({
      .dirPath = dirPath,
      .zipFileDir = zipFileDir,
      .maxGroupSize = maxGroupSize,
      .recursive = recursive,
      .forceNameConflictHandling = forceNameConflictHandling,
      .maxParallelJobs = maxParallelJobs,
      .excludedPath = excludedPath,
    });
    return buildPlanResult;
  }
};

}  // namespace pack::test
