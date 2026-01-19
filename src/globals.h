#pragma once

#include <filesystem>
#include <optional>
#include <unordered_map>

#include <boost/uuid.hpp>
#include <boost/json.hpp>

enum class LogLevel {
  Info,
  Verbose,
  Debug
};

inline static auto CURRENT_LOG_LEVEL  = LogLevel::Info;
inline static auto FFMPEG_INSTALL_DIR = std::optional<std::filesystem::path>{};
inline static auto FFMPEG_PATH        = std::optional<std::filesystem::path>{};
inline static auto FFPROBE_PATH       = std::optional<std::filesystem::path>{};
inline static auto INPUT_PATH         = std::optional<std::filesystem::path>{};
inline static auto OUTPUT_PATH        = std::optional<std::filesystem::path>{};
inline static auto
  VIDEO_INFO_CACHE = std::unordered_map<std::filesystem::path, boost::json::value>{};
inline static auto
  PROGRESS_FILES = std::unordered_map<std::filesystem::path, std::filesystem::path>{};
inline static auto UUID_GENERATOR = boost::uuids::random_generator{};
