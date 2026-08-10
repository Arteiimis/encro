#include "pack/pack.h"

#include <catch2/catch_all.hpp>

#include <type_traits>

// Compile-time boundary check: pack::PackPlan must NOT be reachable from
// the public header pack.h.
//
// SFINAE detector: a visible PackPlan selects the true_type specialization.

template<typename, typename = void>
struct has_PackPlan_in_pack_ns: std::false_type { };

template<typename T>
struct has_PackPlan_in_pack_ns<T, std::void_t<decltype(sizeof(pack::PackPlan))>>:
  std::true_type { };

static_assert(
  !has_PackPlan_in_pack_ns<void>::value,
  "PACKPLAN BOUNDARY BROKEN: pack::PackPlan is still visible from "
  "#include \"pack/pack.h\". It must be internalized in "
  "pack_plan_internal.h only."
);

// Verify public API remains accessible via pack.h.
static_assert(
  sizeof(pack::PackRequest) > 0,
  "PackRequest must be accessible from pack.h"
);
static_assert(
  sizeof(pack::PackRunResult) > 0,
  "PackRunResult must be accessible from pack.h"
);

TEST_CASE("PackPlan is unreachable from public header pack.h", "[pack-plan-boundary]") {
  // The static_assert above would have fired if PackPlan were visible.
  // Reaching here proves the boundary is intact.
  static_assert(!has_PackPlan_in_pack_ns<void>::value, "confirm boundary");
  SUCCEED("pack::PackPlan is not visible from pack.h — boundary intact");
}
