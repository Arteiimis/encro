#include "progress.h"

#include "core/display_text.h"
#include "infra/console_width.h"
#include "infra/terminal.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace progress {

auto resolveColor(Tone tone, bool colorsEnabled) -> indicators::Color {
  using indicators::Color;

  if (!colorsEnabled) { return Color::white; }

  switch (tone) {
    case Tone::Default:
    case Tone::Active    : return Color::cyan;
    case Tone::Overall   : return Color::blue;
    case Tone::Idle      : return Color::white;
    case Tone::Packing   :
    case Tone::Finalizing: return Color::yellow;
    case Tone::Success   : return Color::green;
    case Tone::Failure   : return Color::red;
  }

  return Color::white;
}

namespace {

constexpr auto kMinConsoleColumns = std::size_t{60};
constexpr auto kMinBarWidth = std::size_t{18};
constexpr auto kMaxBarWidth = std::size_t{52};
constexpr auto kMinPostfixBudget = std::size_t{16};
constexpr auto kReservedLayoutWidth = std::size_t{34};

auto trimCopy(std::string_view text) -> std::string {
  auto begin = text.find_first_not_of(" \t");
  if (begin == std::string_view::npos) { return {}; }
  auto end = text.find_last_not_of(" \t");
  return std::string{text.substr(begin, end - begin + 1)};
}

auto truncateWithEllipsis(std::string const& text, std::size_t maxLen) -> std::string {
  return displaytext::truncateWithEllipsis(text, maxLen);
}

struct ProgressLayout {
  std::size_t barWidth;
  std::size_t postfixBudget;
};

auto resolveLayout(std::size_t columns) -> ProgressLayout {
  auto const clampedColumns = std::max(columns, kMinConsoleColumns);
  auto barWidth = std::clamp(clampedColumns / 3, kMinBarWidth, kMaxBarWidth);

  auto postfixBudget = clampedColumns > barWidth + kReservedLayoutWidth
    ? clampedColumns - barWidth - kReservedLayoutWidth
    : kMinPostfixBudget;

  postfixBudget = std::max(postfixBudget, kMinPostfixBudget);
  return {barWidth, postfixBudget};
}

auto splitPostfixParts(std::string_view text) -> std::vector<std::string> {
  auto parts = std::vector<std::string>{};
  auto start = 0ull;
  while (start <= text.size()) {
    auto const pos = text.find('|', start);
    if (pos == std::string_view::npos) {
      parts.push_back(trimCopy(text.substr(start)));
      break;
    }
    parts.push_back(trimCopy(text.substr(start, pos - start)));
    start = pos + 1;
  }
  if (parts.empty()) { parts.push_back(trimCopy(text)); }
  return parts;
}

auto fitPostfixText(std::string_view text, std::size_t budget) -> std::string {
  if (budget == 0) { return {}; }

  auto parts = splitPostfixParts(text);
  if (parts.size() == 1) { return truncateWithEllipsis(parts.front(), budget); }

  constexpr auto delim = std::string_view{" | "};
  auto const delimTotal = displaytext::displayWidth(delim) * (parts.size() - 1);
  if (budget <= delimTotal) { return truncateWithEllipsis(trimCopy(text), budget); }

  auto contentBudget = budget - delimTotal;
  auto alloc = std::vector<std::size_t>(parts.size(), 0);

  auto totalLen = 0ull;
  for (auto const& part: parts) { totalLen += displaytext::displayWidth(part); }
  if (totalLen == 0) { return truncateWithEllipsis(trimCopy(text), budget); }

  auto allocated = 0ull;
  auto const firstWidth = displaytext::displayWidth(parts.front());
  alloc.front() = std::min(firstWidth, contentBudget);
  allocated += alloc.front();
  auto const restLen = totalLen > firstWidth ? totalLen - firstWidth : 0;
  auto const restBudget =
    contentBudget > alloc.front() ? contentBudget - alloc.front() : 0;
  for (auto i = 1ull; i < parts.size() && restLen > 0; ++i) {
    auto const partWidth = displaytext::displayWidth(parts[i]);
    auto share = (partWidth * restBudget) / restLen;
    auto const minShare = std::size_t{4};
    alloc[i] = std::min(partWidth, std::max(share, minShare));
    allocated += alloc[i];
  }

  while (allocated > contentBudget) {
    auto reduced = false;
    for (auto i = 1ull; i < alloc.size() && allocated > contentBudget; ++i) {
      if (alloc[i] > 4) {
        --alloc[i];
        --allocated;
        reduced = true;
      }
    }
    if (!reduced) { break; }
  }

  while (allocated < contentBudget) {
    auto grown = false;
    for (auto i = 0ull; i < alloc.size() && allocated < contentBudget; ++i) {
      if (alloc[i] < displaytext::displayWidth(parts[i])) {
        ++alloc[i];
        ++allocated;
        grown = true;
      }
    }
    if (!grown) { break; }
  }

  auto const lastIndex = parts.size() - 1;
  auto const lastWidth = displaytext::displayWidth(parts[lastIndex]);
  if (alloc[lastIndex] < lastWidth) {
    auto need = lastWidth - alloc[lastIndex];
    for (auto i = 1ull; i < lastIndex && need > 0; ++i) {
      if (alloc[i] > 4) {
        auto const take = std::min(need, alloc[i] - 4);
        alloc[i] -= take;
        need -= take;
      }
    }
    if (need == 0) { alloc[lastIndex] = lastWidth; }
  }

  auto out = std::string{};
  for (auto i = 0ull; i < parts.size(); ++i) {
    if (i > 0) { out += delim; }
    out += truncateWithEllipsis(parts[i], alloc[i]);
  }
  return truncateWithEllipsis(out, budget);
}

void applyTone(indicators::ProgressBar& bar, Tone tone) {
  bar.set_option(
    indicators::option::ForegroundColor{resolveColor(tone, terminal::colorsEnabled())}
  );
}

auto formatEtaPart(float etaSeconds) -> std::string {
  auto const etaInt = static_cast<std::int64_t>(std::ceil(etaSeconds));
  if (etaInt >= 3600) {
    return std::format("ETA {:d}h:{:02d}m", etaInt / 3600, (etaInt % 3600) / 60);
  }
  return std::format("ETA {:02d}m:{:02d}s", etaInt / 60, etaInt % 60);
}

}  // namespace

void EtaEstimator::sample(std::chrono::steady_clock::time_point now, float progress) {
  if (!hasSample_) {
    lastSampleAt_ = now;
    lastProgress_ = progress;
    hasSample_ = true;
    return;
  }

  auto const dtSec = std::chrono::duration<double>(now - lastSampleAt_).count();
  if (dtSec * 1000.0 >= static_cast<double>(kSampleInterval.count())) {
    auto const inst = (progress - lastProgress_) / static_cast<float>(dtSec);
    if (inst > 0.0f && inst <= kMaxRatePerSec) {
      ratePerSec_ = hasRate_ ? kEmaAlpha * inst + (1.0f - kEmaAlpha) * ratePerSec_ : inst;
      hasRate_ = true;
    } else if (inst <= 0.0f && hasRate_) {
      ratePerSec_ *= kStallDecayPerSample;
    }
    lastSampleAt_ = now;
  }

  lastProgress_ = progress;
}

auto EtaEstimator::etaSeconds(float progress) const -> std::optional<float> {
  if (!hasRate_ || progress <= 0.0f || progress >= 100.0f) { return std::nullopt; }
  return (100.0f - progress) / std::max(ratePerSec_, 0.01f);
}

auto EtaEstimator::lastProgress() const -> float {
  return lastProgress_;
}

auto ProgressContext::addBar(std::string_view promptText, Tone tone) -> std::size_t {
  auto lock = std::scoped_lock{mtx_};
  auto const index = progress::addBar(manager_, bars_, tones_, promptText, tone);
  postfixes_.emplace_back(promptText);
  etas_.emplace_back();
  return index;
}

void ProgressContext::applyBarText(std::size_t barIndex, float progress) {
  auto text = postfixes_[barIndex];
  if (auto const eta = etas_[barIndex].etaSeconds(progress); eta.has_value()) {
    text = std::format("[{}] | {}", formatEtaPart(eta.value()), text);
  }

  auto const layout = resolveLayout(
    consolewidth::resolveColumns({
      .defaultColumns = 120,
      .minColumns = kMinConsoleColumns,
    })
  );
  bars_[barIndex]->set_option(indicators::option::BarWidth{layout.barWidth});
  bars_[barIndex]->set_option(
    indicators::option::PostfixText{fitPostfixText(text, layout.postfixBudget)}
  );
}

void ProgressContext::setPostfixText(std::size_t barIndex, std::string_view promptText) {
  auto lock = std::scoped_lock{mtx_};
  postfixes_[barIndex] = std::string{promptText};
  applyBarText(barIndex, etas_[barIndex].lastProgress());
  manager_.print_progress();
}

void ProgressContext::setProgress(std::size_t barIndex, float progress) {
  auto lock = std::scoped_lock{mtx_};
  etas_[barIndex].sample(std::chrono::steady_clock::now(), progress);
  bars_[barIndex]->set_progress(static_cast<std::size_t>(progress));
  applyBarText(barIndex, progress);
  manager_.print_progress();
}

void ProgressContext::setTone(std::size_t barIndex, Tone tone) {
  auto lock = std::scoped_lock{mtx_};
  if (tones_[barIndex] == tone) { return; }

  tones_[barIndex] = tone;
  applyTone(*bars_[barIndex], tone);
  manager_.print_progress();
}

auto ProgressContext::manager() -> Manager& {
  return manager_;
}

auto ProgressContext::manager() const -> Manager const& {
  return manager_;
}

auto makeBar(std::string_view promptText, Tone tone) -> BarPtr {
  using namespace indicators;

  auto const layout = resolveLayout(
    consolewidth::resolveColumns({
      .defaultColumns = 120,
      .minColumns = kMinConsoleColumns,
    })
  );

  return std::make_unique<ProgressBar>(
    option::BarWidth{layout.barWidth},
    option::Start{"["},
    option::End{"]"},
    option::PostfixText{fitPostfixText(promptText, layout.postfixBudget)},
    option::ForegroundColor{resolveColor(tone, terminal::colorsEnabled())},
    option::ShowRemainingTime{false},
    option::MaxProgress{100}
  );
}

auto addBar(
  Manager& manager,
  BarCollection& bars,
  std::vector<Tone>& tones,
  std::string_view promptText,
  Tone tone
) -> std::size_t {
  bars.emplace_back(makeBar(promptText, tone));
  tones.push_back(tone);
  return manager.push_back(*bars.back());
}

void setCursorVisible(bool visible) {
#if defined(_WIN32) || defined(_WIN64)
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_CURSOR_INFO cursorInfo;

  GetConsoleCursorInfo(hConsole, &cursorInfo);
  cursorInfo.bVisible = visible;
  SetConsoleCursorInfo(hConsole, &cursorInfo);
#else
  if (visible) {
    std::print("\033[?25h");
  } else {
    std::print("\033[?25l");
  }
#endif
}

CursorGuard::CursorGuard(bool hideOnConstruct): active_(hideOnConstruct) {
  if (active_) { setCursorVisible(false); }
}

CursorGuard::~CursorGuard() {
  if (active_) { setCursorVisible(true); }
}

}  // namespace progress
