# OpenRCT2 3D

This branch (`Filament`), created from `develop`, uses Google Filament instead of bgfx for OpenRCT2's experimental 3D viewing mode. Read [docs/3d-viewer.md](docs/3d-viewer.md) and [the integration notes](docs/agents/filament-renderer.md) before changing rendering, the viewport, or the frame loop. In-game visual validation remains a user task.

## Rule

Keep changes to the original codebase as small and as few as possible. All 3D renderer code lives in `src/openrct2-3d/`. The existing `src/openrct2/` and `src/openrct2-ui/` trees stay untouched except for the thin hooks that turn the feature on: the View menu entry, and a call into `src/openrct2-3d/` while that option is selected. Do not add extractor, mesh, camera, shader, or renderer-specific code under the original trees.

## Invariants

- The original 2D renderer is the default and must keep working. 3D is a second view of the same park, not a replacement.
- The switch lives in the existing View menu on the top toolbar (`WIDX_VIEW_MENU` in `src/openrct2-ui/windows/TopToolbar.cpp`, tooltip “View options”). Choosing it turns the main park view to 3D. Choosing it again returns to the original 2D renderer. Both modes call the same `gameStateTick()`.
- `gameStateTick()` remains the only park simulation. Guests, vehicles, rides, economy, and networking stay there. The 3D view reads the result. It does not advance the park.
- Use [Google Filament](https://github.com/google/filament) 1.80.0 for the visualization layer. Keep its SDK and material compiler versions matched. Do not write a replacement graphics engine or embed Unity, Unreal, or Godot. Run `git submodule update --init --recursive` for the remaining bx/stb utilities; see `src/openrct2-3d/README.md` for SDK setup.
- Do not grow the 3D world inside `OpenGLDrawingEngine`. That engine composites the 2D paletted screen.
- The original UI and presenter stay. Filament renders an independent offscreen image; the existing OpenRCT2 display path composites the UI over it. Never hand the game window or its GL context to Filament. The main viewport is the transparent hole. Ride, guest, and staff windows may keep their small 2D viewports.
- Do not commit RollerCoaster Tycoon 2 game data. The commercial files (`Data/g1.dat` and the rest of an install) stay outside the repository.

## Where the frame is split

- Simulation tick: `GameScene::Tick()` calls `gameStateTick()` in `src/openrct2/GameState.cpp`. The logical rate is 40 Hz (`kGameUpdateFPS`).
- UI and isometric park paint: `Painter::Paint()` calls `PaintWindows()`. The park image is the viewport widget, drawn by `WindowDrawViewport()` → `ViewportRender()`.
- A 3D mode skips that isometric viewport paint, runs a read-only extractor over `GameState`, and draws with Filament. Preserve this simulation boundary.

## Build and run

Linux dependencies and the Ninja build are documented in the upstream wiki linked from `readme.md`. From a configured `bin/` directory the GUI binary is `./openrct2`. Point it at an original RCT2 install with `--rct2-data-path` (the directory that contains `Data/g1.dat`) and at this tree's data with `--openrct2-data-path`.

`scripts/run-tests` runs the unit tests. They do not need the commercial RCT2 files.

## Agent notes

Project notes, memories, decisions, surveys, and other working documents live in `docs/agents/` and must be committed there. Write new notes in that folder going forward, not only in the Agent Store or on a VM.

This includes procedures, implementation plans and handoffs. If a tool stores a plan outside the repository, preserve its actionable content and outcome in the relevant repository document before finishing. See [working procedures](docs/agents/working-procedures.md) and the [agent documentation index](docs/agents/README.md).
