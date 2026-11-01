# Scenery scale

How the 3D view draws thousands of small-scenery objects without walking the whole map, loading models on the render thread, or giving every mesh its own bgfx buffer.

The first milestone (one GLB, one sprite cross, a startup cache) is `scenery-3d-milestone.md`. This note is the runtime that sits on that cache.

## What a frame does

1. Drain tile edits into the chunk grid. A 16×16 tile chunk (`kSceneryChunkTiles`) is rebuilt only when it is dirty. The first 3D frame, a map-size change, and `NotifyMapChanged(-1, -1)` mark every chunk.
2. Ask the load queue for models that are in view and not on the GPU yet. The queue's worker reads a validated `.o3d`. It does not touch `GameState` and it does not convert a GLB.
3. Upload at most `kUploadsPerFrame` (2) models or `kUploadBytesPerFrame` (1 MB) of mesh bytes, whichever comes first. Until that upload, the object is the X-sprite.
4. Build one instanced batch per model type, and one per sprite image, from the chunks that pass the view-distance and frustum tests.
5. Evict GPU meshes that do not fit `kDefaultVramBudgetBytes` (256 MB). Visible types are pinned for that frame. A dropped type is loaded again the next time it is in view.

Sprite blits (`GfxDrawSpriteSoftware`) stay on the thread that rebuilds the chunk. That is the existing 2D image decode, not a GLB load.

## Change signal

`MapInvalidateTileUnderZoom` calls `NotifyMapChanged(x / 32, y / 32)` before the headless return, so a tile edit still marks a chunk when viewports are not updating. `GameLoadInit` and `GameNotifyMapChanged` pass `-1, -1`, which rebuilds every chunk (load, network, editor).

The hook is registered from `SceneryDraw.cpp` next to the startup cache hook. Headless mode ignores it, and the pending-tile list collapses to "whole map" after 8192 entries so a long 2D session cannot grow it without bound.

An animated tile that invalidates every frame rebuilds only its own 16×16 chunk.

## Models are validated once

Startup (`prepareModelCache`) is the only validation:

- Header magic, format version, converter version, and `MeshLoadLimits`.
- Size and nanosecond mtime against the GLB. The GLB is hashed only when those differ. A matching hash refreshes the stored time.
- The body checksum, streamed in 64 KB chunks. The whole file is not buffered for that pass.

An id that passes is stored under its case-folded name. The data directory is scanned first, so the first file wins and the later spelling is not converted and does not get its own cache. Lookup ignores case, so either spelling reads that one file. The cache stem is folded so a case-insensitive disk cannot hold two copies. A failed conversion, or a cache that fails and cannot be rebuilt from its GLB, is left out of the set.

In-game, an id in that set is read into a `CpuMesh` with no checksum, no source size or mtime check, no version check, and no GLB read. An id that is not in the set draws the X-sprite. A missing file, a short read, or an I/O error does the same and does not crash. That is error handling.

Parsing still checks the cheap bounds: the file must be at least a header and at most 64 MB, primitive and image counts must fit `MeshLoadLimits` and the bytes actually read, and an index must point at a vertex that was read. A file replaced after startup must not be able to over-read or force a huge allocation. Those checks are a memory-safety guard. They are not a second validation, and they do not decide whether the bytes are the model startup accepted.

Trade-off: an edit to a GLB or a `.o3d` during a session is invisible until the next startup. The mesh may be wrong if the file changed and still parsed. It will not allocate from a hostile count.

## GPU budget

bgfx's default handle cap is 4096 (`kHandleCap`). This build does not raise it.

- Geometry goes into shared dynamic slabs (`kSlabVertices` 65535, `kSlabIndices` 262144, at most `kMaxGeometrySlabs` 32). That is two handles per slab. A mesh stays in one slab so its indices fit in uint16.
- RGBA images go into an atlas (`kAtlasSize` 2048, at most `kMaxAtlasPages` 8) with a UV rectangle in the vertex shader. An image that does not fit is its own texture.
- One 1×1 white texture covers a primitive with no image.
- Instance data is a transient bgfx buffer, not a stored handle.

`kDefaultVramBudgetBytes` is 256 MB of mesh and image bytes, not a GPU query. Least-recently-used models that were not drawn this frame are released (vertex ranges returned to the slab, atlas rects returned to the packer, owned textures destroyed). A dedicated texture is also freed on eviction. Sprite images share the atlas too. If the handle estimate is already at the cap, a new dedicated texture is refused and that upload fails closed (sprite skipped, or the model id stays on the sprite fallback).

## Placement

`sceneryAnchor` in `ViewerScale.hpp` is the position of both the GLB instance and the X-sprite fallback.

Full-tile objects, and anything with `occupiesFullTile` (half-tile and three-quarter footprints included), stay at the tile centre: 16 sim units from the corner, 0.5 viewer units. `vOffsetCentre` moves the 2D full-tile sprite to (3, 3) or (1, 1). The 3D view does not apply that nudge. Half and three-quarter image offsets of (3, 3) are deferred with it.

Quarter-tile objects do not set `occupiesFullTile`. The 2D body paint uses the stored quadrant plus the current viewport rotation, then `SceneryQuadrantOffsets[index] - 1`. Checked against `src/openrct2/world/Scenery.cpp` and `Paint.SmallScenery.cpp`: the table is (8, 8), (8, 24), (24, 24), (24, 8), and the −1 makes (7, 7), (7, 23), (23, 23), (23, 7). `vOffsetCentre` is not read on that branch. The 3D view uses the stored quadrant only, because the camera is not the 2D rotation. Those four points are the quadrant centres (tile centre ±8 sim) after the same −1 the painter applies.

The mesh origin is the pivot. The instance shader rotates about that origin and then adds the anchor, so a quarter-tile model turns about its quadrant point. A full-tile model still turns about the tile centre.

## View

`sceneryViewDistanceTiles()` starts at 48 (`kDefaultViewDistanceTiles`). `setSceneryViewDistanceTiles` changes it. A chunk whose centre is farther than that is not drawn. There is no shader fade. The frustum is the six planes of the camera's projection-view matrix, extracted once per frame and reused for the upload pin set and the draw list. Both tests are a hard cutoff.

A frame rebuilds at most `kChunkRebuildsPerFrame` (8) dirty chunks. The rest stay dirty. A chunk's Z span is the placement base plus the scenery entry's declared height, in viewer units.

Sprite faces are stored on the placement and uploaded only when that placement is drawn without a model. The cache key is the image plus the face. Objects whose model is still loading do not upload sprites.

Slab indices are mesh-local. `setVertexBuffer`'s start vertex is the only base (`slabVertexIndex`). Atlas images keep a 2px gutter outside the UV rect, and `meshDrawReleaseModel` returns that rect to the packer. The UV scale and offset still address the image texels, so a point sample matches a gutterless placement of the same image.

## Tuning

The constants live in `src/openrct2-3d/src/SceneryScale.hpp`.

| Constant | Default | Effect |
| --- | --- | --- |
| `kSceneryChunkTiles` | 16 | Smaller chunks rebuild less map and test more boxes. |
| `kDefaultViewDistanceTiles` | 48 | Starting view distance. `setSceneryViewDistanceTiles` changes the live value. |
| `kChunkRebuildsPerFrame` | 8 | Dirty chunks rebuilt on one frame. |
| `kAtlasGutter` | 2 | Empty pixels around an atlas image, outside the UV rect. |
| `kDefaultVramBudgetBytes` | 256 MB | When unused models leave the GPU. |
| `kUploadsPerFrame` | 2 | New models that may appear per frame. |
| `kUploadBytesPerFrame` | 1 MB | Upload byte cap per frame. |
| `kMaxGeometrySlabs` | 32 | 64 buffer handles. |
| `kMaxAtlasPages` | 8 | Shared colour textures. |

## Deferred

KTX/BC, meshopt, LOD, impostors, skeletal animation, texture arrays, indirect draws, and instancing of guests and vehicles. The pipeline note still describes those. Per-frame entity lists are not built here. Freed atlas rects are reused as whole rectangles; neighbouring free space is not merged.
