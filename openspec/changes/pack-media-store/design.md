## Context

The pack phase currently deflates every entry via `zip.addFile(entryName, path)` with libzippp's default compression (see proposal.md - Why). Media containers are already compressed, so deflate on them costs CPU for ~0-2% size gain.

## Goals / Non-Goals

**Goals:**
- STORE (no deflate) for entries whose payload is already compressed.
- Keep deflate for everything else, including uncompressed containers (WAV/BMP/PPM/TIFF) where deflate still wins.

**Non-Goals:**
- Heuristic content sniffing (extension is the contract, per spec).
- Parallel ZIP compression or multi-archive merging.
- User-facing knobs for per-entry compression.

## Decisions

### D1: Whitelist lives in the pack types header, keyed on the source path extension

A `kStoredMediaExtensions` set (lowercase) plus a predicate `shouldStoreEntry(sourcePath)` next to `PackFileEntry` in `pack_types.h`. The predicate keys on `entry.sourcePath.extension()` (case-insensitive), not on the zip entry name — the collision-rename path (`makeUniqueZipEntryName`) preserves the extension, so nothing needs re-applying after a rename; the no-progress overload has no rename path at all.

### D2: Per-entry compression via `setEntryCompressionConfig`, not archive-level

libzippp 7.1: `addFile(entryName, file)` takes no compression parameter; `ZipArchive::setEntryCompressionConfig(ZipEntry&, CompressionMethod, level)` maps to `zip_set_file_compression` and works per entry while the archive is open. The archive-level default (`ZipArchive::setCompressionMethod`) cannot express mixed batches. Flow per entry: `addFile(...)` → `getEntry(name, State::Current)` → `setEntryCompressionConfig(entry, CompressionMethod::STORE, 0)` for media entries only, before `zip.close()`. Both `addFile` and `setEntryCompressionConfig` return bool and are silently ignorable, so the packer checks both results (existing code ignores `addFile`'s return; new code logs a warning on failure) — a null entry from `getEntry` must fail the entry rather than proceeding silently.

## Risks / Trade-offs

- [STORE on a whitelisted-but-actually-compressible file (e.g. a .ts with padding)] → Bounded: whitelist entries are container formats whose payload is dominated by compressed codec data; occasional slight size increase is acceptable in exchange for the speed win.
- [libzippp entry handle stale after later adds] → `getEntry` is called immediately after each `addFile`, before any further mutation; no stored handles are kept.
- [Whitelist drift (new format added, list not updated)] → Unknown extensions default to deflate — the conservative direction; the spec pins the list so drift is a spec change, not a silent behavior change.

## Migration Plan

Additive: existing archives are untouched; new archives differ only in per-entry compression methods, which every standard unzipper handles. Rollback = revert; no persisted state.

## Open Questions

None.
