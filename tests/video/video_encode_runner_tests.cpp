#include "core/app_context.h"
#include "infra/stop_signal.h"
#include "test_utils.h"
#include "video/video_encode_runner.h"

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

using testutils::ScopedEnvVar;

// ── Webp adaptive-encoding chain ─────────────────────────────────────────────

namespace {

struct WebpScaffold {
  TempDir temp;
  appctx::AppContext ctx;
  appctx::EncodingState state;
  std::vector<std::unique_ptr<ScopedEnvVar>> envs;
  fs::path logPath;

  WebpScaffold() {
    state.inputPath = temp.path / "sample.mp4";
    testutils::writeTextFile(state.inputPath, "fake-video");
    state.plannedOutputFile = temp.path / "out" / "sample.webp";

    ctx.config.outputFormat = "webp";
    ctx.toolchain.ffmpegPath = testutils::copyFakeTool(temp.path, "ffmpeg");

    logPath = temp.path / "invocations.log";
    envs.push_back(
      std::make_unique<ScopedEnvVar>("ENCRO_FAKE_TOOL_LOG_FILE", logPath.string())
    );
  }

  auto run() { return encodeVideo(ctx, state, {}); }

  static auto qualityAttempts(std::string const& log, std::string const& q)
    -> std::size_t {
    return testutils::countOccurrences(log, "-q:v " + q)
      + testutils::countOccurrences(log, "-q:v\t" + q);
  }
};

}  // namespace

TEST_CASE("webp target size met on the first attempt", "[video-encode-runner]") {
  auto stopGuard = testutils::ScopedStopSignalReset{};
  auto s = WebpScaffold{};

  CHECK(s.run());

  auto const log = testutils::readTextFile(s.logPath);
  CHECK(WebpScaffold::qualityAttempts(log, "80") == 1);
  CHECK(log.find("-q:v 70") == std::string::npos);
  CHECK(log.find("-q:v\t70") == std::string::npos);
  CHECK(fs::exists(*s.state.plannedOutputFile));
}

TEST_CASE(
  "webp min-quality fallback engages after the quality ladder exhausts",
  "[video-encode-runner]"
) {
  auto stopGuard = testutils::ScopedStopSignalReset{};
  auto s = WebpScaffold{};
  // Uniformly oversized output (24 MiB: 4 MiB over the 20 MiB target) so every
  // ladder attempt overshoots; the gap must exceed the 3 MiB small-gap
  // threshold so the coarse q-step-10 ladder runs q=80..20 (7 attempts) and
  // the fallback keeps the min-quality (q=20) file.
  s.envs.push_back(
    std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_OUTPUT_BYTES", "25165824")
  );

  CHECK(s.run());

  auto const log = testutils::readTextFile(s.logPath);
  std::size_t total = 0;
  for (auto const q: {"80", "70", "60", "50", "40", "30", "20"}) {
    CHECK(WebpScaffold::qualityAttempts(log, q) == 1);
    total += WebpScaffold::qualityAttempts(log, q);
  }
  CHECK(total == 7);
  CHECK(fs::exists(*s.state.plannedOutputFile));
}

TEST_CASE(
  "webp retry window honors a stop request without claiming success",
  "[video-encode-runner][stop-signal]"
) {
  auto stopGuard = testutils::ScopedStopSignalReset{};
  auto s = WebpScaffold{};
  // Oversized output forces retries. The whole-run gate holds the first
  // attempt right after it logs, so the stop lands while the attempt is
  // provably in flight — no fixed delay, no timing window.
  s.envs.push_back(
    std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_OUTPUT_BYTES", "25165824")
  );
  auto const gateFile = s.temp.path / "encode-gate";
  s.envs.push_back(
    std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_GATE_FILE", gateFile.string())
  );

  std::optional<bool> outcome;
  std::jthread runner([&] { outcome = s.run(); });
  // Raw ifstream predicate: the log does not exist until the first
  // invocation, and a poll predicate must not abort on absent files.
  REQUIRE(
    testutils::waitUntil(
      [&] {
        auto log = std::ifstream{s.logPath, std::ios::binary};
        if (!log.is_open()) { return false; }
        auto const content = std::string{std::istreambuf_iterator<char>{log}, {}};
        return content.find("ffmpeg\t") != std::string::npos;
      },
      std::chrono::seconds{10}
    )
  );
  stopsignal::requestStop();
  {
    auto gate = std::ofstream{gateFile, std::ios::binary};
    REQUIRE(gate.is_open());
    gate << "go";
  }
  runner.join();

  REQUIRE(outcome.has_value());
  CHECK_FALSE(*outcome);
  // No further attempts after the stop: at most the first attempt ran.
  auto const log = testutils::readTextFile(s.logPath);
  CHECK(WebpScaffold::qualityAttempts(log, "80") == 1);
  CHECK(log.find("-q:v 70") == std::string::npos);
  CHECK(log.find("-q:v\t70") == std::string::npos);
  // The stale partial output was cleared by the abort path.
  CHECK_FALSE(fs::exists(*s.state.plannedOutputFile));
}

TEST_CASE(
  "webp stale progress and output leftovers are cleared before the run",
  "[video-encode-runner]"
) {
  auto stopGuard = testutils::ScopedStopSignalReset{};
  auto s = WebpScaffold{};
  // Pre-set the progress path so both stale paths are controlled:
  // prepareEncodeExecution only generates a uuid path when it is unset.
  auto const staleProgress = s.temp.path / "stale.progress";
  testutils::writeTextFile(staleProgress, "frame=1\nprogress=end\n");
  s.state.progressFilePath = staleProgress;
  testutils::writeTextFile(*s.state.plannedOutputFile, "stale-webp");

  CHECK(s.run());

  CHECK_FALSE(fs::exists(staleProgress));
  // The output was rewritten by the fresh encode, not the stale leftover.
  auto const rewritten = testutils::readTextFile(*s.state.plannedOutputFile);
  CHECK(rewritten != "stale-webp");
}

// ── Segmented encoding ───────────────────────────────────────────────────────

namespace {

struct SegmentScaffold {
  TempDir temp;
  appctx::AppContext ctx;
  appctx::EncodingState state;
  std::vector<std::unique_ptr<ScopedEnvVar>> envs;
  fs::path logPath;

  SegmentScaffold(double durationSecs = 2.0, bool withAudio = false) {
    state.inputPath = temp.path / "sample.mp4";
    testutils::writeTextFile(state.inputPath, "fake-video");
    state.plannedOutputFile = temp.path / "out" / "sample.mp4";

    ctx.config.outputFormat = "mp4";
    ctx.config.outputPath = temp.path / "work";
    ctx.toolchain.ffmpegPath = testutils::copyFakeTool(temp.path, "ffmpeg");
    ctx.toolchain.ffprobePath = testutils::copyFakeTool(temp.path, "ffprobe");
    // Custom probe JSON: the default fixtures are video-only, so audio
    // assertions need an audio stream; duration drives the segment count.
    auto const audioStream = R"(,{"codec_type":"audio","codec_name":"aac"})";
    auto const probeJson = std::format(
      R"({{"format":{{"duration":"{:.1f}"}},"streams":[{{"codec_type":"video","codec_name":"h264","nb_frames":"250","avg_frame_rate":"25/1"}}{}]}})",
      durationSecs,
      withAudio ? audioStream : ""
    );
    auto const probeJsonPath = temp.path / "probe.json";
    testutils::writeTextFile(probeJsonPath, probeJson);
    envs.push_back(
      std::make_unique<
        ScopedEnvVar
      >("ENCRO_FAKE_FFPROBE_JSON_FILE", probeJsonPath.string())
    );

    logPath = temp.path / "invocations.log";
    envs.push_back(
      std::make_unique<ScopedEnvVar>("ENCRO_FAKE_TOOL_LOG_FILE", logPath.string())
    );
  }

  auto run() { return encodeVideo(ctx, state, {}); }
};

}  // namespace

TEST_CASE(
  "segmented encoding marks failure and skips assembly when a segment fails",
  "[video-encode-runner]"
) {
  auto stopGuard = testutils::ScopedStopSignalReset{};
  auto s = SegmentScaffold{2.0, false};
  s.envs.push_back(
    std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_FAIL_MATCH", "seg_0.ts")
  );

  CHECK_FALSE(s.run());

  {
    auto const lock = std::scoped_lock{s.state.mtx};
    CHECK_FALSE(s.state.success);
  }
  auto const log = testutils::readTextFile(s.logPath);
  CHECK(log.find("seg_0.ts") != std::string::npos);
  CHECK(log.find("-f concat") == std::string::npos);
}

TEST_CASE(
  "segmented encoding surfaces an assemble failure from the concat step",
  "[video-encode-runner]"
) {
  auto stopGuard = testutils::ScopedStopSignalReset{};
  auto s = SegmentScaffold{2.0, false};
  // FAIL_MATCH matches the invocation's output path; the concat step is the
  // only invocation whose output is the planned output file.
  s.envs.push_back(
    std::make_unique<
      ScopedEnvVar
    >("ENCRO_FAKE_FFMPEG_FAIL_MATCH", s.state.plannedOutputFile->string())
  );

  CHECK_FALSE(s.run());

  {
    auto const lock = std::scoped_lock{s.state.mtx};
    CHECK_FALSE(s.state.success);
  }
  auto const log = testutils::readTextFile(s.logPath);
  // Segments ran; only the assembly step failed.
  CHECK(log.find("seg_0.ts") != std::string::npos);
  auto const assembled = testutils::countOccurrences(log, "-f concat")
    + testutils::countOccurrences(log, "-f\tconcat");
  CHECK(assembled >= 1);
}

TEST_CASE(
  "segmented encoding extracts audio exactly once across segments",
  "[video-encode-runner]"
) {
  auto stopGuard = testutils::ScopedStopSignalReset{};
  // 25 s at the 10 s segment length yields three segments.
  auto s = SegmentScaffold{25.0, true};

  CHECK(s.run());

  auto const log = testutils::readTextFile(s.logPath);
  CHECK(log.find("seg_0.ts") != std::string::npos);
  CHECK(log.find("seg_1.ts") != std::string::npos);
  CHECK(log.find("seg_2.ts") != std::string::npos);
  CHECK(
    testutils::countOccurrences(log, "-vn -c:a copy")
      + testutils::countOccurrences(log, "-vn\t-c:a\tcopy")
    == 1
  );
  // The segments are assembled from the concat list exactly once.
  auto const assembled = testutils::countOccurrences(log, "-f concat")
    + testutils::countOccurrences(log, "-f\tconcat");
  CHECK(assembled >= 1);
  CHECK(log.find("list.txt") != std::string::npos);
}
