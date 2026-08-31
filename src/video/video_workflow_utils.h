#pragma once

#include "core/app_context.h"
#include "core/work_dirs.h"
#include "infra/stop_signal.h"
#include "logging/logging.h"
#include "utils/utils.h"

#include <chrono>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace videoworkflow {

namespace fs = std::filesystem;

// Parses a decimal or "num/den" fraction (ffprobe rates, durations) to
// double; nullopt on garbage or a zero denominator.
inline auto parseDouble(std::string_view text) -> std::optional<double> {
  try {
    return std::stod(std::string{text});
  } catch (...) { return std::nullopt; }
}

inline auto parseFraction(std::string_view text) -> std::optional<double> {
  auto const slashPos = text.find('/');
  if (slashPos == std::string_view::npos) { return parseDouble(text); }

  auto const num = parseDouble(text.substr(0, slashPos));
  auto const den = parseDouble(text.substr(slashPos + 1));
  if (!num.has_value() || !den.has_value() || den.value() == 0.0) { return std::nullopt; }
  return num.value() / den.value();
}

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

// Records an active stop request into job state, if any, so a later resume
// sees the interruption instead of pretending nothing happened. Template so
// the requestCancel call is instantiated only in TUs that include the
// complete jobstate::Store (video_batch_execution/video_encoding_state).
template<class StoreT = jobstate::Store>
inline void noteStopRequest(appctx::AppContext& ctx) {
  if (!stopsignal::isStopRequested()) { return; }
  withJobState(ctx, [](StoreT& store) { store.requestCancel(); });
}

// Per-run temp dir under the scratch root; the guard removes it on
// destruction, retrying because a just-exited child (scoring/encode) may
// still hold a transient handle on Windows. warnOnFailure surfaces final
// cleanup failure (encode probes); preview passes false (best-effort UI).
struct ProbeRootCleanupGuard {
  fs::path root;
  int retries;
  std::chrono::milliseconds retryDelay;
  bool warnOnFailure;

  ~ProbeRootCleanupGuard() {  // NOLINT(bugprone-exception-escape): error_code overloads never throw
    for (auto attempt = 0; attempt < retries; ++attempt) {
      auto ec = std::error_code{};
      fs::remove_all(root, ec);
      if (!ec) { return; }
      std::this_thread::sleep_for(retryDelay);
    }
    if (warnOnFailure) { LOG_WARN("Probe temp dir cleanup failed: {}", root.string()); }
  }
};

inline auto createScratchProbeRoot(std::string_view prefix, std::string_view errorKind)
  -> eh::Result<fs::path> {
  auto root = workdirs::scratchDir() / std::format("{}_{}", prefix, getUUID());
  auto ec = std::error_code{};
  fs::create_directories(root, ec);
  if (ec) {
    return eh::makeError(
      "Failed to create {} directory: {} ({})",
      errorKind,
      root.string(),
      ec.message()
    );
  }
  return root;
}

}  // namespace videoworkflow
