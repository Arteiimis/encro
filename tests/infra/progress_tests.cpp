#include "core/progress.h"

#include <catch2/catch_all.hpp>

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
