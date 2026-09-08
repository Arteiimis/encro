#include "infra/terminal.h"

#include "infra/env.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <format>

#if defined(_WIN32) || defined(_WIN64)
  #include <windows.h>  // IWYU pragma: keep -- Windows-only (guarded by _WIN32)
#else
  #include <unistd.h>
#endif

namespace terminal {

namespace {

auto g_colorMode = std::atomic<ColorMode>{ColorMode::Auto};
auto g_quiet = std::atomic<bool>{false};

enum class TokenKind {
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

auto severityPrefix(MessageKind kind) -> std::string_view {
  switch (kind) {
    case MessageKind::Plain        :
    case MessageKind::Success      :
    case MessageKind::Info         :
    case MessageKind::Summary      :
    case MessageKind::Prompt       :
    case MessageKind::Heading      :
    case MessageKind::Usage        :
    case MessageKind::OptionGroup  :
    case MessageKind::OptionName   :
    case MessageKind::OptionDefault:
    case MessageKind::OptionDesc   :
    case MessageKind::Version      : return {};
    case MessageKind::Error        : return "error:";
    case MessageKind::Warning      : return "warning:";
    case MessageKind::Hint         : return "hint:";
  }

  return {};
}

auto styleForToken(TokenKind kind) -> fmt::text_style {
  using fmt::fg;
  using c = fmt::color;

  switch (kind) {
    case TokenKind::Value: return fg(c::floral_white);
    case TokenKind::Path : return fg(c::light_sky_blue);
  }

  return {};
}

auto styleToken(Stream stream, TokenKind kind, std::string_view text) -> std::string {
  if (!colorsEnabled(stream)) { return std::string{text}; }
  return fmt::format(styleForToken(kind), "{}", text);
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
  setQuiet(false);
}

void setQuiet(bool quiet) {
  g_quiet.store(quiet, std::memory_order_release);
}

bool quiet() {
  return g_quiet.load(std::memory_order_acquire);
}

bool suppressedByQuiet(MessageKind kind) {
  switch (kind) {
    case MessageKind::Info   :
    case MessageKind::Success:
    case MessageKind::Heading: return true;
    default                  : return false;
  }
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
    case MessageKind::Error        : return fg(tc::red);
    case MessageKind::Warning      : return fg(tc::yellow);
    case MessageKind::Success      : return fg(tc::green);
    case MessageKind::Info         : return fg(c::steel_blue);
    case MessageKind::Summary      : return fg(c::steel_blue);
    case MessageKind::Hint         : return fg(c::slate_gray);
    case MessageKind::Prompt       : return fg(tc::cyan);
    case MessageKind::Heading      : return fg(c::steel_blue);
    case MessageKind::Usage        : return {};
    case MessageKind::OptionGroup  : return {};
    case MessageKind::OptionName   : return fg(tc::cyan);
    case MessageKind::OptionDefault: return fg(tc::cyan) | emphasis::faint;
    case MessageKind::OptionDesc   :
    case MessageKind::Version      : return {};
  }

  return {};
}

auto streamFor(MessageKind kind) -> Stream {
  switch (kind) {
    case MessageKind::Error        :
    case MessageKind::Warning      :
    case MessageKind::Hint         : return Stream::Stderr;
    case MessageKind::Plain        :
    case MessageKind::Success      :
    case MessageKind::Info         :
    case MessageKind::Summary      :
    case MessageKind::Prompt       :
    case MessageKind::Heading      :
    case MessageKind::Usage        :
    case MessageKind::OptionGroup  :
    case MessageKind::OptionName   :
    case MessageKind::OptionDefault:
    case MessageKind::OptionDesc   :
    case MessageKind::Version      : return Stream::Stdout;
  }

  return Stream::Stdout;
}

auto styledText(Stream stream, MessageKind kind, std::string_view text) -> std::string {
  if (kind == MessageKind::Plain || !colorsEnabled(stream)) { return std::string{text}; }
  return fmt::format(styleFor(kind), "{}", text);
}

auto value(std::string_view text, Stream stream) -> std::string {
  return styleToken(stream, TokenKind::Value, text);
}

auto path(std::filesystem::path const& valuePath, Stream stream) -> std::string {
  return styleToken(stream, TokenKind::Path, valuePath.string());
}

auto renderMessage(Stream stream, MessageKind kind, std::string_view text)
  -> std::string {
  if (kind == MessageKind::Plain) { return std::string{text}; }

  auto const prefix = severityPrefix(kind);
  if (prefix.empty()) { return styledText(stream, kind, text); }

  // The prefix is plain text first: severity survives with colors disabled.
  // Colors decorate the prefix only, never the message body.
  if (!colorsEnabled(stream)) { return std::string{prefix}.append(" ").append(text); }
  return fmt::format("{} {}", fmt::format(styleFor(kind), "{}", prefix), text);
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
