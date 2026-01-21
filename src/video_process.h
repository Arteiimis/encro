#pragma once

#include <filesystem>

bool encodeToHevc(const std::filesystem::path& inputVidPath);

int handleSingleFileEncoding(const std::filesystem::path& videoPath);

auto readLastNLines(const std::filesystem::path& filePath, std::size_t n)
  -> std::vector<std::string>;

auto parseProgressFile(const std::filesystem::path& progressFilePath)
  -> std::pair<uint64_t, std::string>;

int handlePathEncoding(const std::filesystem::path& inputPath);
