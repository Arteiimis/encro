#include "infra/terminal.h"

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

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

TEST_CASE("format renders badge instead of coloring entire line", "[terminal]") {
  auto const _ = ScopedTerminalReset{};
  terminal::configure(terminal::ColorMode::Always);

  auto const text = terminal::format(
    terminal::Stream::Stdout,
    terminal::MessageKind::Info,
    "hello {}",
    "world"
  );

  CHECK(text.find("[info]") != std::string::npos);
  CHECK(text.find("hello world") != std::string::npos);
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
}

// ── Phase 20: MessageKind style/badge contract (table-driven) ───────

TEST_CASE("MessageKind styles and badges follow the display contract", "[terminal]") {
  struct KindProps {
    terminal::MessageKind kind;
    char const* name;
    bool styled;                  // styleFor produces ANSI output
    std::string_view badgeLabel;  // empty = no badge prefix
  };

  // clang-format off
  static constexpr KindProps kKinds[] = {
    {terminal::MessageKind::Plain,         "Plain",         false, {}     },
    {terminal::MessageKind::Error,         "Error",         true,  "error"},
    {terminal::MessageKind::Warning,       "Warning",       true,  "warn" },
    {terminal::MessageKind::Success,       "Success",       true,  "done" },
    {terminal::MessageKind::Info,          "Info",          true,  "info" },
    {terminal::MessageKind::Hint,          "Hint",          true,  "hint" },
    {terminal::MessageKind::Prompt,        "Prompt",        true,  "?"    },
    {terminal::MessageKind::Heading,       "Heading",       true,  {}     },
    {terminal::MessageKind::Usage,         "Usage",         false, {}     },
    {terminal::MessageKind::OptionGroup,   "OptionGroup",   true,  {}     },
    {terminal::MessageKind::OptionName,    "OptionName",    true,  {}     },
    {terminal::MessageKind::OptionDefault, "OptionDefault", true,  {}     },
    {terminal::MessageKind::OptionDesc,    "OptionDesc",    false, {}     },
    {terminal::MessageKind::Version,       "Version",       false, {}     },
  };
  // clang-format on

  for (auto const& entry: kKinds) {
    CAPTURE(entry.name);

    // styleFor: empty styles format the text verbatim; styled kinds emit ANSI
    auto const formattedStyle = fmt::format(terminal::styleFor(entry.kind), "{}", "test");
    if (entry.styled) {
      CHECK(formattedStyle.find("\x1b[") != std::string::npos);
      CHECK(formattedStyle != "test");
    } else {
      CHECK(formattedStyle == "test");
      CHECK(formattedStyle.find("\x1b[") == std::string::npos);
    }

    {
      auto const _ = ScopedTerminalReset{};
      terminal::configure(terminal::ColorMode::Always);

      // styledText emits ANSI in Always mode exactly for styled kinds
      auto const alwaysText =
        terminal::styledText(terminal::Stream::Stdout, entry.kind, "test");
      if (entry.styled) {
        CHECK(alwaysText.find("\x1b[") != std::string::npos);
      } else {
        CHECK(alwaysText == "test");
      }

      // format() prefixes the badge label only for badge kinds; badge-less
      // kinds render exactly the styled text (no "[usage]"-style prefixes)
      auto const rendered =
        terminal::format(terminal::Stream::Stdout, entry.kind, "test");
      if (entry.badgeLabel.empty()) {
        CHECK(rendered == alwaysText);
      } else {
        auto const badgeText = std::string{"["}.append(entry.badgeLabel).append("]");
        CHECK(rendered.find(badgeText) != std::string::npos);
        CHECK(rendered.find("test") != std::string::npos);
      }
    }

    // styledText returns plain text in Never mode for every kind
    {
      auto const _ = ScopedTerminalReset{};
      terminal::configure(terminal::ColorMode::Never);
      CHECK(terminal::styledText(terminal::Stream::Stdout, entry.kind, "test") == "test");
    }
  }
}
