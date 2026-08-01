#include "core/progress.h"

#include <catch2/catch_all.hpp>

#include <chrono>

using namespace std::chrono_literals;

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
