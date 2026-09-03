#include "cmd/completion_install.h"

#include "infra/env.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Redirects every location the install engine resolves to a per-test temp
// root and restores the real environment afterwards (process-global state).
class EnvGuard {
public:
  explicit EnvGuard(fs::path const& root)
    : root_{root},
      localAppData_{root / "AppData" / "Local"},
      userProfile_{root},
      documents_{root / "Docs"},
      encroConfig_{encroConfigPath(root)} {
    setAndSave("LOCALAPPDATA", localAppData_);
    setAndSave("USERPROFILE", userProfile_);
    setAndSave("ENCRO_DOCUMENTS", documents_);
    setAndSave("ENCRO_CONFIG", encroConfig_);
    std::error_code ec;
    fs::create_directories(encroConfig_, ec);
  }

  ~EnvGuard() {
    for (auto const& [name, previous]: saved_) { restore(name, previous); }
  }

  EnvGuard(EnvGuard const&) = delete;
  auto operator=(EnvGuard const&) -> EnvGuard& = delete;

  [[nodiscard]] auto encroRoot() const -> fs::path { return localAppData_ / "encro"; }
  [[nodiscard]] auto home() const -> fs::path { return userProfile_; }
  [[nodiscard]] auto documents() const -> fs::path { return documents_; }

private:
  static auto encroConfigPath(fs::path const& root) -> fs::path {
    return root / "AppData" / "Local" / "encro" / "config.json";
  }

  void setAndSave(char const* name, fs::path const& value) {
    saved_.emplace_back(name, processenv::readEnvVar(name));
    _putenv((std::string{name} + "=" + value.string()).c_str());
  }

  static void
  restore(std::string const& name, std::optional<std::string> const& previous) {
    if (previous.has_value()) {
      _putenv((name + "=").append(*previous).c_str());
    } else {
      _putenv((name + "=").c_str());  // deletes the variable
    }
  }

  fs::path root_;
  fs::path localAppData_;
  fs::path userProfile_;
  fs::path documents_;
  fs::path encroConfig_;
  std::vector<std::pair<std::string, std::optional<std::string>>> saved_;
};

auto countOccurrences(std::string const& text, std::string const& needle) -> std::size_t {
  auto count = std::size_t{0};
  for (
    auto pos = text.find(needle); pos != std::string::npos;
    pos = text.find(needle, pos + needle.size())
  ) {
    ++count;
  }
  return count;
}

// The engine writes forward-slashed paths into wiring lines.
auto forwardSlashes(std::string path) -> std::string {
  std::ranges::replace(path, '\\', '/');
  return path;
}

auto readIfExists(fs::path const& path) -> std::optional<std::string> {
  auto stream = std::ifstream{path, std::ios::binary};
  if (!stream.is_open()) { return std::nullopt; }
  return std::string{
    std::istreambuf_iterator<char>{stream},
    std::istreambuf_iterator<char>{}
  };
}

auto pwshProfile(EnvGuard const& env) -> fs::path {
  return env.documents() / "PowerShell" / "Microsoft.PowerShell_profile.ps1";
}

// Install/uninstall coverage writes shell startup files and is exercised
// rarely, so it stays out of default runs; opt in when touching the install
// engine:   ENCRO_TEST_COMPLETION=1 xmake test-report --tag="[install]"
void requireInstallTestingOrSkip() {
  if (processenv::readNonEmptyEnvVar("ENCRO_TEST_COMPLETION").has_value()) { return; }
  SKIP("Install coverage is opt-in; set ENCRO_TEST_COMPLETION=1 to run it.");
}

}  // namespace

TEST_CASE(
  "powershell install wires a profile and is idempotent",
  "[completion][install]"
) {
  requireInstallTestingOrSkip();
  TempDir temp;
  EnvGuard env{temp.path};

  // Pre-seed unrelated profile content that must survive.
  auto const profile = pwshProfile(env);
  fs::create_directories(profile.parent_path());
  {
    auto stream = std::ofstream{profile, std::ios::binary | std::ios::trunc};
    stream << "Set-PoshPrompt clean\n";
  }

  REQUIRE(completion::installScript("powershell") == 0);

  auto const scriptPath = env.encroRoot() / "completion" / "encro.ps1";
  auto const script = readIfExists(scriptPath);
  REQUIRE(script.has_value());
  CHECK(
    script->find("Register-ArgumentCompleter -CommandName encro -Native")
    != std::string::npos
  );

  auto const wired = readIfExists(profile);
  REQUIRE(wired.has_value());
  CHECK(wired->find("Set-PoshPrompt clean") != std::string::npos);
  CHECK(wired->find(">>> encro-completion >>>") != std::string::npos);
  CHECK(
    wired->find(
      ". '"
      + forwardSlashes((env.encroRoot() / "completion" / "encro.ps1").string())
      + "'"
    )
    != std::string::npos
  );

  // Re-install: no duplicate wiring, script unchanged.
  REQUIRE(completion::installScript("powershell") == 0);
  auto const rewired = readIfExists(profile);
  REQUIRE(rewired.has_value());
  CHECK(countOccurrences(*rewired, ">>> encro-completion >>>") == 1);

  // Script change (simulated upgrade) refreshes in place, wiring stays single.
  auto updated = *script + "\n# v2\n";
  {
    auto stream = std::ofstream{scriptPath, std::ios::binary | std::ios::trunc};
    stream << updated;
  }
  REQUIRE(completion::installScript("powershell") == 0);
  auto const refreshed = readIfExists(scriptPath);
  REQUIRE(refreshed.has_value());
  CHECK(*refreshed == *script);  // rewritten from live emission (drops "# v2")
  CHECK(countOccurrences(readIfExists(profile).value(), ">>> encro-completion >>>") == 1);
}

TEST_CASE(
  "powershell uninstall reverses everything install created",
  "[completion][install]"
) {
  requireInstallTestingOrSkip();
  TempDir temp;
  EnvGuard env{temp.path};

  auto const profile = pwshProfile(env);
  fs::create_directories(profile.parent_path());
  {
    auto stream = std::ofstream{profile, std::ios::binary | std::ios::trunc};
    stream << "Set-PoshPrompt clean\n";
  }

  REQUIRE(completion::installScript("powershell") == 0);
  REQUIRE(completion::uninstallScript("powershell") == 0);

  CHECK_FALSE(fs::exists(env.encroRoot() / "completion" / "encro.ps1"));
  auto const afterUninstall = readIfExists(profile);
  REQUIRE(afterUninstall.has_value());
  CHECK(afterUninstall->find("encro-completion") == std::string::npos);
  CHECK(afterUninstall->find("Set-PoshPrompt clean") != std::string::npos);

  // Uninstall again: clean no-op.
  CHECK(completion::uninstallScript("powershell") == 0);
}

TEST_CASE(
  "bash install falls back to .bashrc without bash-completion",
  "[completion][install]"
) {
  requireInstallTestingOrSkip();
  TempDir temp;
  EnvGuard env{temp.path};

  auto const bashrc = env.home() / ".bashrc";
  {
    auto stream = std::ofstream{bashrc, std::ios::binary | std::ios::trunc};
    stream << "alias ll='ls -la'\n";
  }

  REQUIRE(completion::installScript("bash") == 0);

  auto const bashScript = env.encroRoot() / "completion" / "encro.bash";
  auto const script = readIfExists(bashScript);
  REQUIRE(script.has_value());
  CHECK(script->find('_') != std::string::npos);
  CHECK(script->find('\r') == std::string::npos);  // LF-only, shell-safe

  auto const wired = readIfExists(bashrc);
  REQUIRE(wired.has_value());
  CHECK(wired->find("alias ll='ls -la'") != std::string::npos);
  CHECK(
    wired->find("source \"" + forwardSlashes(bashScript.string()) + "\"")
    != std::string::npos
  );

  REQUIRE(completion::installScript("bash") == 0);
  CHECK(countOccurrences(readIfExists(bashrc).value(), ">>> encro-completion >>>") == 1);

  REQUIRE(completion::uninstallScript("bash") == 0);
  CHECK_FALSE(fs::exists(bashScript));
  auto const unwired = readIfExists(bashrc);
  REQUIRE(unwired.has_value());
  CHECK(unwired->find("encro-completion") == std::string::npos);
  CHECK(unwired->find("alias ll='ls -la'") != std::string::npos);
  CHECK(completion::uninstallScript("bash") == 0);  // clean no-op
}

TEST_CASE(
  "bash install prefers the lazy-load directory when available",
  "[completion][install]"
) {
  requireInstallTestingOrSkip();
  TempDir temp;
  EnvGuard env{temp.path};

  auto const lazyDir =
    env.home() / ".local" / "share" / "bash-completion" / "completions";
  fs::create_directories(lazyDir);

  REQUIRE(completion::installScript("bash") == 0);

  auto const target = lazyDir / "encro";
  auto const script = readIfExists(target);
  REQUIRE(script.has_value());
  CHECK(script->find('\r') == std::string::npos);
  CHECK_FALSE(fs::exists(env.home() / ".bashrc"));  // startup file untouched
}
