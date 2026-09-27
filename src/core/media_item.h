#pragma once

#include "core/collision_naming.h"
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

// "Not attempted" is the default, so a filtered or stop-unreached item never
// reads as a success.
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

// The per-item contract every migrated flow satisfies: a concept rather than a
// shared struct, because each flow carries its own payload. It has no target():
// the shared layer never reads a planned output, so a flow that needs one keeps
// its own field.
//
// Checked against `Ty&`: outcome() hands back the mutable reference the runner
// records the result in, while the rest are const members.
template<class Ty>
concept Item = requires(Ty& item) {
  { item.id() } -> std::convertible_to<std::string>;
  { item.label() } -> std::convertible_to<std::string>;
  { item.source() } -> std::convertible_to<fs::path const&>;
  { item.outcome() } -> std::convertible_to<ItemOutcome&>;
};

// The parts of a parallel stage whose shape is the same across flows; what a
// flow keeps is `alreadyDone`, `runOne`, the bar it draws and its summary.
struct StageSpec {
  // The bar this stage drives, created by the flow with addBar (it may paint a
  // live status into it from inside runOne). Null means "no bar of my own".
  progress::ProgressContext* progress = nullptr;
  std::size_t barIndex = 0;
  // When false the runner still calls `postfix` on each completion but leaves
  // the bar's value alone: a flow whose bar shows more than this stage's
  // fraction owns that value (video's "Overall" bar adds the probe-skipped
  // items and each running encode's partial progress).
  bool setBarProgress = true;
  // The text written on each completion: "{verb}: {done}/{total}[ {unit}]".
  // Until the first completion the bar keeps the text the flow's addBar seeded,
  // so a stage's opening and lasting text are two different strings. Ignored
  // when `postfix` is set.
  std::string verb;
  std::string unit;
  std::size_t maxConcurrency = 1;
  // When set, replaces the counter text entirely on each completion. It also
  // runs for a stage with no bar of its own (`progress` null), which a flow
  // uses to count completions when it draws no bar through the runner.
  std::function<std::string(std::size_t done, std::size_t total, double elapsedSeconds)>
    postfix;
};

// What a stage reports back. `total` is the number of items handed to the
// stage; `total - skipped - attempted` is the cancel-unreached remainder.
struct StageResult {
  std::size_t total = 0;
  std::size_t skipped = 0;
  std::size_t attempted = 0;
  std::size_t succeeded = 0;
  std::size_t failed = 0;
  bool canceled = false;
};

// A handle to an item, dereferencing to one. `EncodingStatePtr` is the case
// this exists for: a flow whose item is neither copyable nor movable keeps a
// vector of pointers, and the concept has to be checked on the pointee.
// A value type has no `operator*`, so this never matches a value span.
template<class Ty>
concept ItemHandle = Item<std::remove_cvref_t<decltype(*std::declval<Ty&>())>>;

// The text a stage writes on each completion when it sets no `postfix`:
// "{verb}: {done}/{total}[ {unit}]".
inline auto stageCounterText(StageSpec const& spec, std::size_t done, std::size_t total)
  -> std::string {
  return spec.unit.empty()
    ? std::format("{}: {}/{}", spec.verb, done, total)
    : std::format("{}: {}/{} {}", spec.verb, done, total, spec.unit);
}

// Runs one stage: filters what the flow says is already done, runs the rest
// through the shared executor, writes each outcome back onto its item and
// counts the result. It never reorders `items` and never prints: the flow owns
// the failure print, and it closes and erases the bar itself (also on cancel).
// This overload takes the items through a pointer and holds the body both
// overloads share; the callbacks receive the item, not the handle, so a stage
// cannot tell which one it came through.
template<ItemHandle Ty, class AlreadyDone, class RunOne>
auto runStage(
  StageSpec const& spec,
  std::span<Ty> items,
  AlreadyDone alreadyDone,
  RunOne runOne
) -> StageResult {
  using ItemTy = std::remove_cvref_t<decltype(*std::declval<Ty&>())>;
  auto result = StageResult{.total = items.size()};

  auto pending = std::vector<ItemTy*>{};
  pending.reserve(items.size());
  for (auto& handle: items) {
    auto& item = *handle;
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
    // A stage that draws a bar owns the cursor; one that draws nothing must
    // not hide it.
    .hideCursor = spec.progress != nullptr,
    .onTaskFinished = [&spec, startedAt](std::size_t done, std::size_t total) {
      auto const elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt)
          .count();
      // `postfix` is also the flow's completion hook when the stage draws no bar.
      auto const text = spec.postfix ? spec.postfix(done, total, elapsed)
                                     : stageCounterText(spec, done, total);
      if (spec.progress == nullptr) { return; }
      if (spec.setBarProgress) {
        spec.progress->setProgress(
          spec.barIndex,
          static_cast<float>(done) / static_cast<float>(total) * 100.0F
        );
      }
      spec.progress->setPostfixText(spec.barIndex, text);
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

// Runs one stage over a span of items held by value: an adapter that hands the
// addresses to the overload above, so the two share one body.
template<Item Ty, class AlreadyDone, class RunOne>
auto runStage(
  StageSpec const& spec,
  std::span<Ty> items,
  AlreadyDone alreadyDone,
  RunOne runOne
) -> StageResult {
  auto handles = std::vector<Ty*>{};
  handles.reserve(items.size());
  for (auto& item: items) { handles.push_back(&item); }
  return runStage(spec, std::span<Ty*>{handles}, alreadyDone, runOne);
}

// Sorts collected failures by path and prints them. The sort is the map's own
// comparison (`fs::path::operator<`), so the printed order matches the
// std::map<fs::path, ...> the flows used to read from.
inline void printFailureLines(std::span<std::pair<fs::path, std::string>> failures) {
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

// Prints one "  <source>: <reason>" line per failed item, in path order: the
// flows printed from a std::map<fs::path, string>, so path order is part of
// their output. An item that failed without a reason has no line to print,
// which is how the flows' failure maps behaved too.
//
// This overload takes the items through a pointer and holds the body both
// overloads share, like runStage's.
template<ItemHandle Ty>
void printFailures(std::span<Ty> items) {
  auto failures = std::vector<std::pair<fs::path, std::string>>{};
  for (auto& handle: items) {
    auto const& outcome = handle->outcome();
    if (outcome.state != ItemState::Failed || outcome.failureReason.empty()) { continue; }
    failures.emplace_back(handle->source(), outcome.failureReason);
  }

  printFailureLines(failures);
}

// The value overload: an adapter that hands the addresses to the one above.
template<Item Ty>
void printFailures(std::span<Ty> items) {
  auto handles = std::vector<Ty*>{};
  handles.reserve(items.size());
  for (auto& item: items) { handles.push_back(&item); }
  printFailures(std::span<Ty*>{handles});
}

// Closes a canceled stage's bar: role Bad, "Canceled: {done}/{total}", erase.
inline void closeCanceledStage(
  progress::ProgressContext& progress,
  std::size_t barIndex,
  std::size_t done,
  std::size_t total
) {
  progress.setRole(barIndex, terminal::Role::Bad);
  progress.setPostfixText(barIndex, std::format("Canceled: {}/{}", done, total));
  progress.eraseBars();
}

}  // namespace mediaitem
