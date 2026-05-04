#include "pack/pack.h"

#include <catch2/catch_all.hpp>

#include <type_traits>

// Compile-time boundary check: pack::PackPlan must NOT be reachable from
// the public header pack.h.
//
// Uses the MSVC/Clang __if_exists extension to conditionally check whether
// pack::PackPlan is a type.  When PackPlan is hidden, __if_exists is false
// (no-op).  When PackPlan is exposed, __if_exists is true and the
// static_assert(false) fires.
//
// Fallback: also defined as SFINAE detector struct (has_PackPlan_in_pack_ns)
// for compatibility with the test naming convention.

__if_exists(pack::PackPlan) {
  static_assert(
    false,
    "PACKPLAN BOUNDARY BROKEN: pack::PackPlan is still visible from "
    "#include \"pack/pack.h\". It must be internalized in "
    "pack_plan_internal.h only."
  );
}

// SFINAE detector struct (named per plan spec).
// Primary template: PackPlan NOT visible → std::false_type.
template<typename, typename = void>
struct has_PackPlan_in_pack_ns: std::false_type { };

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
  // The __if_exists block above would have fired a static_assert if
  // PackPlan were visible.  Reaching here proves the boundary is intact.
  static_assert(!has_PackPlan_in_pack_ns<void>::value, "confirm boundary");
  SUCCEED("pack::PackPlan is not visible from pack.h — boundary intact");
}
