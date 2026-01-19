#pragma once

#include <filesystem>
#include <vector>
#include <chrono>

#include <boost/json.hpp>

auto getVidInfo(const std::filesystem::path& videoPath) -> boost::json::value;

auto getVidLengthMs(const std::filesystem::path& videoPath)
  -> std::chrono::milliseconds;

bool isHevcEncoded(const std::filesystem::path& videoPath);

auto readAllVids(const std::filesystem::path& dirPath)
  -> std::vector<std::filesystem::path>;
