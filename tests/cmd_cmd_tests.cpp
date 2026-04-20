#include "cmd/cmd.h"

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

auto readEnvVar(std::string const& name) -> std::optional<std::string> {
#if defined(_WIN32) || defined(_WIN64)
  auto* value = static_cast<char*>(nullptr);
  auto len = std::size_t{0};
  if (_dupenv_s(&value, &len, name.c_str()) != 0 || value == nullptr) {
    return std::nullopt;
  }

  auto result = std::optional<std::string>{};
  if (len > 1) { result = std::string{value}; }
  std::free(value);
  return result;
#else
  if (auto const* value = std::getenv(name.c_str()); value != nullptr) {
    return std::string{value};
  }
  return std::nullopt;
#endif
}

class ScopedEnvVar {
public:
  ScopedEnvVar(std::string name, std::string value)
    : name_(std::move(name)), hadOriginal_(false) {
    if (auto const current = readEnvVar(name_); current.has_value()) {
      originalValue_ = current.value();
      hadOriginal_ = true;
    }
    set(value);
  }

  ScopedEnvVar(ScopedEnvVar const&) = delete;
  auto operator=(ScopedEnvVar const&) -> ScopedEnvVar& = delete;

  ~ScopedEnvVar() {
    if (hadOriginal_) {
      set(originalValue_);
    } else {
      unset();
    }
  }

private:
  auto set(std::string const& value) -> void {
#if defined(_WIN32) || defined(_WIN64)
    _putenv_s(name_.c_str(), value.c_str());
#else
    setenv(name_.c_str(), value.c_str(), 1);
#endif
  }

  auto unset() -> void {
#if defined(_WIN32) || defined(_WIN64)
    _putenv_s(name_.c_str(), "");
#else
    unsetenv(name_.c_str());
#endif
  }

  std::string name_;
  std::string originalValue_;
  bool hadOriginal_;
};

auto parseArgs(std::vector<std::string> const& args) -> CmdParseResult {
  static thread_local std::vector<std::string> storage;
  storage = args;
  auto argv = std::vector<char*>{};
  argv.reserve(storage.size());
  for (auto& arg: storage) { argv.push_back(arg.data()); }
  argv.push_back(nullptr);

  return commandLineInit(static_cast<int>(argv.size() - 1), argv.data());
}

auto renderHelp(CmdParseResult const& result) -> std::string {
  auto out = std::ostringstream{};
  result.desc.print(out);
  return out.str();
}

auto longestHelpLine(std::string_view text) -> std::size_t {
  auto longest = std::size_t{0};
  auto start = std::size_t{0};

  while (start <= text.size()) {
    auto const end = text.find('\n', start);
    auto const lineLength =
      end == std::string_view::npos ? text.size() - start : end - start;
    longest = std::max(longest, lineLength);

    if (end == std::string_view::npos) { break; }
    start = end + 1;
  }

  return longest;
}

}  // namespace

TEST_CASE("commandLineInit exposes defaults", "[cmd]") {
  auto const result = parseArgs({"encro"});

  REQUIRE(result.vm.count("type") == 1);
  REQUIRE(result.vm.count("output-format") == 1);
  REQUIRE(result.vm.count("force-conflict-handling") == 1);
  REQUIRE(result.vm.count("color") == 1);
  CHECK(result.vm["type"].as<std::string>() == "video");
  CHECK(result.vm["output-format"].as<std::string>() == "mp4");
  CHECK(result.vm["force-conflict-handling"].as<std::string>() == "y");
  CHECK(result.vm["color"].as<std::string>() == "auto");
  CHECK(result.vm.count("folder-summary") == 0);
}

TEST_CASE("commandLineInit parses flag and option values", "[cmd]") {
  auto const result = parseArgs(
    {"encro",
     "--yes",
     "--recursive",
     "--pack",
     "--pack-only",
     "--resume",
     "--restart",
     "--flat",
     "--keep",
     "--force-conflict-handling=n",
     "--folder-summary",
     "--color=always",
     "--verbose",
     "--verbose-echo"}
  );

  CHECK(result.vm.count("yes") == 1);
  CHECK(result.vm.count("recursive") == 1);
  CHECK(result.vm.count("pack") == 1);
  CHECK(result.vm.count("pack-only") == 1);
  CHECK(result.vm.count("resume") == 1);
  CHECK(result.vm.count("restart") == 1);
  CHECK(result.vm.count("flat") == 1);
  CHECK(result.vm.count("keep") == 1);
  CHECK(result.vm.count("force-conflict-handling") == 1);
  CHECK(result.vm["force-conflict-handling"].as<std::string>() == "n");
  CHECK(result.vm.count("folder-summary") == 1);
  CHECK(result.vm.count("color") == 1);
  CHECK(result.vm["color"].as<std::string>() == "always");
  CHECK(result.vm.count("verbose") == 1);
  CHECK(result.vm.count("verbose-echo") == 1);
}

TEST_CASE("commandLineInit parses state-file option", "[cmd]") {
  auto const result =
    parseArgs({"encro", "--state-file", "job-state.json", "-i", "input.mp4"});

  REQUIRE(result.vm.count("state-file") == 1);
  CHECK(result.vm["state-file"].as<std::string>() == "job-state.json");
}

TEST_CASE("commandLineInit parses multi-input values", "[cmd]") {
  auto const result = parseArgs({"encro", "-I", "a.mp4", "b.mkv", "c.mov"});

  REQUIRE(result.vm.count("inputs") == 1);
  auto const inputs = result.vm["inputs"].as<std::vector<std::string>>();
  CHECK(inputs.size() == 3);
  CHECK(inputs[0] == "a.mp4");
  CHECK(inputs[1] == "b.mkv");
  CHECK(inputs[2] == "c.mov");
}

TEST_CASE("commandLineInit parses jobs option", "[cmd]") {
  auto const result = parseArgs({"encro", "--jobs", "4"});

  REQUIRE(result.vm.count("jobs") == 1);
  CHECK(result.vm["jobs"].as<std::size_t>() == 4);
}

TEST_CASE("commandLineInit reports unknown options", "[cmd]") {
  auto const result = parseArgs({"encro", "--nope"});

  REQUIRE(result.error.has_value());
  CHECK(result.error.value().find("unrecognised option") != std::string::npos);
}

TEST_CASE("commandLineInit caps help output to configured maximum width", "[cmd]") {
  auto const columnsVar = ScopedEnvVar{"COLUMNS", "200"};

  auto const result = parseArgs({"encro"});
  auto const help = renderHelp(result);

  CHECK(longestHelpLine(help) <= 120);
}

TEST_CASE("commandLineInit adapts help output to narrower terminal width", "[cmd]") {
  auto const columnsVar = ScopedEnvVar{"COLUMNS", "72"};

  auto const result = parseArgs({"encro"});
  auto const help = renderHelp(result);

  CHECK(longestHelpLine(help) <= 72);
}
