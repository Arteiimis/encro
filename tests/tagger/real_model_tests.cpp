// Real-model smoke (design D9 acceptance): runs the actual ONNX tagger when
// model files are present; SKIPs otherwise so offline suites stay green.
#include "tagger/onnx_tagger.h"
#include "utils/utils.h"

#include "test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE(
  "OnnxTagger classifies a real image with the real model",
  "[tagger][real-model]"
) {
  auto const modelDir = fs::path{"C:/Users/LEGION/.encro/models"};
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
