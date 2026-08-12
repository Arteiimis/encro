#include "progress.h"

#include "core/display_text.h"
#include "infra/console_width.h"
#include "infra/terminal.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <print>
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

// Bars are terminal-only UI: when stdout is not a TTY (pipes, test runs,
// CI) they render into this null sink instead of std::cout.
class NullStreamBuffer final: public std::streambuf {
public:
  auto overflow(int character) -> int override { return character; }
};

auto barOutputStream() -> std::ostream& {
  static auto buffer = NullStreamBuffer{};
  static auto stream = std::ostream{&buffer};
  return terminal::streamIsTerminal(terminal::Stream::Stdout) ? std::cout : stream;
}
constexpr auto kMinPostfixBudget = std::size_t{16};
constexpr auto kReservedLayoutWidth = std::size_t{24};
constexpr auto kScrollColsPerSec = std::size_t{8};
constexpr auto kScrollPauseMs = std::uint64_t{1000};
constexpr auto kMsPerCol = std::uint64_t{1000 / kScrollColsPerSec};
constexpr auto kMinScrollHeadBudget = std::size_t{12};
constexpr auto kPostfixDelim = std::string_view{" | "};
constexpr auto kPostfixDelimWidth = std::size_t{3};

auto trimCopy(std::string_view text) -> std::string {
  auto begin = text.find_first_not_of(" \t");
  if (begin == std::string_view::npos) { return {}; }
  auto end = text.find_last_not_of(" \t");
  return std::string{text.substr(begin, end - begin + 1)};
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

auto scrollWindow(std::string_view text, std::size_t budget, std::size_t startCol)
  -> std::string {
  auto const textWidth = displaytext::displayWidth(text);
  if (textWidth <= budget) { return std::string{text}; }

  auto const maxCol = textWidth - budget;
  auto const clamped = std::min(startCol, maxCol);
  return displaytext::takeWindowByDisplayWidth(text, clamped, budget);
}

auto bounceOffset(std::uint64_t elapsedMs, std::size_t travel) -> std::size_t {
  if (travel == 0) { return 0; }

  auto const sweepMs = static_cast<std::uint64_t>(travel) * kMsPerCol;
  auto const period = sweepMs * 2 + kScrollPauseMs * 2;
  auto const t = elapsedMs % period;
  if (t < sweepMs) { return static_cast<std::size_t>(t / kMsPerCol); }
  if (t < sweepMs + kScrollPauseMs) { return travel; }
  if (t < sweepMs * 2 + kScrollPauseMs) {
    auto const back = t - sweepMs - kScrollPauseMs;
    return travel - static_cast<std::size_t>(back / kMsPerCol);
  }
  return 0;
}

auto fitPostfixText(std::string_view text, std::size_t budget) -> std::string {
  if (budget == 0) { return {}; }
  if (displaytext::displayWidth(text) <= budget) { return std::string{text}; }

  auto const elapsedMs =
    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()
                                                            .time_since_epoch())
      .count();
  auto const travel = displaytext::displayWidth(text) - budget;
  // ponytail: time-based trapezoid — bounce offset without per-bar state; a
  // position+velocity state would desync from wall clock on redraw gaps
  auto const offset = bounceOffset(static_cast<std::uint64_t>(elapsedMs), travel);
  return scrollWindow(text, budget, offset);
}

auto fitPostfixWithEta(
  std::optional<std::string> const& etaText,
  std::string_view postfix,
  std::size_t budget
) -> std::string {
  if (postfix.empty()) {
    if (!etaText.has_value()) { return {}; }
    return fitPostfixText(etaText.value(), budget);
  }

  auto const parts = splitPostfixParts(postfix);
  auto const headPart = parts.front();
  auto tailPart = std::string{};
  for (auto i = 1ull; i < parts.size(); ++i) {
    if (i > 1) { tailPart += kPostfixDelim; }
    tailPart += parts[i];
  }

  auto const etaWidth =
    etaText.has_value() ? displaytext::displayWidth(etaText.value()) : std::size_t{0};
  auto const fixedPrefixWidth = etaWidth + (etaText.has_value() ? kPostfixDelimWidth : 0);

  auto tailWidth = displaytext::displayWidth(tailPart);
  auto tailDelimWidth = tailPart.empty() ? std::size_t{0} : kPostfixDelimWidth;

  auto scrollBudget = std::size_t{0};
  if (budget > fixedPrefixWidth + tailDelimWidth + tailWidth) {
    scrollBudget = budget - fixedPrefixWidth - tailDelimWidth - tailWidth;
  }
  if (scrollBudget < kMinScrollHeadBudget) {
    auto const availForTail =
      budget >= fixedPrefixWidth + kMinScrollHeadBudget + kPostfixDelimWidth
      ? budget - fixedPrefixWidth - kMinScrollHeadBudget - kPostfixDelimWidth
      : std::size_t{0};
    if (availForTail == 0) {
      tailPart.clear();
    } else {
      tailPart = displaytext::truncateWithEllipsis(tailPart, availForTail);
    }
    if (tailPart.empty()) {
      tailWidth = 0;
      tailDelimWidth = 0;
      scrollBudget = budget >= fixedPrefixWidth ? budget - fixedPrefixWidth : 0;
    } else {
      tailWidth = displaytext::displayWidth(tailPart);
      tailDelimWidth = kPostfixDelimWidth;
      scrollBudget = budget - fixedPrefixWidth - tailDelimWidth - tailWidth;
    }
  }

  auto out = std::string{};
  if (etaText.has_value()) { out += etaText.value() + std::string{kPostfixDelim}; }
  out += fitPostfixText(headPart, scrollBudget);
  if (!tailPart.empty()) { out += std::string{kPostfixDelim} + tailPart; }
  return out;
}

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
    lastProgress_ = progress;
  }
}

void EtaEstimator::reset() {
  lastSampleAt_ = {};
  lastProgress_ = 0.0f;
  ratePerSec_ = 0.0f;
  hasSample_ = false;
  hasRate_ = false;
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
  auto etaText = std::optional<std::string>{};
  if (auto const eta = etas_[barIndex].etaSeconds(progress); eta.has_value()) {
    etaText = std::format("[{}]", formatEtaPart(eta.value()));
  }

  auto const layout = resolveLayout(
    consolewidth::resolveColumns({
      .defaultColumns = 120,
      .minColumns = kMinConsoleColumns,
    })
  );
  bars_[barIndex]->set_option(indicators::option::BarWidth{layout.barWidth});
  bars_[barIndex]->set_option(
    indicators::option::PostfixText{
      fitPostfixWithEta(etaText, postfixes_[barIndex], layout.postfixBudget)
    }
  );
}

void ProgressContext::setPostfixText(std::size_t barIndex, std::string_view promptText) {
  auto lock = std::scoped_lock{mtx_};
  postfixes_[barIndex] = std::string{promptText};
  applyBarText(barIndex, etas_[barIndex].lastProgress());
  render();
}

void ProgressContext::setProgress(std::size_t barIndex, float progress) {
  auto lock = std::scoped_lock{mtx_};
  etas_[barIndex].sample(std::chrono::steady_clock::now(), progress);
  bars_[barIndex]->set_progress(static_cast<std::size_t>(progress));
  applyBarText(barIndex, progress);
  render();
}

void ProgressContext::resetEta(std::size_t barIndex) {
  auto lock = std::scoped_lock{mtx_};
  etas_[barIndex].reset();
}

void ProgressContext::setTone(std::size_t barIndex, Tone tone) {
  auto lock = std::scoped_lock{mtx_};
  if (tones_[barIndex] == tone) { return; }

  tones_[barIndex] = tone;
  applyTone(*bars_[barIndex], tone);
  render();
}

void ProgressContext::render() {
  // Non-TTY stdout: skip the render pass entirely. This gate is load-bearing
  // even with the null sink above — DynamicProgress::print_progress writes
  // newlines/cursor escapes directly to std::cout, bypassing per-bar streams.
  if (!terminal::streamIsTerminal(terminal::Stream::Stdout)) { return; }
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
    option::MaxProgress{100},
    option::Stream{barOutputStream()}
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
  // Cursor control is terminal UI: never emit escape sequences when stdout
  // is not a TTY (pipes, test runs, CI) -- they pollute captured output.
  if (!terminal::streamIsTerminal(terminal::Stream::Stdout)) { return; }
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
