#include "tagger/engine_factory.h"

#include "core/sha256.h"
#include "infra/env.h"
#include "tagger/mapping.h"
#include "tagger/tagger_types.h"
#if defined(_WIN32)
  #include "tagger/onnx_features.h"
  #include "tagger/onnx_tagger.h"
#endif

#include <boost/json.hpp>

#include <cstdlib>
#include <map>
#include <fstream>
#include <format>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace tagger {

namespace {

// Both env fakes read one fixture: a JSON object per content hash, each fake
// taking the field it serves. Renames and copies behave exactly as they would
// with the real engines, because the key is the file's content.
// {"<sha256-hex>": {"general": [["tag", conf]...], "character": [...],
//                   "rating": [...], "identity": [0.1, ...]}}
auto readFixtureObject(fs::path const& fixturePath) -> boost::json::object {
  auto file = std::ifstream{fixturePath, std::ios::binary};
  if (!file.is_open()) {
    throw std::runtime_error(
      std::format("cannot open fake tagger fixture: {}", fixturePath.string())
    );
  }
  auto const content = std::string{std::istreambuf_iterator<char>{file}, {}};
  auto ec = boost::system::error_code{};
  auto const parsed = boost::json::parse(content, ec);
  if (ec || !parsed.is_object()) {
    throw std::runtime_error("malformed fake tagger fixture");
  }
  return parsed.as_object();
}

// The fixture key for `path`; "" when the file cannot be read.
auto contentHashOf(fs::path const& path) -> std::string {
  auto file = std::ifstream{path, std::ios::binary};
  if (!file.is_open()) { return {}; }
  auto const bytes = std::string{std::istreambuf_iterator<char>{file}, {}};
  return core::sha256Hex(bytes);
}

auto parseTags(boost::json::value const& value) -> std::vector<TagScore> {
  auto tags = std::vector<TagScore>{};
  if (!value.is_array()) { return tags; }
  for (auto const& pair: value.as_array()) {
    if (!pair.is_array() || pair.as_array().size() != 2) { continue; }
    auto const& fields = pair.as_array();
    if (!fields[0].is_string() || !fields[1].is_number()) { continue; }
    tags.push_back(
      TagScore{
        .tag = std::string{fields[0].as_string().c_str()},
        .confidence = fields[1].to_number<double>(),
      }
    );
  }
  return tags;
}

auto parseOutput(boost::json::value const& value) -> TaggerOutput {
  auto output = TaggerOutput{};
  if (!value.is_object()) { return output; }
  auto const& object = value.as_object();
  if (auto const* g = object.if_contains("general")) { output.general = parseTags(*g); }
  if (auto const* c = object.if_contains("character")) {
    output.character = parseTags(*c);
  }
  if (auto const* r = object.if_contains("rating")) { output.rating = parseTags(*r); }
  return output;
}

// A fixture entry without the field means "no identity evidence", the same
// as an entry that has none — not a malformed fixture.
auto parseFeature(boost::json::value const& value) -> std::vector<float> {
  auto feature = std::vector<float>{};
  if (!value.is_object()) { return feature; }
  auto const* identity = value.as_object().if_contains("identity");
  if (identity == nullptr || !identity->is_array()) { return feature; }
  for (auto const& number: identity->as_array()) {
    if (!number.is_number()) { continue; }
    feature.push_back(static_cast<float>(number.to_number<double>()));
  }
  return feature;
}

// Env-driven fake: the fixture maps content hashes to tags, so renames and
// copies behave exactly as they would with the real engine.
class EnvFakeTagger final: public TaggerEngine {
public:
  explicit EnvFakeTagger(fs::path const& fixturePath) {
    for (auto const& [hash, value]: readFixtureObject(fixturePath)) {
      fixtures_[std::string{hash}] = parseOutput(value);
    }
  }

  auto classify(fs::path const& path) -> eh::Result<TaggerOutput> override {
    auto const hash = contentHashOf(path);
    if (hash.empty()) { return eh::makeError("cannot read {}", path.string()); }
    auto const it = fixtures_.find(hash);
    if (it == fixtures_.end()) {
      return eh::makeError("no fixture for {}", path.filename().string());
    }
    return it->second;
  }

private:
  std::map<std::string, TaggerOutput> fixtures_;
};

// The same fixture, read for the identity feature.
class EnvFakeFeatures final: public FeatureEngine {
public:
  explicit EnvFakeFeatures(fs::path const& fixturePath) {
    for (auto const& [hash, value]: readFixtureObject(fixturePath)) {
      fixtures_[std::string{hash}] = parseFeature(value);
    }
  }

  auto extract(fs::path const& path) -> eh::Result<std::vector<float>> override {
    auto const hash = contentHashOf(path);
    if (hash.empty()) { return eh::makeError("cannot read {}", path.string()); }
    auto const it = fixtures_.find(hash);
    if (it == fixtures_.end()) {
      return eh::makeError("no fixture for {}", path.filename().string());
    }
    auto feature = it->second;
    normalizeFeature(feature);
    return feature;
  }

private:
  std::map<std::string, std::vector<float>> fixtures_;
};

}  // namespace

#if !defined(_WIN32)
// The local onnxruntime-gpu package ships win-x64 binaries only, so non-
// Windows hosts build without the ONNX engines; organize still starts and
// degrades each image to uncategorized/ through the classify/extract error.
class UnsupportedPlatformTagger final: public TaggerEngine {
public:
  auto classify(fs::path const&) -> eh::Result<TaggerOutput> override {
    return eh::makeError(
      "onnx inference requires a windows build (no onnxruntime package for this platform)"
    );
  }
};

class UnsupportedPlatformFeatures final: public FeatureEngine {
public:
  auto extract(fs::path const&) -> eh::Result<std::vector<float>> override {
    return eh::makeError(
      "onnx inference requires a windows build (no onnxruntime package for this platform)"
    );
  }
};
#endif

bool fakeTaggerRequested() {
  return processenv::readNonEmptyEnvVar("ENCRO_FAKE_TAGGER").has_value();
}

auto makeTaggerEngine(fs::path const& modelDir, std::optional<fs::path> const& ffmpegPath)
  -> std::unique_ptr<TaggerEngine> {
  if (
    auto const fixture = processenv::readNonEmptyEnvVar("ENCRO_FAKE_TAGGER");
    fixture.has_value()
  ) {
    return std::make_unique<EnvFakeTagger>(fs::path{*fixture});
  }
#if defined(_WIN32)
  return std::make_unique<
    OnnxTagger
  >(modelDir / "model.onnx", modelDir / "selected_tags.csv", ffmpegPath);
#else
  (void)modelDir;
  (void)ffmpegPath;
  return std::make_unique<UnsupportedPlatformTagger>();
#endif
}

auto makeFeatureEngine(
  fs::path const& modelDir,
  std::optional<fs::path> const& ffmpegPath
) -> std::unique_ptr<FeatureEngine> {
  if (
    auto const fixture = processenv::readNonEmptyEnvVar("ENCRO_FAKE_TAGGER");
    fixture.has_value()
  ) {
    return std::make_unique<EnvFakeFeatures>(fs::path{*fixture});
  }
#if defined(_WIN32)
  return std::make_unique<OnnxFeatureEngine>(modelDir / "model_feat.onnx", ffmpegPath);
#else
  (void)modelDir;
  (void)ffmpegPath;
  return std::make_unique<UnsupportedPlatformFeatures>();
#endif
}

}  // namespace tagger
