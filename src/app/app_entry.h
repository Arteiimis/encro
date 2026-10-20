#pragma once

#include "batch/batch.h"
#include "cmd/cmd.h"

#include <string>
#include <string_view>

namespace appentry {

auto helpIntroLine() -> std::string;

// The release token the help header and the version flag both print: `v` plus
// the declared version, plus the build's commit as semver build metadata unless
// the build has none to report (a git-less build renders the literal "none").
auto versionToken(std::string_view version, std::string_view commit) -> std::string;

auto runStatus(int exitCode, bool stopRequested) -> std::string;

// Assembles the batch request from the parsed command line: the single-run
// config builder fills the processing options (with the root in the input
// slot), the packed root resolves to -o or <root>/packed, and the toolchain is
// resolved once for the whole batch (skipped for pack-only runs, like the
// single-run path). Per-subfolder output and state locations are the driver's
// to derive.
auto buildBatchRequest(CmdParseResult const& cmd) -> eh::Result<batch::BatchRequest>;

int run(int argc, char* argv[]);

}  // namespace appentry
