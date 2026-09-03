// Installs/uninstalls generated completion scripts into shell startup
// locations (add-shell-completion design D5/D6). Idempotent: re-running with
// unchanged content reports "already current" and repairs missing wiring;
// uninstall reverses everything install created and is a no-op when absent.
#pragma once

#include <string>

namespace completion {

// Both return the process exit code; human-readable reports go through
// terminal output. `shell` is "powershell" or "bash".
int installScript(std::string const& shell);
int uninstallScript(std::string const& shell);

}  // namespace completion
