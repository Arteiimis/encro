// Vocabulary loading and the tagger engine seam (tasks 2.1).
#include "tagger/tagger.h"
#include "tagger/vocabulary.h"

#include "test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

// Mirrors the real wd-v3 header and category scheme (0 general,
// 4 character, 9 rating).
auto const kVocabCsv = R"csv(tag_id,name,category,count
0,1girl,0,99
1,hatsune_miku,4,98
2,rating_explicit,9,97
3,thighhighs,0,95
)csv";

}  // namespace

TEST_CASE("loadVocabulary parses rows in file order", "[tagger]") {
  auto temp = TempDir{};
  auto const csvPath = temp.path / "selected_tags.csv";
  testutils::writeTextFile(csvPath, kVocabCsv);

  auto const vocab = tagger::loadVocabulary(csvPath);
  REQUIRE(vocab.has_value());
  REQUIRE(vocab->size() == 4);

  CHECK((*vocab)[0].name == "1girl");
  CHECK((*vocab)[0].category == tagger::TagCategory::General);
  CHECK((*vocab)[1].name == "hatsune_miku");
  CHECK((*vocab)[1].category == tagger::TagCategory::Character);
  CHECK((*vocab)[2].name == "rating_explicit");
  CHECK((*vocab)[2].category == tagger::TagCategory::Rating);
}

TEST_CASE("loadVocabulary handles CRLF and empty lines", "[tagger]") {
  auto temp = TempDir{};
  auto const csvPath = temp.path / "selected_tags.csv";
  testutils::writeTextFile(
    csvPath,
    "tag_id,name,category\r\n0,1girl,0\r\n\r\n1,miku,3\r\n"
  );

  auto const vocab = tagger::loadVocabulary(csvPath);
  REQUIRE(vocab.has_value());
  REQUIRE(vocab->size() == 2);
  CHECK((*vocab)[1].name == "miku");
}

TEST_CASE("loadVocabulary errors on missing file and malformed rows", "[tagger]") {
  auto temp = TempDir{};
  auto const missing = tagger::loadVocabulary(temp.path / "nope.csv");
  REQUIRE(!missing.has_value());

  auto const badCsv = temp.path / "bad.csv";
  testutils::writeTextFile(badCsv, "tag_id,name,category\n0,onlyname\n");
  auto const malformed = tagger::loadVocabulary(badCsv);
  REQUIRE(!malformed.has_value());
}
