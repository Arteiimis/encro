#include "infra/terminal.h"

#include "infra/env.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <format>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
  #include <windows.h>  // IWYU pragma: keep -- Windows-only (guarded by _WIN32)
#else
  #include <unistd.h>
#endif

namespace terminal {

namespace {

auto g_colorMode = std::atomic<ColorMode>{ColorMode::Auto};
auto g_quiet = std::atomic<bool>{false};

// Where a kind's role lands. Prefix and LeadingVerb are the only two sites a
// message may style: wrapping prose would nest any value the caller embedded.
enum class StyleSite {
  None,
  Prefix,
  LeadingVerb,
};

struct KindStyle {
  Role role;
  StyleSite site;
};

// Every kind appears explicitly, so adding one makes -Wswitch fire rather than
// quietly taking a default.
auto kindStyleFor(MessageKind kind) -> KindStyle {
  switch (kind) {
    case MessageKind::Error  : return {Role::Bad, StyleSite::Prefix};
    case MessageKind::Warning: return {Role::Warn, StyleSite::Prefix};
    case MessageKind::Hint   : return {Role::Muted, StyleSite::Prefix};
    case MessageKind::Success:
    case MessageKind::Summary: return {Role::Good, StyleSite::LeadingVerb};
    case MessageKind::Plain  :
    case MessageKind::Info   : return {Role::Default, StyleSite::None};
  }

  return {Role::Default, StyleSite::None};
}

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
    case MessageKind::Plain  :
    case MessageKind::Success:
    case MessageKind::Info   :
    case MessageKind::Summary: return {};
    case MessageKind::Error  : return "error:";
    case MessageKind::Warning: return "warning:";
    case MessageKind::Hint   : return "hint:";
  }

  return {};
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
    case MessageKind::Success: return true;
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

auto roleStyle(Role role) -> fmt::text_style {
  using fmt::emphasis;
  using fmt::fg;
  using c = fmt::terminal_color;

  switch (role) {
    case Role::Default: return {};
    case Role::Muted  : return emphasis::faint;
    case Role::Accent : return fg(c::cyan);
    case Role::Good   : return fg(c::green);
    case Role::Warn   : return fg(c::yellow);
    case Role::Bad    : return fg(c::red);
  }

  return {};
}

auto styled(Stream stream, fmt::text_style style, std::string_view text) -> std::string {
  if (!colorsEnabled(stream)) { return std::string{text}; }
  return fmt::format(style, "{}", text);
}

auto withRole(Role role, std::string_view text, Stream stream) -> std::string {
  return styled(stream, roleStyle(role), text);
}

auto summaryCounts(
  std::size_t succeeded,
  std::size_t total,
  std::string_view noun,
  std::size_t failed,
  std::size_t skipped,
  std::string_view skippedLabel
) -> std::string {
  auto classes = std::vector<std::string>{};
  if (failed > 0) {
    classes.push_back(withRole(Role::Bad, fmt::format("{} failed", failed)));
  }
  if (skipped > 0) {
    classes.push_back(withRole(Role::Warn, fmt::format("{} {}", skipped, skippedLabel)));
  }

  auto out = fmt::format(
    "{}/{} {}",
    withRole(Role::Good, fmt::format("{}", succeeded)),
    withRole(Role::Accent, fmt::format("{}", total)),
    noun
  );
  if (classes.empty()) { return out; }

  auto joined = std::string{};
  for (auto const& part: classes) {
    if (!joined.empty()) { joined += ", "; }
    joined += part;
  }
  out += fmt::format(" ({})", joined);
  return out;
}

auto accent(std::string_view text, Stream stream) -> std::string {
  return withRole(Role::Accent, text, stream);
}

auto streamFor(MessageKind kind) -> Stream {
  switch (kind) {
    case MessageKind::Error  :
    case MessageKind::Warning:
    case MessageKind::Hint   : return Stream::Stderr;
    case MessageKind::Plain  :
    case MessageKind::Success:
    case MessageKind::Info   :
    case MessageKind::Summary: return Stream::Stdout;
  }

  return Stream::Stdout;
}

auto path(std::filesystem::path const& valuePath, Stream stream) -> std::string {
  return accent(valuePath.string(), stream);
}

auto renderMessage(Stream stream, MessageKind kind, std::string_view text)
  -> std::string {
  auto const prefix = severityPrefix(kind);
  auto const [role, site] = kindStyleFor(kind);

  // The prefix is plain text first: severity survives with colors disabled.
  if (!colorsEnabled(stream) || site == StyleSite::None) {
    return prefix.empty() ? std::string{text}
                          : std::string{prefix}.append(" ").append(text);
  }

  // Colors decorate the prefix only, never the message body.
  if (site == StyleSite::Prefix) {
    return std::string{styled(stream, roleStyle(role), prefix)}.append(" ").append(text);
  }

  // LeadingVerb: only the first word carries the role. A message that already
  // opens with a caller-styled value keeps that span — wrapping it would nest
  // one style inside another, which is what this whole design forbids.
  if (text.starts_with('\x1b')) { return std::string{text}; }

  auto const splitAt = text.find(' ');
  if (splitAt == std::string_view::npos) { return styled(stream, roleStyle(role), text); }
  return fmt::format(
    "{}{}",
    styled(stream, roleStyle(role), text.substr(0, splitAt)),
    text.substr(splitAt)
  );
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
