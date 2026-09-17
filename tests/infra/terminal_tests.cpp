#include "infra/terminal.h"

#include "test_utils.h"

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <array>
#include <cctype>
#include <string>
#include <string_view>

using testutils::countOccurrences;
using testutils::ScopedTerminalReset;
using testutils::stripAnsi;

namespace {

// A line is nested when one style span opens before the previous span's reset:
// the inner reset would then end the outer style for the rest of the line,
// which is the defect this design exists to eliminate.
bool hasNestedStyleSpan(std::string_view text) {
  auto open = std::size_t{0};

  for (
    auto index = text.find('\x1b'); index != std::string_view::npos;
    index = text.find('\x1b', index)
  ) {
    auto const end = text.find('m', index);
    if (end == std::string_view::npos) { break; }

    auto const params = text.substr(index + 2, end - index - 2);
    if (params.empty() || params == "0") {
      open = 0;
    } else {
      ++open;
      if (open > 1) { return true; }
    }
    index = end + 1;
  }

  return false;
}

}  // namespace

TEST_CASE("parseColorMode accepts supported values", "[terminal]") {
  CHECK(terminal::parseColorMode("auto") == terminal::ColorMode::Auto);
  CHECK(terminal::parseColorMode("always") == terminal::ColorMode::Always);
  CHECK(terminal::parseColorMode("never") == terminal::ColorMode::Never);
  CHECK(terminal::parseColorMode("ALWAYS") == terminal::ColorMode::Always);
}

TEST_CASE("parseColorMode rejects unsupported values", "[terminal]") {
  CHECK_FALSE(terminal::parseColorMode("sometimes").has_value());
}

TEST_CASE("each role renders as its own palette slot", "[terminal]") {
  using terminal::Role;
  auto const wrap = [](terminal::Role role) {
    return fmt::format(terminal::roleStyle(role), "{}", "x");
  };

  CHECK(wrap(Role::Default) == "x");
  CHECK(wrap(Role::Muted) == "\x1b[2mx\x1b[0m");
  CHECK(wrap(Role::Accent) == "\x1b[36mx\x1b[0m");
  CHECK(wrap(Role::Good) == "\x1b[32mx\x1b[0m");
  CHECK(wrap(Role::Warn) == "\x1b[33mx\x1b[0m");
  CHECK(wrap(Role::Bad) == "\x1b[31mx\x1b[0m");

  // The user's terminal owns the palette: no role may pick a 24-bit RGB or a
  // 256-color-indexed value, which would ignore their theme.
  for (
    auto const role:
    {Role::Default, Role::Muted, Role::Accent, Role::Good, Role::Warn, Role::Bad}
  ) {
    auto const rendered = wrap(role);
    CHECK(rendered.find("38;2;") == std::string::npos);
    CHECK(rendered.find("38;5;") == std::string::npos);
  }
}

TEST_CASE("styling disabled emits nothing and substitutes nothing", "[terminal]") {
  auto const _ = ScopedTerminalReset{};
  terminal::configure(terminal::ColorMode::Never);

  CHECK(
    terminal::styled(
      terminal::Stream::Stdout,
      terminal::roleStyle(terminal::Role::Accent),
      "x"
    )
    == "x"
  );
  CHECK(
    terminal::styled(terminal::Stream::Stdout, terminal::boldStyle(), "heading")
    == "heading"
  );
  CHECK(
    terminal::styled(
      terminal::Stream::Stdout,
      terminal::roleStyle(terminal::Role::Muted),
      "x"
    )
    == "x"
  );
  CHECK(terminal::accent("value") == "value");
  CHECK(terminal::count(7) == "7");
}

TEST_CASE("format prefixes severity instead of bracketing it", "[terminal]") {
  auto const _ = ScopedTerminalReset{};
  terminal::configure(terminal::ColorMode::Never);

  auto const text = terminal::format(
    terminal::Stream::Stderr,
    terminal::MessageKind::Error,
    "hello {}",
    "world"
  );

  CHECK(text == "error: hello world");
}

TEST_CASE("accent styles paths, counts, and pre-formatted strings alike", "[terminal]") {
  auto const _ = ScopedTerminalReset{};
  terminal::configure(terminal::ColorMode::Always);

  auto const styledPath = terminal::path("C:/temp/out.zip");
  auto const styledCount = terminal::count(42);
  auto const styledString = terminal::accent("42");

  CHECK(styledPath == "\x1b[36mC:/temp/out.zip\x1b[0m");
  CHECK(styledCount == "\x1b[36m42\x1b[0m");
  CHECK(styledString == styledCount);

  // The accent is a foreground, never an attribute: bold stays reserved for
  // help headings.
  CHECK(styledPath.find("\x1b[1m") == std::string::npos);
  CHECK(styledCount.find("\x1b[1m") == std::string::npos);
}

// The defect this pins: styling the message body used to be truncated by the
// first embedded value's reset, leaving the rest of the line unstyled.
TEST_CASE("a status line styles its embedded values and nothing else", "[terminal]") {
  auto const _ = ScopedTerminalReset{};
  terminal::configure(terminal::ColorMode::Always);

  auto const line = terminal::format(
    terminal::Stream::Stdout,
    terminal::MessageKind::Info,
    "Found {} file(s) under {}.",
    terminal::count(2),
    terminal::path("C:/tmp")
  );

  CHECK_FALSE(hasNestedStyleSpan(line));
  CHECK(countOccurrences(line, "\x1b[0m") == 2);
  CHECK(stripAnsi(line) == "Found 2 file(s) under C:/tmp.");
}

TEST_CASE("a result line styles its leading verb and nothing else", "[terminal]") {
  auto const _ = ScopedTerminalReset{};
  terminal::configure(terminal::ColorMode::Always);

  auto const line = terminal::format(
    terminal::Stream::Stdout,
    terminal::MessageKind::Success,
    "Encoded {} videos -> {}",
    terminal::count(2),
    terminal::path("C:/out")
  );

  CHECK(line.starts_with("\x1b[32mEncoded\x1b[0m"));
  CHECK_FALSE(hasNestedStyleSpan(line));
  CHECK(countOccurrences(line, "\x1b[0m") == 3);
  CHECK(stripAnsi(line) == "Encoded 2 videos -> C:/out");
}

TEST_CASE("a caller-styled first value keeps its span", "[terminal]") {
  auto const _ = ScopedTerminalReset{};
  terminal::configure(terminal::ColorMode::Always);

  // A LeadingVerb kind accepts arbitrary arguments, so the first "word" can
  // already be a styled value. Opening a verb span around it would nest the
  // two styles, which is what the disjointness rule forbids.
  auto const line = terminal::format(
    terminal::Stream::Stdout,
    terminal::MessageKind::Success,
    "{} packed",
    terminal::count(5)
  );

  CHECK_FALSE(hasNestedStyleSpan(line));
  CHECK(countOccurrences(line, "\x1b[0m") == 1);
  CHECK(stripAnsi(line) == "5 packed");
}

TEST_CASE("no rendered line nests one style span inside another", "[terminal]") {
  auto const _ = ScopedTerminalReset{};
  terminal::configure(terminal::ColorMode::Always);

  auto const samples = std::array{
    terminal::format(
      terminal::Stream::Stdout,
      terminal::MessageKind::Info,
      "Found {} video(s) under {}.",
      terminal::count(2),
      terminal::path("C:/in")
    ),
    terminal::format(
      terminal::Stream::Stdout,
      terminal::MessageKind::Summary,
      "Encoded {}/{} videos -> {}",
      terminal::count(2),
      terminal::count(3),
      terminal::path("C:/out")
    ),
    terminal::format(terminal::Stream::Stderr, terminal::MessageKind::Error, "boom"),
    terminal::format(terminal::Stream::Stderr, terminal::MessageKind::Warning, "careful"),
    terminal::format(terminal::Stream::Stderr, terminal::MessageKind::Hint, "try -hh"),
    terminal::styled(terminal::Stream::Stdout, terminal::boldStyle(), "General options"),
  };

  for (auto const& sample: samples) {
    CAPTURE(sample);
    CHECK_FALSE(hasNestedStyleSpan(sample));
  }
}

// ── Kind → (prefix, stream) contract ────────────────────────────────

namespace {

struct KindProps {
  terminal::MessageKind kind;
  char const* name;
  std::string_view prefix;  // plain-text severity prefix, empty = none
  terminal::Stream stream;  // the stream the kind writes to
};

// clang-format off
constexpr KindProps kKinds[] = {
  {terminal::MessageKind::Plain,         "Plain",         {},        terminal::Stream::Stdout},
  {terminal::MessageKind::Error,         "Error",         "error:",  terminal::Stream::Stderr},
  {terminal::MessageKind::Warning,       "Warning",       "warning:",terminal::Stream::Stderr},
  {terminal::MessageKind::Success,       "Success",       {},        terminal::Stream::Stdout},
  {terminal::MessageKind::Info,          "Info",          {},        terminal::Stream::Stdout},
  {terminal::MessageKind::Summary,       "Summary",       {},        terminal::Stream::Stdout},
  {terminal::MessageKind::Hint,          "Hint",          "hint:",   terminal::Stream::Stderr},
  {terminal::MessageKind::OptionGroup,   "OptionGroup",   {},        terminal::Stream::Stdout},
  {terminal::MessageKind::OptionName,    "OptionName",    {},        terminal::Stream::Stdout},
  {terminal::MessageKind::OptionDefault, "OptionDefault", {},        terminal::Stream::Stdout},
  {terminal::MessageKind::OptionDesc,    "OptionDesc",    {},        terminal::Stream::Stdout},
};
// clang-format on

constexpr char const* kBadgeLiterals[] = {
  "[error]",
  "[warn]",
  "[done]",
  "[info]",
  "[hint]",
  "[?]",
};

}  // namespace

TEST_CASE("every kind prints its severity prefix exactly once", "[terminal]") {
  for (auto const& entry: kKinds) {
    CAPTURE(entry.name);

    {
      auto const _ = ScopedTerminalReset{};
      terminal::configure(terminal::ColorMode::Never);

      // Plain rendering is exactly prefix + body; nothing else may be added
      // (which also proves no bracketed badge survives)
      auto const rendered = terminal::format(entry.stream, entry.kind, "test");
      auto const expected = entry.prefix.empty()
        ? std::string{"test"}
        : std::string{entry.prefix}.append(" test");
      CHECK(rendered == expected);
    }

    {
      auto const _ = ScopedTerminalReset{};
      terminal::configure(terminal::ColorMode::Always);
      auto const colored = terminal::format(entry.stream, entry.kind, "test");
      CHECK(colored.find("test") != std::string::npos);
      if (!entry.prefix.empty()) {
        // The prefix survives coloring exactly once, as plain text inside its
        // span, and never as a bracketed badge or a second marker.
        CHECK(countOccurrences(stripAnsi(colored), std::string{entry.prefix}) == 1);
      }
      for (auto const* badgeLiteral: kBadgeLiterals) {
        CHECK(colored.find(badgeLiteral) == std::string::npos);
      }
    }
  }
}

TEST_CASE("hint diagnostics are muted rather than colored", "[terminal]") {
  auto const _ = ScopedTerminalReset{};
  terminal::configure(terminal::ColorMode::Always);

  auto const line = terminal::renderMessage(
    terminal::Stream::Stderr,
    terminal::MessageKind::Hint,
    "try -hh"
  );

  // Faint dims whichever foreground the user chose; no palette slot is spent,
  // so a theme whose bright-black is its background cannot hide the hint.
  CHECK(line == "\x1b[2mhint:\x1b[0m try -hh");
}

TEST_CASE("messageln routes each kind to its own stream", "[terminal]") {
  for (auto const& entry: kKinds) {
    CAPTURE(entry.name);

    auto const _ = ScopedTerminalReset{};
    terminal::configure(terminal::ColorMode::Never);

    auto const temp = TempDir{};
    auto const outPath = temp.path / "route-out.txt";
    auto const errPath = temp.path / "route-err.txt";
    {
      auto outCapture = testutils::StdoutCapture{outPath};
      auto errCapture = testutils::StderrCapture{errPath};
      terminal::messageln(entry.kind, "routed {}", entry.name);
    }

    auto const outText = testutils::readTextFile(outPath);
    auto const errText = testutils::readTextFile(errPath);

    if (entry.stream == terminal::Stream::Stdout) {
      CHECK(outText.find("routed") != std::string::npos);
      CHECK(errText.empty());
    } else {
      CHECK(errText.find("routed") != std::string::npos);
      CHECK(outText.empty());
    }
  }
}

TEST_CASE("severity prefixes survive every color-disabled mode", "[terminal]") {
  auto const _ = ScopedTerminalReset{};

  SECTION("--color never") {
    terminal::configure(terminal::ColorMode::Never);
    auto const text = terminal::renderMessage(
      terminal::Stream::Stderr,
      terminal::MessageKind::Error,
      "boom"
    );
    CHECK(text.starts_with("error: "));
    CHECK(text.find("\x1b[") == std::string::npos);
  }

  SECTION("NO_COLOR") {
    terminal::configure(terminal::ColorMode::Auto);
    testutils::ScopedEnvVar const noColor{"NO_COLOR", "1"};
    auto const text = terminal::renderMessage(
      terminal::Stream::Stderr,
      terminal::MessageKind::Error,
      "boom"
    );
    CHECK(text.starts_with("error: "));
    CHECK(text.find("\x1b[") == std::string::npos);
  }

  SECTION("non-TTY stream with auto color mode") {
    terminal::configure(terminal::ColorMode::Auto);
    auto const temp = TempDir{};
    auto const outPath = temp.path / "nontty-out.txt";
    auto const errPath = temp.path / "nontty-err.txt";
    {
      auto outCapture = testutils::StdoutCapture{outPath};
      auto errCapture = testutils::StderrCapture{errPath};
      terminal::messageln(terminal::MessageKind::Error, "boom");
    }
    auto const errText = testutils::readTextFile(errPath);
    CHECK(errText.starts_with("error: boom"));
    CHECK(errText.find("\x1b[") == std::string::npos);
  }
}

TEST_CASE("colored failure lines carry the severity marker exactly once", "[terminal]") {
  auto const _ = ScopedTerminalReset{};
  terminal::configure(terminal::ColorMode::Always);

  auto const text = terminal::renderMessage(
    terminal::Stream::Stderr,
    terminal::MessageKind::Error,
    "boom"
  );

  CHECK(text.find("\x1b[") == 0);                   // the prefix opens the colored span
  CHECK(countOccurrences(text, "error:") == 1);
  CHECK(text.find("Error:") == std::string::npos);  // no capitalized duplicate
  CHECK(text.find("[error]") == std::string::npos);
  CHECK(text.find("boom") != std::string::npos);
}
