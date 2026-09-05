// Pipeline data model shared by the organize stages (design D3/D6).
#pragma once

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
  std::string folderName;                  // "" = unassigned
  FolderSource folderSource = FolderSource::Uncategorized;
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
