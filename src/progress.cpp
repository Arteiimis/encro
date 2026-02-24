#include "progress.h"

#include <print>

namespace progress {

auto makeBar(std::string_view promptText) -> BarPtr {
  using namespace indicators;

  return std::make_unique<ProgressBar>(
    option::BarWidth{50},
    option::Start{"["},
    option::End{"]"},
    option::PostfixText{promptText},
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

#if defined(_WIN32) || defined(_WIN64)
  #include <windows.h>
#endif

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
