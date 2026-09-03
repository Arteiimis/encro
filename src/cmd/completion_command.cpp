#include "cmd/completion_command.h"

#include "cmd/completion_emitter.h"
#include "cmd/completion_install.h"
#include "infra/terminal.h"

#include <iostream>

namespace cmd {

int runCompletionCommand(CmdParseResult const& cmd) {
  if (!cmd.completionInstall && !cmd.completionUninstall) {
    // Bare invocation shows the subcommand help; a shell prints its script.
    if (cmd.completionShell.empty()) {
      std::cout << cmd.helpText;
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
      "specify a shell: encro completion <bash|powershell> --{}",
      cmd.completionInstall ? "install" : "uninstall"
    );
    return 1;
  }
  return cmd.completionInstall ? completion::installScript(cmd.completionShell)
                               : completion::uninstallScript(cmd.completionShell);
}

}  // namespace cmd
