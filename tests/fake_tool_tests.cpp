// ============================================================================
// [fake-tool] direct knob coverage: the unit-level behavior contract of the
// fake media tool binary itself, spawned the way the orchestrations do.
// ============================================================================

#include "test_utils.h"
#include "utils/utils.h"

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

namespace fs = std::filesystem;

using testutils::ScopedEnvVar;

namespace {

auto runFakeTool(std::string const& args) -> ExecResult {
  return exec2(std::format("\"{}\" {}", fs::path{FAKE_TOOL_EXE_PATH}.string(), args));
}

auto encodeArg(fs::path const& filePath) -> std::string {
  return std::format("\"{}\"", filePath.string());
}

// Counts ffmpeg-role lines in the fake tool's invocation log; a missing log
// counts as zero (waitUntil predicates must not abort on absent files).
auto countFfmpegInvocations(fs::path const& logPath) -> int {
  auto log = std::ifstream{logPath, std::ios::binary};
  if (!log.is_open()) { return 0; }
  auto const content = std::string{std::istreambuf_iterator<char>{log}, {}};
  auto count = 0;
  auto pos = std::string::size_type{0};
  while ((pos = content.find("ffmpeg\t", pos)) != std::string::npos) {
    ++count;
    pos += 1;
  }
  return count;
}

}  // namespace

TEST_CASE("fake tool writes a default-sized output on success", "[fake-tool]") {
  TempDir temp;
  auto const outPath = temp.path / "nested" / "out.mp4";

  auto const res = runFakeTool("-hide_banner -nostats -y " + encodeArg(outPath));

  CHECK(res.exitCode == 0);
  REQUIRE(fs::exists(outPath));
  CHECK(fs::file_size(outPath) >= 1024);
}

TEST_CASE("fake tool can leave partial output before failing", "[fake-tool]") {
  TempDir temp;
  auto const outPath = temp.path / "photo.jpg.partial";

  auto const exitEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_EXIT_CODE", "1"};
  auto const bytesEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_FAIL_OUTPUT_BYTES", "64"};

  auto const res = runFakeTool("-q:v 5 -i in.png -y " + encodeArg(outPath));

  CHECK(res.exitCode != 0);
  REQUIRE(fs::exists(outPath));
  CHECK(fs::file_size(outPath) == 64);
}

TEST_CASE("fake tool fails without touching output by default", "[fake-tool]") {
  TempDir temp;
  auto const outPath = temp.path / "out.mp4";

  auto const exitEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_EXIT_CODE", "2"};

  auto const res = runFakeTool("-y " + encodeArg(outPath));

  CHECK(res.exitCode == 2);
  CHECK_FALSE(fs::exists(outPath));
}

TEST_CASE("fake tool applies per-call schedules", "[fake-tool]") {
  TempDir temp;
  auto const outPath = temp.path / "seg.ts";
  auto const args = "-y " + encodeArg(outPath);

  auto const cntEnv =
    ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_COUNT_FILE", (temp.path / "calls").string()};
  auto const planEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_PLAN", "2:80:3"};

  // Calls outside the schedule keep the ambient static behavior.
  CHECK(runFakeTool(args).exitCode == 0);

  auto const started = std::chrono::steady_clock::now();
  CHECK(runFakeTool(args).exitCode == 3);
  auto const elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - started
  );
  CHECK(elapsedMs.count() >= 40);

  CHECK(runFakeTool(args).exitCode == 0);
}

TEST_CASE("fake tool ignores schedules without a call-count file", "[fake-tool]") {
  TempDir temp;
  auto const outPath = temp.path / "out.mp4";

  auto const planEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_PLAN", "1:1000:9"};

  auto const res = runFakeTool("-y " + encodeArg(outPath));

  CHECK(res.exitCode == 0);
}

TEST_CASE("fake tool tail schedules persist across later calls", "[fake-tool]") {
  TempDir temp;
  auto const outPath = temp.path / "out.mp4";
  auto const args = "-y " + encodeArg(outPath);

  auto const cntEnv =
    ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_COUNT_FILE", (temp.path / "calls").string()};
  auto const planEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_PLAN", "2-:0:3"};

  CHECK(runFakeTool(args).exitCode == 0);
  CHECK(runFakeTool(args).exitCode == 3);
  CHECK(runFakeTool(args).exitCode == 3);
}

TEST_CASE("fake ffprobe check-input opt-in fails on missing input", "[fake-tool]") {
  TempDir temp;
  auto const ffprobePath = testutils::copyFakeTool(temp.path, "ffprobe");
  auto const existing = temp.path / "existing.avi";
  testutils::writeTextFile(existing, "fake-video");
  auto const missing = temp.path / "missing.avi";

  auto const checkEnv = ScopedEnvVar{"ENCRO_FAKE_FFPROBE_CHECK_INPUT", "1"};
  auto const probeArgs = std::format(
    "\"{}\" -v quiet -print_format json \"{}\"",
    ffprobePath.string(),
    existing.string()
  );

  auto const okProbe = exec2(probeArgs, true);
  CHECK(okProbe.exitCode == 0);

  auto const badArgs = std::format(
    "\"{}\" -v quiet -print_format json \"{}\"",
    ffprobePath.string(),
    missing.string()
  );
  auto const badProbe = exec2(badArgs, true);
  CHECK(badProbe.exitCode == 2);
  CHECK(badProbe.output.find("probe input not found") != std::string::npos);
}

TEST_CASE("fake tool records completed inputs", "[fake-tool]") {
  TempDir temp;
  auto const logPath = temp.path / "inputs.log";

  auto const inputLogEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_INPUT_LOG", logPath.string()};
  CHECK(
    runFakeTool(
      std::format(
        "-i {} -y {}",
        encodeArg(temp.path / "a.png"),
        encodeArg(temp.path / "a.jpg")
      )
    )
      .exitCode
    == 0
  );

  auto const text = testutils::readTextFile(logPath);
  CHECK(text == std::format("{}\n", (temp.path / "a.png").string()));
}

TEST_CASE("fake tool emits progress end time unless suppressed", "[fake-tool]") {
  TempDir temp;
  auto const progressPath = temp.path / "prog.txt";
  auto const args = std::format(
    "-progress {} -ss 10 -t 20 -y {}",
    encodeArg(progressPath),
    encodeArg(temp.path / "out.mp4")
  );

  auto const defaultRun = runFakeTool(args);
  CHECK(defaultRun.exitCode == 0);
  auto const text = testutils::readTextFile(progressPath);
  CHECK(text.find("out_time_us=30000000") != std::string::npos);

  testutils::writeTextFile(progressPath, "");
  auto const suppressEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_PROGRESS_NO_END_TIME", "1"};
  auto const suppressed = runFakeTool(args);
  CHECK(suppressed.exitCode == 0);
  auto const suppressedText = testutils::readTextFile(progressPath);
  CHECK(suppressedText.find("out_time_us") == std::string::npos);
  CHECK(suppressedText.find("frame=") != std::string::npos);
  CHECK(suppressedText.find("progress=end") != std::string::npos);
}

TEST_CASE("fake tool creates parent dirs for progress files", "[fake-tool]") {
  TempDir temp;
  auto const progressPath = temp.path / "missing" / "deeper" / "prog.txt";

  auto const res = runFakeTool(
    std::format(
      "-progress {} -y {}",
      encodeArg(progressPath),
      encodeArg(temp.path / "o.mp4")
    )
  );

  CHECK(res.exitCode == 0);
  CHECK(fs::exists(progressPath));
}

TEST_CASE("fake tool gates from a configured call index onwards", "[fake-tool]") {
  TempDir temp;
  auto const firstOutput = temp.path / "first.mp4";
  auto const secondOutput = temp.path / "second.mp4";
  auto const logPath = temp.path / "tool.log";
  auto const gateFile = temp.path / "gate";

  auto const logEnv = ScopedEnvVar{"ENCRO_FAKE_TOOL_LOG_FILE", logPath.string()};
  auto const countEnv =
    ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_COUNT_FILE", (temp.path / "calls").string()};
  auto const gateEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_GATE_FILE", gateFile.string()};
  auto const fromCallEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_GATE_FROM_CALL", "2"};

  // Call 1 runs to completion despite the configured gate: bounded proof,
  // so a tool that gates everything fails here instead of stalling 30 s.
  auto first = std::jthread{[&] {
    (void)runFakeTool("-hide_banner -nostats -y " + encodeArg(firstOutput));
  }};
  REQUIRE(
    testutils::waitUntil([&] { return fs::exists(firstOutput); }, std::chrono::seconds{5})
  );
  first.join();

  // Call 2 logs its invocation, then blocks on the missing gate file: the
  // log line proves it started while the missing output proves it is held.
  auto holder = std::jthread{[&] {
    (void)runFakeTool("-hide_banner -nostats -y " + encodeArg(secondOutput));
  }};
  REQUIRE(
    testutils::waitUntil(
      [&] { return countFfmpegInvocations(logPath) >= 2; },
      std::chrono::seconds{10}
    )
  );
  CHECK_FALSE(fs::exists(secondOutput));

  // Releasing the gate finishes the held invocation.
  {
    auto gate = std::ofstream{gateFile, std::ios::binary};
    REQUIRE(gate.is_open());
    gate << "go";
  }
  holder.join();
  CHECK(fs::exists(secondOutput));
}

TEST_CASE("fake tool gates every invocation without a from-call index", "[fake-tool]") {
  TempDir temp;
  auto const outPath = temp.path / "out.mp4";
  auto const logPath = temp.path / "tool.log";
  auto const gateFile = temp.path / "gate";

  auto const logEnv = ScopedEnvVar{"ENCRO_FAKE_TOOL_LOG_FILE", logPath.string()};
  auto const gateEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_GATE_FILE", gateFile.string()};

  auto holder = std::jthread{[&] {
    (void)runFakeTool("-hide_banner -nostats -y " + encodeArg(outPath));
  }};
  REQUIRE(
    testutils::waitUntil(
      [&] { return countFfmpegInvocations(logPath) >= 1; },
      std::chrono::seconds{10}
    )
  );
  CHECK_FALSE(fs::exists(outPath));

  {
    auto gate = std::ofstream{gateFile, std::ios::binary};
    REQUIRE(gate.is_open());
    gate << "go";
  }
  holder.join();
  CHECK(fs::exists(outPath));
}

TEST_CASE("fake tool version probes neither gate nor consume an index", "[fake-tool]") {
  TempDir temp;
  auto const outPath = temp.path / "out.mp4";
  auto const countPath = temp.path / "calls";

  auto const logEnv =
    ScopedEnvVar{"ENCRO_FAKE_TOOL_LOG_FILE", (temp.path / "tool.log").string()};
  auto const countEnv =
    ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_COUNT_FILE", countPath.string()};
  auto const gateEnv =
    ScopedEnvVar{"ENCRO_FAKE_FFMPEG_GATE_FILE", (temp.path / "gate").string()};
  auto const fromCallEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_GATE_FROM_CALL", "2"};

  // The probe answers immediately under the configured gate...
  auto const version = runFakeTool("-version");
  CHECK(version.exitCode == 0);
  CHECK(version.output.find("version n5.1-fake") != std::string::npos);

  // ...and consumed no index: the first real encode is call 1, below the
  // from-call threshold, so it completes without ever seeing a gate file.
  auto encode = std::jthread{[&] {
    (void)runFakeTool("-hide_banner -nostats -y " + encodeArg(outPath));
  }};
  CHECK(
    testutils::waitUntil([&] { return fs::exists(outPath); }, std::chrono::seconds{5})
  );
  encode.join();
  CHECK(fs::exists(outPath));
  CHECK(testutils::readTextFile(countPath) == "1");
}
