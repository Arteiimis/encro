#pragma once

#include <filesystem>
#include <vector>

#include <spdlog/spdlog.h>

#include "core/error_handle.h"

template<class Iter>
auto readAllPicsImpl(const std::filesystem::path& dirPath)
  -> std::vector<std::filesystem::path>;

auto readAllPics(const std::filesystem::path& dirPath)
  -> std::vector<std::filesystem::path>;

auto packAllPicsToZipParallel(
  const std::filesystem::path& dirPath,
  const std::filesystem::path& zipFileDir
) -> eh::Result<void>;
