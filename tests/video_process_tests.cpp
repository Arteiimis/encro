#include "globals.h"
#include "test_utils.h"
#include "video_process.h"

#include <catch2/catch_all.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

void createSizedSparseFile(fs::path const& filePath, std::uintmax_t sizeInBytes) {
  auto out = std::ofstream{filePath, std::ios::binary};
  REQUIRE(out.is_open());

  if (sizeInBytes == 0) {
    out.flush();
    return;
  }

  out.seekp(static_cast<std::streamoff>(sizeInBytes - 1));
  out.put('\0');
  out.flush();
}

}  // namespace

TEST_CASE("readLastNLines returns tail of file", "[video-process][readLastNLines]") {
  TempDir temp;
  auto const filePath = temp.path / "progress.log";

  {
    std::ofstream out{filePath};
    for (int i = 1; i <= 5; ++i) { out << "line" << i << "\n"; }
  }

  auto const lastLines = readLastNLines(filePath, 3);

  REQUIRE(lastLines.size() == 3);
  CHECK(lastLines[0] == "line3");
  CHECK(lastLines[1] == "line4");
  CHECK(lastLines[2] == "line5");
}

TEST_CASE("readLastNLines handles short files", "[video-process][readLastNLines]") {
  TempDir temp;
  auto const filePath = temp.path / "short.log";

  {
    std::ofstream out{filePath};
    out << "only-one-line\n";
  }

  auto const lastLines = readLastNLines(filePath, 5);

  REQUIRE(lastLines.size() == 1);
  CHECK(lastLines[0] == "only-one-line");
}

TEST_CASE(
  "readLastNLines returns empty for missing file",
  "[video-process][readLastNLines]"
) {
  TempDir temp;
  auto const missingPath = temp.path / "missing.log";

  auto const lastLines = readLastNLines(missingPath, 2);
  CHECK(lastLines.empty());
}

TEST_CASE(
  "parseProgressFile extracts latest frame and status",
  "[video-process][parseProgressFile]"
) {
  TempDir temp;
  auto const filePath = temp.path / "progress.log";

  {
    std::ofstream out{filePath};
    out << "frame=10\n";
    out << "progress=continue\n";
    out << "frame=25\n";
    out << "progress=end\n";
  }

  auto const [frameCount, status] = parseProgressFile(filePath);

  CHECK(frameCount == 25);
  CHECK(status == "end");
}

TEST_CASE(
  "resolveVideoOutputPath returns webp subfolder when output path is not provided",
  "[video-process][resolve-output-path]"
) {
  TempDir temp;

  GLBs.OUTPUT_PATH.reset();
  GLBs.OUTPUT_FORMAT = "webp";

  auto const outputPath = resolveVideoOutputPath(temp.path);

  REQUIRE(outputPath.has_value());
  CHECK(outputPath.value() == temp.path / "encoded_webp");
}

TEST_CASE(
  "resolveVideoOutputPath uses file parent when input is file and format is webp",
  "[video-process][resolve-output-path]"
) {
  TempDir temp;
  auto const filePath = temp.path / "sample.mp4";

  {
    std::ofstream out{filePath};
    out << "x";
  }

  GLBs.OUTPUT_PATH.reset();
  GLBs.OUTPUT_FORMAT = "webp";

  auto const outputPath = resolveVideoOutputPath(filePath);

  REQUIRE(outputPath.has_value());
  CHECK(outputPath.value() == temp.path / "encoded_webp");
}

TEST_CASE(
  "resolveVideoOutputPath returns user output path when provided",
  "[video-process][resolve-output-path]"
) {
  TempDir temp;
  auto const customOutput = temp.path / "custom_output";
  fs::create_directory(customOutput);

  GLBs.OUTPUT_PATH = customOutput;
  GLBs.OUTPUT_FORMAT = "webp";

  auto const outputPath = resolveVideoOutputPath(temp.path);

  REQUIRE(outputPath.has_value());
  CHECK(outputPath.value() == customOutput);
}

TEST_CASE(
  "resolveVideoOutputPath returns no value for non-webp without custom output",
  "[video-process][resolve-output-path]"
) {
  TempDir temp;

  GLBs.OUTPUT_PATH.reset();
  GLBs.OUTPUT_FORMAT = "mp4";

  auto const outputPath = resolveVideoOutputPath(temp.path);

  CHECK_FALSE(outputPath.has_value());
}

TEST_CASE("splitIntoBatches splits exactly by 10", "[video-process][batch]") {
  auto const batches = splitIntoBatches(10, 10);

  REQUIRE(batches.size() == 1);
  CHECK(batches[0] == std::pair<std::size_t, std::size_t>{0, 10});
}

TEST_CASE("splitIntoBatches creates tail batch", "[video-process][batch]") {
  auto const batches = splitIntoBatches(11, 10);

  REQUIRE(batches.size() == 2);
  CHECK(batches[0] == std::pair<std::size_t, std::size_t>{0, 10});
  CHECK(batches[1] == std::pair<std::size_t, std::size_t>{10, 11});
}

TEST_CASE(
  "splitIntoBatches handles multiple full and partial batches",
  "[video-process][batch]"
) {
  auto const batches = splitIntoBatches(25, 10);

  REQUIRE(batches.size() == 3);
  CHECK(batches[0] == std::pair<std::size_t, std::size_t>{0, 10});
  CHECK(batches[1] == std::pair<std::size_t, std::size_t>{10, 20});
  CHECK(batches[2] == std::pair<std::size_t, std::size_t>{20, 25});
}

TEST_CASE("splitIntoBatches handles empty input", "[video-process][batch]") {
  auto const batches = splitIntoBatches(0, 10);
  CHECK(batches.empty());
}

TEST_CASE(
  "resolveVideoPackOutputPath uses encoded_webp subdir when webp has no custom "
  "output",
  "[video-process][pack]"
) {
  TempDir temp;

  GLBs.OUTPUT_PATH.reset();
  GLBs.OUTPUT_FORMAT = "webp";

  auto const packPath = resolveVideoPackOutputPath(temp.path);
  CHECK(packPath == temp.path / "encoded_webp" / "packed");
}

TEST_CASE(
  "resolveVideoPackOutputPath uses file parent for webp input file",
  "[video-process][pack]"
) {
  TempDir temp;
  auto const filePath = temp.path / "sample.mp4";

  {
    std::ofstream out{filePath};
    out << "x";
  }

  GLBs.OUTPUT_PATH.reset();
  GLBs.OUTPUT_FORMAT = "webp";

  auto const packPath = resolveVideoPackOutputPath(filePath);
  CHECK(packPath == temp.path / "encoded_webp" / "packed");
}

TEST_CASE(
  "resolveVideoPackOutputPath uses file parent for non-webp input file",
  "[video-process][pack]"
) {
  TempDir temp;
  auto const filePath = temp.path / "sample.mp4";

  {
    std::ofstream out{filePath};
    out << "x";
  }

  GLBs.OUTPUT_PATH.reset();
  GLBs.OUTPUT_FORMAT = "mp4";

  auto const packPath = resolveVideoPackOutputPath(filePath);
  CHECK(packPath == temp.path / "packed");
}

TEST_CASE(
  "resolveVideoPackOutputPath uses custom output path when provided",
  "[video-process][pack]"
) {
  TempDir temp;
  auto const customOutput = temp.path / "out";
  fs::create_directory(customOutput);

  GLBs.OUTPUT_PATH = customOutput;
  GLBs.OUTPUT_FORMAT = "mp4";

  auto const packPath = resolveVideoPackOutputPath(temp.path);
  CHECK(packPath == customOutput / "packed");
}

TEST_CASE(
  "groupEncodedVideosForPack splits groups at 500MB",
  "[video-process][pack]"
) {
  TempDir temp;
  auto const v1 = temp.path / "v1.mp4";
  auto const v2 = temp.path / "v2.mp4";
  auto const v3 = temp.path / "v3.mp4";

  createSizedSparseFile(v1, 300ULL * 1024ULL * 1024ULL);
  createSizedSparseFile(v2, 300ULL * 1024ULL * 1024ULL);
  createSizedSparseFile(v3, 100ULL * 1024ULL * 1024ULL);

  auto const grouped = groupEncodedVideosForPack({v1, v2, v3});

  REQUIRE(grouped.size() == 2);
  CHECK(grouped[0] == std::vector{v1});
  CHECK(grouped[1] == std::vector{v2, v3});
}
