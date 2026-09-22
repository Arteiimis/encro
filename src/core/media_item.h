#pragma once

#include "core/progress.h"
#include "core/task_executor.h"
#include "infra/terminal.h"

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <format>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace mediaitem {

namespace fs = std::filesystem;

// "Not attempted" is the default, so an item a filter removed or a slot a stop
// signal never reached can never be read as a success.
enum class ItemState {
  Pending,
  Skipped,
  Succeeded,
  Failed
};

struct ItemOutcome {
  ItemState state = ItemState::Pending;
  std::string failureReason;
};

// The per-item contract every migrated flow satisfies. It is a concept rather
// than a shared struct because video needs chosenCq/totalFrames, organize needs
// contentHash/analysis/folderName and picture needs entryName/originalEntryName:
// one concrete type would need a variant or a base class, which trades visible
// duplication for hidden indirection.
//
// Checked against `Ty&`: outcome() hands back the mutable reference the runner
// records the result in, while the rest are const members.
template<class Ty>
concept Item = requires(Ty& item) {
  { item.id() } -> std::convertible_to<std::string>;
  { item.label() } -> std::convertible_to<std::string>;
  { item.source() } -> std::convertible_to<fs::path const&>;
  { item.target() } -> std::convertible_to<fs::path const&>;
  { item.outcome() } -> std::convertible_to<ItemOutcome&>;
};

// Every part of a parallel stage whose *shape* is the same across flows. What a
// flow keeps is `alreadyDone`, `runOne`, the bar it draws and the summary
// sentence it prints.
struct StageSpec {
  // The bar this stage drives, created by the flow with addBar so the flow can
  // also paint a live status into it from inside runOne. Null means "no bar of
  // my own": the flow paints through its own context (preview's captured slot,
  // video's one-bar-per-worker layout) and gets only the bookkeeping.
  progress::ProgressContext* progress = nullptr;
  std::size_t barIndex = 0;
  // The text written on each completion: "{verb}: {done}/{total}[ {unit}]".
  // Until the first completion the bar keeps the text the flow's addBar seeded,
  // so a stage's opening and lasting text are two different strings and neither
  // is derived from the other. Ignored when `postfix` is set.
  std::string verb;
  std::string unit;
  std::size_t maxConcurrency = 1;
  bool hideCursor = true;
  // When set, replaces the counter text entirely on each completion instead of
  // appending to it: a bar has one text slot, and setPostfixText replaces it.
  std::function<std::string(std::size_t done, std::size_t total, double elapsedSeconds)>
    postfix;
};

// What a stage reports back. `total` is the number of items handed to the
// stage, so `skipped + attempted + (slots a cancel never reached) == total`;
// the unreached remainder is `total - skipped - attempted` and is non-zero only
// when `canceled` is set.
struct StageResult {
  std::size_t total = 0;
  std::size_t skipped = 0;
  std::size_t attempted = 0;
  std::size_t succeeded = 0;
  std::size_t failed = 0;
  bool canceled = false;
};

// Runs one stage: filters what the flow says is already done, runs the rest
// through the shared executor, writes each outcome back onto its item and
// counts the result. It never reorders `items`, and it prints nothing: the
// failure list belongs to the flow, which prints it at its own point through
// printFailures.
//
// The bar is left in place for the flow to finish: the flow writes the text
// that closes the stage and erases the bar, on the cancel path as well as on
// the success path.
// The callbacks are deduced template parameters rather than std::function
// objects: a lambda stays inlinable, and the constraint below is what states
// the contract they have to satisfy.
template<Item Ty, class AlreadyDone, class RunOne>
auto runStage(
  StageSpec const& spec,
  std::span<Ty> items,
  AlreadyDone alreadyDone,
  RunOne runOne
) -> StageResult {
  auto result = StageResult{.total = items.size()};

  auto pending = std::vector<Ty*>{};
  pending.reserve(items.size());
  for (auto& item: items) {
    if (alreadyDone(item)) {
      item.outcome().state = ItemState::Skipped;
      ++result.skipped;
      continue;
    }
    pending.push_back(&item);
  }

  if (pending.empty()) { return result; }

  auto taskSpecs = std::vector<taskexec::TaskSpec>{};
  taskSpecs.reserve(pending.size());
  for (auto* item: pending) {
    taskSpecs.push_back({
      .id = item->id(),
      .label = item->label(),
      .input = item->source().string(),
      .run = [item, &runOne](taskexec::TaskContext& taskCtx) {
        return runOne(*item, taskCtx);
      },
    });
  }

  auto const startedAt = std::chrono::steady_clock::now();
  auto const runState = taskexec::runTasks({
    .tasks = std::move(taskSpecs),
    .maxConcurrency = spec.maxConcurrency,
    .progress = spec.progress,
    .hideCursor = spec.hideCursor,
    .onTaskFinished = [&spec, startedAt](std::size_t done, std::size_t total) {
      if (spec.progress == nullptr) { return; }
      auto const elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt)
          .count();
      spec.progress->setProgress(
        spec.barIndex,
        static_cast<float>(done) / static_cast<float>(total) * 100.0F
      );
      if (spec.postfix) {
        spec.progress->setPostfixText(spec.barIndex, spec.postfix(done, total, elapsed));
        return;
      }
      spec.progress->setPostfixText(
        spec.barIndex,
        spec.unit.empty() ? std::format("{}: {}/{}", spec.verb, done, total)
                          : std::format("{}: {}/{} {}", spec.verb, done, total, spec.unit)
      );
    },
  });

  for (auto index = std::size_t{0}; index < pending.size(); ++index) {
    auto& outcome = pending[index]->outcome();
    switch (runState.outcomes[index].state) {
      case taskexec::TaskState::Succeeded:
        outcome.state = ItemState::Succeeded;
        ++result.succeeded;
        break;
      case taskexec::TaskState::Failed:
        outcome.state = ItemState::Failed;
        outcome.failureReason = runState.outcomes[index].error;
        ++result.failed;
        break;
      case taskexec::TaskState::Skipped:
        // A slot the stop signal never reached: the item stays Pending.
        break;
    }
  }
  result.attempted = runState.attemptedCount;
  result.canceled = runState.canceled;

  return result;
}

// Prints one "  <source>: <reason>" line per failed item, in path order: the
// flows printed from a std::map<fs::path, string>, so path order is part of
// their output. An item that failed without a reason has no line to print,
// which is how the flows' failure maps behaved too.
template<Item Ty>
void printFailures(std::span<Ty const> items) {
  auto failures = std::vector<std::pair<fs::path, std::string>>{};
  for (auto const& item: items) {
    auto const& outcome = item.outcome();
    if (outcome.state != ItemState::Failed || outcome.failureReason.empty()) { continue; }
    failures.emplace_back(item.source(), outcome.failureReason);
  }

  std::ranges::sort(failures, {}, &std::pair<fs::path, std::string>::first);
  for (auto const& [source, reason]: failures) {
    terminal::println(
      terminal::MessageKind::Plain,
      "  {}: {}",
      terminal::path(source),
      reason
    );
  }
}

}  // namespace mediaitem
