// Pipeline orchestration (task 4.x): scan -> cache -> analyze -> route ->
// cluster -> execute -> report. Progress is optional (tests run barless).
#pragma once

#include "organize/organize_types.h"
#include "organize/report.h"
#include "tagger/tagger.h"

#include "core/progress.h"

namespace organize {

// Runs the full organize pipeline over `options`. Never moves or deletes
// originals; dryRun skips the execute stage. Returns the report data; an
// error means the run could not start or was interrupted (completed
// analysis stays cached either way).
auto runOrganize(
  Options const& options,
  tagger::TaggerEngine& engine,
  progress::ProgressContext* progress
) -> eh::Result<ReportData>;

}  // namespace organize
