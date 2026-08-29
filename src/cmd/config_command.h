// Executes `encro config` subcommand actions (design D4). Requires
// commandLineInit to have run first so the config-key registry is populated.
#pragma once

#include "cmd/cmd.h"

namespace cmd {

// Returns the process exit code; output goes through terminal (stdout for
// results, stderr for errors/warnings).
auto runConfigCommand(CmdParseResult const& cmd) -> int;

}  // namespace cmd
