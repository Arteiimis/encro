#include "tagger/vocabulary.h"

#include <charconv>
#include <fstream>
#include <iterator>
#include <utility>

namespace tagger {

namespace {

auto splitCsvLine(std::string_view line) -> std::vector<std::string_view> {
  auto fields = std::vector<std::string_view>{};
  auto start = std::size_t{0};
  while (true) {
    auto const comma = line.find(',', start);
    if (comma == std::string_view::npos) {
      fields.push_back(line.substr(start));
      break;
    }
    fields.push_back(line.substr(start, comma - start));
    start = comma + 1;
  }
  return fields;
}

}  // namespace

auto loadVocabulary(fs::path const& csvPath) -> eh::Result<Vocabulary> {
  auto file = std::ifstream{csvPath, std::ios::binary};
  if (!file.is_open()) {
    return eh::makeError("cannot open vocabulary file: {}", csvPath.string());
  }

  auto vocabulary = Vocabulary{};
  auto line = std::string{};
  auto isFirst = true;
  while (std::getline(file, line)) {
    if (isFirst) {  // header row: tag_id,name,category,...
      isFirst = false;
      continue;
    }
    if (line.empty()) { continue; }
    if (!line.empty() && line.back() == '\r') { line.pop_back(); }
    if (line.empty()) { continue; }

    auto const fields = splitCsvLine(line);
    if (fields.size() < 3) { return eh::makeError("malformed vocabulary row: {}", line); }
    auto category = int{0};
    auto const [ptr, ec] =
      std::from_chars(fields[2].data(), fields[2].data() + fields[2].size(), category);
    if (ec != std::errc{}) {
      return eh::makeError("malformed category in vocabulary row: {}", line);
    }
    vocabulary.push_back(
      VocabEntry{
        .name = std::string{fields[1]},
        .category = static_cast<TagCategory>(category),
      }
    );
  }
  return vocabulary;
}

}  // namespace tagger
