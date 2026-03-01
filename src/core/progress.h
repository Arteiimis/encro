#pragma once

#include <indicators/dynamic_progress.hpp>
#include <indicators/progress_bar.hpp>

#include <cstddef>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace progress {

using Manager = indicators::DynamicProgress<indicators::ProgressBar>;
using BarPtr = std::unique_ptr<indicators::ProgressBar>;
using BarCollection = std::vector<BarPtr>;

class ProgressContext {
public:
  auto addBar(std::string_view promptText) -> std::size_t;
  void setPostfixText(std::size_t barIndex, std::string_view promptText);
  void setProgress(std::size_t barIndex, float progress);

  auto manager() -> Manager&;
  auto manager() const -> Manager const&;

private:
  std::mutex mtx_;
  Manager manager_;
  BarCollection bars_;
};

auto makeBar(std::string_view promptText) -> BarPtr;

auto addBar(Manager& manager, BarCollection& bars, std::string_view promptText)
  -> std::size_t;

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
