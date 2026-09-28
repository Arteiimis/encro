// Real-model smoke (design D9 acceptance): runs the actual ONNX tagger when
// model files are present; SKIPs otherwise so offline suites stay green.
#include "infra/env.h"
#include "tagger/onnx_features.h"
#include "tagger/onnx_tagger.h"
#include "utils/utils.h"

#include "test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE(
  "OnnxTagger classifies a real image with the real model",
  "[tagger][real-model]"
) {
  // The model directory comes from the environment so the fixture runs on any
  // machine (see AGENTS.md - Testing).
  auto const modelDirVar = processenv::readEnvVar("ENCRO_TEST_MODEL_DIR");
  if (!modelDirVar.has_value()) {
    SKIP("ENCRO_TEST_MODEL_DIR is unset; the real-model smoke needs a model dir.");
  }
  auto const modelDir = fs::path{*modelDirVar};
  if (
    !fs::exists(modelDir / "model.onnx") || !fs::exists(modelDir / "selected_tags.csv")
  ) {
    SKIP("Model files not present; run --download-models for the real-model smoke.");
  }
  auto const ffmpeg = findFFmpeg(std::nullopt);
  if (!ffmpeg.has_value()) { SKIP("System FFmpeg not available on PATH."); }

  auto temp = TempDir{};
  auto const input = temp.path / "probe.png";
  auto const [exitCode, output, _, stderrText] = exec2(
    std::format(
      R"("{}" -hide_banner -loglevel error -y -f lavfi -i color=c=red:s=448x448 -frames:v 1 "{}")",
      quoteToolPath(*ffmpeg),
      input.string()
    )
  );
  REQUIRE(exitCode == 0);

  try {
    auto engine =
      tagger::OnnxTagger(modelDir / "model.onnx", modelDir / "selected_tags.csv", ffmpeg);
    CHECK((engine.providerName() == "cuda" || engine.providerName() == "cpu"));

    auto const result = engine.classify(input);
    REQUIRE(result.has_value());
    // Real inference returns *some* general tags even for synthetic input,
    // and every reported confidence is in (0, 1].
    auto const& general = result->general;
    CHECK(!general.empty());
    for (auto const& tag: general) {
      CHECK(tag.confidence > 0.0);
      CHECK(tag.confidence <= 1.0);
    }
  } catch (std::exception const& error) {
    // Models present but unusable (corrupt download, driver mismatch) is an
    // acceptance failure, not a skip.
    FAIL(std::format("real-model smoke failed: {}", error.what()));
  }
}

TEST_CASE(
  "OnnxFeatureEngine embeds a real image with the real model",
  "[tagger][real-model]"
) {
  auto const modelDirVar = processenv::readEnvVar("ENCRO_TEST_MODEL_DIR");
  if (!modelDirVar.has_value()) {
    SKIP("ENCRO_TEST_MODEL_DIR is unset; the real-model smoke needs a model dir.");
  }
  auto const modelDir = fs::path{*modelDirVar};
  if (!fs::exists(modelDir / "model_feat.onnx")) {
    SKIP("Identity model not present; run --download-models for this smoke.");
  }
  auto const ffmpeg = findFFmpeg(std::nullopt);
  if (!ffmpeg.has_value()) { SKIP("System FFmpeg not available on PATH."); }

  auto temp = TempDir{};
  auto const red = temp.path / "red.png";
  auto const blue = temp.path / "blue.png";
  for (auto const& [path, colour]: {std::pair{red, "red"}, std::pair{blue, "blue"}}) {
    auto const [exitCode, output, _, stderrText] = exec2(
      std::format(
        R"("{}" -hide_banner -loglevel error -y -f lavfi -i color=c={}:s=384x384 -frames:v 1 "{}")",
        quoteToolPath(*ffmpeg),
        colour,
        path.string()
      )
    );
    REQUIRE(exitCode == 0);
  }

  try {
    auto engine = tagger::OnnxFeatureEngine(modelDir / "model_feat.onnx", ffmpeg);
    CHECK((engine.providerName() == "cuda" || engine.providerName() == "cpu"));

    auto const first = engine.extract(red);
    REQUIRE(first.has_value());
    // The contract the clustering reads: one unit-length vector per image.
    CHECK(first->size() == 768);
    auto norm = 0.0;
    for (auto const value: *first) { norm += static_cast<double>(value) * value; }
    CHECK(std::sqrt(norm) == Catch::Approx(1.0));

    // The same image embeds to nearly the same vector -- inference on a GPU
    // is not bit-reproducible (intra-op reduction order varies run to run), so
    // the contract the clustering needs is the cosine, not the bits. Both
    // vectors are unit length by contract, so their dot product is it.
    auto const again = engine.extract(red);
    REQUIRE(again.has_value());
    auto dot = 0.0;
    for (auto index = std::size_t{0}; index < first->size(); ++index) {
      dot += static_cast<double>((*again)[index]) * (*first)[index];
    }
    CHECK(dot > 0.999);
    // A different image lands elsewhere, which is all similarity needs.
    auto const other = engine.extract(blue);
    REQUIRE(other.has_value());
    CHECK(*other != *first);
  } catch (std::exception const& error) {
    FAIL(std::format("real-model smoke failed: {}", error.what()));
  }
}
