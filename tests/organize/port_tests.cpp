// Ported pipeline foundations: SHA-256 reference vectors and the scan stage
// (change add-local-character-grouping tasks 1.1/1.2).
#include "organize/disposition.h"
#include "organize/scan.h"
#include "core/sha256.h"

#include "test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

TEST_CASE("sha256Hex matches reference vectors", "[organize]") {
  CHECK(
    core::sha256Hex("")
    == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
  );
  CHECK(
    core::sha256Hex("abc")
    == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
  );
  CHECK(
    core::sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
    == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"
  );
}

TEST_CASE("scanImages picks up supported images flat and hashed", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "a.png", "png-bytes");
  testutils::writeTextFile(temp.path / "b.JPG", "jpg-bytes");
  testutils::writeTextFile(temp.path / "c.txt", "ignored");
  testutils::writeTextFile(temp.path / "d.webp", "webp-bytes");

  auto const res = organize::scanImages(temp.path, false);
  REQUIRE(res.has_value());
  REQUIRE(res->size() == 3);

  auto hashes = std::map<std::string, std::string>{};
  for (auto const& item: *res) {
    hashes[item.path.filename().string()] = item.contentHash;
  }
  // Identical content in two files -> identical hash across files.
  testutils::writeTextFile(temp.path / "a-copy.png", "png-bytes");
  auto const recopied = organize::scanImages(temp.path, false);
  REQUIRE(recopied.has_value());
  for (auto const& item: *recopied) {
    hashes[item.path.filename().string()] = item.contentHash;
  }
  CHECK(hashes.at("a.png") == hashes.at("a-copy.png"));
  CHECK(!hashes.at("a.png").empty());
  CHECK(!hashes.at("d.webp").empty());
  CHECK(hashes.at("a.png") != hashes.at("d.webp"));
}

TEST_CASE("scanImages excludes the organized output tree", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "fresh.png", "fresh");
  testutils::writeTextFile(temp.path / "organized" / "miku" / "old.png", "old");
  testutils::writeTextFile(temp.path / "organized" / ".cache" / "x.json", "{}");

  auto const res = organize::scanImages(temp.path, true);
  REQUIRE(res.has_value());
  REQUIRE(res->size() == 1);
  CHECK(res->front().path.filename() == "fresh.png");
}

TEST_CASE("scanImages respects the recursive flag", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "top.png", "top");
  testutils::writeTextFile(temp.path / "sub" / "nested.jpg", "nested");

  auto const flat = organize::scanImages(temp.path, false);
  REQUIRE(flat.has_value());
  CHECK(flat->size() == 1);

  auto const deep = organize::scanImages(temp.path, true);
  REQUIRE(deep.has_value());
  CHECK(deep->size() == 2);
}

TEST_CASE("scanImages errors on a missing directory", "[organize]") {
  auto const res = organize::scanImages(fs::path{"Z:/definitely/missing"}, false);
  REQUIRE(!res.has_value());
  CHECK(!res.error().empty());
}

// Incremental input assembly (change organize-existing-structure task 3.1):
// loose root images plus recursively collected input-disposition folders.
TEST_CASE("scanImages assembles input from dispositions", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "loose.png", "loose");
  testutils::writeTextFile(temp.path / "角色A" / "ref.png", "ref");
  testutils::writeTextFile(temp.path / "mix" / "x.png", "x");
  testutils::writeTextFile(temp.path / "mix" / "nested" / "y.png", "y");
  testutils::writeTextFile(temp.path / ".stash" / "s.png", "s");
  testutils::writeTextFile(temp.path / "organized" / "miku" / "old.png", "old");

  auto const dispositions = organize::computeFolderDispositions(temp.path, false, {}, {});
  auto const res = organize::scanImages(temp.path, false, dispositions);
  REQUIRE(res.has_value());
  auto names = std::map<std::string, int>{};
  for (auto const& item: *res) { ++names[item.path.filename().string()]; }
  // mix is misc-listed input (recursively collected); 角色A is a reference
  // and .stash ignored — neither contributes scan input; the output tree is
  // excluded as always.
  CHECK(
    names == std::map<std::string, int>{{"loose.png", 1}, {"x.png", 1}, {"y.png", 1}}
  );
}

TEST_CASE("a recursive scan disposes every folder as input", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "loose.png", "loose");
  testutils::writeTextFile(temp.path / "角色A" / "ref.png", "ref");
  testutils::writeTextFile(temp.path / "mix" / "x.png", "x");
  testutils::writeTextFile(temp.path / ".stash" / "s.png", "s");
  testutils::writeTextFile(temp.path / "organized" / "miku" / "old.png", "old");

  auto const dispositions = organize::computeFolderDispositions(temp.path, true, {}, {});
  auto const res = organize::scanImages(temp.path, true, dispositions);
  REQUIRE(res.has_value());
  CHECK(res->size() == 4);  // whole tree minus the output tree

  auto const skipping = organize::computeFolderDispositions(temp.path, true, {}, {"mix"});
  auto const skipped = organize::scanImages(temp.path, true, skipping);
  REQUIRE(skipped.has_value());
  CHECK(skipped->size() == 3);  // --ignore-folder still excludes mix
}
