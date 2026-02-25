#pragma once

#include "core/error_handle.h"

#include <filesystem>
#include <vector>


auto readAllPics(const std::filesystem::path& dirPath)
  -> std::vector<std::filesystem::path>;

auto packAllPicsToZipParallel(
  const std::filesystem::path& dirPath,
  const std::filesystem::path& zipFileDir
) -> eh::Result<void>;
