# QA review summary — PR #6

Point-in-time review. This note records the review as it stood when it was written. It is not a live status of the pull request.

**Merged as reviewed.** GitHub merged PR #6 on 2026-11-01 at 03:35:56 UTC. The merged head is `459d931e1c373eb191000b9f2d957913d2e9af54` (merge commit `0addc497e57f5cb626cc22ce1cec103d7ff9c77d` on `develop`). That is the head this review checked. The fixes requested below had not landed. They are expected in follow-up pull requests.

- Pull request: "Draw small scenery in the 3D view from GLB models" (`cursor/scenery-glb-milestone-1ca6` → `develop`, base `ba627e2808`).
- **Head reviewed:** `459d931e1c373eb191000b9f2d957913d2e9af54` (pushed 2026-11-01 13:18:54 AEST). The head had moved from `f98c30ac`; everything below was re-checked at `459d931e`.
- Finished 2026-11-01 ~13:30 AEST. Read-only. Line references are at that head. Paths are under `src/openrct2-3d/src/` unless shown otherwise.

## Recommendation

**Request changes.** Do not merge yet.

CI is red at the head (macOS x64 and clang-tidy). Windows MSVC x64 and win32 were green. The draw path also cannot reach 10,000+ distinct 1-tile models: a full map walk every frame, synchronous loads on the render thread, one set of bgfx handles per model against the 4096 handle limits, and about 330 KB of VRAM per model. The milestone runs correctly with one model (TCRP). Fallback colour, ghost, and wither logic matches 2D.

## Blockers

1. **CI red: macOS x64.** `test/tests/ModelCacheTests.cpp:15` and `:86-110` use `std::filesystem` (`path`, `temp_directory_path`, `operator/`). Those APIs are unavailable for the macOS 10.14 x64 deployment target (`ModelCacheTests.cpp:86:26`). Use `OpenRCT2::Path` / `Platform` helpers.
2. **CI red: clang-tidy.** `readability-uppercase-literal-suffix` (warnings as errors) at `SceneryDraw.cpp:78` (`0x9e3779b97f4a7c15ull`) and `ModelCache.cpp:62` and `:66`. Use `ULL`.
3. **Full map walk every frame.** `scenerySignature()` (`SceneryDraw.cpp:294-330`, called from `drawScenery` at `:484`) walks every tile and element each frame. Any change then runs a full `rebuild()` (`:332-442`), which allocates a `std::string` identifier per element (`:370-375`) and does a linear `spriteKeys` search (`:408-416`).
4. **Model load runs on the render thread.** `modelHandle()` (`SceneryDraw.cpp:130-160`) calls `loadModelForDraw()` (`:139`) inside `rebuild()`. That path can read the cache, hash the body, and on a stamp mismatch read, hash, parse, and write the cache (`ModelCache.cpp:699-761`, writes at `:735` and `:755`). `meshDrawUploadModel` then decodes the PNG (`MeshDraw.cpp:211-241`). The same stall follows every device recreate, because `Park::release()` drops all handles (`View3D.cpp:370`, `:384`, `:409`, `:446`, `:462`, `:596`, `:676`).
5. **bgfx handle limits cap the model count at about 4k.** Each primitive gets its own VB, IB, and texture (`MeshDraw.cpp:243-282`). Each sprite face gets its own texture plus a 4-vertex VB/IB (`MeshDraw.cpp:431-470`). Defaults, not overridden in `src/openrct2-3d/third_party/bgfx.cmake/CMakeLists.txt:92-102` or the vcxproj, are 4096 textures, 4096 VBs, and 4096 IBs, shared with the rest of the park view.
6. **Memory at 10k models.** Per TCRP model: VB about 61 KB, IB 5 KB, 256² RGBA8 texture 256 KB, about 328 KB in total, so about 3.3 GB of VRAM at 10k distinct models. `_models` (`SceneryDraw.cpp:67`) and `_sprites` (`:68`) never evict.

## Should fix

7. **Instances dropped silently.** `submitPrimitive` (`MeshDraw.cpp:284-328`) copies every instance into a transient buffer and `break`s when `getAvailInstanceDataBuffer` returns 0 (`:293-297`).
8. **One draw call per batch per primitive.** `drawScenery` (`SceneryDraw.cpp:473-506`) submits once per batch per primitive. `hideBatch` calls `WindowGetMain()` per batch per frame (`:444-457`).
9. **Sprite cache scans linearly and never shrinks.** `spriteHandleForFace` (`SceneryDraw.cpp:242-292`) scans `_sprites` on every lookup. The key includes `face`, so one image can be uploaded up to four times (`MeshDraw.cpp:431-470`).
10. **Sprites keep a stale palette.** `texturePalette` (`SceneryDraw.cpp:237-240`) samples LightFx or `gPalette` once, when the sprite is baked.
11. **Startup still reads and hashes every GLB.** `convertFile` (`ModelCache.cpp:589-617`) reads and hashes the GLB, then `cacheIsCurrent` (`:231-274`) reads and hashes the cache, during context init (`SceneryDraw.cpp:107-118`). The draw path accepts size and mtime (`ModelCache.cpp:721`).
12. **Cache body hashed twice per model per session.** The startup pass hashes it, then `readCache` hashes it again (`ModelCache.cpp:379+`). `prepareModelCache` keeps no meshes (`:649-697`).
13. **Cache replace is not atomic.** `writeCache` (`ModelCache.cpp:283-365`) deletes the cache and then moves the new file into place.
14. **`meshDrawInit` never retries.** It sets `_ready = true` even when shader or program creation fails (`MeshDraw.cpp:331-351`).
15. **Fallback gaps compared with 2D.** Colour, ghost, and wither match (`SceneryDraw.cpp:177-185`, `:193-203`, `:348-355`). `SceneryDraw.cpp:208-235` does not draw the hasGlass overlay, animated frames, supports, or the selected-element and track-design-save palettes (see `src/openrct2/paint/tile_element/Paint.SmallScenery.cpp`). GLB models also ignore primary and secondary colours and withering; the milestone should say whether that is deliberate.
16. **The VS test define compiles; the test does not prove the path.** `test/tests/tests.vcxproj:61-62` and `test/tests/GlbMeshTests.cpp:25-46`. `OPENRCT2_TCRP_GLB` is correct on the MSVC CI command line. `tcrpGlbPath()` still falls back to `../data/...`, and tests run from `bin\`, so a pass does not prove the define resolves to the file.

## Also recorded

Linux Ninja build and 290 tests passed at this head. No local Visual Studio build was run; MSVC coverage is CI only (x64 and win32 passed, arm64 still running at review time). No 10k benchmark was run. The in-game X-sprite fallback, remap colours, ghost, wither, glass, and quarter-tile placement were not checked in game. The raw-string define in `tests.vcxproj` works with MSBuild and cl.exe; hardening belongs in the test (item 16), and no define change is required to merge.
