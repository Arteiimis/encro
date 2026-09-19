#include "app/pipeline.h"
#include "core/job_state.h"
#include "core/work_dirs.h"
#include "infra/stop_signal.h"
#include "picture/picture_process.h"
#include "picture/picture_video_webp.h"
#include "test_utils.h"

#include <algorithm>
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
using testutils::copyFakeTool;
using testutils::listZipRegularEntryNames;
using testutils::ScopedEnvVar;
using testutils::ScopedStopSignalReset;
using testutils::writeSizedFile;
using testutils::writeTextFile;

namespace {

// The picture flow's conversion phase is the video workflow's WebP encoder, so
// the fake tool is the ffmpeg role; its default 1024-byte output keeps the
// adaptive search at the first quality tier.
struct ConversionFixture {
  TempDir temp;
  appctx::AppContext ctx;
  std::vector<std::unique_ptr<ScopedEnvVar>> envs;
  fs::path inputDir;
  fs::path logPath;

  explicit ConversionFixture(bool withJobState = false) {
    inputDir = temp.path / "pics";
    fs::create_directories(inputDir);

    ctx.config.processType = "picture";
    ctx.config.yesToAll = true;
    ctx.config.videoWebp = true;
    ctx.config.inputPath = inputDir;
    ctx.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

    logPath = temp.path / "invocations.log";
    envs.push_back(
      std::make_unique<ScopedEnvVar>("ENCRO_FAKE_TOOL_LOG_FILE", logPath.string())
    );
    // One marker file per started encode, so a test can count encodes in
    // flight without reading the shared log.
    envs.push_back(
      std::make_unique<
        ScopedEnvVar
      >("ENCRO_FAKE_FFMPEG_MARKER_DIR", (temp.path / "encode-markers").string())
    );

    if (withJobState) {
      ctx.config.stateFilePath = jobstate::buildDefaultStateFilePath(ctx.config).value();
      ctx.runtime.jobState = std::make_shared<jobstate::Store>(*ctx.config.stateFilePath);
    }
  }

  // Mirrors pipeline's job-state setup so the workflow can run on its own: the
  // state file is created by the first run and reused by the next.
  void ensureState() {
    if (!ctx.runtime.jobState) { return; }
    auto const initRes = ctx.runtime.jobState->initialize(ctx.config, false);
    REQUIRE(initRes);
    ctx.runtime.jobStateMatched = initRes.value();
  }

  auto run() {
    ensureState();
    return runPicturePackWorkflow(ctx, inputDir);
  }

  auto cacheDir() const -> fs::path {
    return workdirs::webpCacheDir(workdirs::resolveWorkRoot(ctx.config).value());
  }

  // Conversions that started, counted from the fake tool's per-encode marker
  // files under their WebP output name (picture compression writes .jpg and
  // stays out of this count). The shared invocation log is not a reliable
  // counter here: two encodes that start at the same moment can lose one of
  // the appended lines.
  auto encodeCount() const -> std::size_t {
    auto const markerDir = temp.path / "encode-markers";
    if (!fs::is_directory(markerDir)) { return 0; }
    auto count = std::size_t{0};
    for (auto const& entry: fs::directory_iterator{markerDir}) {
      if (entry.path().extension() == ".webp") { ++count; }
    }
    return count;
  }

  auto logText() const -> std::string {
    return fs::exists(logPath) ? testutils::readTextFile(logPath) : std::string{};
  }

  // The single archive a small run produces; its name encodes the entry range.
  auto packedZip() const -> fs::path {
    auto const packedDir = inputDir / "packed";
    for (auto const& entry: fs::directory_iterator{packedDir}) {
      if (entry.path().extension() == ".zip") { return entry.path(); }
    }
    return {};
  }

  // Uncompressed size of the packed entry ending with `suffix`; 0 when absent.
  auto packedEntrySize(std::string_view suffix) const -> std::uint64_t {
    auto const zipPath = packedZip();
    if (zipPath.empty()) { return 0; }

    auto zip = libzippp::ZipArchive{zipPath.string()};
    zip.open(libzippp::ZipArchive::ReadOnly);
    for (auto const& entry: zip.getEntries()) {
      if (!entry.getName().ends_with('/') && entry.getName().ends_with(suffix)) {
        auto const size = entry.getSize();
        zip.close();
        return size;
      }
    }
    zip.close();
    return 0;
  }
};

auto containsEntryEndingWith(
  std::vector<std::string> const& entries,
  std::string_view suffix
) -> bool {
  return std::ranges::any_of(entries, [&](std::string const& name) {
    return name.ends_with(suffix);
  });
}

// Runs only the conversion phase, leaving the cache and the job state in place
// the way a run that stopped before packing does. Returns the canceled count.
auto convertWithoutPacking(ConversionFixture& f) -> int {
  f.ensureState();

  auto const tasks = planPictureVideoConversions(f.ctx, f.inputDir);
  REQUIRE(tasks);
  REQUIRE_FALSE(tasks->empty());

  auto const outcome = picturewebp::runConversionPhase(f.ctx, tasks.value(), 1);
  REQUIRE(outcome);
  return outcome.value().canceled ? 1 : 0;
}

}  // namespace

TEST_CASE(
  "picture run with --video-webp packs the converted clip beside the pictures",
  "[picture-process][video-webp]"
) {
  ConversionFixture f;
  writeSizedFile(f.inputDir / "photo.jpg", 32);
  writeSizedFile(f.inputDir / "clip.mp4", 64);

  auto const runRes = f.run();

  REQUIRE(runRes);
  CHECK(runRes.value() == 0);

  auto const entries = listZipRegularEntryNames(f.packedZip());
  REQUIRE(entries.size() == 2);
  CHECK(containsEntryEndingWith(entries, ".webp"));
  CHECK(containsEntryEndingWith(entries, ".jpg"));
  // The source clip is replaced by its conversion, never packed alongside it.
  CHECK_FALSE(containsEntryEndingWith(entries, ".mp4"));
  // The successful run leaves no cache behind.
  CHECK_FALSE(fs::exists(f.cacheDir()));
}

TEST_CASE(
  "picture run without the flag never converts and never scans for videos",
  "[picture-process][video-webp]"
) {
  ConversionFixture f;
  f.ctx.config.videoWebp = false;
  writeSizedFile(f.inputDir / "photo.jpg", 32);
  writeSizedFile(f.inputDir / "clip.mp4", 64);

  auto const runRes = f.run();

  REQUIRE(runRes);
  CHECK(runRes.value() == 0);
  CHECK(f.encodeCount() == 0);

  auto const entries = listZipRegularEntryNames(f.packedZip());
  REQUIRE(entries.size() == 1);
  CHECK(entries.front().ends_with(".jpg"));
  CHECK_FALSE(fs::exists(f.cacheDir()));
}

TEST_CASE(
  "converted clip keeps its source directory as the packing group key",
  "[picture-process][video-webp]"
) {
  ConversionFixture f;
  f.ctx.config.recursive = true;
  auto const subDir = f.inputDir / "sub";
  fs::create_directories(subDir);
  writeSizedFile(subDir / "photo.jpg", 32);
  writeSizedFile(subDir / "clip.mp4", 64);

  auto const runRes = f.run();

  REQUIRE(runRes);
  CHECK(runRes.value() == 0);

  auto const entries = listZipRegularEntryNames(f.packedZip());
  REQUIRE(entries.size() == 2);
  // Both entries carry the same collision-group prefix: the clip is grouped
  // with the pictures of its own directory, not with its cache directory.
  CHECK(
    testutils::collisionGroupPrefix(entries[0])
    == testutils::collisionGroupPrefix(entries[1])
  );
}

TEST_CASE(
  "converted output appears only after the encoder succeeded",
  "[picture-process][video-webp]"
) {
  ConversionFixture f;
  writeSizedFile(f.inputDir / "photo.jpg", 32);
  writeSizedFile(f.inputDir / "clip.mp4", 64);
  f.envs.push_back(std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_EXIT_CODE", "1"));

  auto const runRes = f.run();

  // Every conversion failed: the run aborts instead of packing an archive
  // without the clips the flag promised.
  REQUIRE_FALSE(runRes);
  CHECK(runRes.error().find("All video conversions failed") != std::string::npos);
  CHECK_FALSE(fs::exists(f.inputDir / "packed"));
  // No final cached path holds anything; the failure left no partial output.
  CHECK_FALSE(fs::exists(f.cacheDir() / "1000__clip.webp"));
  CHECK_FALSE(fs::exists(f.cacheDir() / "1000__clip.partial.webp"));
}
TEST_CASE(
  "an interrupted conversion leaves no final output to pack",
  "[picture-process][video-webp]"
) {
  ScopedStopSignalReset stopGuard;
  ConversionFixture f;
  writeSizedFile(f.inputDir / "photo.jpg", 32);
  writeSizedFile(f.inputDir / "clip.mp4", 64);
  // The gate holds the encode in flight; the stop lands inside it, and the
  // encoder's own stop path then discards the partial output.
  auto const gateFile = f.temp.path / "convert-gate";
  f.envs.push_back(
    std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_GATE_FILE", gateFile.string())
  );

  std::optional<eh::Result<int>> outcome;
  auto runner = std::jthread{[&] { outcome = f.run(); }};
  auto const encodeStarted =
    testutils::waitUntil([&] { return f.encodeCount() >= 1; }, std::chrono::seconds{10});
  stopsignal::requestStop();
  {
    auto gate = std::ofstream{gateFile, std::ios::binary};
    REQUIRE(gate.is_open());
    gate << "go";
  }
  runner.join();
  REQUIRE(encodeStarted);

  REQUIRE(outcome.has_value());
  REQUIRE(outcome->has_value());
  CHECK(outcome->value() == stopsignal::kCanceledExitCode);
  // The killed conversion left nothing at a final cached path, so the next run
  // cannot mistake a partial file for a finished conversion.
  auto const cachedEntries = fs::exists(f.cacheDir()) ? [&]() {
    auto names = std::vector<std::string>{};
    for (auto const& entry: fs::directory_iterator{f.cacheDir()}) {
      names.push_back(entry.path().filename().string());
    }
    return names;
  }()
                                                      : std::vector<std::string>{};
  CHECK(std::ranges::none_of(cachedEntries, [](std::string const& name) {
    return name.ends_with(".webp");
  }));
  CHECK_FALSE(fs::exists(f.inputDir / "packed"));
}

TEST_CASE(
  "a completed conversion is reused from the cache on resume",
  "[picture-process][video-webp]"
) {
  ConversionFixture f{true};
  writeSizedFile(f.inputDir / "photo.jpg", 32);
  writeSizedFile(f.inputDir / "clip.mp4", 64);

  // A run that converted and then stopped before packing leaves both the cache
  // and the state record behind for the next run.
  auto const cached = [&]() -> fs::path {
    auto const tasks = planPictureVideoConversions(f.ctx, f.inputDir);
    REQUIRE(tasks);
    REQUIRE(tasks->size() == 1);
    return tasks->front().outputPath;
  }();

  CHECK(convertWithoutPacking(f) == 0);
  REQUIRE(fs::exists(cached));

  // Any encode attempt now fails, so a run that succeeds proves the finished
  // clip came from the cache instead of being converted again.
  f.envs
    .push_back(std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_FAIL_MATCH", ".webp"));
  auto const resumed = f.run();

  REQUIRE(resumed);
  CHECK(resumed.value() == 0);
  CHECK(containsEntryEndingWith(listZipRegularEntryNames(f.packedZip()), ".webp"));
}

TEST_CASE(
  "a replaced source clip discards its stale cached output",
  "[picture-process][video-webp]"
) {
  ConversionFixture f{true};
  writeSizedFile(f.inputDir / "photo.jpg", 32);
  auto const clipPath = writeSizedFile(f.inputDir / "clip.mp4", 64);

  CHECK(convertWithoutPacking(f) == 0);

  // Replace the clip, keeping its path: the cached output no longer describes
  // the source and must not be packed as if it did. The fresh encode writes a
  // different size, so the packed entry proves which output was used.
  writeTextFile(clipPath, std::string(4096, 'x'));
  f.envs
    .push_back(std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_OUTPUT_BYTES", "8192"));

  auto const resumed = f.run();

  REQUIRE(resumed);
  CHECK(resumed.value() == 0);
  CHECK(f.packedEntrySize(".webp") == 8192);
}

TEST_CASE(
  "picture quality change does not invalidate the conversion cache",
  "[picture-process][video-webp]"
) {
  ConversionFixture f{true};
  f.ctx.config.compressImages = true;
  f.ctx.config.imageQuality = 5;
  writeSizedFile(f.inputDir / "photo.jpg", 4096);
  writeSizedFile(f.inputDir / "clip.mp4", 64);

  // A run that converted and stopped before packing leaves the cache behind.
  CHECK(convertWithoutPacking(f) == 0);
  REQUIRE(f.encodeCount() == 1);

  // The picture quality is not part of the conversion's identity: an encode
  // attempt would fail the run, so success proves the cache was reused.
  f.ctx.config.imageQuality = 10;
  f.envs
    .push_back(std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_FAIL_MATCH", ".webp"));
  auto const resumed = f.run();

  REQUIRE(resumed);
  CHECK(resumed.value() == 0);
  CHECK(containsEntryEndingWith(listZipRegularEntryNames(f.packedZip()), ".webp"));
}

TEST_CASE(
  "conversion cache is not trusted without a matching saved state",
  "[picture-process][video-webp]"
) {
  ConversionFixture f;
  writeSizedFile(f.inputDir / "photo.jpg", 32);
  writeSizedFile(f.inputDir / "clip.mp4", 64);

  // A stale cache from an unrelated run, with no state to back it.
  auto const cachedOutput = f.cacheDir() / "1000__clip.webp";
  writeTextFile(cachedOutput, "stale-webp");

  auto const runRes = f.run();

  REQUIRE(runRes);
  CHECK(runRes.value() == 0);
  // The stale file was discarded and the clip converted for real.
  CHECK(f.encodeCount() == 1);
  CHECK_FALSE(fs::exists(cachedOutput));
}

TEST_CASE(
  "conversion phase runs after compression and before packing",
  "[picture-process][video-webp]"
) {
  ConversionFixture f;
  f.ctx.config.compressImages = true;
  f.ctx.config.imageQuality = 5;
  writeSizedFile(f.inputDir / "photo.jpg", 4096);
  writeSizedFile(f.inputDir / "clip.mp4", 64);

  auto const stdoutPath = f.temp.path / "stdout.txt";
  auto exitCode = 0;
  {
    auto const capture = testutils::StdoutCapture{stdoutPath};
    auto const runRes = f.run();
    REQUIRE(runRes);
    exitCode = runRes.value();
  }

  CHECK(exitCode == 0);
  auto const stdoutText = testutils::readTextFile(stdoutPath);
  auto const compressPos = stdoutText.find("Compressing");
  auto const convertPos = stdoutText.find("Converting");
  auto const packPos = stdoutText.find("Packing");
  REQUIRE(compressPos != std::string::npos);
  REQUIRE(convertPos != std::string::npos);
  REQUIRE(packPos != std::string::npos);
  CHECK(compressPos < convertPos);
  CHECK(convertPos < packPos);
}

TEST_CASE("no videos means no conversion phase line", "[picture-process][video-webp]") {
  ConversionFixture f;
  writeSizedFile(f.inputDir / "photo.jpg", 32);

  auto const stdoutPath = f.temp.path / "stdout.txt";
  auto exitCode = 0;
  {
    auto const capture = testutils::StdoutCapture{stdoutPath};
    auto const runRes = f.run();
    REQUIRE(runRes);
    exitCode = runRes.value();
  }

  CHECK(exitCode == 0);
  auto const stdoutText = testutils::readTextFile(stdoutPath);
  CHECK(stdoutText.find("Found 0 video(s)") != std::string::npos);
  CHECK(stdoutText.find("Converting") == std::string::npos);
}

TEST_CASE(
  "a clips-only directory still fails with the picture scan error",
  "[picture-process][video-webp]"
) {
  ConversionFixture f;
  writeSizedFile(f.inputDir / "clip.mp4", 64);

  auto const runRes = f.run();

  REQUIRE_FALSE(runRes);
  CHECK(runRes.error().find("No pictures found in directory") != std::string::npos);
  CHECK(f.encodeCount() == 0);
  CHECK_FALSE(fs::exists(f.inputDir / "packed"));
}

TEST_CASE(
  "converted clip takes the picture name and webp extension",
  "[picture-process][video-webp]"
) {
  ConversionFixture f;
  // Conflict handling off: entries keep the plain name, so the extension is
  // the only difference between the picture and the converted clip.
  f.ctx.config.forceNameConflictHandling = false;
  writeSizedFile(f.inputDir / "photo.jpg", 32);
  writeSizedFile(f.inputDir / "clip.mp4", 64);

  auto const runRes = f.run();

  REQUIRE(runRes);
  CHECK(runRes.value() == 0);
  CHECK(
    listZipRegularEntryNames(f.packedZip())
    == std::vector<std::string>{"1000__clip.webp", "1000__photo.jpg"}
  );
}

TEST_CASE(
  "keep layout names a converted clip by its relative path",
  "[picture-process][video-webp]"
) {
  ConversionFixture f;
  f.ctx.config.outputLayout = appctx::OutputLayout::Keep;
  f.ctx.config.recursive = true;
  auto const subDir = f.inputDir / "sub";
  fs::create_directories(subDir);
  writeSizedFile(subDir / "photo.jpg", 32);
  writeSizedFile(subDir / "clip.mp4", 64);

  auto const runRes = f.run();

  REQUIRE(runRes);
  CHECK(runRes.value() == 0);
  auto const entries = listZipRegularEntryNames(f.packedZip());
  CHECK(containsEntryEndingWith(entries, "sub/clip.webp"));
  CHECK(containsEntryEndingWith(entries, "sub/photo.jpg"));
}

TEST_CASE(
  "clips sharing a stem are disambiguated like the pictures",
  "[picture-process][video-webp]"
) {
  ConversionFixture f;
  f.ctx.config.recursive = true;
  auto const dirA = f.inputDir / "a";
  auto const dirB = f.inputDir / "b";
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  writeSizedFile(dirA / "photo.jpg", 32);
  writeSizedFile(dirA / "same.mp4", 64);
  writeSizedFile(dirB / "same.mp4", 64);

  auto const runRes = f.run();

  REQUIRE(runRes);
  CHECK(runRes.value() == 0);
  auto const entries = listZipRegularEntryNames(f.packedZip());
  REQUIRE(entries.size() == 3);
  CHECK(entries[0] != entries[1]);
  CHECK(
    std::ranges::count_if(
      entries,
      [](std::string const& name) { return name.ends_with(".webp"); }
    )
    == 2
  );
  CHECK(f.encodeCount() == 2);
}

TEST_CASE(
  "conversion concurrency stops at two without a job cap",
  "[picture-process][video-webp]"
) {
  ConversionFixture f;
  writeSizedFile(f.inputDir / "photo.jpg", 32);
  for (auto index = 0; index < 3; ++index) {
    writeSizedFile(f.inputDir / std::format("clip{}.mp4", index), 64);
  }
  // The gate pins every allowed conversion in flight, and each one logs its
  // invocation before blocking, so the log shows exactly how many run at once.
  auto const gateFile = f.temp.path / "convert-gate";
  f.envs.push_back(
    std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_GATE_FILE", gateFile.string())
  );

  std::optional<eh::Result<int>> outcome;
  auto runner = std::jthread{[&] { outcome = f.run(); }};
  auto const twoInFlight =
    testutils::waitUntil([&] { return f.encodeCount() >= 2; }, std::chrono::seconds{30});
  // While the gate holds, a third conversion would already have started if the
  // cap allowed it: the workers are live and only the gate stops them, so this
  // window is not a race against machine load.
  auto const thirdStarted =
    testutils::waitUntil([&] { return f.encodeCount() >= 3; }, std::chrono::seconds{1});
  {
    auto gate = std::ofstream{gateFile, std::ios::binary};
    REQUIRE(gate.is_open());
    gate << "go";
  }
  runner.join();

  REQUIRE(twoInFlight);
  CHECK_FALSE(thirdStarted);
  REQUIRE(outcome.has_value());
  REQUIRE(outcome->has_value());
  CHECK(outcome->value() == 0);
}

TEST_CASE(
  "conversion concurrency follows a lower job count",
  "[picture-process][video-webp]"
) {
  ConversionFixture f;
  f.ctx.config.maxParallelJobs = 1;
  writeSizedFile(f.inputDir / "photo.jpg", 32);
  writeSizedFile(f.inputDir / "clip0.mp4", 64);
  writeSizedFile(f.inputDir / "clip1.mp4", 64);
  auto const gateFile = f.temp.path / "convert-gate";
  f.envs.push_back(
    std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_GATE_FILE", gateFile.string())
  );

  std::optional<eh::Result<int>> outcome;
  auto runner = std::jthread{[&] { outcome = f.run(); }};
  auto const oneInFlight =
    testutils::waitUntil([&] { return f.encodeCount() >= 1; }, std::chrono::seconds{30});
  auto const secondStarted =
    testutils::waitUntil([&] { return f.encodeCount() >= 2; }, std::chrono::seconds{1});
  {
    auto gate = std::ofstream{gateFile, std::ios::binary};
    REQUIRE(gate.is_open());
    gate << "go";
  }
  runner.join();

  REQUIRE(oneInFlight);
  CHECK_FALSE(secondStarted);
  REQUIRE(outcome.has_value());
  REQUIRE(outcome->has_value());
  CHECK(outcome->value() == 0);
}

TEST_CASE(
  "a canceled run keeps the conversions it already finished",
  "[picture-process][video-webp]"
) {
  ScopedStopSignalReset stopGuard;
  ConversionFixture f;
  f.ctx.config.maxParallelJobs = 1;
  writeSizedFile(f.inputDir / "photo.jpg", 32);
  writeSizedFile(f.inputDir / "clip0.mp4", 64);
  writeSizedFile(f.inputDir / "clip1.mp4", 64);
  // Sequential conversions: the second one is held at the gate and then exits
  // with the cancel code, so exactly one clip finishes before the stop.
  auto const gateFile = f.temp.path / "convert-gate";
  auto const countFile = f.temp.path / "call-count.txt";
  f.envs.push_back(
    std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_GATE_FILE", gateFile.string())
  );
  f.envs
    .push_back(std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_GATE_FROM_CALL", "2"));
  f.envs.push_back(
    std::make_unique<
      ScopedEnvVar
    >("ENCRO_FAKE_FFMPEG_CALL_COUNT_FILE", countFile.string())
  );
  f.envs
    .push_back(std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_CALL_PLAN", "2:0:130"));

  std::optional<eh::Result<int>> outcome;
  auto runner = std::jthread{[&] { outcome = f.run(); }};
  auto const secondStarted = testutils::waitUntil(
    [&] {
      auto in = std::ifstream{countFile, std::ios::binary};
      auto started = std::size_t{0};
      if (in.is_open()) { in >> started; }
      return started >= 2;
    },
    std::chrono::seconds{10}
  );
  stopsignal::requestStop();
  {
    auto gate = std::ofstream{gateFile, std::ios::binary};
    REQUIRE(gate.is_open());
    gate << "go";
  }
  runner.join();
  REQUIRE(secondStarted);

  REQUIRE(outcome.has_value());
  REQUIRE(outcome->has_value());
  CHECK(outcome->value() == stopsignal::kCanceledExitCode);
  CHECK_FALSE(fs::exists(f.inputDir / "packed"));
  // The finished conversion survives for the next run; the interrupted one left
  // no output at a final path.
  auto cachedWebps = std::vector<std::string>{};
  for (auto const& entry: fs::directory_iterator{f.cacheDir()}) {
    if (entry.path().extension() == ".webp") {
      cachedWebps.push_back(entry.path().filename().string());
    }
  }
  CHECK(cachedWebps.size() == 1);
}

TEST_CASE(
  "conversion failure of one clip leaves the others packed",
  "[picture-process][video-webp]"
) {
  ConversionFixture f;
  writeSizedFile(f.inputDir / "photo.jpg", 32);
  writeSizedFile(f.inputDir / "bad.mp4", 64);
  writeSizedFile(f.inputDir / "good.mp4", 64);
  // Fail only the clip whose name marks it: the call plan races when both
  // conversions run in parallel, a match on the output path cannot. The match
  // path returns exit code 1 by itself, so no global failure switch is set.
  f.envs
    .push_back(std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_FAIL_MATCH", "__bad__"));

  auto const stderrPath = f.temp.path / "stderr.txt";
  auto const stdoutPath = f.temp.path / "stdout.txt";
  auto exitCode = 0;
  {
    auto const errCapture = testutils::StderrCapture{stderrPath};
    auto const outCapture = testutils::StdoutCapture{stdoutPath};
    auto const runRes = f.run();
    REQUIRE(runRes);
    exitCode = runRes.value();
  }

  CHECK(exitCode == 0);

  auto const entries = listZipRegularEntryNames(f.packedZip());
  // The failed clip contributes nothing; the picture and the other clip pack.
  REQUIRE(entries.size() == 2);
  CHECK(containsEntryEndingWith(entries, ".jpg"));
  CHECK(containsEntryEndingWith(entries, ".webp"));
  // The failure is reported with the clip that caused it.
  auto const stdoutText = testutils::readTextFile(stdoutPath);
  CHECK(stdoutText.find("bad.mp4") != std::string::npos);
}

TEST_CASE(
  "oversized clip is skipped with a warning and never converted",
  "[picture-process][video-webp]"
) {
  ConversionFixture f;
  writeSizedFile(f.inputDir / "photo.jpg", 32);
  writeSizedFile(f.inputDir / "huge.mp4", 32ULL * 1024ULL * 1024ULL);

  auto const stderrPath = f.temp.path / "stderr.txt";
  auto exitCode = 0;
  {
    auto const capture = testutils::StderrCapture{stderrPath};
    auto const runRes = f.run();
    REQUIRE(runRes);
    exitCode = runRes.value();
  }

  CHECK(exitCode == 0);
  CHECK(f.encodeCount() == 0);
  CHECK(
    testutils::readTextFile(stderrPath)
      .find("Skipping oversized video for WebP conversion")
    != std::string::npos
  );
}
