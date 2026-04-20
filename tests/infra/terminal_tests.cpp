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
