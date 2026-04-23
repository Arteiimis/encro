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
inline auto withJobState(appctx::AppContext& ctx, Fn&& fn) -> bool {
  if (auto* store = maybeJobState(ctx); store != nullptr) {
    std::forward<Fn>(fn)(*store);
    return true;
  }

  return false;
}

template<class Fn>
inline auto withActionJobState(
  appctx::AppContext& ctx,
  std::optional<std::string> const& actionId,
  Fn&& fn
) -> bool {
  if (!actionId.has_value()) { return false; }

  return withJobState(ctx, [&](jobstate::Store& store) { fn(store, actionId.value()); });
}

}  // namespace videoworkflow
