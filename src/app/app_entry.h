#pragma once

#include "batch/batch.h"
#include "cmd/cmd.h"

#include <string>

namespace appentry {

auto helpIntroLine() -> std::string;

auto runStatus(int exitCode, bool stopRequested) -> std::string;

// Assembles the batch request from the parsed command line: the single-run
// config builder fills the processing options (with the root in the input
// slot), the packed root resolves to -o or <root>/packed, and the toolchain is
// resolved once for the whole batch (skipped for pack-only runs, like the
// single-run path).
auto buildBatchRequest(CmdParseResult const& cmd) -> eh::Result<batch::BatchRequest>;

int run(int argc, char* argv[]);

}  // namespace appentry
