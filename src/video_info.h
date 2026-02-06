#pragma once

#include <filesystem>
#include <vector>

#include <boost/json.hpp>

#include "error_handle.h"

auto getVidInfo(const std::filesystem::path& videoPath) -> boost::json::value;

auto getVidTotalFrames(const std::filesystem::path& videoPath)
  -> eh::Result<int64_t>;

bool isHevcEncoded(const std::filesystem::path& videoPath);

auto readAllVids(const std::filesystem::path& dirPath)
  -> std::vector<std::filesystem::path>;
