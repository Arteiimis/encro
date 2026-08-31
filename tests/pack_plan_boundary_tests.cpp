#include "pack/pack.h"

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

// Compile-time boundary check: pack_plan_internal.h must NOT be reachable
// from the public header pack.h.
//
// pack_plan_internal.h defines PACK_PLAN_INTERNAL_INCLUDED; if it leaks
// through pack.h's include chain, the #error below fires.
#ifdef PACK_PLAN_INTERNAL_INCLUDED
  #error "PACKPLAN BOUNDARY BROKEN: pack_plan_internal.h leaked via pack.h"
#endif

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
  // The #error above would have fired if the internal header leaked.
  // Reaching here proves the boundary is intact.
  SUCCEED("pack::PackPlan is not visible from pack.h — boundary intact");
}
