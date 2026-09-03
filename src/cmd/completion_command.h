// Executes `encro completion` subcommand actions (print/install/uninstall).
// Emission never touches the stored config; install/uninstall delegate to
// completion_install. Requires commandLineInit to have run first so the
// candidate registry is populated.
#pragma once

#include "cmd/cmd.h"

namespace cmd {

// Returns the process exit code; scripts go to stdout as raw bytes, messages
// through terminal output.
int runCompletionCommand(CmdParseResult const& cmd);

}  // namespace cmd
