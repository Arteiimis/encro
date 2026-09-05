// Ported pipeline foundations: SHA-256 reference vectors and the scan stage
// (change add-local-character-grouping tasks 1.1/1.2).
#include "organize/scan.h"
#include "organize/sha256.h"

#include "test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("sha256Hex matches reference vectors", "[organize]") {
  CHECK(
    organize::sha256Hex("")
    == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
  );
  CHECK(
    organize::sha256Hex("abc")
    == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
  );
  CHECK(
    organize::sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
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
  // Same content -> same hash; every item carries a non-empty digest.
  CHECK(hashes.at("a.png") == hashes.at("a.png"));
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
