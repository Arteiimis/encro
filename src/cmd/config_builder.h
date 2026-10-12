#pragma once

#include "cmd/cmd.h"
#include "core/app_context.h"
#include "core/error_handle.h"

#include <string_view>

namespace cmd {

auto buildConfig(CmdParseResult const& result) -> eh::Result<appctx::AppConfig>;

// True when `value` uses the single-run output-alias grammar (`+`, `=`,
// `input://`, `common://`, each with or without a suffix). The batch
// subcommand rejects these: they resolve against input paths batch does not
// have.
bool isOutputAlias(std::string_view value);

}  // namespace cmd
