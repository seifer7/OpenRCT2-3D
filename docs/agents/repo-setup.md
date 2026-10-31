---
cursor:
  subagentId: "bc-9d6066d8-20f0-5cc1-a1fa-e3b558b1a846"
---

# Repository setup

Read access works. The checkout is already on `develop`, clean, and matches `origin/develop`. No code was changed.

- Clone URL: https://github.com/seifer7/OpenRCT2-3D
- `develop` tip: `afae8ba` (`afae8baad587bd0981fad62ad6267ce5bc5c9081`), subject **Add 3D viewer**
- Parent of that commit is `82b309798a` (**Release v0.5.6**), which is OpenRCT2 `master` and `origin/master-2d`. `develop` is that commit plus this one squashed commit (1 commit ahead, 41 files).
- PR base: `develop` (`origin/HEAD` → `origin/develop`).
- Never commit to `master-2d` or `develop-2d`. Both exist and match upstream: `master-2d` = OpenRCT2 `master` `82b309798a`; `develop-2d` = OpenRCT2 `develop` `3b7114c8ea` (subject **Merge Localisation/master into OpenRCT2/develop**).

## Where the 3D code is

- 3D code: `src/openrct2-3d/` (present on `develop`; `docs/3d-viewer.md` and `AGENTS.md` exist).
- Submodule gitlinks, all under `src/openrct2-3d/third_party/`:
  - `bgfx` `fe9bbdec6611`
  - `bimg` `9bfbd6493b48`
  - `bx` `0e5bbca68780`
  - `bgfx.cmake` `5ae5fa5b1219`
- Those four directories are empty. The pins are in the commit; the working trees were not initialized. Submodule file contents are not on disk until `git submodule update --init --recursive`. That was not run.

## Standing renderer decisions

From `docs/3d-viewer.md` and `AGENTS.md` only:

- The renderer is bgfx. Do not write a renderer, and do not embed Unity, Unreal, or Godot.
- Do not grow the 3D world inside `OpenGLDrawingEngine`. A prototype may use the SDL window’s existing OpenGL 3.3 context; the committed direction is bgfx.
- 2D stays the default. The View menu (`WIDX_VIEW_MENU`) switches the main park view. In 3D, bgfx draws the park and composites the original paletted UI, with palette index 0 as the hole. Ride, guest, and staff windows keep their small 2D viewports.
- `gameStateTick()` is the only simulation. A read-only extractor turns that state into meshes. The 3D view does not advance the park.
- Viewer space is sim `x / 32`, `y / 32`, `z / 16`.

## Model scale

From the previous project, and in scope only as this decision: 3D scenery and object models use 1 Blender unit per game tile, matching the 3D viewer.

## What does not match

- There is no `3d` branch. `AGENTS.md` on `develop` refers to `develop`. The PR base is `develop`.
- Submodule paths and pins match. Their contents are missing from the working tree (not an auth or branch failure).
