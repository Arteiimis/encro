#include "pack/pack_service.h"
#include "pack/pack_internal.h"
#include "pack/pack_plan_internal.h"
#include "test_utils.h"

#include <libzippp/libzippp.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <format>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

pack::PackService testService;

constexpr auto kIndicatorMarker = std::string_view{" | Finalizing "};
constexpr auto kPackingLabelPrefix = std::string_view{"Packing: archive "};
constexpr auto kTwoGroupCompletionText = std::string_view{"Packed: archive 2/2 complete"};
// Enough entries that the bulk archive is still packing while the one-entry
// gated archive holds its finalizing window open.
constexpr auto kBulkEntries = std::size_t{2000};

// One status text as the compact run published it, with its arrival moment and
// whether the gated finalizing window was already open.
struct PublishedText {
  std::string text;
  std::chrono::steady_clock::time_point at;
  bool inFinalizingWindow = false;
};

// Holds an archive's finalizing window open until the test releases it, so the
// compact indicator's texts can be observed deterministically: the plan's
// close hook signals entry and blocks the packing thread on the latch.
class FinalizingWindow {
public:
  void record(std::string_view text) {
    auto lock = std::scoped_lock{mutex_};
    texts_.push_back(
      PublishedText{
        .text = std::string{text},
        .at = std::chrono::steady_clock::now(),
        .inFinalizingWindow = entered_.load(std::memory_order_acquire),
      }
    );
  }

  void hold() {
    entered_.store(true, std::memory_order_release);
    // Poll instead of blocking outright: a run that never publishes the awaited
    // text must fail its assertions rather than hang this thread.
    (void)testutils::waitUntil(
      [this] { return released_.load(std::memory_order_acquire); },
      std::chrono::milliseconds{5'000}
    );
  }

  void release() { released_.store(true, std::memory_order_release); }

  auto snapshot() const -> std::vector<PublishedText> {
    auto lock = std::scoped_lock{mutex_};
    return texts_;
  }

private:
  mutable std::mutex mutex_;
  std::vector<PublishedText> texts_;
  std::atomic<bool> entered_{false};
  std::atomic<bool> released_{false};
};

// The packed-file counter of a compact status text ("... [file n/m]").
auto packedFileCounter(std::string_view text) -> std::size_t {
  auto const marker = text.find("[file ");
  if (marker == std::string_view::npos) { return 0; }

  auto value = std::size_t{0};
  auto const digits = text.substr(marker + 6);
  auto const [end, error] =
    std::from_chars(digits.data(), digits.data() + digits.size(), value);
  return error == std::errc{} ? value : 0;
}

// The animation frame of a text carrying the finalizing indicator, if any.
auto finalizingFrame(std::string_view text) -> std::optional<char> {
  auto const marker = text.find(kIndicatorMarker);
  if (marker == std::string_view::npos) { return std::nullopt; }

  auto const frameIndex = marker + kIndicatorMarker.size();
  if (frameIndex >= text.size()) { return std::nullopt; }
  return text[frameIndex];
}

// Counter range among the packing labels published while the gated window was
// open: a gap between the two is a packing update that landed inside the window.
// The completion text carries no counters and is skipped.
auto windowCounterRange(std::vector<PublishedText> const& texts)
  -> std::pair<std::size_t, std::size_t> {
  auto lowest = std::numeric_limits<std::size_t>::max();
  auto highest = std::size_t{0};
  auto seenLabel = false;
  for (auto const& entry: texts) {
    if (!entry.inFinalizingWindow || !entry.text.starts_with(kPackingLabelPrefix)) {
      continue;
    }
    auto const counter = packedFileCounter(entry.text);
    lowest = std::min(lowest, counter);
    highest = std::max(highest, counter);
    seenLabel = true;
  }
  return seenLabel ? std::pair{lowest, highest}
                   : std::pair{std::size_t{0}, std::size_t{0}};
}

// Frames of the texts published while the window was open, in arrival order.
auto windowFrames(std::vector<PublishedText> const& texts)
  -> std::vector<std::pair<char, std::chrono::steady_clock::time_point>> {
  auto frames = std::vector<std::pair<char, std::chrono::steady_clock::time_point>>{};
  for (auto const& entry: texts) {
    if (!entry.inFinalizingWindow) { continue; }
    if (auto const frame = finalizingFrame(entry.text); frame.has_value()) {
      frames.emplace_back(frame.value(), entry.at);
    }
  }
  return frames;
}

// Steps across the frames the window observed. The indicator steps at most once
// per 120 ms, so the frames spanning S ms cannot hold more than S/120 + 1 steps.
auto frameStepCount(
  std::vector<std::pair<char, std::chrono::steady_clock::time_point>> const& frames
) -> std::size_t {
  auto steps = std::size_t{0};
  for (auto index = std::size_t{1}; index < frames.size(); ++index) {
    if (frames[index].first != frames[index - 1].first) { ++steps; }
  }
  return steps;
}

// Two groups: a one-entry archive whose close window the gate holds open, and a
// bulk archive that keeps publishing packing updates into that window.
auto makeGatedPlan(
  fs::path const& srcDir,
  fs::path const& outDir,
  FinalizingWindow& window
) -> pack::PackPlan {
  auto const gatedFile = testutils::writeTextFile(srcDir / "gated.txt");

  auto bulkEntries = std::vector<pack::PackFileEntry>{};
  bulkEntries.reserve(kBulkEntries);
  for (auto index = std::size_t{0}; index < kBulkEntries; ++index) {
    auto const name = std::format("bulk{:04d}.txt", index);
    bulkEntries.push_back(
      pack::PackFileEntry{
        .sourcePath = testutils::writeTextFile(srcDir / name),
        .zipEntryName = name,
      }
    );
  }

  auto groups = std::vector<std::vector<pack::PackFileEntry>>{};
  groups.push_back(
    std::vector<pack::PackFileEntry>{
      pack::PackFileEntry{.sourcePath = gatedFile, .zipEntryName = "gated.txt"},
    }
  );
  groups.push_back(std::move(bulkEntries));

  return pack::PackPlan{
    .groups = std::move(groups),
    .outputDir = outDir,
    .zipNameForIndex =
      [](std::size_t index) { return std::format("part{}.zip", index + 1); },
    .progressCallbacks =
      {
        .onCompactStatusText = [&window](std::string_view text) { window.record(text); },
      },
    .onBeforeArchiveClose = [&window] { window.hold(); },
    .compact = true,
  };
}

}  // namespace

TEST_CASE("packGroups returns empty for empty plan", "[pack-service]") {
  auto const plan = pack::PackPlan{};
  auto const result = testService.packGroups(plan);

  REQUIRE(result);
  CHECK(result.value().empty());
}

TEST_CASE("packGroups packs grouped files", "[pack-service]") {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);

  auto const f1 = testutils::writeTextFile(srcDir / "a.txt");
  auto const f2 = testutils::writeTextFile(srcDir / "b.txt");
  auto const f3 = testutils::writeTextFile(srcDir / "c.txt");

  auto const groups = std::vector{
    std::vector<pack::PackFileEntry>{
      pack::PackFileEntry{.sourcePath = f1, .zipEntryName = "a.txt"},
      pack::PackFileEntry{.sourcePath = f2, .zipEntryName = "b.txt"},
    },
    std::vector<pack::PackFileEntry>{
      pack::PackFileEntry{.sourcePath = f3, .zipEntryName = "c.txt"},
    }
  };

  auto const plan = pack::PackPlan{
    .groups = groups,
    .outputDir = outDir,
    .zipNameForIndex = [](std::size_t index) {
      return std::format("group{}.zip", index + 1);
    },
  };

  auto const result = testService.packGroups(plan);

  REQUIRE(result);
  REQUIRE(result.value().size() == 2);
  CHECK(fs::exists(outDir / "group1.zip"));
  CHECK(fs::exists(outDir / "group2.zip"));

  libzippp::ZipArchive zip{(outDir / "group1.zip").string()};
  zip.open(libzippp::ZipArchive::ReadOnly);
  CHECK(zip.getEntries().size() == 2);
  zip.close();
}

TEST_CASE("pack range helpers append cumulative ordinal suffixes", "[pack-service]") {
  auto const groups = std::vector{
    std::vector<fs::path>{fs::path{"a"}, fs::path{"b"}},
    std::vector<fs::path>{fs::path{"c"}}
  };

  auto const ranges = pack::internal::buildGroupOrdinalRanges(groups);

  REQUIRE(ranges.size() == 2);
  CHECK(ranges[0].first == 1);
  CHECK(ranges[0].last == 2);
  CHECK(ranges[0].count == 2);
  CHECK(ranges[1].first == 3);
  CHECK(ranges[1].last == 3);
  CHECK(ranges[1].count == 1);
  CHECK(
    pack::internal::appendOrdinalRangeSuffix("bundle_part1.zip", ranges[0])
    == "bundle_part1[1~2#2p].zip"
  );
}

TEST_CASE(
  "selectPackPlanIndexes preserves compact and remaps plan helpers",
  "[pack-service]"
) {
  auto const groups = std::vector<std::vector<pack::PackFileEntry>>{
    std::vector<pack::PackFileEntry>{
      pack::PackFileEntry{.sourcePath = fs::path{"a"}, .zipEntryName = "a"},
    },
    std::vector<pack::PackFileEntry>{
      pack::PackFileEntry{.sourcePath = fs::path{"b"}, .zipEntryName = "b"},
    },
  };
  auto const selectedIndexes = std::vector<std::size_t>{0, 1};

  // The compact flag is carried over from the source plan either way.
  auto const nonCompactPlan = pack::PackPlan{
    .groups = groups,
    .outputDir = fs::path{},
    .compact = false,
  };
  auto const resultNonCompact =
    pack::internal::selectPackPlanIndexes(nonCompactPlan, std::span{selectedIndexes});
  CHECK(resultNonCompact.compact == false);

  auto const compactPlan = pack::PackPlan{
    .groups = groups,
    .outputDir = fs::path{},
    .compact = true,
  };
  auto const resultCompact =
    pack::internal::selectPackPlanIndexes(compactPlan, std::span{selectedIndexes});
  CHECK(resultCompact.compact == true);

  // zipNameForIndex remaps through the selected indexes: selected[0]=1 maps
  // to original index 1.
  auto const remapPlan = pack::PackPlan{
    .groups = groups,
    .outputDir = fs::path{},
    .zipNameForIndex = [](std::size_t i) { return std::format("arch{}.zip", i); },
  };
  auto const selected = std::vector<std::size_t>{1, 0};
  auto const result =
    pack::internal::selectPackPlanIndexes(remapPlan, std::span{selected});
  CHECK(result.zipNameForIndex(0) == "arch1.zip");
}

TEST_CASE(
  "packGroups compact mode reports ordered per-file progress updates",
  "[pack-service]"
) {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);

  SECTION("single group counts every file in order") {
    auto const f1 = testutils::writeTextFile(srcDir / "a.txt");
    auto const f2 = testutils::writeTextFile(srcDir / "b.txt");
    auto const f3 = testutils::writeTextFile(srcDir / "c.txt");

    auto progressUpdates = std::vector<std::string>{};
    auto const plan = pack::PackPlan{
      .groups =
        {
          std::vector<pack::PackFileEntry>{
            pack::PackFileEntry{.sourcePath = f1, .zipEntryName = "a.txt"},
            pack::PackFileEntry{.sourcePath = f2, .zipEntryName = "b.txt"},
            pack::PackFileEntry{.sourcePath = f3, .zipEntryName = "c.txt"},
          },
        },
      .outputDir = outDir,
      .zipNameForIndex = [](std::size_t) { return std::string{"group1.zip"}; },
      .progressCallbacks =
        {
          .onCompactProgress =
            [&](std::size_t completedFiles, std::size_t totalFiles) {
              progressUpdates.push_back(std::format("{}/{}", completedFiles, totalFiles));
            },
        },
      .compact = true,
    };

    auto const result = testService.packGroups(plan);

    REQUIRE(result);
    CHECK(progressUpdates == std::vector<std::string>{"0/3", "1/3", "2/3", "3/3"});
  }

  SECTION("parallel groups keep the global callback order") {
    auto const f1 = testutils::writeTextFile(srcDir / "a.txt");
    auto const f2 = testutils::writeTextFile(srcDir / "b.txt");

    auto progressUpdates = std::vector<std::string>{};
    auto const plan = pack::PackPlan{
      .groups =
        {
          std::vector<pack::PackFileEntry>{
            pack::PackFileEntry{.sourcePath = f1, .zipEntryName = "a.txt"},
          },
          std::vector<pack::PackFileEntry>{
            pack::PackFileEntry{.sourcePath = f2, .zipEntryName = "b.txt"},
          },
        },
      .outputDir = outDir,
      .zipNameForIndex =
        [](std::size_t index) { return std::format("group{}.zip", index + 1); },
      .progressCallbacks =
        {
          .onCompactProgress =
            [&](std::size_t completedFiles, std::size_t totalFiles) {
              // Stall after the first completion to force interleaving; the
              // callback sequence must stay globally ordered regardless.
              if (completedFiles == 1) {
                std::this_thread::sleep_for(
                  std::chrono::milliseconds{50}
                );  // sleep-ok: stall forces callback interleaving
              }
              progressUpdates.push_back(std::format("{}/{}", completedFiles, totalFiles));
            },
        },
      .maxParallelJobs = 2,
      .compact = true,
    };

    auto const result = testService.packGroups(plan);

    REQUIRE(result);
    CHECK(progressUpdates == std::vector<std::string>{"0/2", "1/2", "2/2"});
  }
}

TEST_CASE(
  "packGroups compact mode emits archive and file status text",
  "[pack-service]"
) {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);

  auto const f1 = testutils::writeTextFile(srcDir / "a.txt");
  auto const f2 = testutils::writeTextFile(srcDir / "b.txt");
  auto const f3 = testutils::writeTextFile(srcDir / "c.txt");

  auto statusTexts = std::vector<std::string>{};
  auto const plan = pack::PackPlan{
    .groups =
      {
        std::vector<pack::PackFileEntry>{
          pack::PackFileEntry{.sourcePath = f1, .zipEntryName = "a.txt"},
          pack::PackFileEntry{.sourcePath = f2, .zipEntryName = "b.txt"},
          pack::PackFileEntry{.sourcePath = f3, .zipEntryName = "c.txt"},
        },
      },
    .outputDir = outDir,
    .zipNameForIndex = [](std::size_t) { return std::string{"group1.zip"}; },
    .progressCallbacks =
      {
        .onCompactStatusText =
          [&](std::string_view statusText) { statusTexts.emplace_back(statusText); },
      },
    .compact = true,
  };

  auto const result = testService.packGroups(plan);

  REQUIRE(result);
  // The finalizing indicator appends " | Finalizing <frame>" to the packing
  // label on a 120 ms frame cadence, so its texts interleave anywhere in the
  // stream, not only at the tail. Filter them out: the deterministic packing
  // sequence must then match exactly, independent of position.
  auto statusFrames = std::vector<std::string>{};
  for (auto const& text: statusTexts) {
    if (text.find("| Finalizing") == std::string::npos) { statusFrames.push_back(text); }
  }
  CHECK(
    statusFrames
    == std::vector<std::string>{
      "Packing: archive 0/1 [file 0/3]",
      "Packing: archive 0/1 [file 1/3]",
      "Packing: archive 0/1 [file 2/3]",
      "Packing: archive 0/1 [file 3/3]",
      "Packing: archive 1/1 [file 3/3]",
      "Packed: archive 1/1 complete",
    }
  );
  // The indicator never replaces the label. This run's close is fast, so it may
  // publish no indicator text at all (the gated case above owns the composed
  // text); the archive counter reading is pinned by the expected sequence, where
  // the label reads 0/1 until the close completes and 1/1 after it.
  CHECK(std::ranges::none_of(statusTexts, [](std::string const& text) {
    return text.starts_with("Finalizing");
  }));
}

TEST_CASE(
  "compact postfix keeps the packing label while an archive finalizes",
  "[pack-service]"
) {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);

  auto window = FinalizingWindow{};
  auto const plan = makeGatedPlan(srcDir, outDir, window);

  auto result = eh::Result<std::vector<fs::path>>{};
  auto packThread = std::jthread{[&] { result = testService.packGroups(plan); }};

  // The reproduction needs a packing update published inside the window, on top
  // of the indicator's own texts. Release on that, so a run that publishes
  // neither fails the assertions instead of holding the window open.
  REQUIRE(testutils::waitUntil([&] {
    auto const texts = window.snapshot();
    auto const sawIndicator = std::ranges::any_of(texts, [](PublishedText const& entry) {
      return entry.text.find(kIndicatorMarker) != std::string::npos;
    });
    auto const [lowestCounter, highestCounter] = windowCounterRange(texts);
    return sawIndicator && highestCounter > lowestCounter;
  }));
  window.release();
  packThread.join();

  REQUIRE(result);
  auto const texts = window.snapshot();
  REQUIRE_FALSE(texts.empty());

  // The window opened while the bulk archive was still packing: its updates
  // advanced the label's file counter inside the window, which is the
  // interleaving the defect needs (the REQUIRE above waits for exactly that).
  CHECK(std::ranges::none_of(texts, [](PublishedText const& entry) {
    return entry.text.starts_with("Finalizing");
  }));
  CHECK(std::ranges::all_of(texts, [](PublishedText const& entry) {
    return entry.text.starts_with(kPackingLabelPrefix)
      || entry.text == kTwoGroupCompletionText;
  }));
  CHECK(texts.back().text == kTwoGroupCompletionText);

  // The label keeps the archives-completed-so-far counter: the gated archive is
  // the first of the two to close, so its window reads 0/2 and the bulk
  // archive's later window reads 1/2 - never the finished 2/2, which only the
  // completion text reports.
  auto indicatorTexts = std::vector<std::string>{};
  for (auto const& entry: texts) {
    if (entry.text.find(kIndicatorMarker) != std::string::npos) {
      indicatorTexts.push_back(entry.text);
    }
  }
  REQUIRE_FALSE(indicatorTexts.empty());
  CHECK(indicatorTexts.front().starts_with("Packing: archive 0/2 "));
  CHECK(std::ranges::all_of(indicatorTexts, [](std::string const& text) {
    return text.starts_with("Packing: archive 0/2 ")
      || text.starts_with("Packing: archive 1/2 ");
  }));
}

TEST_CASE("compact finalizing frame steps on a fixed cadence", "[pack-service]") {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);

  auto window = FinalizingWindow{};
  auto const plan = makeGatedPlan(srcDir, outDir, window);

  auto result = eh::Result<std::vector<fs::path>>{};
  auto packThread = std::jthread{[&] { result = testService.packGroups(plan); }};

  // Wait on observable animation state, not a wall-clock dwell: four frame
  // steps are enough to measure the cadence over a span the test actually saw
  // (the spec's 500 ms window is this same rule with T = 500).
  auto const sawFourSteps = testutils::waitUntil([&] {
    return frameStepCount(windowFrames(window.snapshot())) >= 4;
  });
  window.release();
  packThread.join();

  REQUIRE(result);
  REQUIRE(sawFourSteps);

  auto const texts = window.snapshot();
  auto const frames = windowFrames(texts);
  auto const steps = frameStepCount(frames);
  auto const spanMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        frames.back().second - frames.front().second
  )
                        .count();
  // Steps land on 120 ms boundaries, so a span of S ms cannot hold more than
  // S/120 + 1 of them, however many packing updates land inside it.
  CHECK(steps <= static_cast<std::size_t>(spanMs) / 120 + 1);

  // The bulk archive was packing inside the same window, so its updates were
  // published between these frames without adding steps of their own.
  auto const [lowestCounter, highestCounter] = windowCounterRange(texts);
  CHECK(highestCounter > lowestCounter);
}

TEST_CASE(
  "packGroups skips missing sources, advances progress, and still writes the archive",
  "[pack-service]"
) {
  TempDir temp;
  auto const outDir = temp.path / "out";

  auto progressUpdates = std::vector<std::string>{};
  auto const missingFile = temp.path / "missing.txt";
  auto const plan = pack::PackPlan{
    .groups =
      {
        std::vector<pack::PackFileEntry>{
          pack::PackFileEntry{.sourcePath = missingFile, .zipEntryName = "missing.txt"},
        },
      },
    .outputDir = outDir,
    .zipNameForIndex = [](std::size_t) { return std::string{"group1.zip"}; },
    .progressCallbacks =
      {
        .onCompactProgress =
          [&](std::size_t completedFiles, std::size_t totalFiles) {
            progressUpdates.push_back(std::format("{}/{}", completedFiles, totalFiles));
          },
      },
    .compact = true,
  };

  auto const result = testService.packGroups(plan);

  REQUIRE(result);
  CHECK(progressUpdates == std::vector<std::string>{"0/1", "1/1"});
  CHECK(result.value().size() == 1);
}

TEST_CASE(
  "packGroups preserves group callback order across multiple groups in both modes",
  "[pack-service]"
) {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);

  auto const f1 = testutils::writeTextFile(srcDir / "a.txt");
  auto const f2 = testutils::writeTextFile(srcDir / "b.txt");

  auto runCase = [&](bool compact) {
    auto callbackEvents = std::vector<std::string>{};
    auto successZipPaths = std::vector<fs::path>{};
    auto const plan = pack::PackPlan{
      .groups =
        {
          std::vector<pack::PackFileEntry>{
            pack::PackFileEntry{.sourcePath = f1, .zipEntryName = "a.txt"},
          },
          std::vector<pack::PackFileEntry>{
            pack::PackFileEntry{.sourcePath = f2, .zipEntryName = "b.txt"},
          },
        },
      .outputDir = outDir,
      .zipNameForIndex =
        [](std::size_t index) { return std::format("group{}.zip", index + 1); },
      .progressCallbacks = {
        .onGroupStart =
          [&](std::size_t index) {
            callbackEvents.push_back(std::format("start:{}", index));
          },
        .onGroupSuccess =
          [&](std::size_t index, fs::path const& zipPath) {
            callbackEvents.push_back(std::format("success:{}", index));
            successZipPaths.push_back(zipPath);
          },
      },
      .maxParallelJobs = 1,
      .compact = compact,
    };

    auto const result = testService.packGroups(plan);

    REQUIRE(result);
    CHECK(
      callbackEvents
      == std::vector<std::string>{"start:0", "success:0", "start:1", "success:1"}
    );
    // Each success callback receives its own group's zip path.
    CHECK(
      successZipPaths
      == std::vector<fs::path>{outDir / "group1.zip", outDir / "group2.zip"}
    );
  };

  SECTION("compact") {
    runCase(true);
  }

  SECTION("full") {
    runCase(false);
  }
}

TEST_CASE(
  "execute() in Directory mode rejects a non-existent input directory",
  "[pack-service]"
) {
  TempDir temp;
  auto const nonExistentDir = temp.path / "does_not_exist";
  auto const outDir = temp.path / "out";

  pack::PackRequest req{
    .entries = {nonExistentDir},
    .mode = pack::PackMode::Directory,
    .outputDir = outDir,
  };

  auto const result = pack::execute(req);
  REQUIRE_FALSE(result);
  CHECK(result.error().find("not a directory") != std::string::npos);
}

TEST_CASE("packGroups reports failure when a group task throws", "[pack-service]") {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);
  auto const f1 = testutils::writeTextFile(srcDir / "a.txt");

  pack::PackPlan plan{
    .groups =
      {
        {pack::PackFileEntry{.sourcePath = f1, .zipEntryName = "a.txt"}},
      },
    .outputDir = outDir,
    .zipNameForIndex = [](std::size_t) { return std::string{"p.zip"}; },
    .progressCallbacks = {
      .onGroupStart = [](std::size_t) {
        throw std::runtime_error{"boom from onGroupStart"};
      },
    },
  };

  auto const result = testService.packGroups(plan);

  // The run must be reported as failed with the exception message, never as a
  // silent success (error-visibility: pack task failures are never success).
  REQUIRE_FALSE(result);
  CHECK(result.error().find("boom from onGroupStart") != std::string::npos);
}
