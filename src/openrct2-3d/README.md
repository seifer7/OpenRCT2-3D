# openrct2-3d

All of the 3D viewer lives in this directory. The original OpenRCT2 sources stay as they are, apart from the small hooks that open this view.

The `Filament` branch uses [Google Filament](https://github.com/google/filament) 1.80.0 instead of bgfx. The original simulation, default 2D view, controls, and UI remain. See [the branch scope](../../docs/3d-viewer.md#filament-branch-scope).

## Dependencies and build

- Run `git submodule update --init --recursive` for the remaining utilities: bx supplies camera math; bimg supplies only `stb_image.h`. Neither bgfx nor bimg's renderer/image library is compiled.
- The default SDK location is `lib/filament-v1.80.0/` at the repository root. CMake and the Visual Studio renderer project download the matching pinned release when it is absent and verify its SHA-256 checksum. The Windows download is approximately 868 MB and expands to a larger SDK; network access and disk space are needed on the first build.
- To install explicitly, run `cmake -P src/openrct2-3d/cmake/FetchFilament.cmake` from the repository root. `FILAMENT_PLATFORM` can select `windows`, `linux`, `arm-linux`, or `mac` when needed.
- To use an existing SDK, set the `FILAMENT_SDK` environment variable or CMake's `-DFILAMENT_SDK=...`. It must contain `include/`, `lib/`, and the matching `bin/matc` and `bin/resgen` tools. Keep SDK/compiler versions and C++ ABIs matched; cross-builds require a target SDK with host-runnable material tools.
- **Visual Studio:** select **x64** in `openrct2.sln`. `Filament.props` selects the SDK's `mt`, `mtd`, `md`, or `mdd` libraries to match the configuration. `FilamentSdk` and `FilamentLibraryDir` are MSBuild overrides. Official Windows packages are x64 only; custom ARM64 libraries are required for that configuration. Win32 and MinGW are not supported by the pinned desktop SDK.
- **CMake:** Linux and macOS use the same SDK and material targets. `FILAMENT_LIBRARY_DIR` can override a custom SDK's library layout. Native Linux requires OpenGL/EGL development libraries. macOS links Metal and the required Apple frameworks. These platforms require their own build/runtime validation.
- `cmake/CompileMaterials.cmake` compiles the sources in `materials/` for OpenGL, Vulkan, and Metal, then embeds the packages in the static renderer library. No shader files are loaded from the working directory at runtime.

## Implementation

- `FilamentRenderer.cpp` owns the device, scene, materials, instanced renderables, lighting, offscreen targets, and readback. The selected backend is OpenGL on Windows/Linux and Metal on macOS.
- `FilamentView.cpp` preserves the public View3D controls and integrates the read-only park view with OpenRCT2's presenters. Filament does not borrow the game's GL context or present its window.
- `ParkView.cpp` retains terrain/path extraction, cached surface images, animated water, and camera behavior. `MeshDraw.cpp` retains model/sprite IDs, shared geometry slabs, and texture atlases.
- The original hardware display composites paletted UI over the captured park on the CPU; the OpenGL display draws its existing UI over the captured park texture. Palette index 0 reveals the park.

Sun shadows, screen-space AO/reflections, and basic environment lighting are enabled. Existing GLBs still use the existing loader; this is not a full glTF PBR-material import. Distant sprites remain unlit atlas batches. Synchronous readback is a compatibility-first integration and may limit frame rate. Screen-space reflections cannot reflect off-screen geometry and do not promise accurate water reflections. Visual tuning and in-game verification remain outstanding.

`ViewerScale.hpp` is the vertical scale. `GlbMesh.cpp` loads a GLB with cgltf. `SceneryDraw.cpp` instances that mesh, or draws the original four direction sprites on two crossed quads. Shipped models live in the OpenRCT2 data directory under `object-models/`, named by object id (`rct2.scenery_small.tcrp.glb` is the Corsican Pine). `preview/ModelPreview.cpp` uses the same Filament backend; do not run it as an automated in-game verification substitute.

The game still simulates in `gameStateTick()` and still draws with the original 2D renderer unless the View menu option is on. See [docs/3d-viewer.md](../../docs/3d-viewer.md).
