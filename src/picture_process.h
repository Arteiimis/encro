#pragma once

#include <filesystem>
#include <expected>
#include <vector>

#include <spdlog/spdlog.h>

template<class Iter>
auto readAllPicsImpl(const std::filesystem::path& dirPath)
  -> std::vector<std::filesystem::path>;

auto readAllPics(const std::filesystem::path& dirPath)
  -> std::vector<std::filesystem::path>;

auto packAllPicsToZip(
  const std::filesystem::path& dirPath,
  const std::filesystem::path& zipFileDir
) -> std::expected<void, std::string>;

auto packAllPicsToZipParallel(
  const std::filesystem::path& dirPath,
  const std::filesystem::path& zipFileDir
) -> std::expected<void, std::string>;
