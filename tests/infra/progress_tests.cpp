#include "core/display_text.h"
#include "core/progress.h"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <string>

using namespace std::chrono_literals;

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

TEST_CASE("resolveColor maps progress tones to distinct roles", "[progress]") {
  using indicators::Color;
  using progress::Tone;

  CHECK(progress::resolveColor(Tone::Default) == Color::cyan);
  CHECK(progress::resolveColor(Tone::Overall) == Color::blue);
  CHECK(progress::resolveColor(Tone::Active) == Color::cyan);
  CHECK(progress::resolveColor(Tone::Idle) == Color::white);
  CHECK(progress::resolveColor(Tone::Packing) == Color::yellow);
  CHECK(progress::resolveColor(Tone::Finalizing) == Color::yellow);
  CHECK(progress::resolveColor(Tone::Success) == Color::green);
  CHECK(progress::resolveColor(Tone::Failure) == Color::red);
}

TEST_CASE("resolveColor falls back to white when colors are disabled", "[progress]") {
  CHECK(
    progress::resolveColor(progress::Tone::Overall, false) == indicators::Color::white
  );
  CHECK(
    progress::resolveColor(progress::Tone::Failure, false) == indicators::Color::white
  );
}

TEST_CASE("EtaEstimator has no eta before rate is established", "[progress]") {
  progress::EtaEstimator est;
  auto t = std::chrono::steady_clock::now();
  est.sample(t, 0.0f);
  CHECK_FALSE(est.etaSeconds(1.0f).has_value());
  est.sample(t + 100ms, 2.0f);
  CHECK_FALSE(est.etaSeconds(2.0f).has_value());
  CHECK(est.etaSeconds(0.0f).has_value() == false);
}

TEST_CASE("EtaEstimator converges to steady-state rate", "[progress]") {
  progress::EtaEstimator est;
  auto t = std::chrono::steady_clock::now();
  est.sample(t, 0.0f);
  for (auto i = 1; i <= 20; ++i) {
    est.sample(t + std::chrono::milliseconds{250} * i, static_cast<float>(i) * 2.5f);
  }
  auto const eta = est.etaSeconds(50.0f);
  REQUIRE(eta.has_value());
  CHECK(*eta > 4.0f);
  CHECK(*eta < 7.0f);
}

TEST_CASE("EtaEstimator keeps eta stable during short stalls", "[progress]") {
  progress::EtaEstimator est;
  auto t = std::chrono::steady_clock::now();
  est.sample(t, 0.0f);
  for (auto i = 1; i <= 20; ++i) {
    est.sample(t + std::chrono::milliseconds{250} * i, static_cast<float>(i) * 2.5f);
  }
  auto const before = est.etaSeconds(50.0f).value();
  for (auto i = 1; i <= 4; ++i) {
    est.sample(t + std::chrono::milliseconds{250} * (20 + i), 50.0f);
  }
  auto const after = est.etaSeconds(50.0f).value();
  CHECK(after < before * 1.3f);
}

TEST_CASE("EtaEstimator ignores artificial progress jumps", "[progress]") {
  progress::EtaEstimator est;
  auto t = std::chrono::steady_clock::now();
  est.sample(t, 0.0f);
  est.sample(t + 250ms, 60.0f);
  CHECK_FALSE(est.etaSeconds(60.0f).has_value());
  est.sample(t + 500ms, 61.0f);
  auto const eta = est.etaSeconds(61.0f);
  REQUIRE(eta.has_value());
  CHECK(*eta > 8.0f);
  CHECK(*eta < 12.0f);
}

TEST_CASE("EtaEstimator recovers rate after a progress dip", "[progress]") {
  progress::EtaEstimator est;
  auto t = std::chrono::steady_clock::now();
  est.sample(t, 0.0f);
  for (auto i = 1; i <= 20; ++i) {
    est.sample(t + std::chrono::milliseconds{250} * i, static_cast<float>(i) * 2.5f);
  }
  est.sample(t + std::chrono::milliseconds{250} * 22, 40.0f);
  auto const eta = est.etaSeconds(40.0f);
  REQUIRE(eta.has_value());
  CHECK(*eta > 4.0f);
  CHECK(*eta < 8.0f);
}

TEST_CASE("fitPostfixText returns fitting text verbatim", "[progress]") {
  auto const text = std::string{"Encoding: short.mp4 | 33%"};

  CHECK(progress::fitPostfixText(text, 60) == text);
}

TEST_CASE("fitPostfixText scrolls overflowing text without ellipsis", "[progress]") {
  auto const text =
    std::string{"Encoding: a_very_long_filename_that_overflows_the_budget.mp4 | 33%"};

  auto const fitted = progress::fitPostfixText(text, 20);

  CHECK(fitted.find("...") == std::string::npos);
  CHECK(displaytext::displayWidth(fitted) <= 20);
  CHECK_FALSE(fitted.empty());
}

TEST_CASE("scrollWindow returns text as-is when it fits", "[progress]") {
  CHECK(progress::scrollWindow("abc", 4, 0) == "abc");
}

TEST_CASE("scrollWindow clamps offset at the text end", "[progress]") {
  auto const text = std::string{"abcdefghij"};

  CHECK(progress::scrollWindow(text, 4, 0) == "abcd");
  CHECK(progress::scrollWindow(text, 4, 3) == "defg");
  CHECK(progress::scrollWindow(text, 4, 6) == "ghij");
  CHECK(progress::scrollWindow(text, 4, 99) == "ghij");
}

TEST_CASE("bounceOffset sweeps forward, pauses, sweeps back, pauses", "[progress]") {
  auto const travel = std::size_t{6};
  auto const sweepMs = std::uint64_t{6 * 125};

  CHECK(progress::bounceOffset(0, travel) == 0);
  CHECK(progress::bounceOffset(125, travel) == 1);
  CHECK(progress::bounceOffset(500, travel) == 4);
  CHECK(progress::bounceOffset(sweepMs, travel) == 6);
  CHECK(progress::bounceOffset(sweepMs + 500, travel) == 6);
  CHECK(progress::bounceOffset(sweepMs + 1000, travel) == 6);
  CHECK(progress::bounceOffset(sweepMs + 1000 + 125, travel) == 5);
  CHECK(progress::bounceOffset(sweepMs + 1000 + 500, travel) == 2);
  CHECK(progress::bounceOffset(2 * sweepMs + 1000, travel) == 0);
  CHECK(progress::bounceOffset(2 * sweepMs + 2000, travel) == 0);
  CHECK(progress::bounceOffset(2 * sweepMs + 2000 + 125, travel) == 1);
}

TEST_CASE("bounceOffset stays at zero for zero travel", "[progress]") {
  CHECK(progress::bounceOffset(0, 0) == 0);
  CHECK(progress::bounceOffset(999999, 0) == 0);
}

TEST_CASE("scrollWindow keeps CJK code points whole", "[progress]") {
  auto const text = asString(u8"中文文件名很长需要滚动显示完整内容.mp4");

  for (auto startCol = std::size_t{0}; startCol <= 80; ++startCol) {
    auto const window = progress::scrollWindow(text, 12, startCol);
    CHECK(displaytext::displayWidth(window) <= 12);
    CHECK(hasWholeCodePoints(window));
  }
}

TEST_CASE("fitPostfixWithEta keeps eta visible while postfix scrolls", "[progress]") {
  auto const eta = std::string{"[12m:34s]"};
  auto const postfix =
    std::string{"Encoding: a_very_long_filename_that_overflows_the_budget.mp4 | 33%"};

  auto const fitted = progress::fitPostfixWithEta(eta, postfix, 30);

  CHECK(fitted.starts_with(eta + " | "));
  CHECK(fitted.ends_with("| 33%"));
  CHECK(displaytext::displayWidth(fitted) <= 30);
  CHECK(fitted.find("...") == std::string::npos);
}

TEST_CASE("fitPostfixWithEta keeps tail fixed while label scrolls", "[progress]") {
  auto const eta = std::string{"[1m:02s]"};
  auto const postfix = std::string{
    "Encoding: this_is_a_very_long_filename_that_overflows.mp4 | segment 1/1"
  };

  auto const fitted = progress::fitPostfixWithEta(eta, postfix, 40);

  CHECK(fitted.starts_with("[1m:02s] | "));
  CHECK(fitted.ends_with("| segment 1/1"));
  CHECK(displaytext::displayWidth(fitted) <= 40);
  CHECK(fitted.find("...") == std::string::npos);
}

TEST_CASE(
  "fitPostfixWithEta shrinks oversized tail to keep scroll window",
  "[progress]"
) {
  auto const postfix =
    std::string{"Encoding: name.mp4 | frame=12345 fps=30 size=1234kB time=00:01:23.45 "
                "bitrate=1234.5kbits/s speed=3.2x"};

  auto const fitted = progress::fitPostfixWithEta(std::nullopt, postfix, 30);

  CHECK(displaytext::displayWidth(fitted) <= 30);
  CHECK(fitted.ends_with("..."));
}

TEST_CASE("fitPostfixWithEta shows full text when it fits", "[progress]") {
  auto const eta = std::string{"[1m:02s]"};
  auto const postfix = std::string{"Encoding: a.mp4 | 50%"};

  CHECK(progress::fitPostfixWithEta(eta, postfix, 40) == eta + " | " + postfix);
}

TEST_CASE("fitPostfixWithEta without eta matches plain fit", "[progress]") {
  auto const text = std::string{"Encoding: short.mp4 | 33%"};

  CHECK(progress::fitPostfixWithEta(std::nullopt, text, 60) == text);
}
