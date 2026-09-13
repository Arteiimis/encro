#include "cmd/completion_install.h"

#include "cmd/cmd.h"
#include "cmd/completion_emitter.h"
#include "cmd/config_store.h"
#include "infra/env.h"
#include "infra/terminal.h"

#include <fmt/ostream.h>  // IWYU pragma: keep

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

#if defined(_WIN32)
  #include <shlobj.h>
#endif

namespace fs = std::filesystem;

namespace completion {
namespace {

constexpr auto kBegin = "# >>> encro-completion >>>";
constexpr auto kEnd = "# <<< encro-completion <<<";

// ── Small file helpers ──────────────────────────────────────────────────────

auto readText(fs::path const& path) -> std::optional<std::string> {
  auto stream = std::ifstream{path, std::ios::binary};
  if (!stream.is_open()) { return std::nullopt; }
  return std::string{
    std::istreambuf_iterator<char>{stream},
    std::istreambuf_iterator<char>{}
  };
}

// Binary mode: bash targets must keep LF endings regardless of host platform.
bool writeText(fs::path const& path, std::string const& text) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  auto stream = std::ofstream{path, std::ios::binary | std::ios::trunc};
  if (!stream.is_open()) { return false; }
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  return stream.good();
}

auto forwardSlashes(std::string path) -> std::string {
  std::ranges::replace(path, '\\', '/');
  return path;
}

enum class Refresh {
  Created,
  Updated,
  Current,
};

auto refreshScript(fs::path const& path, std::string const& text)
  -> std::optional<Refresh> {
  auto const existing = readText(path);
  if (existing == text) { return Refresh::Current; }
  if (!writeText(path, text)) { return std::nullopt; }
  return existing.has_value() ? Refresh::Updated : Refresh::Created;
}

// ── Marker-guarded startup-file blocks ──────────────────────────────────────

auto blockOf(std::string const& line) -> std::string {
  return std::string{kBegin} + "\n" + line + kEnd + "\n";
}

// Replaces the delimited block, or appends it when absent.
auto spliceBlock(std::string const& content, std::string const& block) -> std::string {
  auto const begin = content.find(kBegin);
  auto const end = content.find(kEnd);
  if (begin != std::string::npos && end != std::string::npos && end > begin) {
    auto const after = content.find('\n', end);
    auto const tail = after == std::string::npos ? content.size() : after + 1;
    return content.substr(0, begin) + block + content.substr(tail);
  }
  auto result = content;
  if (!result.empty() && result.back() != '\n') { result += '\n'; }
  result += block;
  return result;
}

auto isWiredWith(std::string const& content, std::string const& line) -> bool {
  return content.find(kBegin) != std::string::npos
    && content.find(line) != std::string::npos;
}

auto removeBlock(std::string const& content) -> std::string {
  auto const begin = content.find(kBegin);
  if (begin == std::string::npos) { return content; }
  auto const end = content.find(kEnd, begin);
  if (end == std::string::npos) { return content; }
  auto const after = content.find('\n', end);
  auto const tail = after == std::string::npos ? content.size() : after + 1;
  return content.substr(0, begin) + content.substr(tail);
}

enum class Unwire {
  Nothing,
  Changed,
  Failed,
};

// Removes the activation block from a startup/profile file. A zero-length
// remainder carries no content to preserve, so such a file is deleted instead
// of written back; any other content is kept exactly.
auto unwireFile(fs::path const& path) -> Unwire {
  auto const content = readText(path);
  if (!content.has_value() || content->find(kBegin) == std::string::npos) {
    return Unwire::Nothing;
  }
  auto const remaining = removeBlock(*content);
  if (remaining.empty()) {
    std::error_code ec;
    if (!fs::remove(path, ec)) {
      terminal::eprintln(
        terminal::MessageKind::Error,
        "cannot remove {}",
        forwardSlashes(path.string())
      );
      return Unwire::Failed;
    }
    terminal::println(
      terminal::MessageKind::Success,
      "removed: {}",
      forwardSlashes(path.string())
    );
    return Unwire::Changed;
  }
  if (!writeText(path, remaining)) {
    terminal::eprintln(
      terminal::MessageKind::Error,
      "cannot write {}",
      forwardSlashes(path.string())
    );
    return Unwire::Failed;
  }
  terminal::println(
    terminal::MessageKind::Success,
    "unwired: {}",
    forwardSlashes(path.string())
  );
  return Unwire::Changed;
}

// ── Locations ───────────────────────────────────────────────────────────────

// The encro user-data root follows the config-store chain (ENCRO_CONFIG >
// LOCALAPPDATA > APPDATA > ...), so tests can redirect everything via env.
auto userDataRoot() -> fs::path {
  return configstore::resolveConfigPath().parent_path();
}

auto documentsRoot() -> fs::path {
  // ENCRO_DOCUMENTS is a test hook; SHGetKnownFolderPath cannot be redirected.
  if (auto const overrideDir = processenv::readNonEmptyEnvVar("ENCRO_DOCUMENTS")) {
    return fs::path{*overrideDir};
  }
#if defined(_WIN32)
  auto* raw = static_cast<PWSTR>(nullptr);
  if (
    SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DONT_VERIFY, nullptr, &raw) == S_OK
  ) {
    auto const path = fs::path{raw};
    CoTaskMemFree(raw);
    return path;
  }
#endif
  if (auto const profile = processenv::readNonEmptyEnvVar("USERPROFILE")) {
    return fs::path{*profile} / "Documents";
  }
  return fs::temp_directory_path();
}

auto homeDir() -> std::optional<fs::path> {
  if (auto const profile = processenv::readNonEmptyEnvVar("USERPROFILE")) {
    return fs::path{*profile};
  }
  if (auto const home = processenv::readNonEmptyEnvVar("HOME")) {
    return fs::path{*home};
  }
  return std::nullopt;
}

// bash-completion framework present? Checks the user lazy-load directory plus
// a few known Git-for-Windows layouts (design D5; MSYS HOME redirection is a
// documented limitation, install targets USERPROFILE).
auto bashCompletionDetected(fs::path const& userCompletions) -> bool {
  std::error_code ec;
  if (fs::exists(userCompletions, ec)) { return true; }
  auto candidates = std::array<fs::path, 2>{
    "C:/Program Files/Git/usr/share/bash-completion",
    fs::path{"/usr/share/bash-completion"},
  };
  if (auto const scoop = processenv::readNonEmptyEnvVar("SCOOP")) {
    candidates[1] =
      fs::path{*scoop} / "apps/git/current/mingw64/usr/share/bash-completion";
  }
  return std::ranges::any_of(candidates, [&ec](fs::path const& dir) {
    return fs::exists(dir, ec);
  });
}

// ── PowerShell ──────────────────────────────────────────────────────────────

auto powerShellProfiles() -> std::array<fs::path, 2> {
  auto const documents = documentsRoot();
  return std::array<fs::path, 2>{
    documents / "WindowsPowerShell" / "Microsoft.PowerShell_profile.ps1",
    documents / "PowerShell" / "Microsoft.PowerShell_profile.ps1",
  };
}

int installPowerShell(std::string const& scriptText) {
  auto const scriptPath = userDataRoot() / "completion" / "encro.ps1";
  auto const refreshed = refreshScript(scriptPath, scriptText);
  if (!refreshed.has_value()) {
    terminal::eprintln(
      terminal::MessageKind::Error,
      "cannot write {}",
      forwardSlashes(scriptPath.string())
    );
    return 1;
  }
  if (*refreshed == Refresh::Current) {
    terminal::println(
      terminal::MessageKind::Info,
      "completion script already current: {}",
      forwardSlashes(scriptPath.string())
    );
  } else {
    terminal::println(
      terminal::MessageKind::Success,
      "installed completion script: {}",
      forwardSlashes(scriptPath.string())
    );
  }

  auto const line = ". '" + forwardSlashes(scriptPath.string()) + "'\n";
  // A missing profile is treated as empty content: creating it is what makes a
  // first-time install work in either PowerShell edition.
  for (auto const& profile: powerShellProfiles()) {
    auto const content = readText(profile).value_or("");
    if (isWiredWith(content, line)) {
      terminal::println(
        terminal::MessageKind::Info,
        "already wired: {}",
        forwardSlashes(profile.string())
      );
      continue;
    }
    if (!writeText(profile, spliceBlock(content, blockOf(line)))) {
      terminal::eprintln(
        terminal::MessageKind::Error,
        "cannot write {}",
        forwardSlashes(profile.string())
      );
      return 1;
    }
    terminal::println(
      terminal::MessageKind::Success,
      "wired: {}",
      forwardSlashes(profile.string())
    );
  }

  terminal::messageln(
    terminal::MessageKind::Hint,
    "open a new PowerShell session to load it; re-run after upgrading encro"
  );
  return 0;
}

int uninstallPowerShell() {
  auto anything = false;
  for (auto const& profile: powerShellProfiles()) {
    auto const unwired = unwireFile(profile);
    if (unwired == Unwire::Failed) { return 1; }
    anything = anything || unwired == Unwire::Changed;
  }
  std::error_code ec;
  auto const scriptPath = userDataRoot() / "completion" / "encro.ps1";
  if (fs::remove(scriptPath, ec)) {
    terminal::println(
      terminal::MessageKind::Success,
      "removed: {}",
      forwardSlashes(scriptPath.string())
    );
    anything = true;
  }
  if (!anything) {
    terminal::println(terminal::MessageKind::Info, "nothing installed for powershell");
  }
  return 0;
}

// ── Bash ────────────────────────────────────────────────────────────────────

int installBash(std::string const& scriptText) {
  auto const home = homeDir();
  if (!home.has_value()) {
    terminal::eprintln(
      terminal::MessageKind::Error,
      "cannot locate the user home directory"
    );
    return 1;
  }
  auto const completionDir = userDataRoot() / "completion";
  auto const userCompletions =
    *home / ".local" / "share" / "bash-completion" / "completions";

  if (bashCompletionDetected(userCompletions)) {
    // Lazy-load directory: bash-completion sources it on demand, no startup
    // file edits at all.
    auto const target = userCompletions / "encro";
    auto const refreshed = refreshScript(target, scriptText);
    if (!refreshed.has_value()) {
      terminal::eprintln(
        terminal::MessageKind::Error,
        "cannot write {}",
        forwardSlashes(target.string())
      );
      return 1;
    }
    if (*refreshed == Refresh::Current) {
      terminal::println(
        terminal::MessageKind::Info,
        "completion script already current: {}",
        forwardSlashes(target.string())
      );
    } else {
      terminal::println(
        terminal::MessageKind::Success,
        "installed completion script: {}",
        forwardSlashes(target.string())
      );
    }
    terminal::messageln(
      terminal::MessageKind::Hint,
      "bash-completion loads it for new shells; re-run after upgrading encro"
    );
    return 0;
  }

  // Fallback: script under the encro user dir, sourced from ~/.bashrc.
  auto const bashPath = completionDir / "encro.bash";
  auto const refreshed = refreshScript(bashPath, scriptText);
  if (!refreshed.has_value()) {
    terminal::eprintln(
      terminal::MessageKind::Error,
      "cannot write {}",
      forwardSlashes(bashPath.string())
    );
    return 1;
  }
  if (*refreshed != Refresh::Current) {
    terminal::println(
      terminal::MessageKind::Success,
      "installed completion script: {}",
      forwardSlashes(bashPath.string())
    );
  } else {
    terminal::println(
      terminal::MessageKind::Info,
      "completion script already current: {}",
      forwardSlashes(bashPath.string())
    );
  }

  auto const bashrc = *home / ".bashrc";
  auto const line = "source \"" + forwardSlashes(bashPath.string()) + "\"\n";
  auto const content = readText(bashrc).value_or("");
  if (isWiredWith(content, line)) {
    terminal::println(
      terminal::MessageKind::Info,
      "already wired: {}",
      forwardSlashes(bashrc.string())
    );
    return 0;
  }
  if (!writeText(bashrc, spliceBlock(content, blockOf(line)))) {
    terminal::eprintln(
      terminal::MessageKind::Error,
      "cannot write {}",
      forwardSlashes(bashrc.string())
    );
    return 1;
  }
  terminal::println(
    terminal::MessageKind::Success,
    "wired: {}",
    forwardSlashes(bashrc.string())
  );
  terminal::messageln(
    terminal::MessageKind::Hint,
    "open a new bash session to load it; re-run after upgrading encro"
  );
  return 0;
}

int uninstallBash() {
  auto const home = homeDir();
  if (!home.has_value()) {
    terminal::eprintln(
      terminal::MessageKind::Error,
      "cannot locate the user home directory"
    );
    return 1;
  }
  auto anything = false;

  std::error_code ec;
  auto const lazy =
    *home / ".local" / "share" / "bash-completion" / "completions" / "encro";
  if (fs::remove(lazy, ec)) {
    terminal::println(
      terminal::MessageKind::Success,
      "removed: {}",
      forwardSlashes(lazy.string())
    );
    anything = true;
  }

  auto const unwired = unwireFile(*home / ".bashrc");
  if (unwired == Unwire::Failed) { return 1; }
  anything = anything || unwired == Unwire::Changed;

  auto const bashPath = userDataRoot() / "completion" / "encro.bash";
  if (fs::remove(bashPath, ec)) {
    terminal::println(
      terminal::MessageKind::Success,
      "removed: {}",
      forwardSlashes(bashPath.string())
    );
    anything = true;
  }

  if (!anything) {
    terminal::println(terminal::MessageKind::Info, "nothing installed for bash");
  }
  return 0;
}

}  // namespace

int installScript(std::string const& shell) {
  auto const script = scriptFor(shell);
  if (!script.has_value()) {
    terminal::eprintln(
      terminal::MessageKind::Error,
      "unsupported shell '{}' (supported: bash, powershell)",
      shell
    );
    return 1;
  }
  if (shell == "powershell") { return installPowerShell(*script); }
  return installBash(*script);
}

int uninstallScript(std::string const& shell) {
  if (shell != "bash" && shell != "powershell") {
    terminal::eprintln(
      terminal::MessageKind::Error,
      "unsupported shell '{}' (supported: bash, powershell)",
      shell
    );
    return 1;
  }
  return shell == "powershell" ? uninstallPowerShell() : uninstallBash();
}

}  // namespace completion
