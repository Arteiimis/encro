#pragma once

#include <indicators/dynamic_progress.hpp>
#include <indicators/progress_bar.hpp>

#include <cstddef>
#include <memory>
#include <mutex>
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

class ProgressContext {
public:
  auto addBar(std::string_view promptText, Tone tone = Tone::Default) -> std::size_t;
  void setPostfixText(std::size_t barIndex, std::string_view promptText);
  void setProgress(std::size_t barIndex, float progress);
  void setTone(std::size_t barIndex, Tone tone);

  auto manager() -> Manager&;
  auto manager() const -> Manager const&;

private:
  std::mutex mtx_;
  Manager manager_;
  BarCollection bars_;
  std::vector<Tone> tones_;
};

auto makeBar(std::string_view promptText, Tone tone = Tone::Default) -> BarPtr;

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
