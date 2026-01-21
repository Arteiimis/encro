#include <print>
#include <filesystem>

#include <catch2/catch_all.hpp>

#include "video_process.h"

TEST_CASE("Test readLastNLines function", "[readLastNLines]") {
  // Create a temporary file with known content
  auto tempFilePath = std::filesystem::path{"D:\\迅雷下载\\19\\progress.txt"};

  // Call the function to read the last 5 lines
  auto last5Lines = readLastNLines(tempFilePath, 12);

  // Verify the output
  REQUIRE(last5Lines.size() == 5);
  std::println("Last 5 lines:{}", last5Lines);
}

TEST_CASE("Test getFrameCountFromProgress function", "[getFrameCountFromProgress]") {
  // Create a temporary progress file with known content
  auto tempProgressFilePath = std::filesystem::path{"D:\\迅雷下载\\19\\progress.txt"};

  // Call the function to parse the progress file
  auto frameCount = getFrameCountFromProgress(tempProgressFilePath);

  // Verify the output
  REQUIRE(frameCount >= 0);
  std::println("Parsed frame count: {}", frameCount);
}

TEST_CASE("Test path exists", "[filesystem]") {
  auto testPath = std::filesystem::path{"D:\\BaiduNetdiskDownload\\nsfw"};
  REQUIRE(std::filesystem::exists(testPath));
}

int main(int argc, char* argv[]) {
  return Catch::Session().run(argc, argv);
}
