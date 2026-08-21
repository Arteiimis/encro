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

class EtaEstimator {
public:
  static constexpr auto kSampleInterval = std::chrono::milliseconds{250};
  static constexpr auto kEmaAlpha = 0.4f;
  static constexpr auto kMaxRatePerSec = 200.0f;
  static constexpr auto kStallDecayPerSample = 0.98f;

  void sample(std::chrono::steady_clock::time_point now, float progress);
  void reset();
  auto etaSeconds(float progress) const -> std::optional<float>;
  auto lastProgress() const -> float;

private:
  std::chrono::steady_clock::time_point lastSampleAt_{};
  float lastProgress_ = 0.0f;
  float ratePerSec_ = 0.0f;
  bool hasSample_ = false;
  bool hasRate_ = false;
};

class ProgressContext {
public:
  auto addBar(std::string_view promptText, Tone tone = Tone::Default) -> std::size_t;
  void setPostfixText(std::size_t barIndex, std::string_view promptText);
  void setProgress(std::size_t barIndex, float progress);
  void setTone(std::size_t barIndex, Tone tone);
  void resetEta(std::size_t barIndex);

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

  std::mutex mtx_;
  Manager manager_;
  BarCollection bars_;
  std::vector<Tone> tones_;
  std::vector<std::string> postfixes_;
  std::vector<EtaEstimator> etas_;
  // Bars rendered on the last render pass; bars added but never rendered
  // (all-cache-hit probe runs) leave no lines to erase.
  std::size_t renderedBarCount_ = 0;
};

auto makeBar(std::string_view promptText, Tone tone = Tone::Default) -> BarPtr;

auto fitPostfixText(std::string_view text, std::size_t budget) -> std::string;

auto fitPostfixWithEta(
  std::optional<std::string> const& etaText,
  std::string_view postfix,
  std::size_t budget
) -> std::string;

auto scrollWindow(std::string_view text, std::size_t budget, std::size_t startCol)
  -> std::string;

auto bounceOffset(std::uint64_t elapsedMs, std::size_t travel) -> std::size_t;

auto addBar(
  Manager& manager,
  BarCollection& bars,
  std::vector<Tone>& tones,
  std::string_view promptText,
  Tone tone = Tone::Default
) -> std::size_t;

void setCursorVisible(bool visible);

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
