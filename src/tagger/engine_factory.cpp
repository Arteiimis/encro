#include "tagger/engine_factory.h"

#include "core/sha256.h"
#include "tagger/onnx_tagger.h"
#include "tagger/tagger_types.h"

#include <boost/json.hpp>

#include <cstdlib>
#include <map>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <utility>

namespace fs = std::filesystem;

namespace tagger {

namespace {

// Env-driven fake: the fixture maps content hashes to tags, so renames and
// copies behave exactly as they would with the real engine.
// {"<sha256-hex>": {"general": [["tag", conf]...], "character": [...],
//                   "rating": [...]}}
class EnvFakeTagger final: public TaggerEngine {
public:
  explicit EnvFakeTagger(fs::path const& fixturePath) {
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
    for (auto const& [hash, value]: parsed.as_object()) {
      fixtures_[std::string{hash}] = parseOutput(value);
    }
  }

  auto classify(fs::path const& path) -> eh::Result<TaggerOutput> override {
    auto file = std::ifstream{path, std::ios::binary};
    if (!file.is_open()) { return eh::makeError("cannot read {}", path.string()); }
    auto const bytes = std::string{std::istreambuf_iterator<char>{file}, {}};
    auto const it = fixtures_.find(core::sha256Hex(bytes));
    if (it == fixtures_.end()) {
      return eh::makeError("no fixture for {}", path.filename().string());
    }
    return it->second;
  }

private:
  static auto parseTags(boost::json::value const& value) -> std::vector<TagScore> {
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

  static auto parseOutput(boost::json::value const& value) -> TaggerOutput {
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

  std::map<std::string, TaggerOutput> fixtures_;
};

}  // namespace

auto fakeTaggerRequested() -> bool {
  auto* env = std::getenv("ENCRO_FAKE_TAGGER");
  return env != nullptr && *env != '\0';
}

auto makeTaggerEngine(fs::path const& modelDir, std::optional<fs::path> const& ffmpegPath)
  -> std::unique_ptr<TaggerEngine> {
  if (fakeTaggerRequested()) {
    auto const* fixture = std::getenv("ENCRO_FAKE_TAGGER");
    return std::make_unique<EnvFakeTagger>(fs::path{fixture});
  }
  return std::make_unique<
    OnnxTagger
  >(modelDir / "model.onnx", modelDir / "selected_tags.csv", ffmpegPath);
}

}  // namespace tagger
