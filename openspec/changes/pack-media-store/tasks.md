## 1. Media extension whitelist

- [x] 1.1 Write failing unit tests for the media-extension predicate: every whitelisted extension (lowercase and uppercase) is media, uncompressed containers (`.wav`, `.bmp`, `.ppm`, `.tif`/`.tiff`, `.aiff`) and unknown extensions are not, case-insensitivity
- [x] 1.2 Implement the whitelist constant and `shouldStoreEntry` predicate in `pack_types.h`, keyed on the source path extension (case-insensitive)

## 2. STORE compression for media entries

- [x] 2.1 Write failing unit tests reading back entry compression methods from an archive: media entries are STORE, non-media entries stay DEFLATE, mixed batch behaves correctly; `addFile`/`setEntryCompressionConfig` failure surfaces as an error, not silence
- [x] 2.2 Apply `CompressionMethod::STORE` to media entries in both `packFilesToZip` overloads (progress and no-progress paths): `addFile` → `getEntry(name, Current)` → `setEntryCompressionConfig(entry, STORE, 0)`, checking the bool results; predicate keys on `entry.sourcePath.extension()` so the rename path needs no re-application
- [x] 2.3 Add a compression-aware helper to the e2e zip utilities (the existing `listZipEntries` returns names only) and assert media vs text entry compression methods in an e2e pack test

## 3. Verification

- [x] 3.1 Full verification: build, unit + e2e suites, `xmake test-report`
