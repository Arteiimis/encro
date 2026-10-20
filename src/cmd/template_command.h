// Executes `encro template` subcommand actions (spec: command-templates).
// Requires commandLineInit to have run first: the parse result carries the
// assembled template table and, for `save`, the values captured from the
// command line.
#pragma once

#include "cmd/cmd.h"

namespace cmd {

// Returns the process exit code; output goes through terminal (stdout for
// results, stderr for errors/warnings).
int runTemplateCommand(CmdParseResult const& cmd);

}  // namespace cmd
