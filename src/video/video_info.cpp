#include "video/video_info.h"

#include "core/media_scanner.h"
#include "core/task_executor.h"
#include "utils/utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <optional>
#include <string>
#include <thread>

#include "logging/log_tags.h"
#include "logging/logging.h"

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::VIDEO_INFO);

namespace fs = std::filesystem;
using namespace std::literals;

namespace {

constexpr auto kVideoTypes = std::array{
  ".mp4"sv,
  ".mkv"sv,
  ".avi"sv,
  ".mov"sv,
  ".flv"sv,
  ".wmv"sv,
};
constexpr std::uintmax_t kWebpInputMaxSize = 32ULL * 1024ULL * 1024ULL;

auto jsonValToString(boost::json::value const& val) -> std::string {
  if (val.is_string()) { return std::string{val.as_string()}; }
  if (val.is_int64()) { return std::to_string(val.as_int64()); }
  if (val.is_uint64()) { return std::to_string(val.as_uint64()); }
  if (val.is_double()) { return std::to_string(val.as_double()); }
  if (val.is_bool()) { return val.as_bool() ? "true" : "false"; }
  if (val.is_null()) { return "null"; }
  return "<object>";
}

auto parseDouble(std::string_view text) -> std::optional<double> try {
  return std::stod(std::string{text});
} catch (...) { return std::nullopt; }

auto parseFraction(std::string_view text) -> std::optional<double> {
  auto const slashPos = text.find('/');
  if (slashPos == std::string_view::npos) { return parseDouble(text); }

  auto const num = text.substr(0, slashPos);
  auto const den = text.substr(slashPos + 1);
  auto const n = parseDouble(num);
  auto const d = parseDouble(den);
  if (!n.has_value() || !d.has_value() || d.value() == 0.0) { return std::nullopt; }
  return n.value() / d.value();
}

auto getFormatDurationValue(boost::json::object const& obj, std::string& formatDuration)
  -> std::optional<double> {
  auto const formatIt = obj.find("format");
  if (formatIt == obj.end() || !formatIt->value().is_object()) { return std::nullopt; }

  auto const& formatObj = formatIt->value().as_object();
  auto const durationIt = formatObj.find("duration");
  if (durationIt == formatObj.end()) { return std::nullopt; }

  formatDuration = jsonValToString(durationIt->value());
  return parseDouble(formatDuration);
}

auto readStreamValue(boost::json::object const& stream, std::string_view key)
  -> std::string {
  if (auto const it = stream.find(key); it != stream.end()) {
    return jsonValToString(it->value());
  }
  return "<missing>";
}

auto tryParseNbFrames(boost::json::value const& val) -> std::optional<int64_t> {
  if (val.is_int64()) { return val.as_int64(); }
  if (val.is_uint64()) { return static_cast<int64_t>(val.as_uint64()); }
  if (val.is_string()) {
    auto const text = std::string{val.as_string()};
    if (!text.empty() && text != "N/A") { return std::stoll(text); }
  }
  return std::nullopt;
}

bool isHevcEncodedInfo(boost::json::value const& vidInfo) {
  if (!vidInfo.is_object()) { return false; }

  auto const& obj = vidInfo.as_object();
  auto const streamsIt = obj.find("streams");
  if (streamsIt == obj.end() || !streamsIt->value().is_array()) { return false; }

  for (auto const& streamVal: streamsIt->value().as_array()) {
    if (!streamVal.is_object()) { continue; }
    auto const& stream = streamVal.as_object();

    auto const codecTypeIt = stream.find("codec_type");
    if (codecTypeIt == stream.end() || !codecTypeIt->value().is_string()) { continue; }
    auto const codecNameIt = stream.find("codec_name");
    if (codecNameIt == stream.end() || !codecNameIt->value().is_string()) { continue; }

    auto const isVideo = codecTypeIt->value().as_string() == "video";
    auto const isHevc = codecNameIt->value().as_string() == "hevc";
    if (isVideo && isHevc) { return true; }
  }

  return false;
}

bool isKnownVideoExtension(fs::path const& filePath) {
  namespace rng = std::ranges;

  auto const vidsExt = filePath.extension().string();
  return rng::contains(kVideoTypes, vidsExt);
}

auto tryReadFileSize(fs::path const& filePath) -> std::optional<std::uintmax_t> {
  auto ec = std::error_code{};
  if (auto const fileSize = fs::file_size(filePath, ec); !ec) { return fileSize; }

  LOG_DEBUG(
    "Skipping file with unreadable size metadata: {} ({})",
    filePath.string(),
    ec.message()
  );

  return std::nullopt;
}

bool keepsWebpInputSizeLimit(appctx::AppConfig const& config, fs::path const& filePath) {
  if (config.outputFormat != "webp") { return true; }

  auto const fileSize = tryReadFileSize(filePath);
  if (!fileSize.has_value()) { return false; }
  if (fileSize.value() < kWebpInputMaxSize) { return true; }

  LOG_DEBUG(
    "Skipping large video file for webp output: {} ({} bytes)",
    filePath.string(),
    fileSize.value()
  );
  return false;
}

auto tryCollectVideoInput(appctx::AppConfig const& config, fs::path const& filePath)
  -> std::optional<fs::path> {

  if (!fs::is_regular_file(filePath)) {
    LOG_DEBUG("Skipping non-regular file: {}", filePath.string());
    return std::nullopt;
  }

  if (!isKnownVideoExtension(filePath)) { return std::nullopt; }

  if (!keepsWebpInputSizeLimit(config, filePath)) { return std::nullopt; }

  return filePath;
}

bool keepScannedVideoCandidate(
  appctx::AppConfig const& config,
  fs::path const& filePath
) {
  return keepsWebpInputSizeLimit(config, filePath);
}

auto loadCachedOrProbeVideoInfo(
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  fs::path const& videoPath
) -> boost::json::value {
  if (auto const cached = runtime.videoInfoCache.find(videoPath); cached.has_value()) {
    return cached.value();
  }

  auto const vidInfo = getVidInfo(toolchain, videoPath);
  runtime.videoInfoCache.set(videoPath, vidInfo);
  return vidInfo;
}

auto finalizeVideoList(
  appctx::AppConfig const& config,
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  std::span<fs::path const> vids
) -> std::vector<fs::path> {
  if (vids.empty()) { return {}; }

  auto const configuredOrDetected =
    config.maxParallelJobs
      .value_or(static_cast<std::size_t>(std::thread::hardware_concurrency()));
  auto const maxParallelJobs = std::max<std::size_t>(1, configuredOrDetected);
  auto keep = std::vector<char>(vids.size(), 0);
  auto tasks = std::vector<taskexec::TaskSpec>{};
  tasks.reserve(vids.size());

  for (auto index = std::size_t{0}; index < vids.size(); ++index) {
    tasks.push_back({
      .id = vids[index].string(),
      .label = vids[index].filename().string(),
      .run = [&, index](taskexec::TaskContext&) -> eh::Result<void> {
        auto const& vidPath = vids[index];
        auto const vidInfo = getVidInfo(toolchain, vidPath);

        if (config.outputFormat == "mp4" && isHevcEncodedInfo(vidInfo)) {
          LOG_DEBUG(  // NOLINT(bugprone-lambda-function-name): SPDLOG_FUNCTION in task lambda
            "Skipping already HEVC encoded file: {}",
            vidPath.string()
          );
          return {};
        }

        runtime.videoInfoCache.set(vidPath, vidInfo);
        keep[index] = 1;
        return {};
      },
    });
  }

  auto const _ = taskexec::runTasks({
    .tasks = std::move(tasks),
    .maxConcurrency = maxParallelJobs,
    .progress = nullptr,
    .hideCursor = false,
  });

  auto filtered = std::vector<fs::path>{};
  filtered.reserve(vids.size());
  for (auto index = std::size_t{0}; index < vids.size(); ++index) {
    if (keep[index] == 1) { filtered.emplace_back(vids[index]); }
  }

  return filtered;
}

void prewarmWebpVideoInfoCache(
  appctx::AppConfig const& config,
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  std::span<fs::path const> vids
) {
  if (vids.empty()) { return; }

  auto const configuredOrDetected = config.maxParallelJobs.value_or(
    static_cast<std::size_t>(std::thread::hardware_concurrency())  //
  );
  auto const maxParallelJobs = std::max<std::size_t>(1, configuredOrDetected);
  auto const workerCount = taskexec::resolveWorkerCount(vids.size(), maxParallelJobs);
  auto const prewarmCount = std::min(vids.size(), workerCount + 1);
  auto tasks = std::vector<taskexec::TaskSpec>{};
  tasks.reserve(prewarmCount);

  for (auto index = std::size_t{0}; index < prewarmCount; ++index) {
    tasks.push_back({
      .id = vids[index].string(),
      .label = vids[index].filename().string(),
      .run = [&, index](taskexec::TaskContext&) -> eh::Result<void> {
        auto const& vidPath = vids[index];
        runtime.videoInfoCache.set(vidPath, getVidInfo(toolchain, vidPath));
        return {};
      }  //
    });
  }

  auto const _ = taskexec::runTasks({
    .tasks = std::move(tasks),
    .maxConcurrency = maxParallelJobs,
    .progress = nullptr,
    .hideCursor = false,
  });
}

}  // namespace

auto getVidInfo(appctx::ToolchainPaths const& toolchain, fs::path const& videoPath)
  -> boost::json::value {
  namespace json = boost::json;

  auto const cmd = quoteToolPath(toolchain.ffprobePath.value_or("ffprobe"))
    + " -v quiet -print_format json -show_format -show_streams \""
    + videoPath.string()
    + "\"";

  auto const [exitCode, output, pid] = exec2(cmd, false);

  if (exitCode != 0) {
    LOG_DEBUG(
      "ffprobe exit code {} for {} (output bytes: {})",
      exitCode,
      videoPath.string(),
      output.size()
    );
    return json::object{};
  }

  try {
    return json::parse(output);
  } catch (std::exception const& ex) {
    LOG_DEBUG("Failed to parse ffprobe output for {}: {}", videoPath.string(), ex.what());
    return json::object{};
  }
}

// NOLINTNEXTLINE(readability-function-size): linear ffprobe fallback chain
auto getVidTotalFrames(
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  fs::path const& videoPath
) -> eh::Result<int64_t> {
  auto const vidInfo = loadCachedOrProbeVideoInfo(toolchain, runtime, videoPath);

  if (!vidInfo.is_object()) { return eh::makeError("Invalid video info"); }

  auto const& obj = vidInfo.as_object();
  auto const streamsIt = obj.find("streams");
  if (streamsIt == obj.end() || !streamsIt->value().is_array()) {
    LOG_DEBUG("Missing stream info for {}", videoPath.string());
    return eh::makeError("Missing stream info");
  }

  auto formatDuration = std::string{"<missing>"};
  auto const formatDurationValue = getFormatDurationValue(obj, formatDuration);

  struct DebugInfo {
    bool hasVideoStream = false;
    std::string avgRate = "<missing>";
    std::string rRate = "<missing>";
    std::string duration = "<missing>";
    std::string durationTs = "<missing>";
    std::string timeBase = "<missing>";
  } debug;

  for (auto const& streamVal: streamsIt->value().as_array()) {
    if (!streamVal.is_object()) { continue; }
    auto const& stream = streamVal.as_object();

    auto const codecTypeIt = stream.find("codec_type");
    if (codecTypeIt == stream.end() || !codecTypeIt->value().is_string()) { continue; }
    if (codecTypeIt->value().as_string() != "video") { continue; }

    debug.hasVideoStream = true;

    auto const nbFramesIt = stream.find("nb_frames");
    if (nbFramesIt != stream.end()) {
      if (auto const frames = tryParseNbFrames(nbFramesIt->value())) {
        return frames.value();
      }
    }

    debug.avgRate = readStreamValue(stream, "avg_frame_rate");
    debug.rRate = readStreamValue(stream, "r_frame_rate");
    debug.duration = readStreamValue(stream, "duration");
    debug.durationTs = readStreamValue(stream, "duration_ts");
    debug.timeBase = readStreamValue(stream, "time_base");
  }

  if (!debug.hasVideoStream) {
    LOG_DEBUG(
      "No video stream found for {} (streams: {})",
      videoPath.string(),
      streamsIt->value().as_array().size()
    );
    return eh::makeError("Failed to retrieve total frames");
  }

  LOG_DEBUG(
    "No nb_frames for {}. format.duration={}, avg_frame_rate={}, r_frame_rate={}, "
    "duration={}, duration_ts={}, time_base={}",
    videoPath.string(),
    formatDuration,
    debug.avgRate,
    debug.rRate,
    debug.duration,
    debug.durationTs,
    debug.timeBase
  );

  if (formatDurationValue.has_value()) {
    if (auto const rate = parseFraction(debug.avgRate); rate.has_value()) {
      return static_cast<int64_t>(
        std::llround(formatDurationValue.value() * rate.value())
      );
    }
    if (auto const rate = parseFraction(debug.rRate); rate.has_value()) {
      return static_cast<int64_t>(
        std::llround(formatDurationValue.value() * rate.value())
      );
    }
  }

  return eh::makeError("Failed to retrieve total frames");
}

auto getVidTotalDurationUs(
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  fs::path const& videoPath
) -> eh::Result<std::uint64_t> {
  auto const vidInfo = loadCachedOrProbeVideoInfo(toolchain, runtime, videoPath);

  if (!vidInfo.is_object()) { return eh::makeError("Invalid video info"); }

  auto formatDuration = std::string{};
  if (
    auto const durationValue =
      getFormatDurationValue(vidInfo.as_object(), formatDuration);
    durationValue.has_value()
  ) {
    return static_cast<std::uint64_t>(std::llround(durationValue.value() * 1'000'000.0));
  }

  LOG_DEBUG("Missing format duration for {}", videoPath.string());
  return eh::makeError("Failed to retrieve video duration");
}

auto getVidHasAudio(
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  fs::path const& videoPath
) -> eh::Result<bool> {
  auto const vidInfo = loadCachedOrProbeVideoInfo(toolchain, runtime, videoPath);

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
    if (
      codecTypeIt != stream.end()
      && codecTypeIt->value().is_string()
      && codecTypeIt->value().as_string() == "audio"
    ) {
      return true;
    }
  }

  return false;
}

auto getVidDimensions(
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  fs::path const& videoPath
) -> eh::Result<std::pair<int, int>> {
  auto const vidInfo = loadCachedOrProbeVideoInfo(toolchain, runtime, videoPath);

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
    if (
      codecTypeIt != stream.end()
      && codecTypeIt->value().is_string()
      && codecTypeIt->value().as_string() == "video"
    ) {
      auto const widthIt = stream.find("width");
      auto const heightIt = stream.find("height");
      if (
        widthIt != stream.end()
        && widthIt->value().is_int64()
        && heightIt != stream.end()
        && heightIt->value().is_int64()
      ) {
        return std::pair{
          static_cast<int>(widthIt->value().as_int64()),
          static_cast<int>(heightIt->value().as_int64())
        };
      }
    }
  }

  LOG_DEBUG("Missing video dimensions for {}", videoPath.string());
  return eh::makeError("Failed to retrieve video dimensions");
}

bool isHevcEncoded(appctx::ToolchainPaths const& toolchain, fs::path const& videoPath) {
  auto const vidInfo = getVidInfo(toolchain, videoPath);
  return isHevcEncodedInfo(vidInfo);
}

auto readAllVids(
  appctx::AppConfig const& config,
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  fs::path const& dirPath
) -> eh::Result<std::vector<fs::path>> {
  if (!fs::is_directory(dirPath) && !fs::is_regular_file(dirPath)) {
    return eh::makeError(
      "Provided path is not a file or directory: {}",
      dirPath.string()
    );
  }

  auto vids = std::vector<fs::path>{};

  if (fs::is_regular_file(dirPath)) {
    if (auto collected = tryCollectVideoInput(config, dirPath)) {
      vids.emplace_back(collected.value());
    }
  } else {
    auto const scanRes = media::scanByExtensions(dirPath, kVideoTypes, config.recursive);
    if (!scanRes) { return eh::makeError("{}", scanRes.error()); }
    for (auto const& warning: scanRes->warnings) { LOG_WARN("{}", warning); }

    for (auto const& candidate: scanRes->matches) {
      if (keepScannedVideoCandidate(config, candidate)) { vids.emplace_back(candidate); }
    }
  }

  if (vids.empty()) { return vids; }

  if (config.outputFormat == "mp4") {
    return finalizeVideoList(config, toolchain, runtime, vids);
  }

  if (config.outputFormat == "webp") {
    prewarmWebpVideoInfoCache(config, toolchain, runtime, vids);
    return vids;
  }

  return vids;
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
    if (auto collected = tryCollectVideoInput(config, filePath)) {
      vids.emplace_back(collected.value());
    }
  }

  if (vids.empty()) { return vids; }

  if (config.outputFormat == "mp4") {
    return finalizeVideoList(config, toolchain, runtime, vids);
  }

  if (config.outputFormat == "webp") {
    prewarmWebpVideoInfoCache(config, toolchain, runtime, vids);
    return vids;
  }

  return vids;
}
