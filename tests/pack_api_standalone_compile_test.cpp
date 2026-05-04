// Standalone compile test (D-07): verifies pack.h compiles as the single public header
// for the pack module — without including pack_types.h, packer_types.h,
// pack_service.h, or packer.h.
//
// If this file compiles, the public API boundary is clean.
// The compilation step IS the test.
//
// RED phase: this file will fail to compile because pack/pack.h does not exist yet.

#include "pack/pack.h"

// pack.h must NOT transitively include internal headers:
//   pack_types.h, packer_types.h, pack_service.h, packer.h

// Verification: PackRunResult is accessible from pack.h (moved from pack_types.h)
static_assert(
  std::is_aggregate_v<pack::PackRunResult>,
  "PackRunResult must remain an aggregate"
);

// Verification: PackMode enum is accessible
static_assert(std::is_enum_v<pack::PackMode>, "PackMode must be an enum");

// Verification: NamingConfig is defined
static_assert(std::is_class_v<pack::NamingConfig>, "NamingConfig must be a class");

// Verification: PackRequest can be constructed with designated initializers
// (compile-time check only — no runtime catch2 test needed)
inline auto kSampleRequest = [] {
  // Designated initializer construction
  pack::PackRequest req{
    .entries = {},
    .mode = pack::PackMode::Media,
    .outputDir = fs::path{},
    .compact = true,
  };
  return req;
}();

// Verify new Phase 16 fields on PackRequest
inline auto kSampleRequestPhase16 = [] {
  pack::PackRequest req{
    .groupingStrategy = pack::GroupingStrategy::PerSourceDirKeepTogether,
    .summary = pack::SummaryConfig{.enabled = false},
  };
  return req;
}();

// Verify SummaryConfig aggregate
static_assert(
  std::is_aggregate_v<pack::SummaryConfig>,
  "SummaryConfig must remain an aggregate"
);

// Verify GroupingStrategy enum
static_assert(std::is_enum_v<pack::GroupingStrategy>, "GroupingStrategy must be an enum");
