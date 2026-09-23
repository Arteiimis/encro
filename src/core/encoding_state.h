#pragma once

#include "core/collision_naming.h"
#include "core/media_item.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace appctx {

namespace fs = std::filesystem;

// Video's item: the per-file state an encode run keeps from planning to
// summary. It lives beside the contract rather than in app_context.h because it
// is a per-item record, not a config or a runtime handle — and it is not
// video-private, because the picture run's WebP conversion builds one too.
struct EncodingState {
  fs::path inputPath;
  std::optional<std::string> actionId;
  std::optional<fs::path> plannedOutputFile;
  std::optional<fs::path> outputFile;
  std::optional<fs::path> progressFilePath;
  std::optional<std::size_t> barIndex;
  std::optional<std::chrono::steady_clock::time_point> startTime;
  std::optional<std::chrono::steady_clock::time_point> endTime;
  std::atomic<float> lastProgressAtomic{-1.0f};
  std::optional<uint64_t> lastFrameCount;
  std::optional<std::string> lastStatus;
  std::optional<std::string> lastError;
  // Monitor stat-skip state: last observed progress-file path and size, so
  // unchanged files are not re-read. Keyed by path because segments swap the
  // progress file between encodes.
  std::optional<fs::path> lastProgressPath;
  std::uintmax_t lastProgressFileSize = 0;
  std::optional<int> chosenCq;  // probe decision; overrides config.crf
  std::optional<int64_t> totalFrames;
  std::uint64_t baseFrameOffset = 0;
  std::optional<int> subprocessPid;
  std::optional<std::string> subprocessCmdline;
  bool finished = false;
  bool success = false;
  // This file's stage outcome; named `result` because `outcome()` is the
  // accessor the runner calls.
  mediaitem::ItemOutcome result;
  std::mutex mtx;

  // The persisted job-state id, and the task id the encode stage logs under.
  // Without a store the same "encode:<path>" form is derived here, so the id
  // does not depend on whether job-state was enabled.
  auto id() const -> std::string {
    return actionId
      .value_or(std::format("encode:{}", collisionnaming::stablePathString(inputPath)));
  }
  auto label() const -> std::string { return inputPath.filename().string(); }
  auto source() const -> fs::path const& { return inputPath; }
  auto outcome() -> mediaitem::ItemOutcome& { return result; }
};

using EncodingStatePtr = std::shared_ptr<EncodingState>;
using EncodingStateList = std::vector<EncodingStatePtr>;

}  // namespace appctx
