#include "core/app_context.h"
#include "core/job_state.h"
#include "core/work_dirs.h"

#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("work_dirs: scratchDir lives under the temp encro dir", "[work-dirs]") {
  auto const dir = workdirs::scratchDir();
  REQUIRE(dir.filename() == "scratch");
  // Compare against the joined path instead of temp_directory_path() directly:
  // the latter carries a trailing separator on Windows that component-wise
  // equality rejects.
  REQUIRE(dir.parent_path() == fs::temp_directory_path() / "encro");
}

TEST_CASE("work_dirs: ensureScratchDir creates the scratch root", "[work-dirs]") {
  auto const scratch = workdirs::scratchDir();
  fs::remove_all(scratch);
  workdirs::ensureScratchDir();
  REQUIRE(fs::is_directory(scratch));
}

TEST_CASE(
  "work_dirs: subpath helpers are shaped under the hidden .encro dir",
  "[work-dirs]"
) {
  auto const root = fs::path{"C:/work"} / "output";
  REQUIRE(workdirs::encroDir(root) == root / ".encro");
  REQUIRE(
    workdirs::segmentsDir(root, "abc123") == root / ".encro" / "segments" / "abc123"
  );
  REQUIRE(workdirs::compressCacheDir(root, 90) == root / ".encro" / "compress_q90");
  REQUIRE(workdirs::jobStateFile(root) == root / ".encro" / "job-state.json");
}

TEST_CASE("work_dirs: ensureEncroDir creates the dir and is idempotent", "[work-dirs]") {
  auto const tempRoot = TempDir{};
  auto const root = tempRoot.path / "out";

  auto const first = workdirs::ensureEncroDir(root);
  REQUIRE(first.has_value());
  REQUIRE(fs::is_directory(*first));
  auto const second = workdirs::ensureEncroDir(root);
  REQUIRE(second.has_value());
  REQUIRE(*first == *second);
}

TEST_CASE(
  "work_dirs: setHiddenAttribute marks a directory hidden on Windows",
  "[work-dirs]"
) {
  auto const tempRoot = TempDir{};
  auto const dir = tempRoot.path / "hidden_target";
  REQUIRE(workdirs::ensureEncroDir(dir.parent_path()).has_value());
  fs::create_directories(dir);
  workdirs::setHiddenAttribute(dir);
  // No assert on POSIX; the shim is a no-op there.
#if defined(_WIN32)
  auto const attrs = ::GetFileAttributesW(dir.wstring().c_str());
  REQUIRE(attrs != INVALID_FILE_ATTRIBUTES);
  REQUIRE((attrs & FILE_ATTRIBUTE_HIDDEN) != 0);
#endif
}

TEST_CASE(
  "work_dirs: setHiddenOnEncroDir is a no-op without an .encro ancestor",
  "[work-dirs]"
) {
  // Regression: the ancestor walk must terminate at the filesystem root
  // (parent_path() of a root is itself) instead of looping forever, and must
  // never hide a directory that is not the work root's .encro dir.
  auto const tempRoot = TempDir{};
  auto const target = tempRoot.path / "a" / "b" / "c";
  fs::create_directories(target);
  workdirs::setHiddenOnEncroDir(target / "file.txt");
#if defined(_WIN32)
  auto const attrs = ::GetFileAttributesW(tempRoot.path.wstring().c_str());
  REQUIRE(attrs != INVALID_FILE_ATTRIBUTES);
  REQUIRE((attrs & FILE_ATTRIBUTE_HIDDEN) == 0);
#endif
}

namespace {

auto configWith(
  std::string processType,
  std::string outputFormat,
  fs::path inputPath = {},
  std::vector<fs::path> inputPaths = {}
) -> appctx::AppConfig {
  auto config = appctx::AppConfig{};
  config.processType = std::move(processType);
  config.outputFormat = std::move(outputFormat);
  config.inputPath = std::move(inputPath);
  config.inputPaths = std::move(inputPaths);
  return config;
}

}  // namespace

TEST_CASE(
  "work_dirs: resolveWorkRoot honors an explicit output path first",
  "[work-dirs]"
) {
  auto config = configWith("video", "mp4", fs::path{"C:/in/video.mp4"});
  config.outputPath = fs::path{"D:/out"};
  REQUIRE(workdirs::resolveWorkRoot(config).value() == fs::path{"D:/out"});
}

TEST_CASE(
  "work_dirs: resolveWorkRoot webp anchors at input root/encoded_webp",
  "[work-dirs]"
) {
  auto const tempRoot = TempDir{};
  auto const album = tempRoot.path / "album";
  fs::create_directories(album);
  auto dirConfig = configWith("video", "webp", album);
  REQUIRE(workdirs::resolveWorkRoot(dirConfig).value() == album / "encoded_webp");

  auto const video = tempRoot.path / "video.mp4";
  std::ofstream{video}.close();
  auto fileConfig = configWith("video", "webp", video);
  REQUIRE(
    workdirs::resolveWorkRoot(fileConfig).value() == tempRoot.path / "encoded_webp"
  );
}

TEST_CASE(
  "work_dirs: resolveWorkRoot picture with webp format stays at the input",
  "[work-dirs]"
) {
  auto const tempRoot = TempDir{};
  auto const photos = tempRoot.path / "photos";
  fs::create_directories(photos);
  auto config = configWith("picture", "webp", photos);
  REQUIRE(workdirs::resolveWorkRoot(config).value() == photos);
}

TEST_CASE(
  "work_dirs: resolveWorkRoot single input resolves to dir or parent",
  "[work-dirs]"
) {
  auto const tempRoot = TempDir{};
  auto dirConfig = configWith("video", "mp4", tempRoot.path);
  REQUIRE(workdirs::resolveWorkRoot(dirConfig).value() == tempRoot.path);

  auto const video = tempRoot.path / "video.mp4";
  std::ofstream{video}.close();
  auto fileConfig = configWith("video", "mp4", video);
  REQUIRE(workdirs::resolveWorkRoot(fileConfig).value() == tempRoot.path);
}

TEST_CASE(
  "work_dirs: resolveWorkRoot multiple inputs use the lowest common ancestor",
  "[work-dirs]"
) {
  auto const tempRoot = TempDir{};
  auto const in = tempRoot.path / "in";
  fs::create_directories(in / "sub1");
  fs::create_directories(in / "sub2");
  std::ofstream{(in / "a.mp4")}.close();
  std::ofstream{(in / "b.mp4")}.close();
  std::ofstream{(in / "sub1" / "a.mp4")}.close();
  std::ofstream{(in / "sub2" / "b.mp4")}.close();

  auto sameParent = configWith("video", "mp4", {}, {in / "a.mp4", in / "b.mp4"});
  REQUIRE(workdirs::resolveWorkRoot(sameParent).value() == in);

  auto nested =
    configWith("video", "mp4", {}, {in / "sub1" / "a.mp4", in / "sub2" / "b.mp4"});
  REQUIRE(workdirs::resolveWorkRoot(nested).value() == in);
}

TEST_CASE(
  "work_dirs: resolveWorkRoot webp with multiple inputs uses shared parent",
  "[work-dirs]"
) {
  auto const tempRoot = TempDir{};
  auto const in = tempRoot.path / "in";
  fs::create_directories(in);
  std::ofstream{(in / "a.mp4")}.close();
  std::ofstream{(in / "b.mp4")}.close();
  auto config = configWith("video", "webp", {}, {in / "a.mp4", in / "b.mp4"});
  REQUIRE(workdirs::resolveWorkRoot(config).value() == in / "encoded_webp");
}

TEST_CASE(
  "work_dirs: resolveWorkRoot fails without inputs or a common ancestor",
  "[work-dirs]"
) {
  auto empty = configWith("video", "mp4");
  REQUIRE(!workdirs::resolveWorkRoot(empty).has_value());

  auto crossDrive =
    configWith("video", "mp4", {}, {fs::path{"C:/a.mp4"}, fs::path{"D:/b.mp4"}});
  REQUIRE(!workdirs::resolveWorkRoot(crossDrive).has_value());

  auto crossRoot =
    configWith("video", "mp4", {}, {fs::path{"/a/x.mp4"}, fs::path{"/b/y.mp4"}});
  REQUIRE(!workdirs::resolveWorkRoot(crossRoot).has_value());

  auto rootOnly =
    configWith("video", "mp4", {}, {fs::path{"/x.mp4"}, fs::path{"/y.mp4"}});
  REQUIRE(!workdirs::resolveWorkRoot(rootOnly).has_value());
}

TEST_CASE("work_dirs: buildDefaultStateFilePath follows the work root", "[work-dirs]") {
  auto const tempRoot = TempDir{};
  auto const inputDir = tempRoot.path / "in";
  fs::create_directories(inputDir);
  auto config = configWith("picture", "webp", inputDir);
  auto const stateFileRes = jobstate::buildDefaultStateFilePath(config);
  REQUIRE(stateFileRes.has_value());
  CHECK(*stateFileRes == inputDir / ".encro" / "job-state.json");

  config.inputPath = fs::path{};
  config.outputPath = tempRoot.path / "out";
  auto const stateFileRes2 = jobstate::buildDefaultStateFilePath(config);
  REQUIRE(stateFileRes2.has_value());
  CHECK(*stateFileRes2 == tempRoot.path / "out" / ".encro" / "job-state.json");

  auto const stateFileOverride = tempRoot.path / "explicit.json";
  config.stateFilePath = stateFileOverride;
  auto const stateFileRes3 = jobstate::buildDefaultStateFilePath(config);
  REQUIRE(stateFileRes3.has_value());
  CHECK(*stateFileRes3 == stateFileOverride);
}
TEST_CASE(
  "work_dirs: sweepScratchDir removes stale entries but keeps fresh ones",
  "[work-dirs]"
) {
  auto const scratch = workdirs::scratchDir();
  fs::remove_all(scratch);
  fs::create_directories(scratch);
  auto const stale = scratch / "stale.txt";
  auto const fresh = scratch / "fresh.txt";
  std::ofstream{stale}.close();
  std::ofstream{fresh}.close();
  auto const now = fs::file_time_type::clock::now();
  fs::last_write_time(stale, now - std::chrono::hours(30));
  fs::last_write_time(fresh, now - std::chrono::hours(1));

  // Resume data under `<work-root>\.encro\segments\` is never swept.
  auto const temp = TempDir{};
  auto const workRoot = temp.path / "root";
  auto const segment = workdirs::segmentsDir(workRoot, "abc123");
  fs::create_directories(segment);
  auto const segmentFile = segment / "seg_1.ts";
  std::ofstream{segmentFile}.close();
  fs::last_write_time(segmentFile, now - std::chrono::hours(30));

  workdirs::sweepScratchDir();
  REQUIRE_FALSE(fs::exists(stale));
  REQUIRE(fs::exists(fresh));
  REQUIRE(fs::exists(segmentFile));
}
