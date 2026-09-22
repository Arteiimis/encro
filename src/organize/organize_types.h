// Pipeline data model shared by the organize stages (design D3/D6).
#pragma once

#include "core/media_item.h"
#include "tagger/tagger_types.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace organize {

using tagger::TagScore;

// Why an image landed in its folder (report "assignment source").
enum class FolderSource {
  CharacterTag,   // sole at-or-above-threshold character tag
  FolderMatch,    // cluster matched a teaching reference folder
  NewCluster,     // fresh appearance cluster (unknown_*)
  Mixed,          // multi-subject without a dominant character
  Uncategorized,  // analysis failed or nothing applied
};

// One (tag, confidence) pair comes from tagger::TagScore (alias above).

// Raw analysis output cached per content hash; identical shape to the
// tagger's TaggerOutput. Thresholds are
// applied at routing time, never baked in here (design D6): a changed
// --min-confidence applies to cached images too.
using AnalysisResult = tagger::TaggerOutput;

// One scanned image moving through the pipeline.
struct ImageItem {
  fs::path path;
  std::string contentHash;                 // sha256 hex of file bytes
  std::optional<AnalysisResult> analysis;  // nullopt = not analyzed yet
  fs::path folderName;                     // empty = unassigned; kept as a
                                           // path so CJK names survive
  FolderSource folderSource = FolderSource::Uncategorized;
  // Planned destination, filled once routing assigned the folder: the same
  // <root>/organized/<folderName>/<filename> the copy stage derives, which
  // keeps its own collision suffixing.
  fs::path targetPath;
  // Named `result` because `outcome()` is the accessor the runner calls.
  mediaitem::ItemOutcome result;

  auto id() const -> std::string { return contentHash; }
  auto label() const -> std::string { return path.filename().string(); }
  auto source() const -> fs::path const& { return path; }
  auto target() const -> fs::path const& { return targetPath; }
  auto outcome() -> mediaitem::ItemOutcome& { return result; }
  // Read-only overload: the shared failure print reads items through a const
  // span.
  auto outcome() const -> mediaitem::ItemOutcome const& { return result; }
};

struct Options {
  fs::path root;  // directory to scan and organize
  bool recursive = false;
  double minConfidence = 0.35;
  fs::path modelDir;  // resolved before run(); default ~/.encro/models
  bool dryRun = false;
  bool recluster = false;
  bool downloadModels = false;
  std::optional<fs::path> ffmpegPath;  // explicit --ffmpeg-path override
  std::size_t maxJobs = 4;
};

}  // namespace organize
