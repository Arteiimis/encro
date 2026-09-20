#pragma once

#include <fmt/color.h>
#include <fmt/format.h>

#include <cstddef>
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

// The one styling vocabulary. Every console surface draws its foreground from
// these roles: a message's severity prefix or leading verb, help text, values
// embedded in a message, and progress bars. Each role resolves to the
// terminal's own default foreground or to one of its 16 palette slots, never
// to a 24-bit RGB value, so legibility follows the user's terminal theme
// instead of a color this program chose.
enum class Role {
  Default,
  Muted,
  Accent,
  Good,
  Warn,
  Bad,
};

enum class MessageKind {
  Plain,
  Error,
  Warning,
  Success,
  Info,
  Summary,
  Hint,
};

auto parseColorMode(std::string_view text) -> std::optional<ColorMode>;

bool streamIsTerminal(Stream stream);

void configure(ColorMode mode);

// Quiet mode (--quiet) suppresses the narration kinds (Info, Success) at the
// kind-dispatched entry points. Severity diagnostics, run summaries, product
// output, prompts, help and the version line all bypass the gate; the prompt
// and version line print through Plain.
void setQuiet(bool quiet);
bool quiet();

// True for narration kinds the quiet gate suppresses (not severity, run
// summaries, product output, prompts, or help tokens).
bool suppressedByQuiet(MessageKind kind);

void reset();

auto colorMode() -> ColorMode;

auto configureFromColorString(std::string_view colorValue) -> std::optional<std::string>;

bool colorsEnabled(Stream stream = Stream::Stdout);

// The only source of a foreground style. An unsupported attribute (faint on a
// terminal that ignores SGR 2) degrades to plain text, never to another color.
auto roleStyle(Role role) -> fmt::text_style;

// The only place text is wrapped in a style. Styling disabled, an empty
// Default style, and an unstyled target all return the text unchanged.
auto styled(Stream stream, fmt::text_style style, std::string_view text) -> std::string;

// Accents a value embedded in a message (a path, a count, a pre-formatted
// string). Terminates its own span so the surrounding prose is untouched.
auto accent(std::string_view text, Stream stream = Stream::Stdout) -> std::string;

// Wraps a value that carries a semantic role of its own (a succeeded count, a
// failed or skipped segment, a duration). Same span contract as accent().
auto withRole(Role role, std::string_view text, Stream stream = Stream::Stdout)
  -> std::string;

// The role a phase summary line's leading verb takes from its outcome: bad
// when anything failed, warn when work was only skipped, good otherwise.
inline auto outcomeVerbRole(std::size_t failed, std::size_t skipped) -> Role {
  if (failed > 0) { return Role::Bad; }
  return skipped > 0 ? Role::Warn : Role::Good;
}

// The count core of a phase summary line: `<succeeded>/<total> <noun>`, plus
// one parenthesized segment naming the failure and skip classes that actually
// occurred, each in its own role. The succeeded count is good, the total is
// accented, and the surrounding prose is unstyled. `skippedLabel` names the
// skip class in that class's own words (probing calls it "not probed").
auto summaryCounts(
  std::size_t succeeded,
  std::size_t total,
  std::string_view noun,
  std::size_t failed,
  std::size_t skipped,
  std::string_view skippedLabel = "skipped"
) -> std::string;

auto path(std::filesystem::path const& value, Stream stream = Stream::Stdout)
  -> std::string;

auto streamFor(MessageKind kind) -> Stream;

auto renderMessage(Stream stream, MessageKind kind, std::string_view text) -> std::string;

void write(Stream stream, std::string_view text, bool newline);

template<class Ty>
auto count(Ty const& number, Stream stream = Stream::Stdout) -> std::string {
  return accent(fmt::format("{}", number), stream);
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
