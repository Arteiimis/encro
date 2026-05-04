#pragma once

#include "core/error_handle.h"
#include "pack/pack_types.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Forward declarations for types from other headers.
// PackRequest holds a raw pointer to jobstate::Store (D-06).
namespace jobstate {

class Store;

}

namespace pack {

// --- PackRunResult (MOVED from pack_types.h) ---
// Return type of execute(). Small value type, part of public API.
struct PackRunResult {
  int exitCode = 0;
  std::vector<fs::path> zippedFiles;
};

// --- PackMode (D-03) ---
// Media: video/picture encoding output files
// Directory: raw directory tree pack (pack-only mode)
enum class PackMode {
  Media,
  Directory,
};

enum class NamingStrategy {
  Flat,
  FlatWithForce,
  Keep,
};

// --- NamingConfig (D-04, extended D-06, D-09) ---
// Optional sub-struct. When std::nullopt on PackRequest, module uses defaults by mode.
struct NamingConfig {
  NamingStrategy namingStrategy = NamingStrategy::Flat;
  std::optional<std::string> baseName;
  std::function<std::string(
    std::size_t partIndex,
    std::size_t subPartIndex,
    std::size_t totalSubParts,
    std::string_view baseName,
    FileOrdinalRange ordinalRange
  )>
    zipNameStrategy;
};

// --- PackRequest (D-01, D-02, extended D-07) ---
// Single struct with std::optional fields. Construct with designated initializers.
struct PackRequest {
  std::vector<fs::path> entries;  // D-02: flat list of file paths
  std::vector<PackEntryInput>
    entryInputs;  // Optional explicit media entries with stable grouping keys
  PackMode mode = PackMode::Media;      // D-03
  fs::path outputDir;                   // D-09: REQUIRED field
  bool compact = true;                  // D-08: consumers derive from config.fullProgress
  bool removeOnFailure = false;
  std::optional<NamingConfig> naming;   // D-04: nullopt = defaults by mode
  std::optional<std::size_t>
    maxParallelJobs;                    // D-10: null = resolveWorkerCount() internally
  bool recursive = true;                // Directory mode only
  jobstate::Store* jobState = nullptr;  // D-06: non-null = enable resumable execution
  std::function<std::string(fs::path const&)>
    entryNameForFile;                   // D-07: optional callback for path-only entries
};

// --- execute() (D-05) ---
// Free function. Internally manages Packer/PackService lifecycle.
// Handles grouping, naming, PackPlan construction, resumable execution (D-11).
auto execute(PackPlan const& plan, jobstate::Store* jobState = nullptr)
  -> eh::Result<PackRunResult>;
auto execute(PackRequest const& request) -> eh::Result<PackRunResult>;

}  // namespace pack
