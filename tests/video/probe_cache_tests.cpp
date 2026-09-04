#include "test_utils.h"
#include "video/probe_cache.h"

#include <fstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

TEST_CASE("probeCacheKey encodes all decision inputs", "[probe-cache]") {
  // Fixed base inputs; each table row mutates exactly one decision input.
  auto keyFor = [](
                  std::uintmax_t fileSize,
                  std::string const& codec,
                  std::optional<std::string> const& preset,
                  int minVmaf,
                  std::string_view metric
                ) -> std::string {
    return probecache::probeCacheKey(
      "C:\\vids\\a.mp4",
      fileSize,
      5678,
      codec,
      preset,
      10000,
      minVmaf,
      metric
    );
  };

  auto const base =
    keyFor(1234, "hevc_nvenc", std::optional<std::string>{"p6"}, 95, "VMAF");

  // Same inputs twice produce the same key.
  CHECK(keyFor(1234, "hevc_nvenc", std::optional<std::string>{"p6"}, 95, "VMAF") == base);

  auto const mutations = std::vector<std::pair<std::string_view, std::string>>{
    {"preset", keyFor(1234, "hevc_nvenc", std::optional<std::string>{"p7"}, 95, "VMAF")},
    {"file size",
     keyFor(9999, "hevc_nvenc", std::optional<std::string>{"p6"}, 95, "VMAF")},
    {"codec", keyFor(1234, "libx264", std::optional<std::string>{"p6"}, 95, "VMAF")},
    {"min vmaf floor",
     keyFor(1234, "hevc_nvenc", std::optional<std::string>{"p6"}, 90, "VMAF")},
    // Metric is part of the identity, so switching it between runs (the
    // migration) invalidates cached decisions.
    {"metric", keyFor(1234, "hevc_nvenc", std::optional<std::string>{"p6"}, 95, "SSIM")},
  };
  for (auto const& [dimension, mutated]: mutations) {
    CAPTURE(dimension);
    CHECK_FALSE(mutated == base);
  }
}

TEST_CASE("probe cache round-trips entries through save and load", "[probe-cache]") {
  TempDir temp;
  auto const cachePath = temp.path / "probe-cache.json";

  auto const key = probecache::probeCacheKey(
    "a.mp4",
    100,
    200,
    "hevc_nvenc",
    "p5",
    std::nullopt,
    95,
    "VMAF"
  );
  probecache::save(
    {
      probecache::Entry{
        .key = key,
        .chosenCq = 30,
        .p5 = 96.5,
        .estimatedBytes = 123456,
        .metric = "VMAF",
        .unreachableFloor = true,
      },
    },
    cachePath
  );

  auto const loaded = probecache::load(cachePath);
  REQUIRE(loaded.size() == 1);
  CHECK(loaded[0].key == key);
  CHECK(loaded[0].chosenCq == 30);
  CHECK(loaded[0].p5 == 96.5);
  CHECK(loaded[0].estimatedBytes == 123456);
  CHECK(loaded[0].metric == "VMAF");
  CHECK(loaded[0].unreachableFloor);
}

TEST_CASE("probe cache load returns empty for missing file", "[probe-cache]") {
  TempDir temp;
  CHECK(probecache::load(temp.path / "missing.json").empty());
}

TEST_CASE("probe cache discards corrupt json", "[probe-cache]") {
  TempDir temp;
  auto const cachePath = temp.path / "probe-cache.json";
  {
    std::ofstream out{cachePath};
    out << "{ not json";
  }
  CHECK(probecache::load(cachePath).empty());
}

TEST_CASE("probe cache discards on schema version mismatch", "[probe-cache]") {
  TempDir temp;
  auto const cachePath = temp.path / "probe-cache.json";
  {
    std::ofstream out{cachePath};
    out << R"({"version": 999, "entries": []})";
  }
  CHECK(probecache::load(cachePath).empty());
}

TEST_CASE("probe cache evicts oldest entries beyond the cap", "[probe-cache]") {
  TempDir temp;
  auto const cachePath = temp.path / "probe-cache.json";

  // Fill to the cap, then write one fresh entry; the fresh (newest, later
  // timestamp) key must survive and the oldest batch shrink by one.
  auto const now =
    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()
                                                            .time_since_epoch())
      .count();
  auto updates = std::vector<probecache::Entry>{};
  for (auto i = std::size_t{0}; i < probecache::kMaxEntries; ++i) {
    updates.push_back(
      probecache::Entry{
        .key = std::format("old-{}", i),
        .chosenCq = 28,
        .p5 = 95.0,
        .estimatedBytes = 1024,
        .metric = "VMAF",
        .updatedAtMs = static_cast<std::uint64_t>(now + static_cast<std::int64_t>(i)),
      }
    );
  }
  probecache::save(updates, cachePath);
  std::this_thread::sleep_for(
    std::chrono::milliseconds{5}
  );  // sleep-ok: orders saved-vs-fresh timestamps

  probecache::save(
    {
      probecache::Entry{
        .key = "fresh-key",
        .chosenCq = 30,
        .p5 = 96.0,
        .estimatedBytes = 2048,
        .metric = "VMAF",
      },
    },
    cachePath
  );

  auto const loaded = probecache::load(cachePath);
  REQUIRE(loaded.size() == probecache::kMaxEntries);
  CHECK(
    std::ranges::find_if(
      loaded,
      [](probecache::Entry const& e) { return e.key == "fresh-key"; }
    )
    != loaded.end()
  );
  CHECK(std::ranges::all_of(loaded, [](probecache::Entry const& e) {
    return e.updatedAtMs > 0;
  }));
}
