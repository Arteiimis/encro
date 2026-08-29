#include "core/display_text.h"
#include "core/progress.h"
#include "infra/terminal.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

using namespace std::chrono_literals;

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

TEST_CASE("progress bars emit no frames when stdout is not a terminal", "[progress]") {
  TempDir temp;
  auto const capturePath = temp.path / "out.txt";

  {
    auto capture = testutils::StdoutCapture{capturePath};
    auto ctx = progress::ProgressContext{};
    auto const barIndex = ctx.addBar("tick", progress::Tone::Default);
    ctx.setProgress(barIndex, 0.5f);
    terminal::write(terminal::Stream::Stdout, "status line", true);
  }

  auto const text = testutils::readTextFile(capturePath);
  CHECK(text.find("tick") == std::string::npos);
  CHECK(text.find("status line") != std::string::npos);
}

TEST_CASE("ProgressContext tick is safe on an empty context", "[progress]") {
  auto ctx = progress::ProgressContext{};
  ctx.tick();
  ctx.tick();
}

TEST_CASE(
  "ProgressContext tick emits no frames when stdout is not a terminal",
  "[progress]"
) {
  TempDir temp;
  auto const capturePath = temp.path / "out.txt";

  {
    auto capture = testutils::StdoutCapture{capturePath};
    auto ctx = progress::ProgressContext{};
    auto const barIndex = ctx.addBar("tick", progress::Tone::Default);
    ctx.setProgress(barIndex, 0.5f);
    ctx.tick();
    terminal::write(terminal::Stream::Stdout, "status line", true);
  }

  auto const text = testutils::readTextFile(capturePath);
  CHECK(text.find("tick") == std::string::npos);
  CHECK(text.find("status line") != std::string::npos);
}

TEST_CASE(
  "ProgressContext tick keeps subsequent progress updates working",
  "[progress]"
) {
  auto ctx = progress::ProgressContext{};
  auto const barIndex = ctx.addBar("tick", progress::Tone::Default);
  ctx.setProgress(barIndex, 10.0f);
  for (auto i = 0; i < 5; ++i) { ctx.tick(); }
  ctx.setProgress(barIndex, 100.0f);
  ctx.setPostfixText(barIndex, "done");
}

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

TEST_CASE(
  "EtaEstimator resumes correctly when progress starts above zero",
  "[progress]"
) {
  // Segment-resume: barEncodingStart writes an explicit 0, then the monitor's
  // first sample sits at the resume percent (baseFrameOffset, here 80%). The
  // baseline must be that resume point, not the zero -- otherwise the seed
  // treats 80% as done in the first 250 ms and the ETA opens near 0 s.
  progress::EtaEstimator est;
  auto const t = std::chrono::steady_clock::now();
  est.sample(t, 0.0f);              // barEncodingStart's explicit zero
  est.sample(t + 250ms, 80.0f);     // first monitor sample: the resume point

  auto const rate = 20.0 / 3600.0;  // remaining 20% takes ~1 h
  auto progress = 80.0;
  for (auto i = 1; i < 60; ++i) {
    progress += rate * 0.5;
    est.sample(
      t + 250ms + std::chrono::milliseconds{500} * i,
      static_cast<float>(progress)
    );
    CHECK_FALSE(est.etaSeconds(static_cast<float>(progress)).has_value());
  }
  progress += rate * 0.5;
  est.sample(
    t + 250ms + std::chrono::milliseconds{500} * 60,
    static_cast<float>(progress)
  );

  auto const eta = est.etaSeconds(static_cast<float>(progress));
  REQUIRE(eta.has_value());
  auto const trueRemainingSec = (100.0 - progress) / rate;
  INFO("eta: " << eta.value() << " true remaining: " << trueRemainingSec);
  CHECK(eta.value() > trueRemainingSec * 0.9);
  CHECK(eta.value() < trueRemainingSec * 1.2);
}

TEST_CASE("EtaEstimator seeds only from meaningful progress", "[progress]") {
  // ffmpeg's startup ramp crawls: extrapolating elapsed*100/p from it
  // produces hours-scale ETAs. Nothing may be seeded before progress is
  // meaningful (0.5 points gained since the baseline) or 30 s have elapsed
  // (percent-crawling batch-overall bars).
  progress::EtaEstimator est;
  auto const t = std::chrono::steady_clock::now();
  est.sample(t, 0.0f);
  for (auto i = 1; i <= 20; ++i) {  // 5 s crawl to 0.2%
    est.sample(t + std::chrono::milliseconds{250} * i, 0.01f * i);
  }
  CHECK_FALSE(est.etaSeconds(0.2f).has_value());

  est.sample(t + 31s, 0.3f);  // percent-crawling bars: elapsed fallback seeds
  CHECK(est.etaSeconds(0.3f).has_value());
}

TEST_CASE("EtaEstimator does not fabricate an eta from a burst jump", "[progress]") {
  progress::EtaEstimator est;
  auto t = std::chrono::steady_clock::now();
  est.sample(t, 0.0f);
  est.sample(t + 250ms, 60.0f);  // absurd 240%/s jump: anchors only, no fold
  CHECK_FALSE(est.etaSeconds(60.0f).has_value());
  for (auto i = 2; i <= 20; ++i) {
    est.sample(t + std::chrono::milliseconds{250} * i, 60.0f + static_cast<float>(i));
  }
  auto const eta = est.etaSeconds(80.0f);
  REQUIRE(eta.has_value());
  CHECK(*eta > 0.0f);
  CHECK(*eta < 5.0f);
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

TEST_CASE(
  "EtaEstimator stays near true remaining time under bursty speed noise",
  "[progress]"
) {
  // A 40-minute encode whose instantaneous speed wobbles +-30% in a
  // deterministic 4-second cycle (keyframe cadence, disk flushes, sibling
  // workers), sampled at ffmpeg's 0.5 s stats cadence. The displayed ETA
  // must track the true remaining time within a few minutes instead of
  // swinging back and forth by tens of minutes.
  progress::EtaEstimator est;
  auto const t = std::chrono::steady_clock::now();

  constexpr double trueTotalSec = 2400.0;
  constexpr double sampleSec = 0.5;
  constexpr double baseRate = 100.0 / trueTotalSec;  // percent per second
  constexpr double noiseCycle[] = {1.3, 1.3, 1.3, 1.3, 0.7, 0.7, 0.7, 0.7};

  est.sample(t, 0.0f);
  auto progress = 0.0;
  auto maxAbsErrorSec = 0.0;
  for (auto i = 1;; ++i) {
    progress += baseRate * noiseCycle[i % 8] * sampleSec;
    auto const now = t
      + std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::duration<double>(i * sampleSec)
      );
    est.sample(now, static_cast<float>(progress));

    auto const elapsedSec = static_cast<double>(i) * sampleSec;
    if (elapsedSec < 120.0) { continue; }  // warm-up: early wobble is expected
    if (progress >= 99.0) { break; }

    auto const trueRemainingSec = trueTotalSec - elapsedSec;
    auto const eta = est.etaSeconds(static_cast<float>(progress));
    REQUIRE(eta.has_value());
    maxAbsErrorSec = std::max(
      maxAbsErrorSec,
      std::abs(static_cast<double>(eta.value()) - trueRemainingSec)
    );
  }
  INFO("max ETA error (s): " << maxAbsErrorSec);
  CHECK(maxAbsErrorSec <= 180.0);
}

TEST_CASE("EtaEstimator reset clears stale rate for bar reuse", "[progress]") {
  progress::EtaEstimator est;
  auto t = std::chrono::steady_clock::now();
  est.sample(t, 0.0f);
  for (auto i = 1; i <= 20; ++i) {
    est.sample(t + std::chrono::milliseconds{250} * i, static_cast<float>(i) * 5.0f);
  }
  auto const etaBefore = est.etaSeconds(90.0f);
  REQUIRE(etaBefore.has_value());
  CHECK(*etaBefore < 2.0f);

  est.reset();
  CHECK_FALSE(est.etaSeconds(90.0f).has_value());

  // Fresh steady feed at 4%/s: eta must reflect only the new samples
  // (true remaining ~22 s at p=12), not the pre-reset rate.
  est.sample(t + 5s, 0.0f);
  for (auto i = 1; i <= 12; ++i) {
    est.sample(t + 5s + std::chrono::milliseconds{250} * i, static_cast<float>(i));
  }
  auto const eta = est.etaSeconds(12.0f);
  REQUIRE(eta.has_value());
  CHECK(*eta > 15.0f);
  CHECK(*eta < 30.0f);
}

TEST_CASE("EtaEstimator tracks a sustained speed slowdown", "[progress]") {
  // Steady 1%/s for 30 s, then the true rate halves (a sibling worker
  // started). The projection is a since-start average, so it re-locks with a
  // bias toward the fast early phase (see EtaEstimator's ponytail note);
  // within ~60 s (4 tau) the displayed ETA must still land within 40% of
  // the true remaining time.
  progress::EtaEstimator est;
  auto const t = std::chrono::steady_clock::now();
  est.sample(t, 0.0f);
  auto progress = 0.0;
  for (auto i = 1; i <= 60; ++i) {  // 30 s at 1%/s
    progress += 0.5;
    est.sample(t + std::chrono::milliseconds{500} * i, static_cast<float>(progress));
  }
  for (auto i = 61; i <= 180; ++i) {  // 60 s more at 0.5%/s
    progress += 0.25;
    est.sample(t + std::chrono::milliseconds{500} * i, static_cast<float>(progress));
  }
  auto const eta = est.etaSeconds(static_cast<float>(progress));
  REQUIRE(eta.has_value());
  auto const trueRemainingSec = (100.0 - progress) / 0.5;  // 80 s
  INFO("eta: " << eta.value() << " true remaining: " << trueRemainingSec);
  CHECK(eta.value() > trueRemainingSec * 0.6);
  CHECK(eta.value() < trueRemainingSec * 1.1);
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
