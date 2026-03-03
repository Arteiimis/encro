#include "progress.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
  #include <windows.h>
#endif

namespace progress {

namespace {

constexpr auto kDefaultConsoleColumns = std::size_t{120};
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

auto truncateWithEllipsis(std::string const& text, std::size_t maxLen)
  -> std::string {
  if (maxLen == 0) { return {}; }
  if (text.size() <= maxLen) { return text; }
  if (maxLen <= 3) { return text.substr(0, maxLen); }
  return text.substr(0, maxLen - 3) + "...";
}

auto getConsoleColumns() -> std::size_t {
#if defined(_WIN32) || defined(_WIN64)
  auto const out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (out == INVALID_HANDLE_VALUE || out == nullptr) {
    return kDefaultConsoleColumns;
  }

  auto info = CONSOLE_SCREEN_BUFFER_INFO{};
  if (!GetConsoleScreenBufferInfo(out, &info)) { return kDefaultConsoleColumns; }

  auto const width =
    static_cast<std::size_t>(info.srWindow.Right - info.srWindow.Left + 1);
  return std::max(width, kMinConsoleColumns);
#else
  if (auto const* env = std::getenv("COLUMNS"); env != nullptr) {
    auto width = std::strtoul(env, nullptr, 10);
    if (width > 0) {
      return std::max(static_cast<std::size_t>(width), kMinConsoleColumns);
    }
  }
  return kDefaultConsoleColumns;
#endif
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
  auto start = std::size_t{0};
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
  auto const delimTotal = delim.size() * (parts.size() - 1);
  if (budget <= delimTotal) { return truncateWithEllipsis(trimCopy(text), budget); }

  auto contentBudget = budget - delimTotal;
  auto alloc = std::vector<std::size_t>(parts.size(), 0);

  auto totalLen = std::size_t{0};
  for (auto const& part: parts) { totalLen += part.size(); }
  if (totalLen == 0) { return truncateWithEllipsis(trimCopy(text), budget); }

  auto allocated = std::size_t{0};
  for (auto i = std::size_t{0}; i < parts.size(); ++i) {
    auto share = (parts[i].size() * contentBudget) / totalLen;
    auto const minShare = std::size_t{4};
    alloc[i] = std::min(parts[i].size(), std::max(share, minShare));
    allocated += alloc[i];
  }

  while (allocated > contentBudget) {
    auto reduced = false;
    for (auto i = std::size_t{0}; i < alloc.size() && allocated > contentBudget;
         ++i) {
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
    for (auto i = std::size_t{0}; i < alloc.size() && allocated < contentBudget;
         ++i) {
      if (alloc[i] < parts[i].size()) {
        ++alloc[i];
        ++allocated;
        grown = true;
      }
    }
    if (!grown) { break; }
  }

  auto out = std::string{};
  for (auto i = std::size_t{0}; i < parts.size(); ++i) {
    if (i > 0) { out += delim; }
    out += truncateWithEllipsis(parts[i], alloc[i]);
  }
  return truncateWithEllipsis(out, budget);
}

}  // namespace

auto ProgressContext::addBar(std::string_view promptText) -> std::size_t {
  auto lock = std::scoped_lock{mtx_};
  return progress::addBar(manager_, bars_, promptText);
}

void ProgressContext::setPostfixText(
  std::size_t barIndex,
  std::string_view promptText
) {
  auto lock = std::scoped_lock{mtx_};
  auto const layout = resolveLayout(getConsoleColumns());
  manager_[barIndex].set_option(indicators::option::BarWidth{layout.barWidth});
  manager_[barIndex].set_option(
    indicators::option::PostfixText{fitPostfixText(promptText, layout.postfixBudget)}
  );
}

void ProgressContext::setProgress(std::size_t barIndex, float progress) {
  auto lock = std::scoped_lock{mtx_};
  manager_[barIndex].set_progress(progress);
}

auto ProgressContext::manager() -> Manager& {
  return manager_;
}

auto ProgressContext::manager() const -> Manager const& {
  return manager_;
}

auto makeBar(std::string_view promptText) -> BarPtr {
  using namespace indicators;

  auto const layout = resolveLayout(getConsoleColumns());

  return std::make_unique<ProgressBar>(
    option::BarWidth{layout.barWidth},
    option::Start{"["},
    option::End{"]"},
    option::PostfixText{fitPostfixText(promptText, layout.postfixBudget)},
    option::ForegroundColor{Color::white},
    option::ShowElapsedTime{true},
    option::ShowRemainingTime{true},
    option::MaxProgress{100}
  );
}

auto addBar(Manager& manager, BarCollection& bars, std::string_view promptText)
  -> std::size_t {
  bars.emplace_back(makeBar(promptText));
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
