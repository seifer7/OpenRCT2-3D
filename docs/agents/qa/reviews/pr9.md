# QA review summary — PR #9

Point-in-time review. This note records the review as it stood when it was written. It is not a live status of the pull request.

**Merged as reviewed.** GitHub merged PR #9 on 2026-11-01 at 05:29:43 UTC (15:29:43 AEST). The merged head is `22b1328ff3f28182344dbe5b5de81e8f675d6a87` (merge commit `fee6cd2087cef262894e04b1b7e62cf7bf78390e` on `develop`). That is the head this review checked. The `Ready-To-Merge` label was applied at 05:22:21 UTC (15:22:21 AEST), after the READY verdict. The non-blocking items below had not landed at merge.

- Pull request: [#9](https://github.com/seifer7/OpenRCT2-3D/pull/9) "Harden the scenery model cache and its tests" (`cursor/scenery-cache-review-fixes-1ca6` → `develop`, merge base `6685479181`).
- **Head reviewed:** `22b1328ff3f28182344dbe5b5de81e8f675d6a87`. It did not change during the review (checked at the start and at 15:12 AEST). 17 files, +588/−130.
- Two reviews: the first at `f7ed5b1c62` (2026-11-01, early afternoon AEST), the second at `22b1328ff3` (finished 2026-11-01 ~15:13 AEST). Both were read-only. Line references are at `22b1328ff3`. Paths are under `src/openrct2-3d/src/` unless shown otherwise.

## Scope

- Model cache hardening: a header-only startup check, an mtime-only stamp refresh, a cache version bump to 3, a checksum over bytes 0–31 and 40..EOF, and case-preserving identifiers.
- A nanosecond mtime helper moved into the platform layer: `Platform::GetLastModifiedNs` (`src/openrct2/platform/Win32.cpp:505`, `Posix.cpp:256`, `Android.cpp:191`) behind `File::GetLastModifiedNs` (`src/openrct2/core/File.cpp:145`).
- Test rework in `test/tests/ModelCacheTests.cpp`: no `std::filesystem`, and a Windows temp dir from `GetTempPathW`.
- An appimagetool icon fix in `scripts/build-appimage`.
- No GLB model changes. The testdata TCRP copy is byte-identical to `data/object-models/`.

## Recommendation

**Ready to merge.** The first review had no blockers, and all nine of its should-fix and nit items are fixed. The macOS x64 `std::filesystem` failure inherited from PR #6 is gone: `ModelCacheTests.cpp` no longer uses `<filesystem>`, and macOS x64 passes on this head. The remaining items are docs and cosmetic fixes that can go in a follow-up.

## CI at review time

53 check runs on `22b1328ff3`: 51 success, 2 skipped (`labeler`, `check`), 0 failures (final check 15:12 AEST).

- Windows x64, arm64 and win32 passed.
- macOS x64, macOS arm64 and the universal bundle passed.
- All Linux jobs passed, including the AppImage job this head fixes.
- Android, Emscripten, clang-tidy, code formatting, changelog formatting and Release passed.

## Tests

- Linux, GCC, Release, incremental build of the head: exit 0. The only warnings are the existing LTO linker warnings for glad symbol types; none is first-party.
- `ctest -j8`: 296/296 passed. `ModelCache*` / `GlbMesh*`: 17/17 passed.
- First review at `f7ed5b1c62`: clean build, 294/294 tests passed, and `ModelCache|GlbMesh` passed with `--repeat until-fail:20`.

## Earlier findings (first review) — all fixed

1. Windows temp dir on a non-ASCII profile: `GetTempPathW` plus `String::toUtf8` (`test/tests/ModelCacheTests.cpp:99-105`).
2. Uppercase GLB names skipped silently: `identifierIsSafe` keeps case (`ModelCache.cpp:854`), and each rejected file is logged (`ModelCache.cpp:762`). New tests at `ModelCacheTests.cpp:412` and `:426`.
3. `<windows.h>` in `ModelCache.cpp`: moved to the platform helpers listed under Scope.
4. Test copies of the checksum and mtime helpers: `cacheFileChecksum` and `cacheSourceModifiedNs` are now exported from `ModelCache.hpp`.
5. A full CpuMesh was built per model at startup: `readCacheHeader` (`ModelCache.cpp:584`) reads 68 bytes, and `refreshCacheTime` (`:640`) re-signs the stamp only after the checksum verifies.
6. One warning per GLB on a v2 upgrade: replaced by one `LOG_INFO` summary (`ModelCache.cpp:825`), with `LOG_VERBOSE` on the draw path (`:910`).
7. `utimes`: replaced with `utimensat` (`ModelCacheTests.cpp:307`).
8. `TamperedCountFallsBackToGlb`: comment added.
9. Duplicate testdata GLB: `data/object-models/README.md` now says "Update both files together".

## Blockers

None.

## Should fix

1. **Heading lost its space.** `data/object-models/README.md:1` changed from `# Object models` to `#Object models`. CommonMark no longer renders it as a heading. It came in `fd1009c014`, probably from a formatter run.
2. **Same problem in the milestone doc.** `docs/agents/scenery-3d-milestone.md:1` changed from `# Scenery milestone: …` to `#Scenery milestone : …`.

## Nits

3. `ModelCache.cpp:667` and `:669` use the literal offsets 8 and 24 for size and hash, while mtime uses `kModelCacheMtimeOffset`.
4. `readCacheHeader` (`ModelCache.cpp:589`) handles a missing cache by catching the exception from `FileStream`. That is one throw per model on a first launch. A `File::Exists` check first would be cheaper.
5. The duplicate check is an exact string match (`ModelCache.cpp:765`). On a case-insensitive filesystem, `MyTree.glb` and `mytree.glb` both pass it, write the same `.o3d`, and overwrite each other on every launch. This is an edge case only.
6. `scripts/build-appimage:17` copies the 256 px icon without checking that it exists. CMake always installs it (`CMakeLists.txt:530`), so this is fine today.

## Also recorded

- Correctness checks:
  - The header check rejects a bad magic, an old version, a short header, and a converter or limits mismatch before the stamp is trusted.
  - `refreshCacheTime` never re-signs a corrupt file.
  - The Win32 FILETIME arithmetic matches the old code. The Posix and Android mtime paths are separate.
- At 10k+ models, startup reads only the headers of unchanged models.
- No render path, 2D renderer, save/load or remap colour code changed.
- No local Visual Studio build was run; MSVC coverage comes from CI only.

QA VERDICT: READY at 22b1328ff3f28182344dbe5b5de81e8f675d6a87
