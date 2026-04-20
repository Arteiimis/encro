#include "infra/console_width.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <string>
#include <string_view>

#if defined(_WIN32) || defined(_WIN64)
  #include <windows.h>
#else
  #include <sys/ioctl.h>
  #include <unistd.h>
#endif

namespace consolewidth {

namespace {

auto readEnvVar(std::string_view name) -> std::optional<std::string> {
#if defined(_WIN32) || defined(_WIN64)
  auto* value = static_cast<char*>(nullptr);
  auto len = std::size_t{0};
  auto const nameText = std::string{name};
  if (_dupenv_s(&value, &len, nameText.c_str()) != 0 || value == nullptr) {
    return std::nullopt;
  }

  auto result = std::optional<std::string>{};
  if (len > 1) { result = std::string{value}; }
  std::free(value);
  return result;
#else
  auto const nameText = std::string{name};
  if (auto const* value = std::getenv(nameText.c_str()); value != nullptr) {
    return std::string{value};
  }
  return std::nullopt;
#endif
}

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

auto detectRawColumns() -> std::optional<std::size_t> {
  if (auto const env = readEnvVar("COLUMNS"); env.has_value()) {
    if (auto const parsed = parsePositiveColumnCount(env->c_str()); parsed.has_value()) {
      return parsed;
    }
  }

#if defined(_WIN32) || defined(_WIN64)
  auto const out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (out == INVALID_HANDLE_VALUE || out == nullptr) { return std::nullopt; }

  auto info = CONSOLE_SCREEN_BUFFER_INFO{};
  if (!GetConsoleScreenBufferInfo(out, &info)) { return std::nullopt; }

  auto const width =
    static_cast<std::size_t>(info.srWindow.Right - info.srWindow.Left + 1);
  if (width == 0) { return std::nullopt; }
  return width;
#else
  auto info = winsize{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &info) != 0 || info.ws_col == 0) {
    return std::nullopt;
  }

  return static_cast<std::size_t>(info.ws_col);
#endif
}

}  // namespace

auto resolveColumns(Config const& config) -> std::size_t {
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
