#pragma once

#include <filesystem>

bool encodeToHevc(const std::filesystem::path& inputVidPath);

int handleSingleFileEncoding(const std::filesystem::path& videoPath);

auto readLastNLines(const std::filesystem::path& filePath, std::size_t n)
  -> std::vector<std::string>;

auto getFrameCountFromProgress(const std::filesystem::path& progressFilePath)
  -> uint64_t;

int handlePathEncoding(const std::filesystem::path& inputPath);
