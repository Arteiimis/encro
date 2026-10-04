// Pipeline data model shared by the organize stages (design D3/D6).
#pragma once

#include "core/media_item.h"
#include "tagger/tagger_types.h"

#include <cstddef>
#include <filesystem>
#include <map>
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

// Raw analysis of one image (design D2/D6): the tagger's tags plus the
// identity model's feature. Thresholds are applied at routing time, never
// baked in here, so a changed --min-confidence applies to cached images too.
struct AnalysisResult {
  tagger::TaggerOutput tags;
  // Unit-length identity feature (tagger.h FeatureEngine's contract); empty
  // when the image could not be read — which is no identity evidence at all.
  std::vector<float> identity;

  bool operator==(AnalysisResult const&) const = default;
};

// One scanned image moving through the pipeline.
struct ImageItem {
  fs::path path;
  std::string contentHash;                 // sha256 hex of file bytes
  std::optional<AnalysisResult> analysis;  // nullopt = not analyzed yet
  fs::path folderName;                     // empty = unassigned; kept as a
                                           // path so CJK names survive
  FolderSource folderSource = FolderSource::Uncategorized;
  // Named `result` because `outcome()` is the accessor the runner calls.
  mediaitem::ItemOutcome result;

  auto id() const -> std::string { return contentHash; }
  auto label() const -> std::string { return path.filename().string(); }
  auto source() const -> fs::path const& { return path; }
  auto outcome() -> mediaitem::ItemOutcome& { return result; }
};

// The tag side of the identity score: a sparse unit vector over the
// identity-bearing general tags (name -> weight). Sparse because the vocabulary
// is the corpus's own and no run-time corpus statistic may be fitted, so there
// is no global dimension to index into (design D1).
using TagVector = std::map<std::string, double>;

// Calibrated operating points, not derived numbers. A combined score and a bare
// feature cosine are different coordinates — the same images peak at 0.70 and at
// 0.74 — so each mode carries its own default, and `--identity-tau` moves only
// the combined one. Reference point: the identity model's published metric is
// 0.5 * (1 - cosine) with its threshold at 0.178475, i.e. a feature cosine of
// 0.643050; that number describes a score this code no longer computes, so these
// defaults come from labelled character collections instead — the combined score
// reaches F1 0.86 at 0.70 with the plateau holding from 0.62 to 0.72, and the
// feature-only comparison peaks at 0.74 on the same collection. A model,
// preprocessing or weight change invalidates both and calls for re-measuring
// them the same way. They live beside Options because a run's threshold is
// configuration: the clustering reads them from here and the CLI help and the
// config surface derive their default from the same place.
inline constexpr auto kCombinedTau = 0.70;
inline constexpr auto kFeatureOnlyTau = 0.74;

// Above this many analysed images the condensed pairwise matrix costs more
// memory than the observed workload justifies, so the clustering falls back to
// the previous greedy pass and says so (design D5).
inline constexpr std::size_t kAgglomerationImageCeiling = 10'000;

// Reference-folder sample size for incremental organize (design D3): enough
// members for a stable mean feature and a sole-tag majority vote at folder
// scale, small enough that 30 reference folders cost ~600 inferences once.
// Deterministic by content-hash order, so a re-run samples the same members
// and pays nothing. Tune by editing the constant, not the mechanism.
inline constexpr std::size_t kReferenceSampleSize = 20;

struct Options {
  fs::path root;  // directory to scan and organize
  bool recursive = false;
  double minConfidence = 0.35;
  fs::path modelDir;  // resolved before run(); default ~/.encro/models
  bool dryRun = false;
  bool recluster = false;
  std::optional<fs::path> ffmpegPath;  // explicit --ffmpeg-path override
  std::size_t maxJobs = 4;
  // Identity similarity threshold for a combined score (kCombinedTau above); a
  // comparison with no tag evidence uses kFeatureOnlyTau instead.
  double identityTau = kCombinedTau;
  // Above this many analysed images the clustering takes its low-memory path.
  std::size_t clusterImageCeiling = kAgglomerationImageCeiling;
  // First-level folder names from --ingest / --ignore-folder; the disposition
  // pre-pass folds them into its reference/input/ignored decisions.
  std::vector<std::string> ingestFolders;
  std::vector<std::string> ignoreFolders;
};

}  // namespace organize
