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
  Hint,
  Prompt,
  Heading,
};

auto parseColorMode(std::string_view text) -> std::optional<ColorMode>;

void configure(ColorMode mode);

void reset();

auto colorMode() -> ColorMode;

auto configureFromColorString(std::string_view colorValue) -> std::optional<std::string>;

auto colorsEnabled(Stream stream = Stream::Stdout) -> bool;

auto styleFor(MessageKind kind) -> fmt::text_style;

auto styledText(Stream stream, MessageKind kind, std::string_view text) -> std::string;

auto badge(MessageKind kind, std::string_view label, Stream stream = Stream::Stdout)
  -> std::string;

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
  return fmt::format(fmt::fg(fmt::color::golden_rod) | fmt::emphasis::bold, "{}", number);
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

template<class... Tys>
void print(MessageKind kind, fmt::format_string<Tys...> fmtText, Tys&&... args) {
  write(
    Stream::Stdout,
    format(Stream::Stdout, kind, fmtText, std::forward<Tys>(args)...),
    false
  );
}

template<class... Tys>
void println(MessageKind kind, fmt::format_string<Tys...> fmtText, Tys&&... args) {
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

}  // namespace terminal
