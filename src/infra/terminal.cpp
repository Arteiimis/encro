#include "infra/terminal.h"

#include "infra/env.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <format>

#if defined(_WIN32) || defined(_WIN64)
  #include <windows.h>
#else
  #include <unistd.h>
#endif

namespace terminal {

namespace {

auto g_colorMode = std::atomic<ColorMode>{ColorMode::Auto};

enum class TokenKind {
  Badge,
  Value,
  Path,
};

auto toLowerCopy(std::string_view text) -> std::string {
  auto out = std::string{text};
  std::ranges::transform(out, out.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return out;
}

auto streamFile(Stream stream) -> FILE* {
  return stream == Stream::Stdout ? stdout : stderr;
}

bool envVarEquals(std::string_view name, std::string_view expected) {
  auto const value = processenv::readNonEmptyEnvVar(name);
  if (!value.has_value()) { return false; }
  return toLowerCopy(value.value()) == toLowerCopy(expected);
}

bool noColorRequested() {
  return processenv::readNonEmptyEnvVar("NO_COLOR").has_value();
}

#if defined(_WIN32) || defined(_WIN64)
bool enableVirtualTerminal(Stream stream) {
  auto const handle =
    GetStdHandle(stream == Stream::Stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE) { return false; }

  auto mode = DWORD{};
  if (!GetConsoleMode(handle, &mode)) { return false; }
  if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0u) { return true; }

  return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}
#endif

auto defaultBadgeLabel(MessageKind kind) -> std::string_view {
  switch (kind) {
    case MessageKind::Plain:
    case MessageKind::Heading:
    case MessageKind::Usage:
    case MessageKind::OptionGroup:
    case MessageKind::OptionName:
    case MessageKind::OptionDefault:
    case MessageKind::OptionDesc:
    case MessageKind::Version      : return {};
    case MessageKind::Error        : return "error";
    case MessageKind::Warning      : return "warn";
    case MessageKind::Success      : return "done";
    case MessageKind::Info         : return "info";
    case MessageKind::Hint         : return "hint";
    case MessageKind::Prompt       : return "?";
  }

  return {};
}

auto styleForToken(TokenKind kind, MessageKind messageKind) -> fmt::text_style {
  using fmt::emphasis;
  using fmt::fg;
  using c = fmt::color;

  switch (kind) {
    case TokenKind::Badge: return styleFor(messageKind) | emphasis::bold;
    case TokenKind::Value: return fg(c::floral_white) | emphasis::bold;
    case TokenKind::Path : return fg(c::light_sky_blue) | emphasis::bold;
  }

  return {};
}

auto styleToken(
  Stream stream,
  TokenKind kind,
  MessageKind messageKind,
  std::string_view text
) -> std::string {
  if (!colorsEnabled(stream)) { return std::string{text}; }
  return fmt::format(styleForToken(kind, messageKind), "{}", text);
}

}  // namespace

auto parseColorMode(std::string_view text) -> std::optional<ColorMode> {
  auto const normalized = toLowerCopy(text);
  if (normalized == "auto") { return ColorMode::Auto; }
  if (normalized == "always") { return ColorMode::Always; }
  if (normalized == "never") { return ColorMode::Never; }
  return std::nullopt;
}

bool streamIsTerminal(Stream stream) {
#if defined(_WIN32) || defined(_WIN64)
  auto const fd = _fileno(streamFile(stream));
  return fd >= 0 && _isatty(fd) != 0;
#else
  auto const fd = fileno(streamFile(stream));
  return fd >= 0 && ::isatty(fd) != 0;
#endif
}

void configure(ColorMode mode) {
  g_colorMode.store(mode, std::memory_order_release);
}

void reset() {
  configure(ColorMode::Auto);
}

auto colorMode() -> ColorMode {
  return g_colorMode.load(std::memory_order_acquire);
}

auto configureFromColorString(std::string_view colorValue) -> std::optional<std::string> {
  auto const parsed = parseColorMode(colorValue);
  if (!parsed.has_value()) {
    return std::format(
      "Invalid color mode: {}. Valid values are: auto, always, never.",
      colorValue
    );
  }
  configure(parsed.value());
  return std::nullopt;
}

bool colorsEnabled(Stream stream) {
  switch (colorMode()) {
    case ColorMode::Never : return false;
    case ColorMode::Always: return true;
    case ColorMode::Auto  : break;
  }

  if (noColorRequested()) { return false; }
  if (!streamIsTerminal(stream)) { return false; }

#if defined(_WIN32) || defined(_WIN64)
  return enableVirtualTerminal(stream);
#else
  if (envVarEquals("TERM", "dumb")) { return false; }
  return true;
#endif
}

auto styleFor(MessageKind kind) -> fmt::text_style {
  using fmt::emphasis;
  using fmt::fg;
  using c = fmt::color;
  using tc = fmt::terminal_color;

  switch (kind) {
    case MessageKind::Plain        : return {};
    case MessageKind::Error        : return fg(tc::red) | emphasis::bold;
    case MessageKind::Warning      : return fg(tc::yellow) | emphasis::bold;
    case MessageKind::Success      : return fg(tc::green) | emphasis::bold;
    case MessageKind::Info         : return fg(c::steel_blue);
    case MessageKind::Hint         : return fg(c::slate_gray);
    case MessageKind::Prompt       : return fg(tc::cyan) | emphasis::bold;
    case MessageKind::Heading      : return fg(c::steel_blue) | emphasis::bold;
    case MessageKind::Usage        : return {};
    case MessageKind::OptionGroup  : return emphasis::bold;
    case MessageKind::OptionName   : return fg(tc::cyan);
    case MessageKind::OptionDefault: return fg(tc::cyan) | emphasis::faint;
    case MessageKind::OptionDesc   :
    case MessageKind::Version      : return {};
  }

  return {};
}

auto styledText(Stream stream, MessageKind kind, std::string_view text) -> std::string {
  if (kind == MessageKind::Plain || !colorsEnabled(stream)) { return std::string{text}; }
  return fmt::format(styleFor(kind), "{}", text);
}

auto badge(MessageKind kind, std::string_view label, Stream stream) -> std::string {
  return styleToken(stream, TokenKind::Badge, kind, fmt::format("[{}]", label));
}

auto value(std::string_view text, Stream stream) -> std::string {
  return styleToken(stream, TokenKind::Value, MessageKind::Plain, text);
}

auto path(std::filesystem::path const& valuePath, Stream stream) -> std::string {
  return styleToken(stream, TokenKind::Path, MessageKind::Plain, valuePath.string());
}

auto renderMessage(Stream stream, MessageKind kind, std::string_view text)
  -> std::string {
  if (kind == MessageKind::Plain || !colorsEnabled(stream)) { return std::string{text}; }

  if (kind == MessageKind::Heading) { return styledText(stream, kind, text); }

  auto const badgeLabel = defaultBadgeLabel(kind);
  if (badgeLabel.empty()) { return styledText(stream, kind, text); }

  return fmt::format("{} {}", badge(kind, badgeLabel, stream), text);
}

void write(Stream stream, std::string_view text, bool newline) {
  auto* file = streamFile(stream);
  if (newline) {
    fmt::print(file, "{}\n", text);
  } else {
    fmt::print(file, "{}", text);
  }
}

}  // namespace terminal
