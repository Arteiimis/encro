## Context

See `proposal.md` - Why. The two facts that shape the approach: Directory-mode packing builds one group for the whole input tree, so the pack stage's only existing checkpoint is the executor's pre-task check (`src/core/task_executor.cpp:111`); and `Packer::packFilesToZip` has two write loops - the entries+progress form (`src/pack/packer.cpp:401-433`, used with `--full-progress`) and the entries+callbacks form (`:484-508`, used by compact mode, which is the pack-only default).

## Goals / Non-Goals

**Goals:**

- A stop requested while an archive's entries are being written ends that archive at its next entry, with no half-written archive left on disk.
- Packing reports the abort through its existing funnel: one `Packing canceled by user.` notice and the cancellation exit code, in both resumable and non-resumable runs.

**Non-Goals:**

- Guaranteeing a notice when the force-exit watchdog ends a run that reached no checkpoint - unchanged, documented in `cancellation-reporting`.
- Splitting Directory-mode packs into more, smaller archives so the executor's check fires sooner - that changes every pack-only run's archive count and names.
- Interrupting libzippp's write of a single entry: the checkpoint is between entries, so one very large entry still finishes before the stop is seen.

## Decisions

### D1: The checkpoint lives in the packer's entry loops

Both loops check `stopsignal::isStopRequested()` at the head of each entry and return `Packing canceled by user.` on a pending stop. Alternative considered: split Directory-mode packs into more groups (the backlog's second option) - rejected because it changes the archives every pack-only run produces, a far larger behaviour change than adding a checkpoint where the time is actually spent.

### D2: An aborted archive is discarded, not left to `removeOnFailure`

On the abort the loop calls `libzippp::ZipArchive::discard()` before returning. libzip materialises the archive only at `close()`, so the discard drops it before anything is committed: a fresh output path is left with no file at all, and a pre-existing file at that path stays untouched. Alternative considered: leave the archive for `PackTaskRecorder::fail` (`src/pack/pack_service.cpp:51-66`) to remove - but that removal is gated on `plan.removeOnFailure`, and pack-only builds its request without it (`src/app/pipeline.cpp:110-122`), so the archive's destructor would commit a partial zip holding fewer entries than the input tree. The packer owns the archive it opened, so it discards it on abort; a resumable run re-executes the interrupted archive task from the job state, so nothing depends on reading a partial archive.

### D3: The message matches the pack stage's existing cancellation error

The returned error is `Packing canceled by user.`, the same string `PackService::packGroups` already returns for a stop-skipped batch (`src/pack/pack_service.cpp:315`). The funnel's translation is untouched: `runNonResumable` / `runResumable` map an aborted batch with a pending stop to the cancellation exit code (`src/pack/pack.cpp:46-59`, `:409-413`) and `execute(PackRequest const&)` prints the single notice (`pack.cpp:486-490`).

## Risks / Trade-offs

- [A caller wanted the partial archive] -> no caller reads a partial archive: the resumable path re-runs the interrupted task, and the non-resumable path reports the run as aborted.
- [One `isStopRequested()` load per entry] -> it is an atomic read plus a one-time log record against per-entry file I/O; the entry loop is also where the duplicate-name bookkeeping already runs per entry.
- [A single huge entry still outlives the stop] -> accepted and recorded as a non-goal; the watchdog remains the backstop for a run that reaches no checkpoint.
