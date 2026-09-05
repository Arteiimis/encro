// organize e2e (task 5.2): full CLI runs against the in-process fake tagger
// seam (ENCRO_FAKE_TAGGER), the fake_media_tool pattern applied to model
// inference. No network, no model files.
#include "e2e_test_utils.h"

#include "core/sha256.h"
#include "test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

namespace fs = std::filesystem;

namespace {

auto sha256Of(std::string_view bytes) -> std::string {
  return core::sha256Hex(bytes);
}

auto writeImage(fs::path const& path, std::string_view bytes) -> void {
  auto out = std::ofstream{path, std::ios::binary};
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Fixture: content hash -> tags for the fake engine.
auto writeFixture(
  fs::path const& path,
  std::map<std::string, std::pair<std::string, double>> const& characterTags,
  std::map<std::string, std::pair<std::string, double>> const& generalTags
) -> void {
  auto entries = std::map<std::string, std::string>{};
  for (auto const& [name, tag]: characterTags) {
    entries[sha256Of(name)] =
      std::format(R"({{"character": [[ "{}", {} ]]}})", tag.first, tag.second);
  }
  for (auto const& [name, tag]: generalTags) {
    entries[sha256Of(name)] =
      std::format(R"({{"general": [[ "{}", {} ]]}})", tag.first, tag.second);
  }
  auto json = std::string{"{"};
  for (auto const& [hash, tags]: entries) {
    json += std::format(R"("{}": {},)", hash, tags);
  }
  if (!entries.empty()) { json.pop_back(); }
  json += "}";
  auto out = std::ofstream{path, std::ios::binary};
  out << json;
}

}  // namespace

TEST_CASE("organize groups images by character via the CLI", "[e2e][organize]") {
  auto temp = TempDir{};
  auto const pics = temp.path / "pics";
  fs::create_directories(pics);
  writeImage(pics / "miku1.png", "miku1");
  writeImage(pics / "miku2.png", "miku2");
  writeImage(pics / "oc1.png", "oc1");

  auto fixture = temp.path / "fixture.json";
  writeFixture(
    fixture,
    {{"miku1", {"hatsune_miku", 0.9}}, {"miku2", {"hatsune_miku", 0.85}}},
    {{"oc1", {"pink_hair", 0.9}}}
  );

  auto const run = e2e::runEncro(
    {"organize", pics.string()},
    std::nullopt,
    {{"ENCRO_FAKE_TAGGER", fixture.string()}}
  );
  REQUIRE_SUCCESS(run);
  CHECK(fs::exists(pics / "organized" / "hatsune_miku" / "miku1.png"));
  CHECK(fs::exists(pics / "organized" / "hatsune_miku" / "miku2.png"));
  // Single-subject remainder clustered into an unknown_ folder.
  auto foundCluster = false;
  for (auto const& entry: fs::directory_iterator{pics / "organized"}) {
    if (entry.path().filename().string().starts_with("unknown_")) {
      foundCluster = fs::exists(entry.path() / "oc1.png");
    }
  }
  CHECK(foundCluster);
  // Report names the sources and the originals stay put.
  CHECK(run.stdoutText.find("character tag") != std::string::npos);
  CHECK(run.stdoutText.find("new cluster") != std::string::npos);
  CHECK(fs::exists(pics / "miku1.png"));
}

TEST_CASE("organize dry run prints the plan without copying", "[e2e][organize]") {
  auto temp = TempDir{};
  auto const pics = temp.path / "pics";
  fs::create_directories(pics);
  writeImage(pics / "miku1.png", "miku1");

  auto fixture = temp.path / "fixture.json";
  writeFixture(fixture, {{"miku1", {"hatsune_miku", 0.9}}}, {});

  auto const run = e2e::runEncro(
    {"organize", pics.string(), "--dry-run"},
    std::nullopt,
    {{"ENCRO_FAKE_TAGGER", fixture.string()}}
  );
  REQUIRE_SUCCESS(run);
  CHECK(run.stdoutText.find("hatsune_miku") != std::string::npos);
  CHECK(!fs::exists(pics / "organized" / "hatsune_miku"));
}

TEST_CASE("organize re-run resumes from the cache", "[e2e][organize]") {
  auto temp = TempDir{};
  auto const pics = temp.path / "pics";
  fs::create_directories(pics);
  writeImage(pics / "miku1.png", "miku1");
  writeImage(pics / "miku2.png", "miku2");

  auto fixture = temp.path / "fixture.json";
  writeFixture(
    fixture,
    {{"miku1", {"hatsune_miku", 0.9}}, {"miku2", {"hatsune_miku", 0.85}}},
    {}
  );

  auto const env =
    std::map<std::string, std::string>{{{"ENCRO_FAKE_TAGGER", fixture.string()}}};
  (void)e2e::runEncro({"organize", pics.string()}, std::nullopt, env);

  auto const second = e2e::runEncro({"organize", pics.string()}, std::nullopt, env);
  REQUIRE_SUCCESS(second);
  CHECK(second.stdoutText.find("(2 cache hits)") != std::string::npos);
  CHECK(second.stdoutText.find("skipped existing 2") != std::string::npos);
}

TEST_CASE("renamed character folder teaches a later e2e run", "[e2e][organize]") {
  auto temp = TempDir{};
  auto const pics = temp.path / "pics";
  fs::create_directories(pics);
  writeImage(pics / "miku1.png", "miku1");

  auto fixture = temp.path / "fixture.json";
  writeFixture(
    fixture,
    {{"miku1", {"hatsune_miku", 0.9}}, {"miku2", {"hatsune_miku", 0.9}}},
    {}
  );

  auto const env =
    std::map<std::string, std::string>{{{"ENCRO_FAKE_TAGGER", fixture.string()}}};
  (void)e2e::runEncro({"organize", pics.string()}, std::nullopt, env);

  fs::rename(pics / "organized" / "hatsune_miku", pics / "organized" / "初音ミク");
  writeImage(pics / "miku2.png", "miku2");

  auto const second = e2e::runEncro({"organize", pics.string()}, std::nullopt, env);
  REQUIRE_SUCCESS(second);
  CHECK(fs::exists(pics / "organized" / "初音ミク" / "miku2.png"));
  CHECK(!fs::exists(pics / "organized" / "hatsune_miku"));
}

TEST_CASE("missing models fail fast with guidance", "[e2e][organize]") {
  auto temp = TempDir{};
  auto const pics = temp.path / "pics";
  fs::create_directories(pics);
  writeImage(pics / "miku1.png", "miku1");
  auto const emptyModels = temp.path / "models";
  fs::create_directories(emptyModels);

  auto const run =
    e2e::runEncro({"organize", pics.string(), "--model-dir", emptyModels.string()});
  CHECK(run.exitCode != 0);
  CHECK(
    (run.stdoutText.find("models not found") != std::string::npos
     || run.stderrText.find("models not found") != std::string::npos)
  );
}
