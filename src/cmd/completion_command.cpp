#include "cmd/completion_command.h"

#include "cmd/completion_emitter.h"
#include "cmd/completion_install.h"
#include "cmd/completion_registry.h"
#include "cmd/template_store.h"
#include "infra/terminal.h"

#include <iostream>
#include <string>
#include <vector>

namespace cmd {

namespace {

// Resolves one dynamic slot's live candidates into `out`; false when the slot
// id is unknown, which the caller reports instead of silently answering empty
// (a script from a newer build asking an older binary gets a clear failure).
bool slotCandidates(std::string const& slot, std::vector<std::string>& out) {
  if (slot != completion::kTemplateNameProvider) { return false; }
  for (auto const& name: templatestore::list(templatestore::resolveTemplatesDir())) {
    // The store reports lowercased file stems without validating them, so a
    // hand-placed stem the name rules reject is skipped: it is not a template
    // anyone could select, and it would break the glue's word splitting.
    if (templatestore::normalizeName(name) != name) { continue; }
    out.push_back(name);
  }
  return true;
}

}  // namespace

int runCompletionCommand(CmdParseResult const& cmd) {
  // The candidate channel comes first: it needs no shell and never installs.
  if (cmd.completionCandidates.has_value()) {
    auto names = std::vector<std::string>{};
    if (!slotCandidates(*cmd.completionCandidates, names)) {
      terminal::eprintln(
        terminal::MessageKind::Error,
        "unknown completion slot: {} (supported: {})",
        *cmd.completionCandidates,
        completion::kTemplateNameProvider
      );
      return 1;
    }
    // terminal::write goes through C stdio, the stream output captures
    // redirect (std::cout's own buffer escapes it once sync_with_stdio is off).
    for (auto const& name: names) {
      terminal::println(terminal::MessageKind::Plain, "{}", name);
    }
    return 0;
  }

  if (!cmd.completionInstall && !cmd.completionUninstall) {
    // Bare invocation shows the subcommand help; a shell prints its script.
    if (cmd.completionShell.empty()) {
      std::cout << cmd.helpText();
      return 0;
    }
    auto const script = completion::scriptFor(cmd.completionShell);
    if (!script.has_value()) {
      terminal::eprintln(
        terminal::MessageKind::Error,
        "unsupported shell '{}' (supported: bash, powershell)",
        cmd.completionShell
      );
      return 1;
    }
    std::cout << *script;
    return 0;
  }

  if (cmd.completionShell.empty()) {
    terminal::eprintln(
      terminal::MessageKind::Error,
      "specify a shell: encro completion --{} <powershell|bash>",
      cmd.completionInstall ? "install" : "uninstall"
    );
    return 1;
  }
  return cmd.completionInstall ? completion::installScript(cmd.completionShell)
                               : completion::uninstallScript(cmd.completionShell);
}

}  // namespace cmd
