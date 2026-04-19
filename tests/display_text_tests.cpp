#include "core/display_text.h"

#include <catch2/catch_all.hpp>

namespace {

auto asString(std::u8string_view text) -> std::string {
  auto out = std::string{};
  out.reserve(text.size());
  for (auto const ch: text) { out.push_back(static_cast<char>(ch)); }
  return out;
}

}  // namespace

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
