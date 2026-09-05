// Vocabulary loading and the tagger engine seam (tasks 2.1).
#include "tagger/tagger.h"
#include "tagger/vocabulary.h"

#include "test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

auto const kVocabCsv = R"csv(tag_id,name,category,keep_for_model,keep_for_data,count
0,general,0,1,1,100
1,1girl,0,1,1,99
2,hatsune_miku,3,1,1,98
3,rating_explicit,4,1,1,97
4,artist_foo,1,1,1,96
5,thighhighs,0,1,1,95
)csv";

}  // namespace

TEST_CASE("loadVocabulary parses rows in file order", "[tagger]") {
  auto temp = TempDir{};
  auto const csvPath = temp.path / "selected_tags.csv";
  testutils::writeTextFile(csvPath, kVocabCsv);

  auto const vocab = tagger::loadVocabulary(csvPath);
  REQUIRE(vocab.has_value());
  REQUIRE(vocab->size() == 6);

  CHECK((*vocab)[0].name == "general");
  CHECK((*vocab)[0].category == tagger::TagCategory::General);
  CHECK((*vocab)[1].name == "1girl");
  CHECK((*vocab)[2].name == "hatsune_miku");
  CHECK((*vocab)[2].category == tagger::TagCategory::Character);
  CHECK((*vocab)[3].name == "rating_explicit");
  CHECK((*vocab)[3].category == tagger::TagCategory::Rating);
  CHECK((*vocab)[4].name == "artist_foo");
  CHECK((*vocab)[4].category == tagger::TagCategory::Artist);
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
