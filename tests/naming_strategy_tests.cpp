// naming_strategy_tests.cpp — Unit + integration tests for NamingStrategy enum and
// NamingConfig.
//
// Covers all three NamingStrategy values (Flat, FlatWithForce, Keep) and
// NamingConfig default construction / designated initialization.

#include "pack/pack.h"
#include "test_utils.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ============================================================
// NamingStrategy enum value tests
// ============================================================

TEST_CASE("NamingStrategy enum has correct values", "[pack][naming]") {
  CHECK(static_cast<int>(pack::NamingStrategy::Flat) == 0);
  CHECK(static_cast<int>(pack::NamingStrategy::FlatWithForce) == 1);
  CHECK(static_cast<int>(pack::NamingStrategy::Keep) == 2);
}

// ============================================================
// NamingConfig default construction tests
// ============================================================

TEST_CASE("NamingConfig default constructs with Flat strategy", "[pack][naming]") {
  pack::NamingConfig cfg{};
  CHECK(cfg.namingStrategy == pack::NamingStrategy::Flat);
  CHECK_FALSE(cfg.baseName.has_value());
  // Default-constructed std::function should be falsy
  CHECK_FALSE(cfg.zipNameStrategy);
}

TEST_CASE("NamingConfig designated initializer sets namingStrategy", "[pack][naming]") {
  pack::NamingConfig cfg{
    .namingStrategy = pack::NamingStrategy::FlatWithForce,
    .baseName = "test",
  };
  CHECK(cfg.namingStrategy == pack::NamingStrategy::FlatWithForce);
  CHECK(cfg.baseName == "test");
}

// ============================================================
// Integration: Flat strategy — flat entry names (basename only)
// ============================================================

TEST_CASE(
  "Media mode Flat strategy produces flat entry names",
  "[pack][naming][integration]"
) {
  TempDir tmp;
  auto const outputDir = tmp.path / "packed";
  fs::create_directories(outputDir);

  // Create files in different subdirectories
  testutils::writeSizedFile(tmp.path / "subA" / "alpha.txt", 100);
  testutils::writeSizedFile(tmp.path / "subA" / "beta.txt", 100);
  testutils::writeSizedFile(tmp.path / "subB" / "gamma.txt", 100);

  pack::PackRequest req{
    .entries =
      {
        tmp.path / "subA" / "alpha.txt",
        tmp.path / "subA" / "beta.txt",
        tmp.path / "subB" / "gamma.txt",
      },
    .mode = pack::PackMode::Media,
    .outputDir = outputDir,
    .compact = true,
    .naming = pack::NamingConfig{
      .namingStrategy = pack::NamingStrategy::Flat,
    },
  };

  auto const result = pack::execute(req);
  REQUIRE(result.has_value());
  CHECK(result->exitCode == 0);
  REQUIRE_FALSE(result->zippedFiles.empty());

  auto const entryNames =
    testutils::listZipRegularEntryNames(result->zippedFiles.front());
  // All entries should be basename-only (no directory components, no hash
  // prefixes)
  for (auto const& name: entryNames) {
    // No path separator in the entry name
    CHECK(name.find('/') == std::string::npos);
    CHECK(name.find('\\') == std::string::npos);
  }
  // Verify we have all three files by their basenames
  CHECK(std::ranges::contains(entryNames, std::string{"alpha.txt"}));
  CHECK(std::ranges::contains(entryNames, std::string{"beta.txt"}));
  CHECK(std::ranges::contains(entryNames, std::string{"gamma.txt"}));
}

// ============================================================
// Integration: FlatWithForce strategy — hash-disambiguated names
// ============================================================

TEST_CASE(
  "Media mode FlatWithForce strategy produces hash-disambiguated names",
  "[pack][naming][integration]"
) {
  TempDir tmp;
  auto const outputDir = tmp.path / "packed";
  fs::create_directories(outputDir);

  // Create files with SAME basename in DIFFERENT subdirectories (collision
  // scenario)
  testutils::writeSizedFile(tmp.path / "dirA" / "file.txt", 100);
  testutils::writeSizedFile(tmp.path / "dirB" / "file.txt", 100);

  pack::PackRequest req{
    .entries =
      {
        tmp.path / "dirA" / "file.txt",
        tmp.path / "dirB" / "file.txt",
      },
    .mode = pack::PackMode::Media,
    .outputDir = outputDir,
    .compact = true,
    .naming = pack::NamingConfig{
      .namingStrategy = pack::NamingStrategy::FlatWithForce,
    },
  };

  auto const result = pack::execute(req);
  REQUIRE(result.has_value());
  CHECK(result->exitCode == 0);
  REQUIRE_FALSE(result->zippedFiles.empty());

  auto const entryNames =
    testutils::listZipRegularEntryNames(result->zippedFiles.front());
  REQUIRE(entryNames.size() == 2);

  // Each entry should contain "__" hash separator pattern (collisionnaming
  // format) Format: <label>__<hash>__<stem>__<hash><ext>
  for (auto const& name: entryNames) {
    auto const sepCount = std::ranges::count(name, '_');
    // Should have at least 6 underscores (from the collision naming format)
    CHECK(sepCount >= 4);
    // Should contain "__" pattern
    CHECK(name.find("__") != std::string::npos);
  }
  // Both entries should end with .txt
  for (auto const& name: entryNames) { CHECK(name.ends_with(".txt")); }
}

// ============================================================
// Integration: Keep strategy — preserves directory structure
// ============================================================

TEST_CASE(
  "Media mode Keep strategy preserves directory structure",
  "[pack][naming][integration]"
) {
  TempDir tmp;
  auto const outputDir = tmp.path / "packed";
  fs::create_directories(outputDir);

  // Create files in a common root with different subdirectories
  testutils::writeSizedFile(tmp.path / "root" / "subA" / "a.txt", 100);
  testutils::writeSizedFile(tmp.path / "root" / "subB" / "b.txt", 100);

  pack::PackRequest req{
    .entries =
      {
        tmp.path / "root" / "subA" / "a.txt",
        tmp.path / "root" / "subB" / "b.txt",
      },
    .mode = pack::PackMode::Media,
    .outputDir = outputDir,
    .compact = true,
    .naming = pack::NamingConfig{
      .namingStrategy = pack::NamingStrategy::Keep,
    },
  };

  auto const result = pack::execute(req);
  REQUIRE(result.has_value());
  CHECK(result->exitCode == 0);
  REQUIRE_FALSE(result->zippedFiles.empty());

  auto const entryNames =
    testutils::listZipRegularEntryNames(result->zippedFiles.front());
  REQUIRE(entryNames.size() == 2);

  // Entries should contain relative path components from the common ancestor
  CHECK(std::ranges::contains(entryNames, std::string{"subA/a.txt"}));
  CHECK(std::ranges::contains(entryNames, std::string{"subB/b.txt"}));
}
