# OpenRCT2 3D

3D viewing mode for OpenRCT2. SeiferX7 owns the work. King Grok relays direction. Further feature work waits for that direction.

## Repo

- Clone: https://github.com/seifer7/OpenRCT2-3D
- PR base: `develop`. The 3D viewer commit is `afae8ba` (**Add 3D viewer**), on OpenRCT2 `master` / `origin/master-2d` at `82b309798a` (**Release v0.5.6**).
- [PR #1](https://github.com/seifer7/OpenRCT2-3D/pull/1) is merged (`ba12c87`). `AGENTS.md` on `develop` now refers to `develop`. There is no `3d` branch.
- Never commit to `master-2d` or `develop-2d`. They mirror OpenRCT2 `master` (`82b309798a`) and `develop` (`3b7114c8ea`).
- OpenRCT2 release tags `v0.0.1` through `v0.5.6` are on this fork. [PR #2](https://github.com/seifer7/OpenRCT2-3D/pull/2) is merged (`cba2102`). It formats `ParkView.cpp` and `HardwareDisplayDrawingEngine.cpp`, and `.github/workflows/dependent-issues.yml` on `develop` skips unless the repository is `OpenRCT2/OpenRCT2`.

## Where the 3D code is

- Code: `src/openrct2-3d/`. Decisions already made: `docs/3d-viewer.md` and `AGENTS.md`.
- Submodules under `src/openrct2-3d/third_party/`: `bgfx` `fe9bbdec6611`, `bimg` `9bfbd6493b48`, `bx` `0e5bbca68780`, `bgfx.cmake` `5ae5fa5b1219`.
- Those four directories are empty until `git submodule update --init --recursive`. Pins are in the commit; contents were not initialized. Do not commit RollerCoaster Tycoon 2 game data.

## Decisions

- bgfx is the renderer. Do not write a renderer or embed Unity, Unreal, or Godot. Do not grow the 3D world inside `OpenGLDrawingEngine`.
- 2D stays the default. The View menu (`WIDX_VIEW_MENU`) switches the main park view. In 3D, bgfx draws the park and composites the original paletted UI, with palette index 0 as the hole. Ride, guest, and staff windows keep their small 2D viewports.
- `gameStateTick()` is the only simulation. A read-only extractor turns that state into meshes. The 3D view does not advance the park.
- Viewer space is sim `x / 32`, `y / 32`, `z / 32`. `ParkView.cpp` converts heights in `worldZ` as world Z times `kLandStepHeight / 16`, with `kLandStepHeight = 0.5f` (world Z / 32).
- 3D scenery and object models use 1 Blender unit = 1 tile = 32 sim units on every axis, matching the 3D viewer. Nothing else carries over from the previous project.

## Agent notes

Project notes, memories, decisions, surveys, and other working documents live in `docs/agents/` on `develop` and must be committed there. Write new notes in that folder. The Agent Store is not a substitute.
