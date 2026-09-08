#pragma once

#include <fmt/color.h>
#include <fmt/format.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace terminal {

enum class Stream {
  Stdout,
  Stderr,
};

enum class ColorMode {
  Auto,
  Always,
  Never,
};

enum class MessageKind {
  Plain,
  Error,
  Warning,
  Success,
  Info,
  Summary,
  Hint,
  Prompt,
  Heading,
  // ── Phase 20 additions ──
  Usage,
  OptionGroup,
  OptionName,
  OptionDefault,
  OptionDesc,
  Version,
};

auto parseColorMode(std::string_view text) -> std::optional<ColorMode>;

bool streamIsTerminal(Stream stream);

void configure(ColorMode mode);

// Quiet mode (--quiet): narration lines (Info/Success/Heading) are suppressed
// at the kind-dispatched entry points; severity diagnostics
// (error/warning/hint), run summaries, failure lists, product output (Plain),
// prompts, help, and version bypass the gate.
void setQuiet(bool quiet);
bool quiet();

// True for narration kinds the quiet gate suppresses (not severity, run
// summaries, product output, prompts, or help tokens).
bool suppressedByQuiet(MessageKind kind);

void reset();

auto colorMode() -> ColorMode;

auto configureFromColorString(std::string_view colorValue) -> std::optional<std::string>;

bool colorsEnabled(Stream stream = Stream::Stdout);

auto styleFor(MessageKind kind) -> fmt::text_style;

auto streamFor(MessageKind kind) -> Stream;

auto styledText(Stream stream, MessageKind kind, std::string_view text) -> std::string;

auto value(std::string_view text, Stream stream = Stream::Stdout) -> std::string;

auto path(std::filesystem::path const& value, Stream stream = Stream::Stdout)
  -> std::string;

auto renderMessage(Stream stream, MessageKind kind, std::string_view text) -> std::string;

void write(Stream stream, std::string_view text, bool newline);

template<class... Tys>
auto value(fmt::format_string<Tys...> fmtText, Tys&&... args) -> std::string {
  return value(fmt::format(fmtText, std::forward<Tys>(args)...));
}

template<class Ty>
auto count(Ty const& number, Stream stream = Stream::Stdout) -> std::string {
  if (!colorsEnabled(stream)) { return fmt::format("{}", number); }
  return fmt::format(fmt::fg(fmt::color::golden_rod), "{}", number);
}

template<class... Tys>
auto format(
  Stream stream,
  MessageKind kind,
  fmt::format_string<Tys...> fmtText,
  Tys&&... args
) -> std::string {
  auto const message = fmt::format(fmtText, std::forward<Tys>(args)...);
  return renderMessage(stream, kind, message);
}

// True when the quiet gate suppresses this narration kind right now.
inline bool quietSuppresses(MessageKind kind) {
  return quiet() && suppressedByQuiet(kind);
}

template<class... Tys>
void print(MessageKind kind, fmt::format_string<Tys...> fmtText, Tys&&... args) {
  if (quietSuppresses(kind)) { return; }
  write(
    Stream::Stdout,
    format(Stream::Stdout, kind, fmtText, std::forward<Tys>(args)...),
    false
  );
}

template<class... Tys>
void println(MessageKind kind, fmt::format_string<Tys...> fmtText, Tys&&... args) {
  if (quietSuppresses(kind)) { return; }
  write(
    Stream::Stdout,
    format(Stream::Stdout, kind, fmtText, std::forward<Tys>(args)...),
    true
  );
}

template<class... Tys>
void eprint(MessageKind kind, fmt::format_string<Tys...> fmtText, Tys&&... args) {
  write(
    Stream::Stderr,
    format(Stream::Stderr, kind, fmtText, std::forward<Tys>(args)...),
    false
  );
}

template<class... Tys>
void eprintln(MessageKind kind, fmt::format_string<Tys...> fmtText, Tys&&... args) {
  write(
    Stream::Stderr,
    format(Stream::Stderr, kind, fmtText, std::forward<Tys>(args)...),
    true
  );
}

// Kind-dispatched entry points: the message kind decides both rendering and
// the target stream (see streamFor). Severity diagnostics (Error/Warning/Hint)
// land on stderr; narration, results, prompts, and help text stay on stdout.
template<class... Tys>
void messageln(MessageKind kind, fmt::format_string<Tys...> fmtText, Tys&&... args) {
  if (quietSuppresses(kind)) { return; }
  auto const stream = streamFor(kind);
  write(stream, format(stream, kind, fmtText, std::forward<Tys>(args)...), true);
}

}  // namespace terminal
