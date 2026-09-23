#include "picture/picture_video_webp.h"

#include "core/encoding_state.h"
#include "core/job_state.h"
#include "core/media_item.h"
#include "core/task_executor.h"
#include "infra/stop_signal.h"
#include "infra/terminal.h"
#include "picture/picture_compress.h"
#include "video/video_encode_runner.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "logging/log_tags.h"
#include "logging/logging.h"

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::PICTURE_PROCESS);

namespace fs = std::filesystem;
using enum terminal::MessageKind;

namespace {

// Each conversion is a single-core libwebp encode plus an ffmpeg decode, so a
// picture-style size-derived cap would oversubscribe the machine while gaining
// little; throughput is bounded per conversion, not per machine.
constexpr auto kVideoConversionMaxParallel = std::size_t{2};

// The encoder writes here and the phase renames on success, so a file at the
// final cached path is always a complete output. Same rule as the picture
// workflow's compression temp (recognizable media extension, `.partial`), so
// both share partialTempPath.

// A cached output may be reused only when the saved state already records a
// succeeded conversion of this exact source. Anything else - a missing record,
// one reset because its source changed, an interrupted or failed one - drops
// the file, because the merge restores any Pending/Interrupted task whose
// target exists. The purge must run before the merge, not after it.
bool cacheBackedByState(jobstate::Store* store, jobstate::TaskRecord const& planned) {
  if (store == nullptr) { return false; }
  auto const saved = store->findTask(planned.id);
  return saved.has_value()
    && saved->status == jobstate::TaskStatus::Succeeded
    && saved->fingerprint == planned.fingerprint;
}

void dropCachedOutput(fs::path const& outputPath) {
  auto ec = std::error_code{};
  fs::remove(outputPath, ec);
  fs::remove(partialTempPath(outputPath), ec);
}

// Renames the finished encode onto its final cached name. A failure here is a
// failed conversion: the temp file is never packed.
bool finalizeConvertedOutput(fs::path const& outputPath) {
  auto const tempPath = partialTempPath(outputPath);
  if (!fs::exists(tempPath)) { return false; }

  auto ec = std::error_code{};
  fs::remove(outputPath, ec);
  fs::rename(tempPath, outputPath, ec);
  if (ec) {
    LOG_WARN(
      "Video conversion output rename failed: output={} error={}",
      outputPath.string(),
      ec.message()
    );
    return false;
  }
  return true;
}

// Plans the run's conversions: one job-state record per clip, the cache purge
// for every clip the saved state does not back, and the merge that registers
// them. The records are parallel to the items, in item order.
auto planConversionRecords(jobstate::Store* store, std::span<MediaItem> items)
  -> std::vector<jobstate::TaskRecord> {
  auto records = std::vector<jobstate::TaskRecord>{};
  records.reserve(items.size());
  for (auto const& item: items) {
    records.push_back(jobstate::makeEncodeTask(item.sourcePath, item.outputPath));
  }

  for (auto index = std::size_t{0}; index < items.size(); ++index) {
    if (cacheBackedByState(store, records[index])) { continue; }
    dropCachedOutput(items[index].outputPath);
  }

  if (store != nullptr) { store->mergeTasks(records); }
  return records;
}

// Encodes one clip and records its outcome in the phase's job state. The
// status callback paints the encoder's live line onto the phase's bar, which
// is why it is passed in rather than built here.
auto convertClip(
  appctx::AppContext& ctx,
  jobstate::Store* store,
  MediaItem& item,
  std::function<void(std::string const&)> const& statusUpdater
) -> eh::Result<void> {
  if (stopsignal::isStopRequested()) {
    return eh::makeError("Video conversion canceled by user.");
  }

  auto const actionId = item.id();
  if (store != nullptr) { store->markRunning(actionId); }

  auto encodingState = appctx::EncodingState{};
  encodingState.inputPath = item.sourcePath;
  encodingState.actionId = actionId;
  // The encoder writes the temp path; the final cached name appears only after
  // the encoder exited successfully.
  encodingState.plannedOutputFile = partialTempPath(item.outputPath);

  auto const encoded = encodeVideo(ctx, encodingState, "webp", statusUpdater);
  auto const converted = encoded && finalizeConvertedOutput(item.outputPath);
  if (converted) {
    if (store != nullptr) { store->markSucceeded(actionId); }
    return {};
  }

  auto reason = std::string{};
  {
    auto const lock =
      std::scoped_lock{encodingState.mtx};  // NOLINT(bugprone-unused-raii)
    reason = encodingState.lastError.value_or(std::string{});
  }
  if (reason.empty()) { reason = "conversion failed"; }
  if (store != nullptr) { store->markFailed(actionId, reason); }
  return eh::makeError("{}", reason);
}

// Closes the bar on the cancel path: the clips that were still pending are
// interrupted in the saved state, and the bar reports how many conversions had
// finished before the stop.
void closeCanceledConversion(
  jobstate::Store* store,
  std::vector<std::string> const& pendingIds,
  std::size_t converted,
  progress::ProgressContext& progressCtx,
  std::size_t barIndex
) {
  if (store != nullptr) {
    store->markIncompleteInterrupted(pendingIds, "canceled by user");
  }

  mediaitem::closeCanceledStage(progressCtx, barIndex, converted, pendingIds.size());
  terminal::messageln(Warning, "Video conversion canceled by user.");
}

// Closes the bar on the success path, printing the failures first - while the
// bar is still on screen, which is where this flow has always printed them.
// `ready` is how many clips the pack step may use: a cache hit is Skipped and a
// conversion is Succeeded, and both have a cached output.
void closeConvertedConversion(
  std::span<MediaItem> items,
  mediaitem::StageResult const& result,
  std::size_t ready,
  progress::ProgressContext& progressCtx,
  std::size_t barIndex
) {
  mediaitem::printFailures(items);

  progressCtx.setRole(barIndex, terminal::Role::Good);
  progressCtx.setPostfixText(
    barIndex,
    std::format(
      "Converted: {}/{}{}",
      ready,
      items.size(),
      result.failed > 0 ? " (some failed)" : ""
    )
  );
  progressCtx.eraseBars();
}

// Splits the clips into the ids that must be encoded and the already-done
// decision the stage filters by, keyed on item address: an id-keyed set could
// let two items that share a source share one decision, since a conversion's id
// derives from its source path alone. `cacheBackedByState(nullptr, ...)` is
// false, which makes every clip pending when there is no store.
auto splitConversions(
  jobstate::Store* store,
  std::span<MediaItem const> items,
  std::span<jobstate::TaskRecord const> records
) -> std::pair<std::vector<std::string>, std::unordered_map<MediaItem const*, bool>> {
  auto pendingIds = std::vector<std::string>{};
  auto backedByItem = std::unordered_map<MediaItem const*, bool>{};
  backedByItem.reserve(items.size());
  for (auto index = std::size_t{0}; index < items.size(); ++index) {
    auto const backed = cacheBackedByState(store, records[index]);
    backedByItem.emplace(&items[index], backed);
    if (!backed) { pendingIds.push_back(records[index].id); }
  }
  return {std::move(pendingIds), std::move(backedByItem)};
}

}  // namespace

auto picturewebp::runConversionPhase(
  appctx::AppContext& ctx,
  std::span<MediaItem> items,
  std::size_t maxParallel
) -> eh::Result<ConversionOutcome> {
  if (items.empty()) { return ConversionOutcome{}; }

  auto* store = ctx.runtime.jobState.get();
  auto const records = planConversionRecords(store, items);

  auto const [pendingIds, backedByItem] = splitConversions(store, items, records);

  if (pendingIds.empty()) {
    // Nothing to encode: every clip is a cache-backed conversion already, and
    // the pack step reads that off the items.
    for (auto& item: items) { item.outcome().state = mediaitem::ItemState::Skipped; }
    terminal::println(
      Info,
      "Recovered {} converted video(s) from the conversion cache.",
      terminal::count(items.size())
    );
    return ConversionOutcome{};
  }

  terminal::println(
    Info,
    "Converting {} video(s) to WebP...",
    terminal::count(pendingIds.size())
  );

  auto progressCtx = progress::ProgressContext{};
  auto const barIndex = progressCtx.addBar(
    std::format("Converting videos: 0/{}", pendingIds.size()),
    terminal::Role::Accent
  );

  // The runner is the single source of the finished-clip count: the postfix
  // stores what it reports and the encoder's live status line reads it.
  auto completed = std::atomic_size_t{0};

  auto const result = mediaitem::runStage(
    mediaitem::StageSpec{
      .progress = &progressCtx,
      .barIndex = barIndex,
      .verb = "Converting videos",
      .maxConcurrency =
        std::max(std::size_t{1}, std::min(maxParallel, kVideoConversionMaxParallel)),
      .postfix =
        [&completed](std::size_t done, std::size_t total, double) {
          completed.store(done, std::memory_order_release);
          return std::format("Converting videos: {}/{}", done, total);
        },
    },
    items,
    [&backedByItem](MediaItem const& item) { return backedByItem.at(&item); },
    [&](MediaItem& item, taskexec::TaskContext&) -> eh::Result<void> {
      auto const statusUpdater = [&](std::string const& status) {
        progressCtx.setPostfixText(
          barIndex,
          std::format(
            "Converting videos: {}/{} [{}]",
            completed.load(std::memory_order_acquire),
            pendingIds.size(),
            status
          )
        );
      };
      return convertClip(ctx, store, item, statusUpdater);
    }
  );

  if (result.canceled || stopsignal::isStopRequested()) {
    closeCanceledConversion(store, pendingIds, result.succeeded, progressCtx, barIndex);
    return ConversionOutcome{.canceled = true};
  }

  auto const ready = result.succeeded + result.skipped;
  closeConvertedConversion(items, result, ready, progressCtx, barIndex);
  if (ready == 0) { return eh::makeError("All video conversions failed."); }

  return ConversionOutcome{.canceled = false, .failedCount = result.failed};
}
