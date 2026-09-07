#include "infra/terminal.h"

#include "test_utils.h"

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <string>

using testutils::countOccurrences;

namespace {

class ScopedTerminalReset {
public:
  ScopedTerminalReset() = default;
  ScopedTerminalReset(ScopedTerminalReset const&) = delete;
  auto operator=(ScopedTerminalReset const&) -> ScopedTerminalReset& = delete;
  ~ScopedTerminalReset() { terminal::reset(); }
};

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

TEST_CASE("styledText stays plain when colors are disabled", "[terminal]") {
  auto const _ = ScopedTerminalReset{};
  terminal::configure(terminal::ColorMode::Never);

  auto const text =
    terminal::styledText(terminal::Stream::Stdout, terminal::MessageKind::Success, "ok");

  CHECK(text == "ok");
}

TEST_CASE("styledText emits ansi escapes when colors are forced", "[terminal]") {
  auto const _ = ScopedTerminalReset{};
  terminal::configure(terminal::ColorMode::Always);

  auto const text =
    terminal::styledText(terminal::Stream::Stdout, terminal::MessageKind::Success, "ok");

  CHECK(text.find("\x1b[") != std::string::npos);
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

TEST_CASE("path and count helpers style key values independently", "[terminal]") {
  auto const _ = ScopedTerminalReset{};
  terminal::configure(terminal::ColorMode::Always);

  auto const styledPath = terminal::path("C:/temp/out.zip");
  auto const styledCount = terminal::count(42);

  CHECK(styledPath.find("out.zip") != std::string::npos);
  CHECK(styledPath.find("\x1b[") != std::string::npos);
  CHECK(styledCount.find("42") != std::string::npos);
  CHECK(styledCount.find("\x1b[") != std::string::npos);

  // No kind or token helper may emit the bold SGR
  CHECK(styledPath.find("\x1b[1m") == std::string::npos);
  CHECK(styledCount.find("\x1b[1m") == std::string::npos);
}

// ── Kind → (prefix, color, stream) contract (table-driven) ──────────

namespace {

struct KindProps {
  terminal::MessageKind kind;
  char const* name;
  bool styled;              // styleFor produces ANSI output
  std::string_view prefix;  // plain-text severity prefix, empty = none
  terminal::Stream stream;  // the stream the kind writes to
};

// clang-format off
constexpr KindProps kKinds[] = {
  {terminal::MessageKind::Plain,         "Plain",         false, {},        terminal::Stream::Stdout},
  {terminal::MessageKind::Error,         "Error",         true,  "error:",  terminal::Stream::Stderr},
  {terminal::MessageKind::Warning,       "Warning",       true,  "warning:",terminal::Stream::Stderr},
  {terminal::MessageKind::Success,       "Success",       true,  {},        terminal::Stream::Stdout},
  {terminal::MessageKind::Info,          "Info",          true,  {},        terminal::Stream::Stdout},
  {terminal::MessageKind::Hint,          "Hint",          true,  "hint:",   terminal::Stream::Stderr},
  {terminal::MessageKind::Prompt,        "Prompt",        true,  {},        terminal::Stream::Stdout},
  {terminal::MessageKind::Heading,       "Heading",       true,  {},        terminal::Stream::Stdout},
  {terminal::MessageKind::Usage,         "Usage",         false, {},        terminal::Stream::Stdout},
  {terminal::MessageKind::OptionGroup,   "OptionGroup",   false, {},        terminal::Stream::Stdout},
  {terminal::MessageKind::OptionName,    "OptionName",    true,  {},        terminal::Stream::Stdout},
  {terminal::MessageKind::OptionDefault, "OptionDefault", true,  {},        terminal::Stream::Stdout},
  {terminal::MessageKind::OptionDesc,    "OptionDesc",    false, {},        terminal::Stream::Stdout},
  {terminal::MessageKind::Version,       "Version",       false, {},        terminal::Stream::Stdout},
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

TEST_CASE(
  "MessageKind prefixes, styles, and streams follow the display contract",
  "[terminal]"
) {
  for (auto const& entry: kKinds) {
    CAPTURE(entry.name);

    CHECK(terminal::streamFor(entry.kind) == entry.stream);

    // styleFor: empty styles format the text verbatim; styled kinds emit ANSI
    auto const formattedStyle = fmt::format(terminal::styleFor(entry.kind), "{}", "test");
    CHECK(formattedStyle.find("\x1b[1m") == std::string::npos);
    if (entry.styled) {
      CHECK(formattedStyle.find("\x1b[") != std::string::npos);
      CHECK(formattedStyle != "test");
    } else {
      CHECK(formattedStyle == "test");
      CHECK(formattedStyle.find("\x1b[") == std::string::npos);
    }

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

      auto const rendered = terminal::format(entry.stream, entry.kind, "test");
      CHECK(rendered.find("test") != std::string::npos);
      if (entry.styled) { CHECK(rendered.find("\x1b[") != std::string::npos); }
      if (!entry.prefix.empty()) {
        // The prefix survives coloring exactly once, as plain text inside the
        // ANSI span, and never as a bracketed badge or a second marker.
        CHECK(countOccurrences(rendered, std::string{entry.prefix}) == 1);
      }
      for (auto const* badgeLiteral: kBadgeLiterals) {
        CHECK(rendered.find(badgeLiteral) == std::string::npos);
      }
    }

    // styledText returns plain text in Never mode for every kind
    {
      auto const _ = ScopedTerminalReset{};
      terminal::configure(terminal::ColorMode::Never);
      CHECK(terminal::styledText(entry.stream, entry.kind, "test") == "test");
    }
  }
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
