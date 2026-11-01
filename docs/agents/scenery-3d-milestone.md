# Scenery milestone: GLB loader and sprite cross

First scenery pass in the 3D view. Small scenery only. The 2D renderer is unchanged.

## What is implemented

- **GLB load.** `src/openrct2-3d/src/GlbMesh.cpp` reads a `.glb` with [cgltf](https://github.com/jkuhlmann/cgltf) 1.14 (MIT, `src/openrct2-3d/third_party/cgltf/`). It checks `cgltf_validate`, rejects a mesh over the caps in `MeshLoadLimits` (65535 vertices, 200000 indices, 8 primitives, 16 MB embedded image), and keeps embedded PNG bytes. Node transforms are applied. Triangles only.
- **Axis conversion, one place.** glTF is Y-up. The viewer is Z-up, +X east, +Y north, the same axes as the terrain in `ParkView.cpp`. On load: `x = gx`, `y = -gz`, `z = gy * kGlbVerticalScale`. Checked against `TCRP.glb`: the position accessor's Y range is the tree height (-0.09375 to 4.1875), and the converted bounds match the Blender Z-up box in the meta file.
- **Vertical scale, one constant.** `kLandStepHeight` in `src/openrct2-3d/src/ViewerScale.hpp` is `0.5`. Viewer Z is sim Z / 32 on the terrain, on scenery placement, and on sprite quads. `kGlbVerticalScale` is `kViewerZPerSimUnit * 32`, which is 1 while the land step stays 0.5, and grows with the constant so an authored model stays the same height as the ground. X and Y stay 1 Blender unit per tile.
- **Draw.** After paths and before water, `SceneryDraw.cpp` walks small scenery. Ghosts are included (construction remap). Invisible elements are skipped. Hide Vegetation and Hide Scenery are honoured. The list is rebuilt when a hash of positions, entry, direction, quadrant, colours, wither band, and ghost changes.
- **Models.** One instanced draw per GLB primitive per object id. The instance is position plus a yaw. Direction 0 is the authored facing. Each step is a clockwise quarter turn about +Z, the same as `CoordsXY::rotate`. Full-tile objects (including half and three-quarter footprints) are placed at the tile centre. Quarter-tile objects use `SceneryQuadrantOffsets`, in world space, not rotated by the camera. Alpha mode comes from the material: mask uses the file's cutoff and discards, blend blends, opaque does not discard. Double-sided comes from the material. A sampler of nearest/nearest and clamp-to-edge is uploaded that way, with no mipmaps.
- **Sprite cross.** No GLB, or a GLB that fails the caps, falls back to four quads in two planes. From above they make an X. Each face looks at one isometric camera (the same corners `ParkView` uses for rotations 0..3) and shows `image + ((direction + face) & 3)`, plus the wither offset of +4 or +8. Colour remap goes through `GfxDrawSpriteSoftware`, so primary, secondary, tertiary, and the ghost palette match the 2D paint. One pixel of sprite height is one sim Z. One pixel of width is that times `cos(30°)`, so a face-on view at the isometric pitch keeps the image's pixel aspect. Transparent palette index 0 is discarded (cutoff 0.5). Sprites are not lit again.
- **Asset.** `data/object-models/rct2.scenery_small.tcrp.glb` is the Corsican Pine (`rct2.scenery_small.tcrp`). The meta JSON sits beside it and is not read at runtime. `install(DIRECTORY "data/")` in the root `CMakeLists.txt` copies `object-models/` into the OpenRCT2 data directory. A Visual Studio build copies the same folder into `bin\data\object-models` from `CopyObjectModels` in `openrct2.targets`. At runtime the search is `{openrct2}/object-models/<id>.glb`, then `{user}/object-models/<id>.glb`.
- **Cache.** During startup, after the title-sequence scan, each of those GLBs is converted to `{cache}/object-models/<id>.o3d` (format version 2). The header stores the source size, modification time, and an FNV-1a hash of the GLB, plus an FNV-1a checksum of the body, `kGlbConverterVersion`, and the `MeshLoadLimits` the converter used. A mismatch rebuilds the file. Drawing reads the cache once: if size and modification time still match, it does not hash the GLB. A bad GLB is logged and does not stop startup. Drawing parses the GLB when the cache is missing, truncated, or its checksum fails.
- **Preview.** `openrct2-3d-preview` (CMake, not installed) renders a GLB and a stand-in X offscreen. It does not use the cache.

## How to add a model

1. Author it in Blender at 1 unit = 1 tile. Origin at the tile centre. Z = 0 at the tile base. Export glTF binary, Y-up.
2. Foliage: alpha mode `MASK`, cutoff 0.5, double-sided, nearest mag and min filter, clamp, no mipmaps.
3. Name the file with the object identifier: `object-models/rct2.scenery_small.example.glb`.
4. Put it in this tree's `data/object-models/` or in the user `object-models/` directory. Do not put RCT2 `g1.dat` or extracted original sprites in the repository.
5. `openrct2-3d-preview path/to/object.glb /tmp/out` writes three PPM frames (two isometric corners and a high view) with a coloured X beside the model.

## What is deferred

From `docs/agents/glb-model-pipeline-recommendation.md`, not in this pass:

- Offline build-time baking, geometryc, and KTX/BC textures. The runtime `.o3d` cache stores the converted mesh and the embedded PNG. It is not a GPU-compressed texture.
- LOD, impostors, and distance fade.
- Chunked (16×16) culling and a frustum test. Every small scenery element in the park is submitted. Instancing is per object type, which is the part of the 10k path this milestone does.
- Texture arrays and compressed textures. Each image is its own RGBA texture.
- Skeletal animation, rigid node clips, shader sway, fountains, clocks, and glass overlays. A withering tree with a GLB does not swap meshes. The sprite fallback does advance the wither image.
- Remap masks (`extras.rct_remap`, mask textures, vertex colour). Recolouring is the 2D palette path on the sprite fallback only. A GLB is drawn with its own texture.
- Large scenery, walls, banners, entrances, and supports. Half-tile and three-quarter objects use the tile centre rather than the 2D footprint offset.
- See-through (invisible scenery / invisible vegetation) stays solid. The hide flags do remove them.
- Mipmaps, even if a sampler asks for them.

## Scale decision for the owner

`docs/3d-viewer.md` and `ParkView.cpp` now agree: one land step is 0.5 viewer units (sim Z / 32). An older note described the viewer as sim Z / 16, which would make one land step 1 unit and would make this terrain, and this pine, look twice as tall. The pine was built for the 0.5 step. `kLandStepHeight` is the knob. Set it to `1.0` and the ground, the sprite quads, and the GLB height all double together. Horizontal size stays one tile per Blender unit.
