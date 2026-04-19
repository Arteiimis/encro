#include "core/app_context.h"

#include <boost/json.hpp>

#include <catch2/catch_all.hpp>

#include <memory>

namespace fs = std::filesystem;

TEST_CASE("video info cache store reads values from immer snapshot", "[app-context]") {
  auto runtime = appctx::RuntimeContext{};
  auto const path = fs::path{"sample.mp4"};

  runtime.videoInfoCache.set(
    path,
    boost::json::parse(R"({"streams":[{"codec_type":"video","nb_frames":"12"}]})")
  );

  auto const cached = runtime.videoInfoCache.find(path);

  REQUIRE(cached.has_value());
  CHECK(cached->is_object());
  CHECK(runtime.videoInfoCache.size() == 1);
}

TEST_CASE("encoding state store erases registered states", "[app-context]") {
  auto runtime = appctx::RuntimeContext{};
  auto state = std::make_shared<appctx::EncodingState>();
  state->inputPath = fs::path{"sample.mp4"};

  runtime.encodingStates.set(state);

  REQUIRE(runtime.encodingStates.size() == 1);
  CHECK(runtime.encodingStates.find(state->inputPath) == state);
  REQUIRE(runtime.encodingStates.values().size() == 1);

  runtime.encodingStates.erase(state->inputPath);

  CHECK(runtime.encodingStates.find(state->inputPath) == nullptr);
  CHECK(runtime.encodingStates.values().empty());
  CHECK(runtime.encodingStates.size() == 0);
}
