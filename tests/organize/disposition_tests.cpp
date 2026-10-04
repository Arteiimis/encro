// Folder dispositions for incremental organize (change
// organize-existing-structure tasks 1.1/1.2): the pre-pass rules and the
// miscellaneous-name list boundaries.
#include "organize/disposition.h"

#include "core/display_text.h"

#include "test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

auto kindByName(std::vector<organize::FolderDisposition> const& dispositions)
  -> std::map<std::string, organize::DispositionKind> {
  auto out = std::map<std::string, organize::DispositionKind>{};
  for (auto const& disposition: dispositions) {
    out[displaytext::pathToUtf8String(disposition.name)] = disposition.kind;
  }
  return out;
}

auto dispositionByName(
  std::vector<organize::FolderDisposition> const& dispositions,
  std::string const& name
) -> organize::FolderDisposition const& {
  for (auto const& disposition: dispositions) {
    if (displaytext::pathToUtf8String(disposition.name) == name) { return disposition; }
  }
  FAIL("no disposition named " << name);
}

}  // namespace

TEST_CASE("dispositions default to reference and skip the output tree", "[organize]") {
  auto temp = TempDir{};
  fs::create_directories(temp.path / "角色A");
  fs::create_directories(temp.path / "album");
  fs::create_directories(temp.path / "organized" / "miku");

  auto const dispositions = organize::computeFolderDispositions(temp.path, false, {}, {});
  auto const kinds = kindByName(dispositions);
  CHECK(kinds.size() == 2);
  CHECK(kinds.at("角色A") == organize::DispositionKind::Reference);
  CHECK(kinds.at("album") == organize::DispositionKind::Reference);
  CHECK(kinds.find("organized") == kinds.end());
  CHECK(
    dispositionByName(dispositions, "角色A").reason
    == organize::DispositionReason::Default
  );
}

TEST_CASE(
  "dispositions route misc names to input and dot folders to ignored",
  "[organize]"
) {
  auto temp = TempDir{};
  fs::create_directories(temp.path / "mix");
  fs::create_directories(temp.path / "未分类");
  fs::create_directories(temp.path / ".stash");
  fs::create_directories(temp.path / "夏mix子");

  auto const dispositions = organize::computeFolderDispositions(temp.path, false, {}, {});
  auto const kinds = kindByName(dispositions);
  CHECK(kinds.at("mix") == organize::DispositionKind::Input);
  CHECK(kinds.at("未分类") == organize::DispositionKind::Input);
  CHECK(kinds.at(".stash") == organize::DispositionKind::Ignored);
  CHECK(kinds.at("夏mix子") == organize::DispositionKind::Reference);
  CHECK(
    dispositionByName(dispositions, "mix").reason == organize::DispositionReason::NameList
  );
  CHECK(
    dispositionByName(dispositions, ".stash").reason
    == organize::DispositionReason::Default
  );
}

TEST_CASE("disposition flags override the list and the default", "[organize]") {
  auto temp = TempDir{};
  fs::create_directories(temp.path / "角色A");
  fs::create_directories(temp.path / "mix");
  fs::create_directories(temp.path / ".stash");

  auto const dispositions =
    organize::computeFolderDispositions(temp.path, false, {"角色A"}, {"mix", ".stash"});
  auto const kinds = kindByName(dispositions);
  CHECK(kinds.at("角色A") == organize::DispositionKind::Input);
  CHECK(kinds.at("mix") == organize::DispositionKind::Ignored);
  // --ingest overrides the dot rule too.
  CHECK(
    organize::computeFolderDispositions(temp.path, false, {".stash"}, {}).front().kind
    == organize::DispositionKind::Input
  );
  CHECK(
    dispositionByName(dispositions, "角色A").reason == organize::DispositionReason::Flag
  );
}

TEST_CASE("a recursive run disposes every folder as input except ignored", "[organize]") {
  auto temp = TempDir{};
  fs::create_directories(temp.path / "角色A");
  fs::create_directories(temp.path / "mix");
  fs::create_directories(temp.path / ".stash");

  auto const all = organize::computeFolderDispositions(temp.path, true, {}, {});
  auto const kinds = kindByName(all);
  CHECK(kinds.at("角色A") == organize::DispositionKind::Input);
  CHECK(kinds.at("mix") == organize::DispositionKind::Input);
  // The dot rule is an incremental-mode rule; recursive keeps whole-set
  // behavior, which always entered dot folders.
  CHECK(kinds.at(".stash") == organize::DispositionKind::Input);

  auto const ignored = organize::computeFolderDispositions(temp.path, true, {}, {"mix"});
  CHECK(kindByName(ignored).at("mix") == organize::DispositionKind::Ignored);
}

TEST_CASE(
  "a disposition flag naming an unknown folder is an argument error",
  "[organize]"
) {
  auto temp = TempDir{};
  fs::create_directories(temp.path / "mix");
  fs::create_directories(temp.path / "organized" / "miku");

  CHECK(organize::validateDispositionFlags(temp.path, {"mix"}, {}).has_value() == false);

  auto const unknown = organize::validateDispositionFlags(temp.path, {"nonexistent"}, {});
  REQUIRE(unknown.has_value());
  CHECK(unknown->find("--ingest") != std::string::npos);
  CHECK(unknown->find("nonexistent") != std::string::npos);

  // The output tree is not an ingestable folder either: same class of
  // unresolvable name.
  CHECK(organize::validateDispositionFlags(temp.path, {"organized"}, {}).has_value());
}

TEST_CASE(
  "the same folder in both disposition flags is an argument error",
  "[organize]"
) {
  auto temp = TempDir{};
  fs::create_directories(temp.path / "mix");

  auto const clash = organize::validateDispositionFlags(temp.path, {"mix"}, {"mix"});
  REQUIRE(clash.has_value());
  CHECK(clash->find("mix") != std::string::npos);
}

TEST_CASE("the misc list matches whole trimmed names, never substrings", "[organize]") {
  using organize::isMiscFolderName;
  // ASCII entries fold case; surrounding whitespace is trimmed.
  CHECK(isMiscFolderName("Misc"));
  CHECK(isMiscFolderName("UNFILED"));
  CHECK(isMiscFolderName(" unsorted "));
  // CJK entries match exactly, simplified and traditional listed separately.
  CHECK(isMiscFolderName("雜項"));
  CHECK(isMiscFolderName("其他"));
  CHECK(isMiscFolderName("新图"));
  CHECK(isMiscFolderName("新圖"));
  CHECK(isMiscFolderName("その他"));
  CHECK(isMiscFolderName("とりあえず"));
  // Never by substring, never aggressive names, no cross-script conversion.
  CHECK_FALSE(isMiscFolderName("夏mix子"));
  CHECK_FALSE(isMiscFolderName("miscs"));
  CHECK_FALSE(isMiscFolderName("unfile"));
  CHECK_FALSE(isMiscFolderName("downloads"));
  CHECK_FALSE(isMiscFolderName("new"));
  CHECK_FALSE(isMiscFolderName("新下载"));
  CHECK_FALSE(isMiscFolderName("杂項"));  // mixed-script spelling is neither entry
  CHECK_FALSE(isMiscFolderName(""));
}
