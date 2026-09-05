// `encro organize` command body (task 5.1): model management, provider
// notice, pipeline run, and the end-of-run report.
#pragma once

#include <string>

struct CmdParseResult;

namespace organize {

// `encro organize` command body (task 5.1): model management, provider
// notice, pipeline run, and the end-of-run report.
auto runOrganizeCommand(CmdParseResult const& cmd) -> int;

}  // namespace organize
