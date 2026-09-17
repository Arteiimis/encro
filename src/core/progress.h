#pragma once

#include "infra/terminal.h"

#include <indicators/dynamic_progress.hpp>
#include <indicators/progress_bar.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace progress {

// The bar path's role mapper, kept beside the bar library rather than in
// terminal.h so that the styling module stays free of the indicators
// dependency. It carries no font style: a bar's role is its foreground alone.
//
// `Role::Default` must not reach a bar in a colored frame: the bar library
// sets one foreground per bar and resets only after the whole frame, so a bar
// that sets no color keeps the previous bar's. Every role taken here resolves
// to a real color whenever colors are enabled.
auto barColor(terminal::Role role, bool colorsEnabled = true) -> indicators::Color;

using Manager = indicators::DynamicProgress<indicators::ProgressBar>;
using BarPtr = std::unique_ptr<indicators::ProgressBar>;
using BarCollection = std::vector<BarPtr>;

// ETA from an EMA-smoothed projected total time (elapsed * 100 / progress
// gained since the baseline), not from instantaneous rate: per-update encode
// speed wobbles +-30% and a short-window rate estimate turns that into tens
// of minutes of ETA swing. Baseline-relative progress keeps resumed jobs
// (overall bar opening at, say, 80%) from extrapolating to ~zero remaining.
// ponytail: the since-start average biases the ETA toward the early phase for
// ~tau after a sustained speed change; a decayed-window rate would remove that.
class EtaEstimator {
public:
  static constexpr auto kSampleInterval = std::chrono::milliseconds{250};
  // Projection fold time constant: long enough to swallow bursty speed noise,
  // short enough to track real slowdowns when parallel jobs start/stop.
  static constexpr auto kProjectionTauSec = 15.0f;
  // Seed the projection only once >= kSeedMinProgress percent has been gained
  // since the baseline (seeding during ffmpeg's startup/warmup ramp would
  // extrapolate to hours), or after kSeedMaxElapsed for batch-overall bars
  // whose percent crawls.
  static constexpr auto kSeedMinProgress = 0.5f;
  static constexpr auto kSeedMaxElapsed = std::chrono::milliseconds{30000};

  void sample(std::chrono::steady_clock::time_point now, float progress);
  void reset(float elapsedBaseSec = 0.0f);
  auto etaSeconds(float progress) const -> std::optional<float>;
  // Real seconds since the encoding anchor (first positive-progress sample),
  // plus the base injected at reset for resumed attempts; nullopt before the
  // anchor (no elapsed clock yet).
  auto elapsedSeconds(std::chrono::steady_clock::time_point now) const
    -> std::optional<float>;
  float lastProgress() const;

private:
  std::chrono::steady_clock::time_point startAt_{};
  std::chrono::steady_clock::time_point lastFoldAt_{};
  float baseProgress_ = 0.0f;
  float lastProgress_ = 0.0f;
  float elapsedBaseSec_ = 0.0f;
  float projectedTotalSec_ = 0.0f;
  float lastFoldedProgress_ = 0.0f;
  bool hasSample_ = false;
  bool hasProjection_ = false;
};

class ProgressContext {
public:
  std::size_t
  addBar(std::string_view promptText, terminal::Role role = terminal::Role::Accent);
  void setPostfixText(std::size_t barIndex, std::string_view promptText);
  void setProgress(std::size_t barIndex, float progress);
  void setRole(std::size_t barIndex, terminal::Role role);
  void resetEta(std::size_t barIndex, float elapsedBaseSec = 0.0f);
  // Real seconds spent on the bar's current task (base + time since the
  // encoding anchor); nullopt before the anchor. Read-only view for
  // diagnostics and tests.
  auto elapsedSeconds(
    std::size_t barIndex,
    std::chrono::steady_clock::time_point now
  ) const -> std::optional<float>;

  // Remaining-time estimate for the bar's last progress sample; nullopt while
  // the estimator is unseeded. Read-only view for diagnostics and tests.
  auto etaSeconds(std::size_t barIndex) const -> std::optional<float>;

  // Last progress value set for the bar; the value a repaint re-renders.
  // Read-only view for diagnostics and tests.
  float progressValue(std::size_t barIndex) const;

  // Repaint passes run so far, including the ones that rendered nothing.
  // Read-only view for diagnostics and tests.
  std::uint64_t tickCount() const;

  // Re-renders every bar from the state the setters already stored, advancing
  // the postfix scroll window on the context's own clock; touches neither
  // progress values nor ETA sampling.
  void tick();

  // True when this context can actually paint: it holds at least one bar,
  // stdout is a terminal, and output is not quiet. Callers use it to skip work
  // whose output has nowhere to go.
  bool renderable() const;

  // Clears the rendered bar lines from the terminal. The bars stay alive in
  // the manager (it holds references to them), so no render call may follow
  // until a bar is added again.
  void eraseBars();

  auto manager() -> Manager&;
  auto manager() const -> Manager const&;

private:
  void applyBarText(std::size_t barIndex, float progress);
  void render();

  // Repaint clock lifetime. ensureTicker() runs with mtx_ held (addBar);
  // stopTicker() takes mtx_ only to detach the clock and joins outside it,
  // since a wakeup already inside tick() may be holding the lock.
  void ensureTicker();
  void stopTicker();

  mutable std::mutex mtx_;
  Manager manager_;
  BarCollection bars_;
  std::vector<terminal::Role> roles_;
  std::vector<std::string> postfixes_;
  std::vector<EtaEstimator> etas_;
  std::uint64_t tickCount_ = 0;
  // Bars rendered on the last render pass; bars added but never rendered
  // (all-cache-hit probe runs) leave no lines to erase.
  std::size_t renderedBarCount_ = 0;
  // Declared after every member the clock touches, so destruction joins it
  // before those members die.
  std::jthread ticker_;
};

auto fitPostfixText(std::string_view text, std::size_t budget) -> std::string;

// Renders the "[<elapsed>/<estimate>]" badge: nullopt elapsed means no badge
// at all (no progress sample yet); nullopt estimate renders the "--:--"
// placeholder while the estimator is still seeding.
auto formatEtaBadge(
  std::optional<float> const& elapsedSec,
  std::optional<float> const& etaSec
) -> std::optional<std::string>;

auto fitPostfixWithEta(
  std::optional<std::string> const& etaText,
  std::string_view postfix,
  std::size_t budget
) -> std::string;

auto scrollWindow(std::string_view text, std::size_t budget, std::size_t startCol)
  -> std::string;

std::size_t bounceOffset(std::uint64_t elapsedMs, std::size_t travel);

class CursorGuard {
public:
  explicit CursorGuard(bool hideOnConstruct = true);
  CursorGuard(CursorGuard const&) = delete;
  CursorGuard& operator=(CursorGuard const&) = delete;
  CursorGuard(CursorGuard&&) = delete;
  CursorGuard& operator=(CursorGuard&&) = delete;
  ~CursorGuard();

private:
  bool active_;
};

}  // namespace progress
