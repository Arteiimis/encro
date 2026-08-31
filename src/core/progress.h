#pragma once

#include <indicators/dynamic_progress.hpp>
#include <indicators/progress_bar.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace progress {

enum class Tone {
  Default,
  Overall,
  Active,
  Idle,
  Packing,
  Finalizing,
  Success,
  Failure,
};

auto resolveColor(Tone tone, bool colorsEnabled = true) -> indicators::Color;

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
  std::size_t addBar(std::string_view promptText, Tone tone = Tone::Default);
  void setPostfixText(std::size_t barIndex, std::string_view promptText);
  void setProgress(std::size_t barIndex, float progress);
  void setTone(std::size_t barIndex, Tone tone);
  void resetEta(std::size_t barIndex, float elapsedBaseSec = 0.0f);
  // Real seconds spent on the bar's current task (base + time since the
  // encoding anchor); nullopt before the anchor. Read-only view for
  // diagnostics and tests.
  auto elapsedSeconds(
    std::size_t barIndex,
    std::chrono::steady_clock::time_point now
  ) const -> std::optional<float>;

  // Repaints all bars with their current scroll state without touching
  // progress values or ETA sampling; lets the postfix scroll animation
  // advance on a timer of its own, decoupled from progress data updates.
  void tick();

  // Clears the rendered bar lines from the terminal. The bars stay alive in
  // the manager (it holds references to them), so no render call may follow
  // until the context is destroyed.
  void eraseBars();

  auto manager() -> Manager&;
  auto manager() const -> Manager const&;

private:
  void applyBarText(std::size_t barIndex, float progress);
  void render();

  mutable std::mutex mtx_;
  Manager manager_;
  BarCollection bars_;
  std::vector<Tone> tones_;
  std::vector<std::string> postfixes_;
  std::vector<EtaEstimator> etas_;
  // Bars rendered on the last render pass; bars added but never rendered
  // (all-cache-hit probe runs) leave no lines to erase.
  std::size_t renderedBarCount_ = 0;
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
