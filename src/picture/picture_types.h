#pragma once

#include "core/job_state.h"
#include "core/media_item.h"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// One item of a picture run: a picture to compress or a clip to convert. Both
// phases run over the same type, so the pack input and the summary read one
// shape. The runner records each phase's result in `result`; `outputPath` is
// the phase's own artifact, never the archive entry.
struct MediaItem {
  fs::path sourcePath;
  fs::path outputPath;    // this phase's temp/cache artifact
  std::string entryName;  // the archive entry name to pack under
  // The picture's original entry name; empty for a conversion.
  std::string originalEntryName;
  // Named `result` because `outcome()` is the accessor the runner calls.
  mediaitem::ItemOutcome result;

  // The conversion phase's persisted job-state id, and so the id a resumed run
  // must keep matching: the "encode:" id of the clip's conversion.
  auto id() const -> std::string {
    return jobstate::makeEncodeTask(sourcePath, outputPath).id;
  }
  auto label() const -> std::string { return sourcePath.filename().string(); }
  auto source() const -> fs::path const& { return sourcePath; }
  auto outcome() -> mediaitem::ItemOutcome& { return result; }
};

// An outcome the pack step may use: the item has a cached output (Succeeded),
// or a conversion cache already backs it (Skipped).
inline bool isPackable(MediaItem const& item) {
  return item.result.state == mediaitem::ItemState::Succeeded
    || item.result.state == mediaitem::ItemState::Skipped;
}
