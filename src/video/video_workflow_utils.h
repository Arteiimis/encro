#pragma once

#include "core/app_context.h"

#include <optional>
#include <string>
#include <utility>

namespace videoworkflow {

namespace fs = std::filesystem;

inline auto maybeJobState(appctx::AppContext& ctx) -> jobstate::Store* {
  return ctx.runtime.jobState.get();
}

inline auto lookupPlannedOutputFile(
  appctx::path_map<fs::path> const& plannedOutputFiles,
  fs::path const& inputPath
) -> std::optional<fs::path> {
  if (
    auto const it = plannedOutputFiles.find(inputPath); it != plannedOutputFiles.end()
  ) {
    return it->second;
  }

  return std::nullopt;
}

template<class Fn>
inline bool withJobState(appctx::AppContext& ctx, Fn&& fn) {
  if (auto* store = maybeJobState(ctx); store != nullptr) {
    std::forward<Fn>(fn)(*store);
    return true;
  }

  return false;
}

}  // namespace videoworkflow
