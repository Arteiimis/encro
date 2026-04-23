#include "test_utils.h"
#include "video/video_progress_parser.h"

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <string_view>

namespace fs = std::filesystem;

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
  "isLikelyFfmpegErrorLine ignores ffmpeg metadata comment payloads",
  "[video-process][ffmpeg]"
) {
  auto const metadataLine = std::string_view{
    R"(comment         : {"prompt": "lowres, bad anatomy, text, error, low quality"})"
  };

  CHECK_FALSE(isLikelyFfmpegErrorLine(metadataLine));
}

TEST_CASE(
  "isLikelyFfmpegErrorLine keeps real ffmpeg diagnostics",
  "[video-process][ffmpeg]"
) {
  CHECK(isLikelyFfmpegErrorLine("Option foo not found."));
  CHECK(isLikelyFfmpegErrorLine("[libwebp @ 000001] Error parsing option quality."));
}
