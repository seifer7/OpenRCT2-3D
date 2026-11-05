# 3D viewer

Memory of the implementation plan for this fork. The goal is a 3D view of a park that OpenRCT2 is already simulating.

## Filament branch scope

The `Filament` branch was created from `develop` to test [Google Filament](https://github.com/google/filament) in place of bgfx. The aim is to evaluate its lighting, ambient occlusion, shadows, and reflections without replacing the existing OpenRCT2 game engine.

The renderer and build now use Filament 1.80.0. bgfx and its build submodule have been removed. See [build instructions](../src/openrct2-3d/README.md) and [integration notes](agents/filament-renderer.md). Older notes under `docs/agents/` may describe the earlier bgfx direction.

The experiment is limited to the 3D rendering layer and its thin integration hooks. Preserve `gameStateTick()` as the sole simulation, the default 2D renderer, the View menu toggle, and the existing UI overlay. Keep Filament integration under `src/openrct2-3d/`. Compilation and unit tests do not establish visual correctness; evaluate the effects and window/UI integration in-game before adopting this branch.

## Goal

Build a 3D version of RollerCoaster Tycoon 2 by adding a viewing mode, not by replacing the game. The original 2D renderer stays the default and must keep working on its own. The original rules, ticks, and most of the codebase stay. From the View menu, the player can switch the main park view to 3D and switch it back.

## Where the code lives

Keep changes to the original codebase as small and as few as possible. Everything for the 3D renderer goes in `src/openrct2-3d/`: Filament setup, the scene extractor, meshes, the camera, and the UI composite.

The Filament SDK is a pinned, checksum-verified build dependency under the ignored `lib/` directory by default. The remaining submodules are `third_party/bx` for camera math and `third_party/bimg` for its bundled stb image header. Run `git submodule update --init --recursive` after cloning.

CMake builds `openrct2-3d` and its Filament resource/material library for desktop GUI builds and unit tests. Visual Studio builds `openrct2-3d.vcxproj` and links it into `openrct2-win` and the unit tests. Both use `cmake/CompileMaterials.cmake`; the SDK and material compiler versions must match.

`src/openrct2/` and `src/openrct2-ui/` change only where the feature has to meet the existing game:

- One toggle in the View menu (`TopToolbar::initViewMenu()` and `TopToolbar::viewMenuDropdown()`).
- One call into `src/openrct2-3d/` when that toggle is on, instead of painting the main viewport with the isometric renderer.

When the toggle is off, those hooks do nothing and the original paint path runs unchanged.

## Renderer

The implementation uses [Filament](https://github.com/google/filament), an Apache-2.0 rendering engine, without adopting another game simulation. `FilamentRenderer.cpp` owns an independent device, scene, camera, sun, materials, and offscreen target. Model copies use Filament instance buffers; distant sprites use reusable atlas-page geometry batches.

Filament supplies physically based shading, cascaded sun shadows, screen-space ambient occlusion/reflections, and basic environment lighting. Material texture sampling, terrain edge blending, grid lines, and animated water follow the existing assets and view flags. The GLB loader and its material limitations remain; accurate water reflections and visual tuning are not guaranteed by the backend swap.

`OpenGLDrawingEngine` (`src/openrct2-ui/drawing/engines/opengl/`) is the wrong place to hang the 3D world. It requires OpenGL 3.3 and composites paletted sprites, UI rectangles, and dirty screen blocks. It never sees the park as geometry. The 3D mode is a second view of `GameState`, beside that engine.

Filament never adopts the game window or its GL context. A complete offscreen image is copied to the existing presenter. This preserves integration at the cost of synchronous readback; it is not a zero-copy presentation path.

Unity, Unreal, and Godot are rejected. Each expects to own the frame and the scene, which fights `gameStateTick()`.

## Simulation boundary

`GameScene::Tick()` in `src/openrct2/scenes/game/GameScene.cpp` only calls `gameStateTick()`. That function, in `src/openrct2/GameState.cpp`, is the authority for guests, trains, money, and the network. Normal play advances at `kGameUpdateFPS` (40) in `src/openrct2/Game.h`.

The 3D mode keeps calling that tick, then reads the result. It does not send game commands during the draw.

## Translation layer

One read-only extractor sits between the simulation and Filament. It does not call game commands and it does not allocate tile elements.

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
Viewer3D (Filament)    camera, meshes, materials
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
- **3D, after the View menu toggle:** `MainWindow::onDraw()` fills the main viewport with palette index 0 and returns, so the isometric park is not painted. Filament renders the park offscreen, and OpenRCT2 composites its UI over that image. Surface patterns and the water overlay are copied from the loaded graphics into a GPU cache when the 3D view starts. Later frames reuse that cache. Heights come from `SurfaceElement` corner heights, including slopes and valleys. Grid lines, remove base land, remove vertical faces, underground view, and transparent water follow the existing viewport flags.
- **Back to 2D:** the same menu entry clears the toggle and invalidates the screen. The original renderer draws the park again; the independent Filament device can remain available until display shutdown. Device or scene initialization failure clears the toggle and restores 2D.

Both modes call the same `gameStateTick()`. The 3D camera starts from the main viewport, then moves on its own. Hold the right mouse button and drag to look around. Shift and the right button pans, and Ctrl and the right button moves along the view. The mouse wheel moves forward and back, and WASD moves relative to the facing direction. The toolbar rotate buttons turn that camera 90 degrees. Guests and coasters keep moving because the tick still runs. Picking, building, and ride construction stay on the existing 2D commands until the ray-to-`CoordsXYZ` path exists. Ride, guest, and staff windows keep their small 2D viewports in both modes.

## UI over the 3D view

Keep the original UI code in both modes. While the View menu toggle is on 3D, OpenRCT2 draws that UI on top of the Filament park image. While it is on 2D, the UI is drawn exactly as it is today. Do not reimplement widgets in Filament.

`Painter::Paint()` (`src/openrct2/paint/Painter.cpp`) calls `PaintWindows()`. Each window paints widgets into one paletted screen buffer. The park is only the viewport widget: `WindowDrawViewport()` calls `ViewportRender()` (`src/openrct2/interface/Viewport.cpp`). Palette index 0 is `PaletteIndex::transparent` (`src/openrct2/drawing/PaletteIndex.h`). Sprites already skip that index. The giant-screenshot path clears a viewport to it with `VIEWPORT_FLAG_TRANSPARENT_BACKGROUND`.

Presentation flow:

1. The existing window code paints toolbars, panels, buttons, and text, leaving the main viewport transparent.
2. Filament renders the extracted park into an independent color/depth target. Readback waits for completion and returns top-row-first RGBA pixels.
3. Hardware display uses `View3D::capture()` to composite the indexed UI on the CPU. OpenGL display uses `View3D::renderPark()` and its existing UI blending pass. Palette index 0 reveals the park; all other UI pixels remain opaque.
4. The original SDL/OpenGL presenter displays the result. Filament never swaps the game window.

The main park viewport is the hole. `VIEWPORT_FLAG_RENDERING_INHIBITED` makes `ViewportRender()` return without painting the isometric map. The window frame can still have filled that rectangle with the panel colour, so after the main window's widgets are drawn, fill that viewport rectangle with palette index 0. Toolbars and dialogs are separate windows drawn later, so they stay opaque. Ride, guest, and staff windows call `WindowDrawViewport()` themselves and can keep their small 2D previews.

Clicks on buttons stay on the old widget path. A click inside the transparent viewport is a screen position over the 3D image and needs a ray into the park later.

The 3D image is drawn and composited while 3D is active, including frames when only the park moved.

## Suggested order of work

1. Terrain and water from `SurfaceElement` slopes, with images cached from the loaded graphics at 3D startup. Entity markers are still to come. The camera starts from the main viewport, then moves with the right mouse button, the wheel, and WASD.
2. Track instances from `TrackElement` type, direction, and base height. Tunnel openings omit a band of the cliff quads.
3. Scenery, paths, walls, and entrances.
4. View menu toggle and UI composite. Done: `STR_VIEW_3D`, the hole in `MainWindow::onDraw()`, and `View3D::capture()` / `View3D::renderPark()` in the existing display paths.
5. Ray picking back into `CoordsXYZ` and the existing game actions.
