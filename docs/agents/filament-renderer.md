# Filament renderer integration

The `Filament` branch replaces bgfx with Filament 1.80.0 in the visualization layer. OpenRCT2 remains the simulation and UI owner. See [build setup](../../src/openrct2-3d/README.md).

## Ownership and contracts

- `FilamentRenderer.cpp` owns the device, scene, material packages, lighting, instance buffers, and offscreen targets. `FilamentView.cpp` implements the existing View3D boundary.
- The game window and GL context stay with OpenRCT2. Filament renders separately; synchronous readback supplies RGBA to the existing presenters. SDL's current GL context is detached/restored around these operations.
- The OpenGL display engine must reset its framebuffer, shader, and drawing-context owners while its SDL context is current, before `SDL_GL_DeleteContext`. C++ otherwise destroys those members after the destructor body, causing `GL_INVALID_OPERATION` in texture/framebuffer/shader deletion. Keep GL error reporting enabled; confirm shutdown and display-engine switching in-game after changes to this order.
- Filament readback is already top-row-first. The target color texture must have `BLIT_SRC` usage. `flushAndWait()` waits for completion and dispatches the buffer callback before the caller consumes pixels.
- Model/sprite IDs, atlas allocation, eviction, and terrain caches remain. Shared slab indices are 32-bit and rebased once when uploaded because Filament binds the full vertex buffer.
- GPU vertices are packed to position, tangent quaternion, UV0, tile XY in UV1, and color bytes. Budget accounting uses the actual packed stride and 32-bit indices, not the CPU mesh layout.
- All viewer materials explicitly set `flipUV : false`. Filament defaults to flipping V in both UV sets, but our sprite/model UVs already match top-row-first pixel uploads. The default inverted near sprites and redirected distant atlas samples; it also corrupted terrain's world tile Y in UV1. Do not compensate by flipping the whole rendered frame.
- Scene batches retain entities/instance buffers. Buffer and material destruction invalidates referencing renderables first. Release park/scenery resources before destroying the engine.
- Camera conversion flips view-space Z in both view and projection factors. Their product remains unchanged, keeping CPU visibility consistent with the existing camera.
- Filament's NOOP backend supports windowless unit tests of resource/submission lifetimes. It is not a substitute for visual verification.

## Dependencies

The SDK and `matc`/`resgen` are pinned together. Both build systems use the same material compiler script. Windows SDK runtime directories must match OpenRCT2's CRT configuration. Official Windows packages require MSVC x64; custom architectures/ABIs need matching SDKs. bx remains for camera math, and the bimg checkout supplies only stb's image header. The bgfx and bgfx.cmake submodules are removed.

## Validation and limitations

Validation completed during implementation:

- Full Visual Studio workspace build passed, including the Windows executable and native test executable.
- The isolated Windows x64 CMake compile/link check passed with the final renderer source. Materials compiled for desktop OpenGL, Vulkan, and Metal.
- 73 selected tests ran: 71 passed, 2 skipped. All 9 new Filament tests passed, including real resource creation/submission/release through the NOOP backend. Existing GLB, cache, scale, sprite, and CPU scenery coverage also passed. The skips were an existing filesystem timestamp-resolution check and an opt-in 10,000-item cache benchmark.
- Run native tests from `bin/` so relative `testdata/` paths resolve. The final filter excluded the three scenery game-context fixtures: `DefaultCameraDrawsParkScenery`, `DefaultCameraDrawsDistantSprites`, and `NearFacesOutrankTheSpriteCap`. An earlier broader unit-test run included the latter two NOOP fixtures; they passed, but they do not establish visual correctness.
- Linux and macOS SDK archive checksums, extraction, and required library names were verified. Their archives have a top-level `filament/` directory that the fetch script normalizes. No Linux/macOS build or runtime validation was performed.
- Final source searches found no remaining bgfx API/header use outside historical notes and retained utility dependencies. Whitespace checks passed.

The NOOP test caught an invalid alpha-cutoff call on opaque materials; configuration now calls `setMaskThreshold` only for masked materials. No game executable or preview was launched. In-game checks belong to SeiferX7 under the repository testing rules.

User checks should cover the 2D/3D toggle, both display engines, resizing, camera orientation, paths/terrain, water animation/transparency, crossed sprites, GLB models, UI opacity, and shutdown. Also compare shadows/AO/reflections with their cost. Linux/macOS build and runtime behavior require validation on those platforms.

This is a basic material port, not full glTF PBR-material support. The environment is a simple placeholder; screen-space reflections cannot include off-screen geometry. Synchronous readback can limit frame rate. Lighting, alpha edges, water reflections, and visual parity are not established by compilation or NOOP tests.

## Material orientation regression check

From the repository root, run `cmake "-DFILAMENT_SDK=lib/filament-v1.80.0" "-DOUTPUT_DIR=obj/filament-uv-check" -P src/openrct2-3d/cmake/TestMaterialUvs.cmake`. This compiles all six variants and uses the pinned SDK's `matinfo` to check that generated vertex shaders preserve UV0 and terrain/water UV1. It requires no graphics device or game run. It guards the automatic-flip regression, not visual correctness of individual assets. SeiferX7 should confirm upright near/distant scenery and unchanged terrain mapping in-game.
