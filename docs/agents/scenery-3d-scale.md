# Scenery scale

How the 3D view draws thousands of small-scenery objects without walking the whole map, loading models on the render thread, or giving every mesh its own bgfx buffer.

The first milestone (one GLB, one sprite cross, a startup cache) is `scenery-3d-milestone.md`. This note is the runtime that sits on that cache.

## What a frame does

1. Drain tile edits into the chunk grid. A 16×16 tile chunk (`kSceneryChunkTiles`) is rebuilt only when it is dirty. The first 3D frame, a map-size change, and `NotifyMapChanged(-1, -1)` mark every chunk.
2. Ask the load queue for models that are in view and not on the GPU yet. The queue's worker reads a validated `.o3d`. It does not touch `GameState` and it does not convert a GLB.
3. Upload at most `kUploadsPerFrame` (2) models or `kUploadBytesPerFrame` (1 MB) of mesh bytes, whichever comes first. While a model is loading, and when it cannot be uploaded, the object is the X-sprite. A broken model is recorded and not requested again. A model that does not fit is held and retried with a backoff. At most one held model retries in a frame, and which one rotates (`selectHeldRetry`), so the same pair cannot take both slots. The other slot stays free for a model that just finished loading. Freed space lets that one retry early. The ready queue is not drained in one frame.
4. Build one instanced batch per model type, and one per sprite image, from the chunks inside the 3D radius and the frustum. Chunks past that radius, still on the visible ground and inside the sprite far cap, add one camera-facing G1 sprite per object to those sprite batches.
5. Evict GPU meshes that do not fit `kDefaultVramBudgetBytes` (256 MB). Visible types are pinned for that frame. A dropped type is loaded again the next time it is in view.

Sprite blits (`GfxDrawSpriteSoftware`) stay on the thread that rebuilds the chunk. That is the existing 2D image decode, not a GLB load.

## Change signal

Ride and guest invalidations do not mark chunks. The calls that do are small-scenery place, remove, and recolour, clearance removal, land height, the tile inspector, scripted tile removal and property edits, scenery ageing and withering, and staff watering. `GameLoadInit` and `GameNotifyMapChanged` pass `-1, -1`, which rebuilds every chunk (load, network, editor).

The hook is registered from `SceneryDraw.cpp` next to the startup cache hook. Headless mode ignores it, and the pending-tile list collapses to "whole map" after 8192 entries so a long 2D session cannot grow it without bound.

An animated tile that invalidates every frame rebuilds only its own 16×16 chunk.

## Models are validated once

Startup (`prepareModelCache`) is the only validation:

- Header magic, format version, converter version, and `MeshLoadLimits`.
- Size and nanosecond mtime against the GLB. If both match, the cache is trusted and nothing is hashed.
- If the size or the mtime differs, or there is no cache yet, the GLB is hashed. A matching hash rewrites the 16 stamp bytes in place and does not reconvert. A different hash reconverts. A cache from any other format version is reconverted; there is no old-format reader. Files that need a hash are processed in parallel. Progress is reported on the thread that called `prepareModelCache`.

King Grok measured `dd1d952d` on 10,000 synthetic models (8 cores, 658 MB of source GLBs): first conversion 4.43 s, cold startup 0.18–0.21 s, warm startup 0.045–0.085 s with zero hashes, in-game load of all 10,000 0.54 s warm and 2.39 s cold, every mtime changed 2.1–3.2 s. The decoded RGBA cache was 1.34 GB, against 698 MB when the caches still stored compressed images. Size and mtime now sit outside the checksum, so an mtime-only pass rewrites the header instead of those 1.34 GB. On this machine, after that change, 10,000 synthetic caches padded to 70 KB (about 683 MB) warm-started in 62 ms with zero checksum passes (`ModelCache.WarmStartSizeAndMtimeOfTenThousand`, `O3D_WARM_BENCH=1`). That is a page-cache warm run, not a cold remeasure of King Grok's source set.

An id that passes is stored under its case-folded name. The data directory is scanned first, so the first file wins and the later spelling is not converted and does not get its own cache. `MyTree.glb` and `mytree.glb` do not both get a cache. Lookup ignores case, so either spelling reads the one file the first name produced. The cache stem is folded so a case-insensitive disk cannot hold two copies. A failed conversion, or a cache that fails and cannot be rebuilt from its GLB, is left out of the set.

In-game, an id in that set is read into a `CpuMesh` with no checksum, no source size or mtime check, no version check, no `stat`, and no GLB read. An id that is not in the set draws the X-sprite. A missing file, a short read, or an I/O error does the same and does not crash. That is error handling.

Parsing still checks the cheap bounds: the file must be at least a header and at most 64 MB, image width and height must be at most 256, primitive and image counts must fit `MeshLoadLimits` and the bytes actually read, and an index must point at a vertex that was read. A file replaced after startup must not be able to over-read or force a huge allocation. Those checks are a memory-safety guard. They are not a second validation.

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

`sceneryViewDistanceTiles()` starts at 48 (`kDefaultViewDistanceTiles`). `setSceneryViewDistanceTiles` changes it. A chunk whose centre is farther than that is not drawn as a mesh. There is no shader fade. The frustum is the six planes of the camera's projection-view matrix, extracted once per frame and reused for the upload pin set and the draw list. Both tests are a hard cutoff.

The 3D radius is the ground footprint of the screen, and the far side (the top corners) is capped at the eye-to-focus distance. That cap is the whole point of `groundFootprintRadius`. Without it the top of a zoomed-out frame meets the ground near the horizon and the mesh set grows without a bound: about 75 tiles at 1080p zoom 2, 146 at 1080p zoom 3, and 288 at 4K zoom 3, and several times that if the cap is removed. Do not raise the 3D radius to fill the top of the screen.

The top is a sprite layer. A chunk outside the 3D radius, inside the uncapped ground reach, inside the frustum, and inside `min(map corner, sceneryDistantSpriteTiles())` draws one original G1 sprite. The image, direction, and colour remap are the X-sprite's for the direction that faces the camera (`spriteFaceForView`): one quad, not the crossed pair. The art is already drawn for that view, and one instance is a quarter of the fallback's overdraw. Runs are grouped by image when the chunk is rebuilt, on the same dirty flag as the placements. A frame copies the chosen face into the sprite batches. Those vectors keep their capacity, so a steady camera does not allocate per placement. Uploads still share `kSpriteUploadsPerFrame` and the sprite LRU. The far cap defaults to 1280 tiles, which covers the top of a 1080p zoom-3 frame, and the map corner cuts it further so a park never walks past its own edge. When the top of the view misses the ground (the camera is near the horizon), that reach falls back to the same 1280-tile limit so the band is not left empty. Sprites share at most two atlas pages and 384 images. A sprite unused for 120 frames is dropped, a sprite drawn this frame is not, and a freed sprite id is not reused until the next frame. A model that needs atlas space evicts a cold sprite before it gives up. Distant sprites on one atlas page are one instanced draw.

A frame rebuilds at most `kChunkRebuildsPerFrame` (8) dirty chunks. The rest stay dirty. A chunk's Z span is the placement base plus the scenery entry's declared height, in viewer units.

Sprite faces are stored on the placement and uploaded only when that placement is drawn without a model. The cache key is the image plus the face. Objects whose model is still loading do not upload sprites.

Slab indices are mesh-local. `setVertexBuffer`'s start vertex is the only base (`slabVertexIndex`). Atlas images keep a 2px gutter outside the UV rect, and `meshDrawReleaseModel` returns that rect to the packer. The UV scale and offset still address the image texels, so a point sample matches a gutterless placement of the same image.

## Tuning

The constants live in `src/openrct2-3d/src/SceneryScale.hpp`.

| Constant | Default | Effect |
| --- | --- | --- |
| `kSceneryChunkTiles` | 16 | Smaller chunks rebuild less map and test more boxes. |
| `kDefaultViewDistanceTiles` | 48 | Starting view distance. `setSceneryViewDistanceTiles` changes the live value. |
| `kDefaultDistantSpriteTiles` | 1280 | Far cap of the sprite layer, in tiles. The map corner cuts it further. `setSceneryDistantSpriteTiles` changes the live value. |
| `kChunkRebuildsPerFrame` | 8 | Dirty chunks rebuilt on one frame. |
| `kAtlasGutter` | 2 | Empty pixels around an atlas image, outside the UV rect. |
| `kDefaultVramBudgetBytes` | 256 MB | When unused models leave the GPU. |
| `kUploadsPerFrame` | 2 | New models that may appear per frame. |
| `kUploadBytesPerFrame` | 1 MB | Upload byte cap per frame. |
| `kMaxGeometrySlabs` | 32 | 64 buffer handles. |
| `kMaxAtlasPages` | 8 | Shared colour textures. |

## Deferred

KTX/BC, meshopt, LOD, impostors, skeletal animation, texture arrays, indirect draws, and instancing of guests and vehicles. The pipeline note still describes those. Per-frame entity lists are not built here. Freed atlas rects are reused as whole rectangles; neighbouring free space is not merged.
