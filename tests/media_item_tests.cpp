#include "core/media_item.h"
#include "core/progress.h"
#include "infra/stop_signal.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <cstddef>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// The runner is defined over a concept, so its own test type is deliberately
// unrelated to any flow's item: the concept is the whole contract, and a type
// that satisfies it must be enough to be run.
struct FakeItem {
  std::size_t ordinal = 0;
  std::string taskId;
  fs::path sourcePath;
  fs::path targetPath;
  mediaitem::ItemOutcome result;

  auto id() const -> std::string { return taskId; }
  auto label() const -> std::string { return sourcePath.filename().string(); }
  auto source() const -> fs::path const& { return sourcePath; }
  auto target() const -> fs::path const& { return targetPath; }
  auto outcome() -> mediaitem::ItemOutcome& { return result; }
};

auto makeItems(std::size_t count) -> std::vector<FakeItem> {
  auto items = std::vector<FakeItem>{};
  items.reserve(count);
  for (auto index = std::size_t{0}; index < count; ++index) {
    items.push_back(
      FakeItem{
        .ordinal = index,
        .taskId = std::format("item-{}", index),
        .sourcePath = fs::path{"/in"} / std::format("{}.png", index),
        .targetPath = fs::path{"/out"} / std::format("{}.webp", index),
      }
    );
  }
  return items;
}

}  // namespace

TEST_CASE(
  "runStage filters what the flow says is done and writes outcomes back",
  "[media-item]"
) {
  stopsignal::reset();

  auto items = makeItems(4);
  // One call per item whose body ran, so a filtered item cannot be mistaken
  // for work that happened.
  auto ran = std::vector<std::size_t>(items.size(), 0);

  auto const result = mediaitem::runStage(
    mediaitem::StageSpec{.verb = "Working", .maxConcurrency = 2},
    std::span<FakeItem>{items},
    [](FakeItem const& item) { return item.taskId == "item-1"; },
    [&ran](FakeItem& item, taskexec::TaskContext&) -> eh::Result<void> {
      ++ran[item.ordinal];
      if (item.taskId == "item-2") { return eh::makeError("boom"); }
      return {};
    }
  );

  // The filtered item is counted, is not attempted, and records no work.
  CHECK(result.total == 4);
  CHECK(result.skipped == 1);
  CHECK(result.attempted == 3);
  CHECK(ran[1] == 0);
  CHECK(items[1].result.state == mediaitem::ItemState::Skipped);

  // A failure is recorded on its own item and does not stop the others.
  REQUIRE(items[2].result.state == mediaitem::ItemState::Failed);
  CHECK(items[2].result.failureReason == "boom");
  CHECK(items[0].result.state == mediaitem::ItemState::Succeeded);
  CHECK(items[3].result.state == mediaitem::ItemState::Succeeded);
  CHECK(ran[0] == 1);
  CHECK(ran[2] == 1);
  CHECK(ran[3] == 1);

  CHECK(result.succeeded == 2);
  CHECK(result.failed == 1);
  CHECK(result.canceled == false);
  CHECK(result.succeeded + result.failed + result.skipped == result.total);
}

TEST_CASE("runStage writes the counter text after each completion", "[media-item]") {
  stopsignal::reset();

  auto items = makeItems(2);
  auto ctx = progress::ProgressContext{};
  auto const barIndex = ctx.addBar("Warming up");

  // Nothing is written until a task finishes, so the flow's own prompt is what
  // the bar shows first.
  CHECK(ctx.postfixText(barIndex) == "Warming up");

  auto const result = mediaitem::runStage(
    mediaitem::StageSpec{
      .progress = &ctx,
      .barIndex = barIndex,
      .verb = "Working",
      .unit = "files",
      .maxConcurrency = 1,
    },
    std::span<FakeItem>{items},
    [](FakeItem const&) { return false; },
    [](FakeItem&, taskexec::TaskContext&) -> eh::Result<void> { return {}; }
  );

  REQUIRE(result.succeeded == 2);
  CHECK(ctx.postfixText(barIndex) == "Working: 2/2 files");
  // The stage leaves its bar in place: the flow writes the final text and
  // erases it, on the success path as well as the cancel path.
  CHECK(ctx.cleared() == false);
  CHECK(ctx.progressValue(barIndex) == 100.0F);
}

TEST_CASE("a postfix callback replaces the counter text entirely", "[media-item]") {
  stopsignal::reset();

  auto items = makeItems(3);
  auto ctx = progress::ProgressContext{};
  auto const barIndex = ctx.addBar("Analyzing");

  auto const result = mediaitem::runStage(
    mediaitem::StageSpec{
      .progress = &ctx,
      .barIndex = barIndex,
      .verb = "Working",
      .maxConcurrency = 1,
      .postfix =
        [](std::size_t done, std::size_t total, double) {
          return std::format("{}/{} - 12 img/s", done, total);
        },
    },
    std::span<FakeItem>{items},
    [](FakeItem const&) { return false; },
    [](FakeItem&, taskexec::TaskContext&) -> eh::Result<void> { return {}; }
  );

  REQUIRE(result.succeeded == 3);
  CHECK(ctx.postfixText(barIndex) == "3/3 - 12 img/s");
  // The postfix replaces the text, it never appends to the prompt.
  CHECK(ctx.postfixText(barIndex).find("Analyzing") == std::string::npos);
}

TEST_CASE("a canceled stage leaves its unreached items pending", "[media-item]") {
  auto const stopGuard = testutils::ScopedStopSignalReset{};

  auto items = makeItems(3);
  auto ctx = progress::ProgressContext{};
  auto const barIndex = ctx.addBar("Working: 0/3");

  auto const result = mediaitem::runStage(
    mediaitem::StageSpec{
      .progress = &ctx,
      .barIndex = barIndex,
      .verb = "Working",
      .maxConcurrency = 1,
    },
    std::span<FakeItem>{items},
    [](FakeItem const&) { return false; },
    [](FakeItem&, taskexec::TaskContext&) -> eh::Result<void> {
      stopsignal::requestStop();
      return {};
    }
  );

  CHECK(result.canceled);
  CHECK(result.attempted == 1);
  CHECK(result.skipped == 0);
  CHECK(items[0].result.state == mediaitem::ItemState::Succeeded);
  // A slot the stop signal never reached is not attempted and not failed, so
  // the item stays Pending rather than claiming an outcome it never got.
  CHECK(items[1].result.state == mediaitem::ItemState::Pending);
  CHECK(items[2].result.state == mediaitem::ItemState::Pending);
  // The flow still owns the bar on this path too.
  CHECK(ctx.cleared() == false);
}
