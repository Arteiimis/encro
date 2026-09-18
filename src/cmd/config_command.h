// Executes `encro config` subcommand actions (design D4). Requires
// commandLineInit to have run first so the parse result carries the assembled
// config-key table.
#pragma once

#include "cmd/cmd.h"

namespace cmd {

// Returns the process exit code; output goes through terminal (stdout for
// results, stderr for errors/warnings).
int runConfigCommand(CmdParseResult const& cmd);

}  // namespace cmd
