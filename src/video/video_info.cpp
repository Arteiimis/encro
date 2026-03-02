#include "video/video_info.h"

#include "core/media_scanner.h"
#include "core/parallel.h"
#include "utils/utils.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>

namespace fs = std::filesystem;
using namespace std::literals;

namespace {

constexpr auto kVideoTypes = std::array{
  // Common video file extensions
  ".mp4"sv,
  ".mkv"sv,
  ".avi"sv,
  ".mov"sv,
  ".flv"sv,
  ".wmv"sv
};
constexpr std::uintmax_t kWebpInputMaxSize = 32ULL * 1024ULL * 1024ULL;

auto isHevcEncodedInfo(boost::json::value const& vidInfo) -> bool {
  if (!vidInfo.is_object()) { return false; }

  auto const& obj = vidInfo.as_object();
  auto const streamsIt = obj.find("streams");
  if (streamsIt == obj.end() || !streamsIt->value().is_array()) { return false; }

  for (auto const& streamVal: streamsIt->value().as_array()) {
    if (!streamVal.is_object()) { continue; }
    auto const& stream = streamVal.as_object();

    auto const codecTypeIt = stream.find("codec_type");
    if (codecTypeIt == stream.end() || !codecTypeIt->value().is_string()) {
      continue;
    }
    auto const codecNameIt = stream.find("codec_name");
    if (codecNameIt == stream.end() || !codecNameIt->value().is_string()) {
      continue;
    }

    auto const isVideo = codecTypeIt->value().as_string() == "video";
    auto const isHevc = codecNameIt->value().as_string() == "hevc";
    if (isVideo && isHevc) { return true; }
  }

  return false;
}

auto tryCollectVideo(appctx::AppConfig const& config, fs::path const& filePath)
  -> std::optional<fs::path> {
  namespace rng = std::ranges;

  if (!fs::is_regular_file(filePath)) {
    spdlog::debug("Skipping non-regular file: {}", filePath.string());
    return std::nullopt;
  }

  auto const vidsExt = filePath.extension().string();
  if (!rng::contains(kVideoTypes, vidsExt)) { return std::nullopt; }

  auto const fileSize = fs::file_size(filePath);
  if (fileSize >= kWebpInputMaxSize && config.outputFormat == "webp") {
    spdlog::debug(
      "Skipping large video file for webp output: {} ({} bytes)",
      filePath.string(),
      fileSize
    );
    return std::nullopt;
  }

  return filePath;
}

auto finalizeVideoList(
  appctx::AppConfig const& config,
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  std::span<fs::path const> vids
) -> std::vector<fs::path> {
  if (vids.empty()) { return {}; }

  auto const workerCount =
    std::min<std::size_t>(vids.size(), std::thread::hardware_concurrency());

  auto keep = std::vector<char>(vids.size(), 0);
  auto cacheMtx = std::mutex{};

  parallel::runIndexedTasks(vids.size(), workerCount, [&](std::size_t index) {
    auto const& vidPath = vids[index];
    auto const vidInfo = getVidInfo(toolchain, vidPath);

    if (config.outputFormat == "mp4" && isHevcEncodedInfo(vidInfo)) {
      spdlog::debug("Skipping already HEVC encoded file: {}", vidPath.string());
      return;
    }

    {
      auto lock = std::scoped_lock{cacheMtx};
      runtime.videoInfoCache[vidPath] = vidInfo;
    }
    keep[index] = 1;
  });

  auto filtered = std::vector<fs::path>{};
  filtered.reserve(vids.size());
  for (auto index = std::size_t{0}; index < vids.size(); ++index) {
    if (keep[index] == 1) { filtered.emplace_back(vids[index]); }
  }

  return filtered;
}

}  // namespace

auto getVidInfo(appctx::ToolchainPaths const& toolchain, fs::path const& videoPath)
  -> boost::json::value {
  namespace json = boost::json;

  auto const cmd = std::format(
    "{} -v quiet -print_format json -show_format -show_streams \"{}\"",
    toolchain.ffprobePath.value_or("ffprobe").string(),
    videoPath.string()
  );

  auto const [exitCode, output] = exec2(cmd, false);

  if (exitCode != 0) { return json::object{}; }

  try {
    return json::parse(output);
  } catch (std::exception const& ex) {
    spdlog::debug(
      "Failed to parse ffprobe output for {}: {}",
      videoPath.string(),
      ex.what()
    );
    return json::object{};
  }
}

auto getVidTotalFrames(
  appctx::RuntimeContext const& runtime,
  fs::path const& videoPath
) -> eh::Result<int64_t> {
  auto const vidInfoIt = runtime.videoInfoCache.find(videoPath);
  if (vidInfoIt == runtime.videoInfoCache.end()) {
    return eh::makeError("Missing cached video info");
  }

  auto const& vidInfo = vidInfoIt->second;
  if (!vidInfo.is_object()) { return eh::makeError("Invalid video info"); }

  auto const& obj = vidInfo.as_object();
  auto const streamsIt = obj.find("streams");
  if (streamsIt == obj.end() || !streamsIt->value().is_array()) {
    return eh::makeError("Missing stream info");
  }

  for (auto const& streamVal: streamsIt->value().as_array()) {
    if (!streamVal.is_object()) { continue; }
    auto const& stream = streamVal.as_object();

    auto const codecTypeIt = stream.find("codec_type");
    if (codecTypeIt == stream.end() || !codecTypeIt->value().is_string()) {
      continue;
    }
    if (codecTypeIt->value().as_string() != "video") { continue; }

    auto const nbFramesIt = stream.find("nb_frames");
    if (nbFramesIt == stream.end()) { continue; }

    auto const& nbFramesVal = nbFramesIt->value();
    if (nbFramesVal.is_int64()) { return nbFramesVal.as_int64(); }
    if (nbFramesVal.is_uint64()) {
      return static_cast<int64_t>(nbFramesVal.as_uint64());
    }
    if (nbFramesVal.is_string()) {
      auto const nbFramesStr = nbFramesVal.as_string();
      if (!nbFramesStr.empty() && nbFramesStr != "N/A") {
        return std::stoll(std::string{nbFramesStr});
      }
    }
  }

  return eh::makeError("Failed to retrieve total frames");
}

bool isHevcEncoded(
  appctx::ToolchainPaths const& toolchain,
  fs::path const& videoPath
) {
  auto const vidInfo = getVidInfo(toolchain, videoPath);
  return isHevcEncodedInfo(vidInfo);
}

auto readAllVids(
  appctx::AppConfig const& config,
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  fs::path const& dirPath
) -> std::vector<fs::path> {
  if (!fs::is_directory(dirPath) && !fs::is_regular_file(dirPath)) {
    spdlog::warn("Provided path is not a file or directory: {}", dirPath.string());
    return {};
  }

  auto vids = std::vector<fs::path>{};

  if (fs::is_regular_file(dirPath)) {
    if (auto collected = tryCollectVideo(config, dirPath)) {
      vids.emplace_back(collected.value());
    }
  } else {
    auto const candidates =
      media::scanByExtensions(dirPath, kVideoTypes, config.recursive);

    for (auto const& candidate: candidates) {
      if (auto collected = tryCollectVideo(config, candidate)) {
        vids.emplace_back(collected.value());
      }
    }
  }

  if (vids.empty()) { return vids; }

  return finalizeVideoList(config, toolchain, runtime, vids);
}

auto readAllVidsFromFiles(
  appctx::AppConfig const& config,
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  std::span<fs::path const> filePaths
) -> std::vector<fs::path> {
  auto vids = std::vector<fs::path>{};
  vids.reserve(filePaths.size());

  for (auto const& filePath: filePaths) {
    if (auto collected = tryCollectVideo(config, filePath)) {
      vids.emplace_back(collected.value());
    }
  }

  return finalizeVideoList(config, toolchain, runtime, vids);
}
