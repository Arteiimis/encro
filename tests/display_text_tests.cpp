#include "core/display_text.h"

#include <catch2/catch_all.hpp>


namespace {

auto asString(std::u8string_view text) -> std::string {
  auto out = std::string{};
  out.reserve(text.size());
  for (auto const ch: text) { out.push_back(static_cast<char>(ch)); }
  return out;
}

auto hasWholeCodePoints(std::string const& text) -> bool {
  if (text.empty()) { return true; }
  auto const first = static_cast<unsigned char>(text.front());
  auto const last = static_cast<unsigned char>(text.back());
  return (first & 0xC0u) != 0x80u && (last & 0xC0u) != 0xC0u;
}

}  // namespace

TEST_CASE("takeWindowByDisplayWidth at offset zero equals prefix", "[display-text]") {
  auto const text = std::string{"abcdefghij"};

  auto const window = displaytext::takeWindowByDisplayWidth(text, 0, 4);

  CHECK(window == "abcd");
}

TEST_CASE("takeWindowByDisplayWidth slices at column offsets", "[display-text]") {
  auto const text = std::string{"abcdefghij"};

  CHECK(displaytext::takeWindowByDisplayWidth(text, 3, 4) == "defg");
  CHECK(displaytext::takeWindowByDisplayWidth(text, 8, 4) == "ij");
  CHECK(displaytext::takeWindowByDisplayWidth(text, 10, 4).empty());
}

TEST_CASE("takeWindowByDisplayWidth never exceeds maxWidth", "[display-text]") {
  auto const text = std::string{"abcdefghij"};

  for (auto startCol = std::size_t{0}; startCol <= 12; ++startCol) {
    auto const window = displaytext::takeWindowByDisplayWidth(text, startCol, 5);
    CHECK(displaytext::displayWidth(window) <= 5);
  }
}

TEST_CASE("takeWindowByDisplayWidth keeps CJK code points whole", "[display-text]") {
  auto const text = asString(u8"中文文件名很长需要滚动显示以完整展示内容.mp4");

  for (auto startCol = std::size_t{0}; startCol <= 60; ++startCol) {
    auto const window = displaytext::takeWindowByDisplayWidth(text, startCol, 10);
    CHECK(displaytext::displayWidth(window) <= 10);
    CHECK(hasWholeCodePoints(window));
  }
}

TEST_CASE("truncateWithEllipsis preserves valid long Chinese text", "[display-text]") {
  auto const text =
    asString(u8"这是一个非常非常长的中文文件名用于验证进度条截断不会切坏字符.mp4");

  auto const truncated = displaytext::truncateWithEllipsis(text, 18);

  CHECK(unicode::display_width(truncated) >= 0);
  CHECK(displaytext::displayWidth(truncated) <= 18);
  CHECK(truncated.ends_with("..."));
}

TEST_CASE("truncateWithEllipsis keeps ascii text within width budget", "[display-text]") {
  auto const text = std::string{"this_is_a_very_long_ascii_filename_for_progress.mp4"};

  auto const truncated = displaytext::truncateWithEllipsis(text, 16);

  CHECK(displaytext::displayWidth(truncated) <= 16);
  CHECK(truncated.ends_with("..."));
}
