# OpenRCT2 3D

This branch (`3d`) is an attempt to add a 3D viewing mode to OpenRCT2. Read [docs/3d-viewer.md](docs/3d-viewer.md) before changing rendering, the viewport, or the frame loop. That document is the memory of the decisions already made.

## Rule

Keep changes to the original codebase as small and as few as possible. All 3D renderer code lives in `src/openrct2-3d/`. The existing `src/openrct2/` and `src/openrct2-ui/` trees stay untouched except for the thin hooks that turn the feature on: the View menu entry, and a call into `src/openrct2-3d/` while that option is selected. Do not add extractor, mesh, camera, shader, or bgfx code under the original trees.

## Invariants

- The original 2D renderer is the default and must keep working. 3D is a second view of the same park, not a replacement.
- The switch lives in the existing View menu on the top toolbar (`WIDX_VIEW_MENU` in `src/openrct2-ui/windows/TopToolbar.cpp`, tooltip “View options”). Choosing it turns the main park view to 3D. Choosing it again returns to the original 2D renderer. Both modes call the same `gameStateTick()`.
- `gameStateTick()` remains the only park simulation. Guests, vehicles, rides, economy, and networking stay there. The 3D view reads the result. It does not advance the park.
- The renderer is [bgfx](https://github.com/bkaradzic/bgfx), an existing library. Its sources are the submodules under `src/openrct2-3d/third_party/`. Do not write a renderer, and do not embed Unity, Unreal, or Godot. After a fresh clone, run `git submodule update --init --recursive`.
- Do not grow the 3D world inside `OpenGLDrawingEngine`. That engine composites the 2D paletted screen.
- The original UI stays. bgfx composites its paletted buffer over the 3D image. The main viewport is the transparent hole. Ride, guest, and staff windows may keep their small 2D viewports.
- Do not commit RollerCoaster Tycoon 2 game data. The commercial files (`Data/g1.dat` and the rest of an install) stay outside the repository.

## Where the frame is split

- Simulation tick: `GameScene::Tick()` calls `gameStateTick()` in `src/openrct2/GameState.cpp`. The logical rate is 40 Hz (`kGameUpdateFPS`).
- UI and isometric park paint: `Painter::Paint()` calls `PaintWindows()`. The park image is the viewport widget, drawn by `WindowDrawViewport()` → `ViewportRender()`.
- A 3D mode skips that isometric viewport paint, runs a read-only extractor over `GameState`, and draws with bgfx.

## Build and run

Linux dependencies and the Ninja build are documented in the upstream wiki linked from `readme.md`. From a configured `bin/` directory the GUI binary is `./openrct2`. Point it at an original RCT2 install with `--rct2-data-path` (the directory that contains `Data/g1.dat`) and at this tree's data with `--openrct2-data-path`.

`scripts/run-tests` runs the unit tests. They do not need the commercial RCT2 files.
