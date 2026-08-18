## Why

The pack phase deflates every file in the archive by default, including media files that are already compressed (MP4/MKV/WebP/JPEG/…). Deflate on these gains ~0-2% size while burning CPU for every byte — for a batch of large videos this adds minutes of pure waste to the pack step.

## What Changes

- Packed files whose extension is on a media whitelist (video/audio/image containers whose payload is already compressed) are stored in the ZIP with `CompressionMethod::STORE` (no deflate).
- All other files (text, logs, data, unknown extensions) keep the current default deflate behavior.
- The archive remains fully standard ZIP: stored entries are readable by every unzipper; only the compression method per entry changes.

## Capabilities

### New Capabilities

- `pack-media-store`: pack entries of already-compressed media types are stored uncompressed in the archive; other entries keep deflate compression.

### Modified Capabilities

- none

## Impact

- `src/pack/packer.cpp`: per-entry compression decision (`addFile` + `setEntryCompressionConfig`/`CompressionMethod::STORE` on media entries).
- `src/pack/pack_types.h`: media extension whitelist.
- Both `packFilesToZip` overloads (progress and no-progress paths).
- Tests: unit assertions reading back entry compression methods; e2e zip inspection.
