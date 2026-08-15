#include "video/encode_probe.h"
#include "video/encode_config.h"
#include "video/video_batch_execution.h"
#include "video/video_quality.h"

#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

auto makePoint(int cq, double p5, std::uint64_t bytes = 100'000)
  -> encodeprobe::ProbePoint {
  return encodeprobe::ProbePoint{
    .cq = cq,
    .p5 = p5,
    .metric = videoquality::QualityMetric::Vmaf,
    .segmentBytes = bytes,
  };
}

// A scripted measure: scores per cq from a table (missing cq → nullopt).
auto scriptedMeasure(std::array<std::optional<double>, 7> const& scoreByCq)
  -> encodeprobe::ProbeMeasure {
  // index = (cq - 16) / 4 → cq 16..40
  return [scoreByCq](int cq) -> std::optional<encodeprobe::ProbePoint> {
    auto const index = static_cast<std::size_t>((cq - encodeprobe::kMinCq) / 4);
    if (index >= scoreByCq.size() || !scoreByCq[index].has_value()) {
      return std::nullopt;
    }
    return makePoint(cq, scoreByCq[index].value());
  };
}

}  // namespace

TEST_CASE("pickProbeWindows picks two uniform 10s windows at 25%/75%", "[encode-probe]") {
  auto const windows = encodeprobe::pickProbeWindows(40'000'000);
  REQUIRE(windows.has_value());
  CHECK(windows->first.startUs == 10'000'000);
  CHECK(windows->first.durationUs == 10'000'000);
  CHECK(windows->second.startUs == 30'000'000);

  auto const longVideo = encodeprobe::pickProbeWindows(100'000'000);
  REQUIRE(longVideo.has_value());
  CHECK(longVideo->first.startUs == 25'000'000);
  CHECK(longVideo->second.startUs == 75'000'000);
}

TEST_CASE("pickProbeWindows skips videos shorter than the budget", "[encode-probe]") {
  CHECK_FALSE(encodeprobe::pickProbeWindows(39'999'999).has_value());
  CHECK_FALSE(encodeprobe::pickProbeWindows(30'000'000).has_value());
  CHECK_FALSE(encodeprobe::pickProbeWindows(0).has_value());
}

TEST_CASE(
  "decideCq interpolates the crossing between adjacent probed points",
  "[encode-probe]"
) {
  // Spec scenario: 28 at p5 95.8, 32 at p5 94.6, floor 95.
  auto const points = std::vector{
    makePoint(24, 96.5),
    makePoint(28, 95.8),
    makePoint(32, 94.6),
  };
  auto const decision = encodeprobe::decideCq(points, 95);
  CHECK_FALSE(decision.unreachableFloor);
  // t = (95.8-95)/(95.8-94.6) = 2/3 → cq = 28 + 2.667 = 30.67 → 30.
  CHECK(decision.cq == 30);
  CHECK(decision.p5 == Catch::Approx(95.0));
  CHECK(decision.videoBitrateBps == Catch::Approx(40'000.0));  // 100000B*8/20s
}

TEST_CASE(
  "decideCq picks the highest probed cq when the floor is always met",
  "[encode-probe]"
) {
  auto const points = std::vector{
    makePoint(24, 98.0),
    makePoint(28, 97.5),
    makePoint(32, 97.0),
    makePoint(36, 96.5),
    makePoint(40, 96.0),
  };
  auto const decision = encodeprobe::decideCq(points, 95);
  CHECK_FALSE(decision.unreachableFloor);
  CHECK(decision.cq == 40);
  CHECK(decision.p5 == Catch::Approx(96.0));
}

TEST_CASE(
  "decideCq degrades to the lowest cq when the floor is unreachable",
  "[encode-probe]"
) {
  auto const points = std::vector{
    makePoint(16, 93.0),
    makePoint(20, 92.5),
    makePoint(24, 92.0),
    makePoint(28, 91.0),
    makePoint(32, 90.0),
  };
  auto const decision = encodeprobe::decideCq(points, 95);
  CHECK(decision.unreachableFloor);
  CHECK(decision.cq == 16);
  CHECK(decision.p5 == Catch::Approx(93.0));
}

TEST_CASE(
  "decideCq keeps a probed cq whose p5 exactly equals the floor",
  "[encode-probe]"
) {
  auto const points = std::vector{
    makePoint(24, 96.0),
    makePoint(28, 95.0),
    makePoint(32, 94.0),
  };
  auto const decision = encodeprobe::decideCq(points, 95);
  CHECK_FALSE(decision.unreachableFloor);
  CHECK(decision.cq == 28);
  CHECK(decision.p5 == Catch::Approx(95.0));
}

TEST_CASE("decideCq compares ssim points against the mapped floor", "[encode-probe]") {
  auto const ssim = [](int cq, double p5) {
    return encodeprobe::ProbePoint{
      .cq = cq,
      .p5 = p5,
      .metric = videoquality::QualityMetric::Ssim,
      .segmentBytes = 100'000,
    };
  };
  // Floor 95 → 0.980; crossing between 28 (0.985) and 32 (0.975).
  auto const points = std::vector{
    ssim(24, 0.990),
    ssim(28, 0.985),
    ssim(32, 0.975),
  };
  auto const decision = encodeprobe::decideCq(points, 95);
  CHECK_FALSE(decision.unreachableFloor);
  CHECK(decision.cq == 30);
  CHECK(decision.p5 == Catch::Approx(0.980));
}

TEST_CASE(
  "probeCqSequence stops at the base grid when the floor is bracketed",
  "[encode-probe]"
) {
  auto const points = encodeprobe::probeCqSequence(
    scriptedMeasure(
      std::array{
        std::optional<double>{96.0},  // 16
        std::optional<double>{96.5},  // 20
        std::optional<double>{97.0},  // 24
        std::optional<double>{96.0},  // 28
        std::optional<double>{93.0},  // 32
        std::optional<double>{92.0},  // 36
        std::optional<double>{91.0},  // 40
      }
    ),
    95
  );
  REQUIRE(points.has_value());
  auto cqs = std::vector<int>{};
  for (auto const& point: points.value()) { cqs.push_back(point.cq); }
  CHECK(cqs == std::vector<int>({24, 28, 32}));
}

TEST_CASE("probeCqSequence steps down while the floor is unmet", "[encode-probe]") {
  auto const unmet20 = encodeprobe::probeCqSequence(
    scriptedMeasure(
      std::array{
        std::optional<double>{96.0},  // 16 (unused: 20 met)
        std::optional<double>{95.5},  // 20 met
        std::optional<double>{94.0},  // 24 unmet
        std::optional<double>{93.0},  // 28
        std::optional<double>{92.0},  // 32
        std::optional<double>{91.0},  // 36
        std::optional<double>{90.0},  // 40
      }
    ),
    95
  );
  REQUIRE(unmet20.has_value());
  auto cqs = std::vector<int>{};
  for (auto const& point: unmet20.value()) { cqs.push_back(point.cq); }
  CHECK(cqs == std::vector<int>({24, 28, 32, 20}));

  auto const unmet16 = encodeprobe::probeCqSequence(
    scriptedMeasure(
      std::array{
        std::optional<double>{94.0},  // 16 unmet
        std::optional<double>{94.5},  // 20 unmet
        std::optional<double>{94.0},  // 24 unmet
        std::optional<double>{93.0},  // 28
        std::optional<double>{92.0},  // 32
        std::optional<double>{91.0},  // 36
        std::optional<double>{90.0},  // 40
      }
    ),
    95
  );
  REQUIRE(unmet16.has_value());
  cqs.clear();
  for (auto const& point: unmet16.value()) { cqs.push_back(point.cq); }
  CHECK(cqs == std::vector<int>({24, 28, 32, 20, 16}));
}

TEST_CASE("probeCqSequence steps up while the floor stays met", "[encode-probe]") {
  auto const met36 = encodeprobe::probeCqSequence(
    scriptedMeasure(
      std::array{
        std::optional<double>{91.0},  // 16
        std::optional<double>{92.0},  // 20
        std::optional<double>{95.5},  // 24 met
        std::optional<double>{96.0},  // 28 met
        std::optional<double>{95.5},  // 32 met
        std::optional<double>{95.2},  // 36 met
        std::optional<double>{94.0},  // 40 unmet
      }
    ),
    95
  );
  REQUIRE(met36.has_value());
  auto cqs = std::vector<int>{};
  for (auto const& point: met36.value()) { cqs.push_back(point.cq); }
  CHECK(cqs == std::vector<int>({24, 28, 32, 36, 40}));

  auto const met40 = encodeprobe::probeCqSequence(
    scriptedMeasure(
      std::array{
        std::optional<double>{91.0},  // 16
        std::optional<double>{92.0},  // 20
        std::optional<double>{95.5},  // 24 met
        std::optional<double>{96.0},  // 28 met
        std::optional<double>{95.5},  // 32 met
        std::optional<double>{95.4},  // 36 met
        std::optional<double>{95.2},  // 40 met
      }
    ),
    95
  );
  REQUIRE(met40.has_value());
  cqs.clear();
  for (auto const& point: met40.value()) { cqs.push_back(point.cq); }
  CHECK(cqs == std::vector<int>({24, 28, 32, 36, 40}));
}

TEST_CASE("probeCqSequence fails when a measurement fails", "[encode-probe]") {
  auto const points = encodeprobe::probeCqSequence(
    scriptedMeasure(
      std::array{
        std::optional<double>{96.0},          // 16
        std::optional<double>{95.5},          // 20 (unused)
        std::optional<double>{94.0},          // 24 unmet
        std::optional<double>{93.0},          // 28
        std::optional<double>{std::nullopt},  // 32 → fails
        std::optional<double>{91.0},          // 36
        std::optional<double>{90.0},          // 40
      }
    ),
    95
  );
  CHECK_FALSE(points.has_value());
}

TEST_CASE(
  "probe segment config equals production config modulo cq and output path",
  "[encode-probe]"
) {
  auto toolchain = appctx::ToolchainPaths{};
  toolchain.ffmpegPath = fs::path{"ffmpeg"};
  auto const settings = EncodeInputSettings{
    .nvencPreset = std::string{"p7"},
    .maxrateKbps = 15000,
  };
  auto const videoCodec = std::optional<std::string>{"hevc_nvenc"};

  auto const probeCfg = encodeprobe::buildProbeSegmentConfig(
    toolchain,
    "in.mp4",
    "mp4",
    videoCodec,
    settings,
    24,
    10'000'000,
    10'000'000,
    "probe/cq24_0.ts"
  );
  auto const productionCfg = buildSegmentEncodeConfig(
    toolchain,
    "in.mp4",
    "mp4",
    24,
    videoCodec,
    settings,
    0,
    10'000'000,
    10'000'000,
    "probe/cq24_0.ts"
  );
  CHECK(probeCfg == productionCfg);

  auto const defaultCfg = buildSegmentEncodeConfig(
    toolchain,
    "in.mp4",
    "mp4",
    28,
    videoCodec,
    settings,
    0,
    10'000'000,
    10'000'000,
    "probe/cq24_0.ts"
  );
  CHECK(probeCfg.crf == 24);
  CHECK(defaultCfg.crf == 28);
  CHECK(probeCfg.tempOutputPath == defaultCfg.tempOutputPath);
}

TEST_CASE("previewHint formats the comparison command", "[encode-probe]") {
  CHECK(
    encodeprobe::previewHint("a.mp4", "b.mp4") == "encro preview \"a.mp4\" \"b.mp4\""
  );
}

#if defined(_WIN32)

namespace {

class ScopedEnvVar {
public:
  ScopedEnvVar(std::string name, std::string value)
    : name_(std::move(name)), hadOriginal_(false) {
    auto const original = std::getenv(name_.c_str());
    if (original != nullptr) {
      originalValue_ = original;
      hadOriginal_ = true;
    }
    _putenv_s(name_.c_str(), value.c_str());
  }

  ScopedEnvVar(ScopedEnvVar const&) = delete;
  auto operator=(ScopedEnvVar const&) -> ScopedEnvVar& = delete;

  ~ScopedEnvVar() {
    if (hadOriginal_) {
      _putenv_s(name_.c_str(), originalValue_.c_str());
    } else {
      _putenv_s(name_.c_str(), "");
    }
  }

private:
  std::string name_;
  std::string originalValue_;
  bool hadOriginal_;
};

auto copyFakeTool(fs::path const& dir, std::string const& name) -> fs::path {
  auto const dst = dir / (name + ".exe");
  fs::copy_file(fs::path{FAKE_TOOL_EXE_PATH}, dst, fs::copy_options::overwrite_existing);
  return dst;
}

// Fake ffmpeg/ffprobe = the e2e fake_media_tool.exe (built once, FAKE_TOOL_EXE_PATH),
// copied per role so argv[0] selects ffprobe vs ffmpeg. The ffmpeg side writes a
// fake libvmaf JSON log for scoring invocations when ENCRO_FAKE_FFMPEG_WRITE_VMAF=1,
// with scores from ENCRO_FAKE_FFMPEG_VMAF_SCORES; duration via ENCRO_FAKE_FFPROBE_DURATION_SECS.
auto fillProbeContext(
  appctx::AppContext& ctx,
  fs::path const& toolDir,
  fs::path const& inputPath,
  std::string const& duration,
  std::string const& vmafScores,
  std::vector<std::unique_ptr<ScopedEnvVar>>& envs
) -> void {
  ctx.toolchain.ffprobePath = copyFakeTool(toolDir, "ffprobe");
  ctx.toolchain.ffmpegPath = copyFakeTool(toolDir, "ffmpeg");
  envs.push_back(
    std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFPROBE_DURATION_SECS", duration)
  );
  envs.push_back(std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_WRITE_VMAF", "1"));
  envs.push_back(
    std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_VMAF_SCORES", vmafScores)
  );

  ctx.config.outputFormat = "mp4";
  ctx.config.minVmaf = 95;
  testutils::touchFile(inputPath);
}

auto leftoverProbeDirs() -> std::vector<fs::path> {
  auto dirs = std::vector<fs::path>{};
  for (auto const& entry: fs::directory_iterator{fs::temp_directory_path()}) {
    if (
      entry.is_directory() && entry.path().filename().string().starts_with("encro_probe_")
    ) {
      dirs.push_back(entry.path());
    }
  }
  return dirs;
}

}  // namespace

TEST_CASE("runProbePhase probes and decides with fake tools", "[encode-probe]") {
  TempDir temp;
  auto const inputPath = temp.path / "sample.mp4";
  auto ctx = appctx::AppContext{};
  auto envs = std::vector<std::unique_ptr<ScopedEnvVar>>{};
  fillProbeContext(ctx, temp.path, inputPath, "100.0", "96.0", envs);

  auto const result = encodeprobe::runProbePhase(ctx, std::vector<fs::path>{inputPath});
  REQUIRE(result.has_value());

  auto const it = result->plans.find(inputPath);
  REQUIRE(it != result->plans.end());
  auto const& plan = it->second;
  CHECK(plan.probed);
  // All probed cqs meet the floor (96 >= 95), so the highest cq 40 wins.
  CHECK(plan.chosenCq == 40);
  REQUIRE(plan.p5.has_value());
  CHECK(plan.p5.value() == Catch::Approx(96.0));
  CHECK(plan.estimatedBytes.has_value());
  CHECK_FALSE(plan.unreachableFloor);
  CHECK(result->attentionWarnings.empty());
  CHECK(leftoverProbeDirs().empty());
}

TEST_CASE(
  "runProbePhase reports unreachable floors as attention warnings",
  "[encode-probe]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.mp4";
  auto ctx = appctx::AppContext{};
  auto envs = std::vector<std::unique_ptr<ScopedEnvVar>>{};
  fillProbeContext(ctx, temp.path, inputPath, "100.0", "93.0", envs);

  auto const result = encodeprobe::runProbePhase(ctx, std::vector<fs::path>{inputPath});
  REQUIRE(result.has_value());

  auto const it = result->plans.find(inputPath);
  REQUIRE(it != result->plans.end());
  auto const& plan = it->second;
  CHECK(plan.probed);
  CHECK(plan.chosenCq == encodeprobe::kMinCq);
  CHECK(plan.unreachableFloor);
  REQUIRE(result->attentionWarnings.size() == 1);
  CHECK(
    result->attentionWarnings.front().find("quality floor unreachable")
    != std::string::npos
  );
  CHECK(leftoverProbeDirs().empty());
}

TEST_CASE("runProbePhase skips short videos with the default cq", "[encode-probe]") {
  TempDir temp;
  auto const inputPath = temp.path / "short.mp4";
  auto ctx = appctx::AppContext{};
  auto envs = std::vector<std::unique_ptr<ScopedEnvVar>>{};
  fillProbeContext(ctx, temp.path, inputPath, "30.0", "96.0", envs);

  auto const result = encodeprobe::runProbePhase(ctx, std::vector<fs::path>{inputPath});
  REQUIRE(result.has_value());

  auto const it = result->plans.find(inputPath);
  REQUIRE(it != result->plans.end());
  auto const& plan = it->second;
  CHECK_FALSE(plan.probed);
  CHECK(plan.chosenCq == encodeprobe::kDefaultCq);
  CHECK_FALSE(plan.unreachableFloor);
  CHECK_FALSE(plan.p5.has_value());
}

namespace {

// Writes a file of the given byte size without allocating a huge buffer.
auto writeSizedFile(fs::path const& path, std::uintmax_t size) -> void {
  auto out = std::ofstream{path, std::ios::binary};
  out.seekp(static_cast<std::streamoff>(size) - 1);
  out.put('\0');
}

}  // namespace

TEST_CASE(
  "printProbePlan renders a sorted table with warnings at the bottom",
  "[encode-probe]"
) {
  TempDir temp;
  auto const kMegabyte = std::uintmax_t{1'048'576};
  auto const normalA = temp.path / "beta.mp4";
  auto const normalB = temp.path / "alpha.mp4";
  auto const bad = temp.path / "gamma.mp4";
  writeSizedFile(normalA, kMegabyte);
  writeSizedFile(normalB, kMegabyte);
  writeSizedFile(bad, kMegabyte);

  auto plans = std::vector<encodeprobe::ProbePlan>{
    {.inputPath = bad,
     .chosenCq = 16,
     .metric = videoquality::QualityMetric::Vmaf,
     .p5 = 3.72,
     .estimatedBytes = 2 * kMegabyte,
     .probed = true,
     .unreachableFloor = true},
    {.inputPath = normalB,
     .chosenCq = 29,
     .metric = videoquality::QualityMetric::Vmaf,
     .p5 = 95.0,
     .estimatedBytes = kMegabyte / 2,
     .probed = true},
    {.inputPath = normalA,
     .chosenCq = 26,
     .metric = videoquality::QualityMetric::Vmaf,
     .p5 = 95.04,
     .estimatedBytes = 2 * kMegabyte,
     .probed = true},
  };

  auto const out = temp.path / "stdout.txt";
  {
    ScopedEnvVar columns("COLUMNS", "80");
    auto capture = testutils::StdoutCapture{out};
    encodeprobe::printProbePlan(plans, 95);
  }
  auto const text = testutils::readTextFile(out);

  // Header with aligned columns.
  CHECK(text.find("  File") != std::string::npos);
  CHECK(text.find("CQ") != std::string::npos);
  CHECK(text.find("Est.Size") != std::string::npos);
  CHECK(text.find("Ratio") != std::string::npos);
  // Normal rows sorted by name; warning row last with a marker.
  auto const alphaPos = text.find("alpha.mp4");
  auto const betaPos = text.find("beta.mp4");
  auto const gammaPos = text.find("gamma.mp4");
  REQUIRE(alphaPos != std::string::npos);
  REQUIRE(betaPos != std::string::npos);
  REQUIRE(gammaPos != std::string::npos);
  CHECK(alphaPos < betaPos);
  CHECK(betaPos < gammaPos);
  CHECK(text.find("\xE2\x9A\xA0") != std::string::npos);  // warning marker
  // Unreachable count line.
  CHECK(text.find("1 file(s) can't reach the floor") != std::string::npos);
  // One-decimal p5 at/above the floor, two decimals below it.
  CHECK(text.find(" 26") != std::string::npos);
  CHECK(text.find("95.0") != std::string::npos);
  CHECK(text.find("3.72") != std::string::npos);
  // Signed percentage ratios: growing rows marked, shrinking row plain.
  CHECK(text.find("+100% \xE2\x86\x91") != std::string::npos);
  CHECK(
    text.find(
      "\xE2\x88\x92"
      "50%"
    )
    != std::string::npos
  );  // -50%
  // Total line with signed percentage (4.5 MB est / 3 MB source = +50%).
  CHECK(text.find("Total: 3 file(s)") != std::string::npos);
  CHECK(text.find("+50% \xE2\x86\x91") != std::string::npos);
}

TEST_CASE(
  "printProbePlan renders the two-line fallback on tiny terminals",
  "[encode-probe]"
) {
  TempDir temp;
  auto const kMegabyte = std::uintmax_t{1'048'576};
  auto const input = temp.path / "clip.mp4";
  writeSizedFile(input, kMegabyte);
  auto plans = std::vector<encodeprobe::ProbePlan>{
    {.inputPath = input,
     .chosenCq = 28,
     .metric = videoquality::QualityMetric::Vmaf,
     .p5 = 96.0,
     .estimatedBytes = kMegabyte / 2,
     .probed = true},
  };
  auto const out = temp.path / "stdout.txt";
  {
    ScopedEnvVar columns("COLUMNS", "40");
    auto capture = testutils::StdoutCapture{out};
    encodeprobe::printProbePlan(plans, 95);
  }
  auto const text = testutils::readTextFile(out);
  CHECK(text.find("clip.mp4") != std::string::npos);
  CHECK(
    text.find(
      "CQ 28 \xC2\xB7 p5 96.0 \xC2\xB7 0.5 MB \xC2\xB7 \xE2\x88\x92"
      "50%"
    )
    != std::string::npos
  );
  // No table header in the two-line form.
  CHECK(text.find("Est.Size") == std::string::npos);
}

TEST_CASE("printProbePlan caps the name column on wide terminals", "[encode-probe]") {
  TempDir temp;
  auto const kMegabyte = std::uintmax_t{1'048'576};
  auto const first = temp.path / "alpha.mp4";
  auto const second = temp.path / "beta.mp4";
  writeSizedFile(first, kMegabyte);
  writeSizedFile(second, kMegabyte);
  auto plans = std::vector<encodeprobe::ProbePlan>{
    {.inputPath = first,
     .chosenCq = 26,
     .metric = videoquality::QualityMetric::Vmaf,
     .p5 = 95.0,
     .estimatedBytes = 2 * kMegabyte,
     .probed = true},
    {.inputPath = second,
     .chosenCq = 28,
     .metric = videoquality::QualityMetric::Vmaf,
     .p5 = 96.0,
     .estimatedBytes = kMegabyte / 2,
     .probed = true},
  };
  auto const out = temp.path / "stdout.txt";
  {
    ScopedEnvVar columns("COLUMNS", "250");
    auto capture = testutils::StdoutCapture{out};
    encodeprobe::printProbePlan(plans, 95);
  }
  auto const text = testutils::readTextFile(out);
  // The name column is capped at the longest file name (min 20), so no row
  // approaches the 250-column terminal width.
  auto in = std::istringstream{text};
  for (std::string line; std::getline(in, line);) {
    if (line.starts_with("  ")) { CHECK(line.size() <= 64); }
  }
}

#endif

TEST_CASE(
  "runEncodingTasks probes, prints the plan, and encodes with the chosen CQ",
  "[encode-probe][video-batch-execution]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.mp4";
  testutils::touchFile(inputPath);
  auto const outputFile = temp.path / "encoded" / "sample.mp4";

  auto ctx = appctx::AppContext{};
  auto envs = std::vector<std::unique_ptr<ScopedEnvVar>>{};
  fillProbeContext(ctx, temp.path, inputPath, "100.0", "96.0", envs);
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;  // no progress bars in tests

  auto const logPath = fs::path{"C:/Users/LEGION/AppData/Local/Temp/zz_fix_log.txt"};
  ScopedEnvVar logEnv{"ENCRO_FAKE_TOOL_LOG_FILE", logPath.string()};

  auto plannedOutputFiles = appctx::path_map<fs::path>{{inputPath, outputFile}};
  auto const outcome = videobatch::runEncodingTasks(
    ctx,
    {inputPath},
    plannedOutputFiles,
    videobatch::ActionIdMap{},
    1,
    0
  );

  REQUIRE(outcome.results.has_value());
  CHECK(outcome.results.value()[inputPath]);
  CHECK_FALSE(outcome.dryRun);
  CHECK(outcome.attentionWarnings.empty());
  CHECK(fs::exists(outputFile));

  // The probe chose CQ 40 (all probed cqs met the floor) and the real encode
  // must use it: the probe alone only produces 2 `-cq 40` segment encodes,
  // the 10s×10 production segments add more.
  auto const log = testutils::readTextFile(logPath);
  // The fake tool logs tab-separated argv tokens, so '-cq 40' appears as
  // "-cq\t40"; accept both separators.
  auto const cq40Count = static_cast<std::size_t>(
    std::ranges::count_if(std::views::split(log, '\n'), [](auto const& line) {
      auto const text = std::string_view{line};
      return text.find("-cq 40") != std::string_view::npos
        || text.find("-cq\t40") != std::string_view::npos;
    })
  );
  CHECK(cq40Count > 2);
}

TEST_CASE(
  "runEncodingTasks surfaces unreachable floors as attention warnings",
  "[encode-probe][video-batch-execution]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.mp4";
  testutils::touchFile(inputPath);
  auto const outputFile = temp.path / "encoded" / "sample.mp4";

  auto ctx = appctx::AppContext{};
  auto envs = std::vector<std::unique_ptr<ScopedEnvVar>>{};
  fillProbeContext(ctx, temp.path, inputPath, "100.0", "93.0", envs);
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;

  auto const logPath = temp.path / "ffmpeg_invocations.log";
  ScopedEnvVar logEnv{"ENCRO_FAKE_TOOL_LOG_FILE", logPath.string()};

  auto plannedOutputFiles = appctx::path_map<fs::path>{{inputPath, outputFile}};
  auto const outcome = videobatch::runEncodingTasks(
    ctx,
    {inputPath},
    plannedOutputFiles,
    videobatch::ActionIdMap{},
    1,
    0
  );

  REQUIRE(outcome.results.has_value());
  CHECK(outcome.results.value()[inputPath]);
  REQUIRE(outcome.attentionWarnings.size() == 1);
  CHECK(
    outcome.attentionWarnings.front().find("quality floor unreachable")
    != std::string::npos
  );
}

TEST_CASE(
  "runEncodingTasks dry-run prints the plan and encodes nothing",
  "[encode-probe]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.mp4";
  testutils::touchFile(inputPath);
  auto const outputFile = temp.path / "encoded" / "sample.mp4";

  auto ctx = appctx::AppContext{};
  auto envs = std::vector<std::unique_ptr<ScopedEnvVar>>{};
  fillProbeContext(ctx, temp.path, inputPath, "100.0", "96.0", envs);
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.dryRun = true;

  auto plannedOutputFiles = appctx::path_map<fs::path>{{inputPath, outputFile}};
  auto const outcome = videobatch::runEncodingTasks(
    ctx,
    {inputPath},
    plannedOutputFiles,
    videobatch::ActionIdMap{},
    1,
    0
  );

  CHECK(outcome.dryRun);
  REQUIRE(outcome.results.has_value());
  CHECK(outcome.results.value().empty());
  CHECK_FALSE(fs::exists(outputFile));
  CHECK(leftoverProbeDirs().empty());
}

TEST_CASE("runEncodingTasks skips probing entirely with --crf", "[encode-probe]") {
  TempDir temp;
  auto const inputPath = temp.path / "sample.mp4";
  testutils::touchFile(inputPath);
  auto const outputFile = temp.path / "encoded" / "sample.mp4";

  auto ctx = appctx::AppContext{};
  auto envs = std::vector<std::unique_ptr<ScopedEnvVar>>{};
  fillProbeContext(ctx, temp.path, inputPath, "100.0", "96.0", envs);
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.crf = 28;

  auto const logPath = temp.path / "ffmpeg_invocations.log";
  ScopedEnvVar logEnv{"ENCRO_FAKE_TOOL_LOG_FILE", logPath.string()};

  auto plannedOutputFiles = appctx::path_map<fs::path>{{inputPath, outputFile}};
  auto const outcome = videobatch::runEncodingTasks(
    ctx,
    {inputPath},
    plannedOutputFiles,
    videobatch::ActionIdMap{},
    1,
    0
  );

  REQUIRE(outcome.results.has_value());
  CHECK(outcome.results.value()[inputPath]);
  CHECK(fs::exists(outputFile));

  auto const log = testutils::readTextFile(logPath);
  // No probe segment encodes (they live under encro_probe_ temp dirs) and no
  // scoring invocations (log_path=).
  CHECK(log.find("encro_probe_") == std::string::npos);
  CHECK(log.find("log_path=") == std::string::npos);
}
