#include "infra/console_width.h"

#include "infra/env.h"

#include <algorithm>
#include <charconv>
#include <string>
#include <string_view>

#if defined(_WIN32) || defined(_WIN64)
  #include <windows.h>
#else
  #include <sys/ioctl.h>
  #include <unistd.h>
#endif

namespace consolewidth {

auto parsePositiveColumnCount(char const* text) -> std::optional<std::size_t> {
  if (text == nullptr || *text == '\0') { return std::nullopt; }

  auto const view = std::string_view{text};
  auto value = std::size_t{0};
  auto const [end, error] =
    std::from_chars(view.data(), view.data() + view.size(), value);
  if (error != std::errc{} || end != view.data() + view.size() || value == 0) {
    return std::nullopt;
  }

  return value;
}

namespace {

auto detectRawColumns() -> std::optional<std::size_t> {
  if (auto const env = processenv::readNonEmptyEnvVar("COLUMNS"); env.has_value()) {
    if (auto const parsed = parsePositiveColumnCount(env->c_str()); parsed.has_value()) {
      return parsed;
    }
  }

#if defined(_WIN32) || defined(_WIN64)
  auto const out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (out == INVALID_HANDLE_VALUE || out == nullptr) { return std::nullopt; }

  auto info = CONSOLE_SCREEN_BUFFER_INFO{};
  if (!GetConsoleScreenBufferInfo(out, &info)) { return std::nullopt; }

  auto const width = info.srWindow.Right - info.srWindow.Left + 1;
  if (width <= 0) { return std::nullopt; }
  return static_cast<std::size_t>(width);
#else
  auto info = winsize{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &info) != 0 || info.ws_col == 0) {
    return std::nullopt;
  }

  return static_cast<std::size_t>(info.ws_col);
#endif
}

}  // namespace

std::size_t resolveColumns(Config const& config) {
  auto const minColumns = std::max(config.minColumns, std::size_t{1});

  auto columns = detectRawColumns().value_or(std::max(config.defaultColumns, minColumns));
  columns = std::max(columns, minColumns);

  if (config.maxColumns.has_value()) {
    auto const maxColumns = std::max(config.maxColumns.value(), minColumns);
    columns = std::min(columns, maxColumns);
  }

  return columns;
}

}  // namespace consolewidth
