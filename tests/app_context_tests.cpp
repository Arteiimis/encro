#include "core/app_context.h"

#include <catch2/catch_all.hpp>

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
