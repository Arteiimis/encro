#include "preview/preview_process.h"

#include "infra/open_file.h"
#include "infra/terminal.h"
#include "utils/utils.h"
#include "video/video_info.h"
#include "video/video_quality.h"

#include "logging/log_tags.h"
#include "logging/logging.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

DEFINE_LOGGER(logtags::PREVIEW_PROCESS);

namespace fs = std::filesystem;
using enum terminal::MessageKind;

namespace preview {

namespace {

constexpr auto kWindowCount = std::size_t{5};
constexpr auto kWindowDurationUs = std::uint64_t{10'000'000};
constexpr auto kFullComparisonBudgetUs = std::uint64_t{50'000'000};

auto seconds(std::uint64_t micros) -> double {
  return static_cast<double>(micros) / 1'000'000.0;
}

auto parseDouble(std::string_view text) -> std::optional<double> {
  try {
    return std::stod(std::string{text});
  } catch (...) { return std::nullopt; }
}

auto parseFraction(std::string_view text) -> std::optional<double> {
  auto const slashPos = text.find('/');
  if (slashPos == std::string_view::npos) { return parseDouble(text); }
  auto const num = parseDouble(text.substr(0, slashPos));
  auto const den = parseDouble(text.substr(slashPos + 1));
  if (!num.has_value() || !den.has_value() || den.value() == 0.0) { return std::nullopt; }
  return num.value() / den.value();
}

auto probeVideo(
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  fs::path const& path
) -> eh::Result<VideoProbe> {
  auto const info = getVidInfo(toolchain, path);
  runtime.videoInfoCache.set(path, info);
  if (!info.is_object()) {
    return eh::makeError("Failed to probe video: {}", path.string());
  }

  auto const streamsIt = info.as_object().find("streams");
  if (streamsIt == info.as_object().end() || !streamsIt->value().is_array()) {
    return eh::makeError("Failed to probe video: {}", path.string());
  }

  auto probe = VideoProbe{};
  auto foundVideo = false;
  for (auto const& streamVal: streamsIt->value().as_array()) {
    if (!streamVal.is_object()) { continue; }
    auto const& stream = streamVal.as_object();
    auto const codecTypeIt = stream.find("codec_type");
    if (codecTypeIt == stream.end() || !codecTypeIt->value().is_string()) { continue; }
    auto const codecType = std::string_view{codecTypeIt->value().as_string()};

    if (codecType == "video") {
      foundVideo = true;
      if (
        auto const codecIt = stream.find("codec_name");
        codecIt != stream.end() && codecIt->value().is_string()
      ) {
        auto const codecName = std::string_view{codecIt->value().as_string()};
        if (codecName == "webp" || codecName == "webp_anim") {
          return eh::makeError(
            "Preview supports video comparison only; input is WebP: {}",
            path.string()
          );
        }
      }
      if (
        auto const widthIt = stream.find("width");
        widthIt != stream.end() && widthIt->value().is_int64()
      ) {
        probe.width = static_cast<int>(widthIt->value().as_int64());
      }
      if (
        auto const heightIt = stream.find("height");
        heightIt != stream.end() && heightIt->value().is_int64()
      ) {
        probe.height = static_cast<int>(heightIt->value().as_int64());
      }
      if (
        auto const rateIt = stream.find("avg_frame_rate");
        rateIt != stream.end() && rateIt->value().is_string()
      ) {
        probe.fps =
          parseFraction(std::string_view{rateIt->value().as_string()}).value_or(0.0);
      }
    } else if (codecType == "audio") {
      probe.hasAudio = true;
      if (
        auto const codecIt = stream.find("codec_name");
        codecIt != stream.end() && codecIt->value().is_string()
      ) {
        probe.audioCodec = std::string{codecIt->value().as_string()};
      }
    }
  }

  if (!foundVideo) { return eh::makeError("Not a video file: {}", path.string()); }
  if (probe.width == 0 || probe.height == 0) {
    return eh::makeError("No video stream dimensions found for: {}", path.string());
  }

  // Prefer the video stream's own duration: format.duration can be dragged
  // past the video end by a longer audio track, and windows must stay within
  // the video.
  if (
    auto const durationIt = info.as_object().find("streams");
    durationIt != info.as_object().end() && durationIt->value().is_array()
  ) {
    for (auto const& stream: durationIt->value().as_array()) {
      if (!stream.is_object()) { continue; }
      auto const codecIt = stream.as_object().find("codec_type");
      if (
        codecIt == stream.as_object().end()
        || !codecIt->value().is_string()
        || std::string_view{codecIt->value().as_string()} != "video"
      ) {
        continue;
      }
      if (
        auto const streamDurIt = stream.as_object().find("duration");
        streamDurIt != stream.as_object().end() && streamDurIt->value().is_string()
      ) {
        if (
          auto const duration =
            parseDouble(std::string_view{streamDurIt->value().as_string()})
        ) {
          probe.durationUs =
            static_cast<std::uint64_t>(std::llround(duration.value() * 1'000'000.0));
        }
      }
      break;
    }
  }

  // Fall back to format.duration when the video stream carries none.
  if (probe.durationUs == 0) {
    if (
      auto const formatIt = info.as_object().find("format");
      formatIt != info.as_object().end() && formatIt->value().is_object()
    ) {
      auto const durationIt = formatIt->value().as_object().find("duration");
      if (
        durationIt != formatIt->value().as_object().end()
        && durationIt->value().is_string()
      ) {
        if (
          auto const duration =
            parseDouble(std::string_view{durationIt->value().as_string()})
        ) {
          probe.durationUs =
            static_cast<std::uint64_t>(std::llround(duration.value() * 1'000'000.0));
        }
      }
    }
  }

  return probe;
}

auto metricText(videoquality::QualityMetric metric) -> std::string_view {
  return metric == videoquality::QualityMetric::Vmaf ? "VMAF" : "SSIM";
}

auto printWindows(std::span<Window const> windows, std::optional<std::size_t> worstIndex)
  -> void {
  terminal::println(
    Info,
    "Preview windows ({}):",
    windows.size() == 1 ? "full comparison" : "5 x 10s"
  );
  for (auto index = std::size_t{}; index < windows.size(); ++index) {
    auto const& window = windows[index];
    auto scoreText = std::string{"-"};
    if (window.score.has_value()) {
      if (window.metric == videoquality::QualityMetric::Vmaf) {
        scoreText =
          std::format("{} {:.1f}", metricText(window.metric), window.score.value());
      } else {
        scoreText =
          std::format("{} {:.3f}", metricText(window.metric), window.score.value());
      }
    }
    auto suffix =
      worstIndex.has_value() && worstIndex.value() == index ? "  (worst)" : "";
    terminal::println(
      Info,
      "  [{}] {}  {}{}",
      index + 1,
      formatTimeRange(window.startUs, window.durationUs),
      scoreText,
      suffix
    );
  }
}

}  // namespace

auto pickPreviewWindows(
  std::uint64_t shorterDurationUs,
  std::optional<std::pair<double, double>> manualRange
) -> eh::Result<std::vector<Window>> {
  if (manualRange.has_value()) {
    auto const startSec = manualRange->first;
    auto const durationSec = manualRange->second;
    auto const totalSec = seconds(shorterDurationUs);
    if (startSec < 0.0 || std::isnan(startSec)) {
      return eh::makeError("--start must be a non-negative number.");
    }
    if (startSec >= totalSec) {
      return eh::makeError(
        "--start {}s is beyond the video duration ({:.1f}s).",
        startSec,
        totalSec
      );
    }
    auto const clamped = std::min(durationSec, totalSec - startSec);
    if (clamped <= 0.0) {
      return eh::makeError(
        "Invalid preview range: --start {}s --duration {}s.",
        startSec,
        durationSec
      );
    }
    return std::vector<Window>{Window{
      static_cast<std::uint64_t>(startSec * 1'000'000.0),
      static_cast<std::uint64_t>(clamped * 1'000'000.0),
    }};
  }

  if (shorterDurationUs < kFullComparisonBudgetUs) {
    return std::vector<Window>{Window{0, shorterDurationUs}};
  }

  auto windows = std::vector<Window>{};
  windows.reserve(kWindowCount);
  for (auto index = std::size_t{}; index < kWindowCount; ++index) {
    auto const startUs = static_cast<std::uint64_t>(
      static_cast<double>(index)
      * static_cast<double>(shorterDurationUs - kWindowDurationUs)
      / static_cast<double>(kWindowCount - 1)
    );
    windows.push_back(Window{startUs, kWindowDurationUs});
  }
  return windows;
}

auto run(appctx::AppContext& ctx, PreviewOptions const& options) -> eh::Result<int> {
  for (auto const& path: {options.original, options.encoded}) {
    if (!fs::is_regular_file(path)) {
      return eh::makeError("Preview input does not exist: {}", path.string());
    }
  }

  auto const outputPath = options.output.has_value() ? options.output.value()
                                                     : options.original.parent_path()
      / std::format("{}.preview.mp4", options.original.stem().string());
  if (outputPath == options.original || outputPath == options.encoded) {
    return eh::makeError(
      "Preview output would overwrite an input file: {}",
      outputPath.string()
    );
  }

  auto const originalRes = probeVideo(ctx.toolchain, ctx.runtime, options.original);
  if (!originalRes) { return eh::makeError("{}", originalRes.error()); }
  auto const encodedRes = probeVideo(ctx.toolchain, ctx.runtime, options.encoded);
  if (!encodedRes) { return eh::makeError("{}", encodedRes.error()); }
  auto const& original = originalRes.value();
  auto const& encoded = encodedRes.value();

  auto const shorterDurationUs = std::min(original.durationUs, encoded.durationUs);
  if (shorterDurationUs == 0) {
    return eh::makeError(
      "Cannot preview a video with zero duration: {}",
      options.original.string()
    );
  }

  auto manualRange = std::optional<std::pair<double, double>>{};
  if (options.startSeconds.has_value() || options.durationSeconds.has_value()) {
    manualRange = std::pair{
      options.startSeconds.value_or(0.0),
      options.durationSeconds.value_or(0.0),
    };
  }

  auto windowsRes = pickPreviewWindows(shorterDurationUs, manualRange);
  if (!windowsRes) { return eh::makeError("{}", windowsRes.error()); }
  auto windows = std::move(windowsRes.value());

  auto worstIndex = std::optional<std::size_t>{};
  if (!manualRange.has_value()) {
    auto const ffmpeg = ctx.toolchain.ffmpegPath.value_or(fs::path{"ffmpeg"});
    auto const info =
      ctx.runtime.videoInfoCache.find(options.original).value_or(boost::json::value{});
    auto worstScore = std::optional<double>{};
    for (auto index = std::size_t{}; index < windows.size(); ++index) {
      auto& window = windows[index];
      auto const scores = videoquality::measureSegmentQuality(
        ffmpeg,
        options.original,
        options.encoded,
        window.startUs,
        window.durationUs,
        info
      );
      if (scores.has_value()) {
        window.metric = scores->metric;
        window.score = videoquality::percentile(scores->frameScores, 5.0);
      } else {
        LOG_WARN(
          "Preview scoring failed for window {}us: {}",
          window.startUs,
          scores.error()
        );
      }
      if (
        window.score.has_value()
        && (!worstScore.has_value() || window.score.value() < worstScore.value())
      ) {
        worstScore = window.score;
        worstIndex = index;
      }
    }
    printWindows(windows, worstIndex);
  }

  // outputPath was validated against the inputs at the top of run().
  fs::create_directories(outputPath.parent_path());

  auto const spec = FiltergraphSpec{
    .original = original,
    .encoded = encoded,
    .windows = std::move(windows),
  };
  auto const cmd = buildPreviewCommand(
    ctx.toolchain.ffmpegPath.value_or(fs::path{"ffmpeg"}),
    options.original,
    options.encoded,
    spec,
    outputPath
  );

  ExecResult result{};
  try {
    result = exec2(cmd);
  } catch (std::exception const& ex) {
    return eh::makeError("Preview generation could not be launched: {}", ex.what());
  }
  if (result.exitCode != 0 || !fs::exists(outputPath)) {
    return eh::makeError("Preview generation failed (exit code {}).", result.exitCode);
  }

  terminal::println(Success, "Preview written to: {}", terminal::path(outputPath));
  if (!options.noOpen) {
    if (openfile::openWithDefaultApp(outputPath)) {
      terminal::println(Info, "Opened in the default player.");
    } else {
      terminal::println(Warning, "Could not open the preview in the default player.");
    }
  }
  return 0;
}

}  // namespace preview
