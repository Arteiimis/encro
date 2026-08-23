#include "core/display_text.h"

#include <catch2/catch_all.hpp>

namespace {

auto asString(std::u8string_view text) -> std::string {
  auto out = std::string{};
  out.reserve(text.size());
  for (auto const ch: text) { out.push_back(static_cast<char>(ch)); }
  return out;
}

bool hasWholeCodePoints(std::string const& text) {
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

TEST_CASE(
  "formatSignedPercent renders signed percentages with growth marker",
  "[display-text]"
) {
  CHECK(
    displaytext::formatSignedPercent(0.80)
    == "\xE2\x88\x92"
       "20%"
  );                                                                     // −20%
  CHECK(displaytext::formatSignedPercent(1.24) == "+24% \xE2\x86\x91");  // +24% ↑
  CHECK(displaytext::formatSignedPercent(1.0) == "+0%");
  CHECK(
    displaytext::formatSignedPercent(1.005) == "+0% \xE2\x86\x91"
  );  // rounds to +0%, still > 1
  CHECK(displaytext::formatSignedPercent(2.87) == "+187% \xE2\x86\x91");
  CHECK(
    displaytext::formatSignedPercent(0.45)
    == "\xE2\x88\x92"
       "55%"
  );
}

TEST_CASE("formatSizeBytes auto-scales MB and GB", "[display-text]") {
  CHECK(displaytext::formatSizeBytes(std::uintmax_t{0}) == "0.0 MB");
  CHECK(displaytext::formatSizeBytes(std::uintmax_t{1'469'824}) == "1.4 MB");
  CHECK(displaytext::formatSizeBytes(std::uintmax_t{1'500'000'000}) == "1.40 GB");
  CHECK(displaytext::formatSizeBytes(std::optional<std::uintmax_t>{}) == "\xE2\x80\x94");
}

TEST_CASE("truncateMiddle keeps the extension and stays within width", "[display-text]") {
  auto const name = std::string{"35e5a22dece198d78d9815c6056ef21e.mp4"};
  CHECK(displaytext::displayWidth(name) == 36);

  auto const truncated = displaytext::truncateMiddle(name, 30);

  CHECK(displaytext::displayWidth(truncated) <= 30);
  CHECK(truncated.ends_with(".mp4"));
  CHECK(truncated.find("\xE2\x80\xA6") != std::string::npos);
}

TEST_CASE(
  "truncateMiddle passes through short names and handles no extension",
  "[display-text]"
) {
  CHECK(displaytext::truncateMiddle("a.mp4", 30) == "a.mp4");
  CHECK(displaytext::truncateMiddle("noext", 5) == "noext");
  CHECK(displaytext::displayWidth(displaytext::truncateMiddle("abcdef", 3)) <= 3);
}

TEST_CASE(
  "layoutColumns reserves the numeric columns and rejects tiny widths",
  "[display-text]"
) {
  auto const wide = displaytext::layoutColumns(120);
  REQUIRE(wide.has_value());
  CHECK(wide.value().nameWidth == 120 - 32);

  auto const standard = displaytext::layoutColumns(80);
  REQUIRE(standard.has_value());
  CHECK(standard.value().nameWidth == 80 - 32);

  CHECK_FALSE(displaytext::layoutColumns(40).has_value());
  CHECK_FALSE(displaytext::layoutColumns(51).has_value());
  CHECK(displaytext::layoutColumns(52).has_value());
}
