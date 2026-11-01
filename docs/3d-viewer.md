# 3D viewer

Memory of the implementation plan for this fork. The goal is a 3D view of a park that OpenRCT2 is already simulating.

## Goal

Build a 3D version of RollerCoaster Tycoon 2 by adding a viewing mode, not by replacing the game. The original 2D renderer stays the default and must keep working on its own. The original rules, ticks, and most of the codebase stay. From the View menu, the player can switch the main park view to 3D and switch it back.

## Where the code lives

Keep changes to the original codebase as small and as few as possible. Everything for the 3D renderer goes in `src/openrct2-3d/`: the bgfx setup, the scene extractor, meshes, the camera, and the UI composite.

bgfx itself is vendored there as submodules, not copied into `src/openrct2/` or `src/openrct2-ui/`:

- `src/openrct2-3d/third_party/bgfx.cmake`
- `src/openrct2-3d/third_party/bx`
- `src/openrct2-3d/third_party/bimg`
- `src/openrct2-3d/third_party/bgfx`

Those three library pins are the revisions named in `bgfx.cmake`'s `cmake/dependencies.cmake`. CMake adds this directory with `add_subdirectory(src/openrct2-3d)` when the GUI is enabled, except on the Emscripten web build. That builds the static library `openrct2-3d` and a static `bgfx`. The Visual Studio solution builds the same library from `src/openrct2-3d/openrct2-3d.vcxproj` and links it into `openrct2-win`. That project compiles the amalgamated bx and bgfx sources, plus `bimg`'s `image.cpp`, with only the OpenGL renderer enabled. Examples and tools are not built. If the submodule directories are empty, that Visual Studio project fetches them before compiling. A manual checkout is `git submodule update --init --recursive`.

`src/openrct2/` and `src/openrct2-ui/` change only where the feature has to meet the existing game:

- One toggle in the View menu (`TopToolbar::initViewMenu()` and `TopToolbar::viewMenuDropdown()`).
- One call into `src/openrct2-3d/` when that toggle is on, instead of painting the main viewport with the isometric renderer.

When the toggle is off, those hooks do nothing and the original paint path runs unchanged.

## Renderer

Use [bgfx](https://github.com/bkaradzic/bgfx). It is an existing cross-platform rendering library (Direct3D, Metal, OpenGL, Vulkan) by Branimir Karadžić, licensed BSD-2-Clause. It draws meshes and textures. It does not own a game loop, physics, or a scene graph.

The code this project writes is the translation layer and the camera. bgfx only receives what to draw after each tick.

`OpenGLDrawingEngine` (`src/openrct2-ui/drawing/engines/opengl/`) is the wrong place to hang the 3D world. It requires OpenGL 3.3 and composites paletted sprites, UI rectangles, and dirty screen blocks. It never sees the park as geometry. The 3D mode is a second view of `GameState`, beside that engine.

A prototype may draw through the SDL window's existing OpenGL 3.3 context. The extractor below does not change if that prototype later moves onto bgfx. The committed direction is bgfx.

Unity, Unreal, and Godot are rejected. Each expects to own the frame and the scene, which fights `gameStateTick()`.

## Simulation boundary

`GameScene::Tick()` in `src/openrct2/scenes/game/GameScene.cpp` only calls `gameStateTick()`. That function, in `src/openrct2/GameState.cpp`, is the authority for guests, trains, money, and the network. Normal play advances at `kGameUpdateFPS` (40) in `src/openrct2/Game.h`.

The 3D mode keeps calling that tick, then reads the result. It does not send game commands during the draw.

## Translation layer

One read-only extractor sits between the simulation and bgfx. It does not call game commands and it does not allocate tile elements.

```
gameStateTick()
        │
        ▼
SceneExtractor
  terrain patches      SurfaceElement corner heights, water
  track instances      TrackElement type, direction, base height
  prop instances       scenery, paths, walls, entrances
  actor transforms     EntityBase position and orientation
        │
        ▼
Viewer3D (bgfx)        camera, meshes, materials
```

The world is already spatial:

- One map tile is 32 units in X and Y (`kCoordsXYStep` in `src/openrct2/world/MapLimits.h`).
- One height unit is 8 units in Z (`kCoordsZStep`). A sloped corner rises 16 (`kLandHeightStep`).
- Each tile is a stack of `TileElement`s: surface, path, track, small scenery, large scenery, wall, entrance, banner. Surfaces expose per-corner heights.
- Guests, staff, vehicles, and other sprites are `EntityBase` records (`src/openrct2/entity/EntityBase.h`) with `x`, `y`, `z`, and `orientation`, including positions between tiles.

Sim position to viewer space:

- `x / 32`, `y / 32`, `z / 32`

One tile is one unit on every axis, so one slope step (16) is half a vertical unit. `ParkView.cpp` converts heights in `worldZ` as world Z times `kLandStepHeight / 16`, with `kLandStepHeight = 0.5f` (world Z / 32). A height unit (8) is 0.25. A later pick ray converts a 3D point back to `CoordsXYZ` when a click should call an existing game action. The first mode does not need that. Camera movement stays inside the viewer.

The terrain and water mesh is rebuilt when a surface tile's height, slope, water, or surface type changes, and when remove base land or remove vertical faces is toggled. Other frames reuse the mesh already on the GPU. A later pass can rebuild the changed tiles instead of the whole park.

Track has no 3D mesh today. A `TrackElement` identifies a piece, a direction, and a height. The extractor maps that triple onto a mesh prototype and a transform. Until real art exists, a missing piece is a coloured box or a camera-facing sprite from the existing `g1` images.

Small scenery is drawn. An object with a GLB at `object-models/<object id>.glb` (under the OpenRCT2 data directory, or the user directory) is instanced from that mesh. The file is glTF Y-up. On load, `x = gx`, `y = -gz`, `z = gy * kGlbVerticalScale`, with the origin at the tile centre and Z = 0 at the tile base. `kLandStepHeight` in `src/openrct2-3d/src/ViewerScale.hpp` is the only vertical scale: one land step is 0.5 viewer units, so viewer Z is sim Z / 32, matching this mesh. A missing GLB is two crossed vertical quads, one face for each `image + direction` sprite. Large scenery, walls, entrances, animation, and remap masks are not drawn yet. See `docs/agents/scenery-3d-milestone.md`.

## View switch

The game always has both renderers. 2D is the default. Nothing about 3D may disable or bypass the original isometric paint when 2D is selected.

The switch is an option in the existing View menu, not a second game loop and not a separate program. That menu is the top-toolbar button `WIDX_VIEW_MENU` in `src/openrct2-ui/windows/TopToolbar.cpp`. Its tooltip is “View options”. The dropdown is built in `TopToolbar::initViewMenu()` and handled in `TopToolbar::viewMenuDropdown()`. Add one toggle there, beside the current view flags (underground view, see-through rides, height marks, and the rest).

- **2D, the default:** `ViewportPaint` and `IDrawingEngine` draw the isometric screen, unchanged.
- **3D, after the View menu toggle:** `MainWindow::onDraw()` fills the main viewport with palette index 0 and returns, so the isometric park is not painted. bgfx draws terrain and water, then composites the paletted UI, discarding index 0. Surface patterns and the water overlay are copied from the loaded graphics into a GPU cache when the 3D view starts. Later frames reuse that cache. Heights come from `SurfaceElement` corner heights, including slopes and valleys. Cliff faces are separate quads so a tunnel can later omit a height band. Grid lines, remove base land, remove vertical faces, underground view, and transparent water follow the existing viewport flags.
- **Back to 2D:** the same menu entry clears the toggle, shuts bgfx down, and the original renderer draws the park again.

Both modes call the same `gameStateTick()`. The 3D camera starts from the main viewport, then moves on its own. Hold the right mouse button and drag to look around. Shift and the right button pans, and Ctrl and the right button moves along the view. The mouse wheel moves forward and back, and WASD moves relative to the facing direction. The toolbar rotate buttons turn that camera 90 degrees. Guests and coasters keep moving because the tick still runs. Picking, building, and ride construction stay on the existing 2D commands until the ray-to-`CoordsXYZ` path exists. Ride, guest, and staff windows keep their small 2D viewports in both modes.

## UI over the 3D view

Keep the original UI code in both modes. While the View menu toggle is on 3D, bgfx draws that UI on top of the 3D park. While it is on 2D, the UI is drawn exactly as it is today. Do not reimplement widgets in bgfx.

`Painter::Paint()` (`src/openrct2/paint/Painter.cpp`) calls `PaintWindows()`. Each window paints widgets into one paletted screen buffer. The park is only the viewport widget: `WindowDrawViewport()` calls `ViewportRender()` (`src/openrct2/interface/Viewport.cpp`). Palette index 0 is `PaletteIndex::transparent` (`src/openrct2/drawing/PaletteIndex.h`). Sprites already skip that index. The giant-screenshot path clears a viewport to it with `VIEWPORT_FLAG_TRANSPARENT_BACKGROUND`.

Frame order:

1. bgfx draws the 3D park into the window.
2. The existing window code draws toolbars, panels, buttons, and text into the paletted buffer.
3. bgfx uploads that buffer and draws it as a screen-space texture. A pixel of palette index 0 is discarded so the 3D image shows through. Every other pixel is looked up in the game palette and drawn on top.

The main park viewport is the hole. `VIEWPORT_FLAG_RENDERING_INHIBITED` makes `ViewportRender()` return without painting the isometric map. The window frame can still have filled that rectangle with the panel colour, so after the main window's widgets are drawn, fill that viewport rectangle with palette index 0. Toolbars and dialogs are separate windows drawn later, so they stay opaque. Ride, guest, and staff windows call `WindowDrawViewport()` themselves and can keep their small 2D previews.

Clicks on buttons stay on the old widget path. A click inside the transparent viewport is a screen position over the 3D image and needs a ray into the park later.

The UI texture is re-uploaded when the UI is dirty. The 3D image is drawn every frame, so the composite pass still runs when only the park moved.

## Suggested order of work

1. Terrain and water from `SurfaceElement` slopes, with images cached from the loaded graphics at 3D startup. Entity markers are still to come. The camera starts from the main viewport, then moves with the right mouse button, the wheel, and WASD.
2. Track instances from `TrackElement` type, direction, and base height. Tunnel openings omit a band of the cliff quads.
3. Scenery, paths, walls, and entrances.
4. View menu toggle and UI composite. Done: `STR_VIEW_3D` in the View menu, the hole in `MainWindow::onDraw()`, and `View3D::present()` from the software and OpenGL display paths.
5. Ray picking back into `CoordsXYZ` and the existing game actions.
