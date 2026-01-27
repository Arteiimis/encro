#include <spdlog/spdlog.h>

#include "video_info.h"
#include "globals.h"
#include "utils.h"

namespace fs = std::filesystem;

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

auto getVidTotalFrames(const fs::path& videoPath) -> int64_t {
  const auto vidInfo = GLBs.VIDEO_INFO_CACHE.at(videoPath);

  for (const auto& stream: vidInfo.at("streams").as_array()) {
    if (stream.at("codec_type").as_string() == "video") {
      return std::stoll(stream.at("nb_frames").as_string().c_str());
    }
  }

  throw std::runtime_error(
    std::format("Failed to get total frames for video: {}", videoPath.string())
  );
}

bool isHevcEncoded(const fs::path& videoPath) {
  const auto vidInfo = getVidInfo(videoPath);

  for (const auto& stream: vidInfo.at("streams").as_array()) {
    if (stream.at("codec_type").as_string() == "video") {
      if (stream.at("codec_name").as_string() == "hevc") { return true; }
    }
  }

  return false;
}

template<class Iter>
  requires std::same_as<Iter, fs::directory_iterator>
        || std::same_as<Iter, fs::recursive_directory_iterator>
auto readAllVidsImpl(const fs::path& dirPath) -> std::vector<fs::path> {
  namespace rng = std::ranges;

  constexpr auto videoTypes = std::array{
    // Common video file extensions
    ".mp4",
    ".mkv",
    ".avi",
    ".mov",
    ".flv",
    ".wmv"
  };

  auto vids = std::vector<fs::path>{};

  for (const auto& entry: Iter(dirPath)) {
    if (!entry.is_regular_file()) {
      spdlog::debug("Skipping non-regular file: {}", entry.path().string());
      continue;
    }

    const auto vidsExt = entry.path().extension().string();
    if (rng::contains(videoTypes, vidsExt) && !isHevcEncoded(entry.path())) {
      vids.emplace_back(entry.path());
      GLBs.VIDEO_INFO_CACHE[entry.path()] = getVidInfo(entry.path());
    }
  }

  return vids;
}

auto readAllVids(const fs::path& dirPath) -> std::vector<fs::path> {
  if (GLBs.RECURSIVE) {
    return readAllVidsImpl<fs::recursive_directory_iterator>(dirPath);
  } else {
    return readAllVidsImpl<fs::directory_iterator>(dirPath);
  }
}
