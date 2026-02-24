#include "video_info.h"

#include "globals.h"
#include "utils.h"

#include <spdlog/spdlog.h>

#include <array>
#include <ranges>


namespace fs = std::filesystem;

namespace {

constexpr auto kVideoTypes = std::array{
  // Common video file extensions
  ".mp4",
  ".mkv",
  ".avi",
  ".mov",
  ".flv",
  ".wmv"
};

auto tryCollectVideo(fs::path const& filePath, std::vector<fs::path>& vids) -> void {
  namespace rng = std::ranges;

  if (!fs::is_regular_file(filePath)) {
    spdlog::debug("Skipping non-regular file: {}", filePath.string());
    return;
  }

  auto const vidsExt = filePath.extension().string();
  if (!rng::contains(kVideoTypes, vidsExt)) { return; }

  if (isHevcEncoded(filePath)) {
    spdlog::debug("Skipping already HEVC encoded file: {}", filePath.string());
    return;
  }

  GLBs.VIDEO_INFO_CACHE[filePath] = getVidInfo(filePath);
  vids.emplace_back(filePath);
}

}  // namespace

auto getVidInfo(const fs::path& videoPath) -> boost::json::value {
  namespace json = boost::json;

  const auto cmd = std::format(
    "{} -v quiet -print_format json -show_format -show_streams \"{}\"",
    GLBs.FFPROBE_PATH.value_or("ffprobe").string(),
    videoPath.string()
  );

  const auto [exitCode, output] = exec2(cmd);

  if (exitCode != 0) { return json::object{}; }

  return json::parse(output);
}

auto getVidTotalFrames(const fs::path& videoPath) -> eh::Result<int64_t> {
  const auto vidInfo = GLBs.VIDEO_INFO_CACHE.at(videoPath);

  for (const auto& stream: vidInfo.at("streams").as_array()) {
    if (stream.at("codec_type").as_string() == "video") {
      return std::stoll(stream.at("nb_frames").as_string().c_str());
    }
  }

  return eh::makeError(
    "Failed to retrieve total frames for video: {}",
    videoPath.string()
  );
}

bool isHevcEncoded(const fs::path& videoPath) {
  const auto vidInfo = getVidInfo(videoPath);

  for (const auto& stream: vidInfo.at("streams").as_array()) {
    try {
      auto const isVideo = stream.at("codec_type").as_string() == "video";
      auto const isHevc = stream.at("codec_name").as_string() == "hevc";
      if (isVideo && isHevc) { return true; }
    } catch (...) { continue; }
  }

  return false;
}

template<class Iter>
  requires std::same_as<Iter, fs::directory_iterator>
        || std::same_as<Iter, fs::recursive_directory_iterator>
auto readAllVidsImpl(fs::path const& dirPath) -> std::vector<fs::path> {
  auto vids = std::vector<fs::path>{};

  for (auto const& entry: Iter(dirPath)) { tryCollectVideo(entry.path(), vids); }

  return vids;
}

auto readAllVids(fs::path const& dirPath) -> std::vector<fs::path> {
  if (fs::is_regular_file(dirPath)) {
    auto vids = std::vector<fs::path>{};
    tryCollectVideo(dirPath, vids);
    return vids;
  }

  if (!fs::is_directory(dirPath)) {
    spdlog::warn("Provided path is not a file or directory: {}", dirPath.string());
    return {};
  }

  if (GLBs.RECURSIVE) {
    return readAllVidsImpl<fs::recursive_directory_iterator>(dirPath);
  } else {
    return readAllVidsImpl<fs::directory_iterator>(dirPath);
  }
}
