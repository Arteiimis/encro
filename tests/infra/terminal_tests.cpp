#include "infra/terminal.h"

#include <catch2/catch_all.hpp>

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

// ── Phase 20: New MessageKind value tests ──────────────────────────

TEST_CASE("styleFor(Usage) returns empty style", "[terminal]") {
  auto const style = terminal::styleFor(terminal::MessageKind::Usage);
  auto const text = fmt::format(style, "{}", "test");
  CHECK(text.find("\x1b[") == std::string::npos);
  CHECK(text == "test");
}

TEST_CASE("styleFor(OptionGroup) differs from styleFor(Usage)", "[terminal]") {
  auto const usageStyle = terminal::styleFor(terminal::MessageKind::Usage);
  auto const groupStyle = terminal::styleFor(terminal::MessageKind::OptionGroup);
  auto const usageText = fmt::format(usageStyle, "{}", "test");
  auto const groupText = fmt::format(groupStyle, "{}", "test");
  CHECK(usageText != groupText);
}

TEST_CASE("styleFor(OptionName) produces ANSI but not bold", "[terminal]") {
  auto const style = terminal::styleFor(terminal::MessageKind::OptionName);
  auto const text = fmt::format(style, "{}", "test");
  CHECK(text.find("\x1b[") != std::string::npos);
  auto const plainStyle = terminal::styleFor(terminal::MessageKind::Usage);
  auto const plainText = fmt::format(plainStyle, "{}", "test");
  CHECK(text != plainText);
}

TEST_CASE("styleFor(OptionDesc) returns empty style", "[terminal]") {
  auto const style = terminal::styleFor(terminal::MessageKind::OptionDesc);
  auto const text = fmt::format(style, "{}", "test");
  CHECK(text.find("\x1b[") == std::string::npos);
  CHECK(text == "test");
}

TEST_CASE("styleFor(Version) returns empty style", "[terminal]") {
  auto const style = terminal::styleFor(terminal::MessageKind::Version);
  auto const text = fmt::format(style, "{}", "test");
  CHECK(text.find("\x1b[") == std::string::npos);
  CHECK(text == "test");
}

TEST_CASE("format with Usage has no badge prefix", "[terminal]") {
  auto const _ = ScopedTerminalReset{};
  terminal::configure(terminal::ColorMode::Always);

  auto const text =
    terminal::format(terminal::Stream::Stdout, terminal::MessageKind::Usage, "test");

  CHECK(text.find("[usage]") == std::string::npos);
}

TEST_CASE("format with OptionGroup has no badge prefix", "[terminal]") {
  auto const _ = ScopedTerminalReset{};
  terminal::configure(terminal::ColorMode::Always);

  auto const text = terminal::format(
    terminal::Stream::Stdout,
    terminal::MessageKind::OptionGroup,
    "test"
  );

  CHECK(text.find("[optiongroup]") == std::string::npos);
}

TEST_CASE("styledText with OptionName emits ANSI when Always", "[terminal]") {
  auto const _ = ScopedTerminalReset{};
  terminal::configure(terminal::ColorMode::Always);

  auto const text = terminal::styledText(
    terminal::Stream::Stdout,
    terminal::MessageKind::OptionName,
    "test"
  );

  CHECK(text.find("\x1b[") != std::string::npos);
}

TEST_CASE("styledText with OptionName returns plain when Never", "[terminal]") {
  auto const _ = ScopedTerminalReset{};
  terminal::configure(terminal::ColorMode::Never);

  auto const text = terminal::styledText(
    terminal::Stream::Stdout,
    terminal::MessageKind::OptionName,
    "test"
  );

  CHECK(text == "test");
}
