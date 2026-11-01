# Sprite-to-3D style guide

How to turn an RCT2 scenery sprite into a GLB that matches the approved Corsican Pine. This is the authoring guide. The runtime plan for loading those GLBs (instancing, the cached `.o3d` blob, remap colours in the shader, LOD) is [glb-model-pipeline-recommendation.md](glb-model-pipeline-recommendation.md). Viewer scale and the frame loop are in [docs/3d-viewer.md](../3d-viewer.md). Sprite projection, the four directions, and the recolour index ranges are surveyed in [scenery-sprites-angles-and-3d-view-survey.md](scenery-sprites-angles-and-3d-view-survey.md).

Read this file on its own. The pine is the reference object:

| | |
| --- | --- |
| Object id | `rct2.scenery_small.tcrp` |
| RCT2 DAT | `TCRP` |
| Display name | Corsican Pine |
| Archetype | `conifer` |
| Size class | `tree_1tile` |
| Triangles | 864 |
| Atlas | 256×256, alpha cut-out |
| Height | 134 sim Z, 4.1875 Blender units at the tip |
| Bounds | about 1.49 × 1.42 × 4.28 units |
| Approved silhouette IoU (raw) | 0.733, 0.712, 0.766, 0.687 (mean 0.725) |
| Closed-mask IoU (3px, the gate) | 0.780, 0.737, 0.787, 0.734 (mean 0.759) |

The loader looks for a flat file, `data/object-models/<object id>.glb`, and then the same name under the user directory. It does not search subfolders. Dropping a correctly named `.glb` into `object-models` is the whole install step. `<object id>.meta.json` may sit beside it. The game never reads the meta; it is for people and for `validate.py`. The scripts live in `tools/sprite-to-3d/`.

The approved pine GLB is the Blender 4.2 LTS export (`Khronos glTF Blender I/O v4.2.83`), patched so the sampler is nearest and the alpha mode is MASK. That file belongs to the loader change (PR #6) at `data/object-models/rct2.scenery_small.tcrp.glb`. This pipeline does not commit a second copy. `--no-blender` writes the same mesh with welded vertices and u16 indices (1908 vertices, 864 triangles on the pine). Its file is a few hundred bytes different because the JSON chunk is not Blender's.

## What the pipeline recommendation already decided

Use that document for anything about drawing the park. In particular:

- A GLB is the authoring and modding file. The game may later convert it to a cached blob. This guide only produces the GLB.
- Draw many instances of one mesh. Do not author a separate model per placed tree.
- Foliage is alpha-tested. Blended transparency does not scale to thousands of trees.
- Recolour is a mask plus per-instance colours, flagged so the loader can see it. Section "Recolour" below follows that document's section 4 rather than inventing a second scheme.
- Missing meshes fall back to a coloured box or a g1 billboard. The original sprites are a missing-art fallback, not a distance LOD, and not the model.

Two points that document left open are closed here, and they match its "Recommended pipeline" step 1:

- The origin is the **tile centre**, at the element base (Z = 0). It is not the tile min-corner.
- The file is glTF **Y-up**. Positions are already Y-up. Node rotation and scale stay at identity, so a loader does not have to hunt for a conversion node.

## Conventions

### Scale

1 Blender unit = 1 tile = 32 sim units on **every** axis.

```text
blender_x = sim_x / 32
blender_y = sim_y / 32
blender_z = sim_z / 32
```

That is the same conversion as `ParkView.cpp` `worldZ`: world Z times `kLandStepHeight / 16` with `kLandStepHeight = 0.5`, which is world Z / 32. One land step (16 sim Z) is 0.5 units. One height unit (`kCoordsZStep` = 8) is 0.25 units.

Author the mesh in sim units if that is easier (the conifer builder does) and divide by 32 at export. Do not author in "one unit = one land step". A model built at Z/16 stands twice as tall as the terrain.

### Origin and axes

- XY origin is the centre of the primary tile. A 1×1 object occupies X and Y from −0.5 to 0.5 before overhang.
- Z = 0 is the element base, the bottom of the tile element, not the visual bottom of every vertex. A trunk may sink a few sim units below 0 so it still meets sloped ground. The pine sinks 3 sim units (−0.09375 units). Declare that sink in the meta. More than 8 sim units (0.25 units) below 0 is a mistake unless the meta says otherwise.
- Blender is Z-up while you work. The glTF file is Y-up:

```text
gltf_x = blender_x
gltf_y = blender_z
gltf_z = -blender_y
```

- Node translation is 0, rotation is identity, scale is 1. The Y-up conversion is baked into the positions.
- Rotation in the park is a 90 degree step about the tile centre, applied by the viewer. Do not pre-rotate the mesh to a favourite direction.

### The 0.8165 squash is render-only

RCT draws 1 sim Z unit as 1 screen pixel. A true orthographic camera at 30 degrees elevation draws `sqrt(2) * cos(30°) ≈ 1.2247` pixels per sim Z, about 22% taller than the sprite. The factor that lines the render up with the sprite is `sqrt(2/3) ≈ 0.81649658`.

Apply that factor only while rendering a comparison or a labelled reference beauty. Multiply Z for the duration of the render, then set the scale back to 1 before export. The GLB, the OBJ, and any saved `.blend` stay at true sim/32 scale. A perspective view in the park showing heights about 22% taller than the 2D sprites is the correct result of this scale, not a bug in the mesh.

`tools/sprite-to-3d/projection.py` checks that the squashed 30 degree camera and the RCT formula land on the same pixels (error under a hundredth of a pixel).

## Reading the four direction sprites

Small scenery that is not glass, withering, or animated uses image `base + direction` with direction in 0..3. At view rotation 0 the image direction equals the element direction (`TileElementBase.h`):

| Index | Name | Camera horizontal aim (Blender XY) |
| --- | --- | --- |
| 0 | West | (1, 1) |
| 1 | North | (−1, 1) |
| 2 | East | (−1, −1) |
| 3 | South | (1, −1) |

The view rotates the world, then projects (`Viewport.cpp` `Translate3DTo2DWithZ`):

```text
0: (x, y)    1: (y, −x)    2: (−x, −y)    3: (−y, x)
screen_x = y' − x'
screen_y = (x' + y') / 2 − z'
```

Units are sim units, and for this projection they are also pixels. A flat tile (32×32) is a 64×32 diamond. Vertical edges are 1 pixel per sim Z.

### Offsets

Every sprite has `x_offset` and `y_offset`. The sprite's top-left pixel is drawn at `anchor_screen + (x_offset, y_offset)`. The anchor inside the PNG is `(-x_offset, -y_offset)`, which is often outside the bitmap. `tools/sprite-to-3d/extract.py` writes those numbers to `offsets.json` next to `0.png`..`3.png`. That folder is a local cache. Do not commit it.

Small-scenery anchors at view rotation 0 (world units from the tile corner):

| Shape | Anchor |
| --- | --- |
| Full tile | (15, 15) |
| Full tile, half footprint | (3, 3) |
| Full tile with `vOffsetCentre` | (3, 3), or (1, 1) when walls are prohibited (full-tile objects only) |
| Quarter tile | (7, 7), (7, 23), (23, 23), (23, 7) by quadrant |

The tile centre is (16, 16). The model origin is the tile centre, so a full-tile object (the pine) has its sprite anchor 1 sim unit toward the corner from the origin. That shift is 0 pixels in screen X and 1 pixel up in screen Y. The comparison paste uses that as `sprite_y_nudge = -1`.

### Comparison image

Renders are 128×192. The model origin lands on pixel **(64, 160)** (x right, y down). Paste sprite `i` with:

```text
paste_x = x_offset + 64 − sprite_shift_x
paste_y = y_offset + 160 + sprite_y_nudge − sprite_shift_y
```

The pine's fitted `sprite_shift` is `[-2, 0]` and `sprite_y_nudge` is `-1`, so the paste is `(x_offset + 66, y_offset + 159)`. The −2 is the pine's trunk sitting 2 pixels left of the pure anchor; it is data on that object, not a global constant.

Quarter-tile objects: OpenRCT2 draws them at `SceneryQuadrantOffsets` − 1, that is anchors (7, 7), (7, 23), (23, 23), (23, 7) by quadrant. The model origin is the quadrant centre, (8, 8), (8, 24), (24, 24), (24, 8), so the anchor sits 1 sim unit toward the corner exactly as on a full tile, and the paste uses the same `sprite_y_nudge = -1`. `vOffsetCentre` applies only to full-tile objects; it never moves a quarter-tile anchor. A quarter-tile model is authored about its own quadrant centre, and the loader translates it to the tile centre ±8 sim in x and y according to the quadrant stored on the scenery element. A fitted `sprite_shift` is still per-object data, used only if the silhouette needs one.

Silhouette IoU is the overlap of opaque pixels (`alpha > 0` on the sprite, `alpha > 127` on a MASK render) divided by the union. The approved figures were measured with Blender 4.2 Workbench, flat lighting, no anti-aliasing, the Z squash on, and this paste.

**Closed-mask IoU is the gate.** RCT2 sprites are stippled: single transparent pixels inside foliage, one-pixel gaps between leaves. Scored pixel by pixel, that dither costs a solid model several points of IoU it can do nothing about. So before scoring, the validator applies a morphological close (dilate, then erode) with a square `IOU_CLOSE_KERNEL` × `IOU_CLOSE_KERNEL` element (3×3, "3px") to **both** masks, for every object. The gate is closed IoU **≥ 0.70 on every view and ≥ 0.75 mean** (`IOU_CLOSED_MIN_PER_VIEW`, `IOU_CLOSED_MIN_MEAN`). The close inflates every score, which is why these thresholds are higher than the raw ones. Raw IoU is still computed and reported on its own row (and against the old 0.65 / 0.70 raw reference), so a stippled sprite stays visible, but it does not gate. The area ratio (render pixels / sprite pixels, 0.9–1.3) is computed on the raw masks and gates exactly as before. The meta records both: the approved raw figures in `iou.views` / `iou.mean`, and the rebuild's measurement in `iou.closed` and `iou.raw_measured`.

**Empty-render floor.** Each view must also render at least `min_render_px` opaque pixels for its size class (prop_quarter 100, prop_tile 250, tree_quarter 150, every other class 500). That is a guard against a broken projection or alpha cut, not a size check; with sprites present the area ratio is the real guard. Quarter-tile objects draw about 200 px a view, which is why their floor is lower.

### Matching footprint and height

- **Height.** The declared `height_sim` is the object's logical height (the DAT height byte for small scenery; the pine is 134). The top of the mesh, in Blender Z, should land on `height_sim / 32`. The pine's tip is exactly 4.1875. When sprites are supplied, the validator measures tip height upward from the z=0 anchor row. It does not use the bottom of the mask, so the sunk trunk is not part of the height. The comparison render already includes the Z squash, so one pixel is one sim unit. The four sprites do not share one tip (their y offsets differ by a few pixels). The check is the rendered tip against the tallest sprite tip, within ±2 sim. Each view is also checked on its own (render tip minus that view's sprite tip): beyond ±2 sim (`TIP_VIEW_WARN_SIM`) warns, beyond ±4 sim (`TIP_VIEW_FAIL_SIM`) fails. The pine's per-view deltas are 0, +4, +4, +3, so it carries that warning; it passes the tallest-tip check.
- **Footprint.** `footprint_tiles` is the tile rectangle the object occupies. The horizontal AABB must sit inside that rectangle expanded by `max_overhang_tiles`. Trees overhang. The pine is a 1×1 tile with overhang 0.4, and its foliage reaches about 0.31 tiles past the tile edge. The trunk and the origin stay inside the tile. The AABB must contain the origin in both horizontal axes. The validator checks the horizontal extent of the whole mesh against the class's `footprint_bu` (max |x| and |y| in tile units, about the model origin): ±0.5 for `prop_tile` and `large_scenery`, ±0.85 for the full-tile tree classes, ±0.25 for `prop_quarter` (inside its quadrant), and ±0.40 for the `tree_quarter` canopy.
- **Trunk base (`tree_quarter`).** A quarter-tile tree's canopy may overhang its quadrant, but its trunk base may not: the trunk base must be within ±0.25 units (`trunk_base_bu`). The trunk base is the bottom slice of the mesh, from the lowest vertex up to `TRUNK_BASE_BAND_SIM` = 4 sim (1/8 tile, a quarter land step) above it. Vertices inside the slice count, and so does every point where a triangle edge crosses the top of the slice, so a leaning trunk is measured where it leaves the band. Four sim is tall enough to include the sunk base and a root flare, and short enough that the lowest canopy cards on the batch 1 trees sit above it.
- **Base.** Some geometry must reach Z = 0 (or the declared sink just below it). A floating tree fails.

## How the pine mesh is built

The archetype is `conifer` (`tools/sprite-to-3d/archetypes/conifer.py`). Positions are sim units. The approved params are `tools/sprite-to-3d/examples/rct2.scenery_small.tcrp/params.json`. Rebuilding with those params reproduces the reference mesh exactly (864 triangles, 1137 vertices, identical positions and UVs).

1. **Trunk.** An 8-sided flared prism. Nine rings from sim Z −3 to 134, radius 4.4 down to 0.5. The seam column is duplicated so the bark UVs wrap. Flat shaded, not a smooth cylinder.
2. **Fourteen foliage tiers.** Each clump is an ellipse (`a`, `b`, rotation `th`) at `(x, y, z)` with a top thickness `ht` and a bottom thickness `hb`. From the trunk: a 4-sided drooping branch, two 3-sided twigs, then cut-out cards:
   - one tilted top card (the pad),
   - a ring of 6 skirt cards leaning outward, scalloped by the atlas,
   - an underside card so you cannot see through the pad,
   - a smaller crown card,
   - 4 tilted rim tufts.
3. **Ten bare twigs** from the `branches` list (4-sided, tapered to a point).

Card proportions (`quality` in the params: pad scale 1.1, skirt lean, tuft size, branch radii) are shared by the archetype. The clump positions are the fitted part. They came from a (1+1) random search that maximised mean silhouette IoU (`fit.py`). Seeds are fixed (`foliage_seed` 500, one RNG per clump), so the same params always build the same twigs.

An earlier trunk table in the original fit file was not what the builder used. The rings in `params.json` are the ones in the mesh.

## Texturing

The atlas is 256×256 RGBA, painted in palette indices and then coloured with the game palette. Alpha is only 0 or 255. Filtering is nearest, about one texel per sprite pixel. The pine's layout (`conifer_atlas.py`):

| Tiles | Rect (x, y, w, h) | Use |
| --- | --- | --- |
| `pad_top_L0..3` | (48i, 0, 48, 48) | large top cards |
| `pad_top_S0..3` | the 32×32 block at (192, 0) | small tops, crowns, undersides |
| `pad_side0..3` | (0, 48+16i, 96, 16) | skirt cards |
| `tuft0..7` | 16×16 block at (96, 48) | rim tufts |
| `bark` | (160, 64, 32, 128) | trunk |
| `branch` | (192, 64, 16, 64) | branches and twigs |

Foliage is overlapping needle lumps: a scalloped disc, light at the top and centre, dark along the lower rim, with a few bright pixels. Edges stay scalloped so the card reads as needles rather than a disc. Skirt cards have a lit bumpy top and a dark lower edge. Bark is vertical streaks, histogram-matched to the trunk. Branch wood is a short brown ramp.

Histogram targets for the pine (palette-index counts) live as the defaults in `conifer_atlas.py`. They were counted on the TCRP sprite: foliage greens 142–150, bark browns 34–38, grey-blue highlights 11–15. A new conifer overrides `atlas.top_hist`, `side_hist`, `sprite_hist`, and `bark_hist` in its params after counting its own sprite. The paint seed is 2611.

Histogram provenance: counts derived locally from the extracted sprite; no pixel data stored. Histograms live in `params.json` only, with at most `MAX_HIST_ENTRIES` = 16 entries each (`rebuild.py` refuses a longer one), and `rebuild.py` does not copy them into the meta. The validator fails a meta whose `params` still carry one.

Colour the indices from the user's `Data/g1.dat` (sprite 1532, `SPR_GAME_PALETTE_DEFAULT`, bytes stored BGR). Those RGB values are the same table as `StandardPalette` in `src/openrct2/drawing/ImageImporter.h` (stored there as B, G, R). The pine atlas matches that palette: every opaque texel is one of those colours, and a byte comparison against an atlas painted from `g1.dat` shows no differing pixels. `rebuild.py --palette-header` reads the header so a machine without an install can still colour the example. Do not commit a palette file.

UVs in the mesh are texel coordinates with the origin at the top-left of the atlas. The glTF file stores them with the glTF top-left origin `(u / width, v / height)`. Blender's exporter flips V on the way out; `build.py` sets Blender UVs as `(u / width, 1 − v / height)` so both exporters agree.

## Recolour

Ranges, from `PaletteIndex.h` (also the survey):

| Role | Indices |
| --- | --- |
| Primary | 243–254 |
| Secondary | 202–213 |
| Tertiary | 46–57 |

The pine does not recolour (`hasPrimaryColour` and the others are clear). Its atlas uses real greens and browns.

When an object does recolour:

1. Do not bake one player's colour into the albedo. Those pixels are a shade, not a finished colour.
2. Follow the pipeline recommendation, section 4. Paint a mask (a second texture, or the glTF `COLOR_1` vertex attribute) where R, G, and B are the primary, secondary, and tertiary weights. Set `extras.rct_remap` on the material so the loader can find it.
3. Keep the albedo as the grey or brown shade the sprite uses inside the ramp, so the mix `base * remapColour` still has the sprite's shading. The runtime converts the chosen RCT colour to linear RGB, or samples a 1D ramp. Every colour variant stays in the same instanced draw.
4. Glass, overlay, wither, and animation frames are extra images beyond the four directions. Do not model them as the base mesh until those frames are part of the object's params.

`extract.py` records the DAT flags (`hasPrimaryColour`, and the rest) on the local `offsets.json`.

## Materials

One material is enough when the whole object is cut-out pixel art, which is the pine.

| Property | Value |
| --- | --- |
| Alpha | `MASK`, cutoff `0.5` |
| Magnify / minify | nearest (`9728` / `9728`) |
| Mipmaps | off. Minify must not be a mipmap mode (`9984`–`9987`) |
| Wrap | clamp to edge (`33071`) |
| Sidedness | double-sided on every MASK material (foliage cards and the trunk, when they share one material) |
| Roughness | 1 |
| Metallic | 0 |
| Specular | `KHR_materials_specular` `specularFactor` 0 |

The atlas is already shaded. The Blender material is flat-lit, roughness 1, specular IOR level 0, blend method CLIP, backface culling off. Workbench renders use no anti-aliasing for the IoU views. Beauty renders may anti-alias; they are previews, not the measurement.

Blender 4.2's glTF exporter writes `NEAREST_MIPMAP_NEAREST` and `alphaMode: BLEND` for this material. `tools/sprite-to-3d/patch_glb.py` runs after every export and forces the table above. It is idempotent. The Python writer emits the corrected material and is still patched, so both paths end in the same place.

## Art direction

The model should read as the sprite from the four RCT angles and as a small physical object from a free camera.

Enough detail to read naturally: a real trunk, real branches, and separate leaf cards. The pine has fourteen tiers of needles, not one cone. Edges are scalloped pixels. Shading is the handful of greens and browns already in the sprite, not a smooth gradient.

Stop short of a scanned tree. No individual pine needles as geometry, no normal maps, no subsurface scattering, no 4k textures. Also stop short of a low-poly blob: an 8-sided trunk is the floor for a tree this size, a 4-sided trunk reads as a post, and a stack of cubes reads as a voxel. Faceted "low poly art" and billboard crosses (two cards for the whole crown) are both the wrong style. The sprite is pixel art of a tree; the mesh is that tree, with the pixels kept on the texture.

Match the silhouette before adding more cards. A clump that does not move the IoU is clutter. The pine's fit stopped at raw mean IoU 0.725 (closed 0.759), which is the bar a 1-tile tree is expected to clear.

## Poly and texture budgets

Every numeric cap is in `tools/sprite-to-3d/conventions.py` (`SIZE_CLASSES` and the `MAX_*` constants). `validate.py` reads that file. Change a number there and nowhere else. The classes, and why each one exists:

| Size class | What it is | Why this class |
| --- | --- | --- |
| `prop_quarter` | Quarter-tile prop (bone, logs, cauldron) | The mesh stays inside its quadrant (±0.25 units). No canopy allowance. Atlas stays at the 128² default. |
| `prop_tile` | Full-tile prop that is not a tree | Same file limits as a small object. A short prop may use fewer than 600 triangles; that lower bound is a warning for trees only. |
| `tree_quarter` | Quarter-tile tree or bush (one of four per tile) | 0.5×0.5 tile. Trunk base within ±0.25 units, canopy within ±0.40. Floor 200, soft cap 400, hard cap 600 triangles. Atlas 128². |
| `tree_small` | One-tile tree shorter than the pine | Canopy may reach ±0.85 units. Atlas stays 128². |
| `tree_1tile` | One-tile tree the size of the pine | Canopy class. The target band is 600–1200 triangles and the hard cap is the file maximum. 256² is allowed for this class only with a written `texture.justification`, or for the pine object id, which is the reference exception. |
| `tree_large` | Taller or wider tree, still one mesh | Deliberately capped at the same 1500 triangles and 256² atlas as a 1-tile tree for now. A bigger tree does not get a larger file. Canopy allowance applies. 256² needs a justification. |
| `large_scenery` | Multi-tile scenery, one mesh for the whole footprint | Deliberately capped at 1500 triangles and 256² for now, the same file ceiling as one tile. Multi-tile large scenery will need its own per-tile budget later; until then the cap does not grow with the footprint. 256² needs a justification. |

New models use a 128² atlas. 256² is the pine's atlas and the ceiling, not the default. A 256² RGBA texture is about 256 KB before the GPU rounds it up (about 328 KB), which dominates VRAM once thousands of distinct models are loaded. The validator warns when a new model is above 128² unless `texture.justification` is a non-empty string. The pine object id is the exception and does not warn.

Each class has an explicit soft cap, `soft_max_tris`, and a hard cap, `hard_tris`. The numbers (all in `SIZE_CLASSES`):

| Size class | Floor (`tree_warn_below`) | `soft_max_tris` | `hard_tris` | Max atlas | `min_render_px` |
| --- | --- | --- | --- | --- | --- |
| `prop_quarter` | none | 250 | 500 | 128² | 100 |
| `prop_tile` | none | 500 | 1000 | 128² | 250 |
| `tree_quarter` | 200 | 400 | 600 | 128² | 150 |
| `tree_small` | 600 | 1000 | 1200 | 128² | 500 |
| `tree_1tile` | 600 | 1200 | 1500 | 256² | 500 |
| `tree_large` | 600 | 1200 | 1500 | 256² | 500 |
| `large_scenery` | none | 1200 | 1500 | 256² | 500 |

Over `soft_max_tris` warns (the exit code is unaffected). A top-level `triangle_justification` string in the meta (copied from `object.json` by `rebuild.py`) is shown on that warning; it annotates the warning and does not remove it. A tree under its floor warns. It does not fail. A prop has no floor. Anything over the hard cap fails. The GLB must be at most 128 KB; over the 100 KB target warns. Vertices must be at most 2500, with u16 indices. Exactly one node, one mesh, one primitive and one material.

## Export

1. Build the mesh from the archetype and the params.
2. Paint the atlas from the user's palette.
3. Export glTF binary, Y-up, positions in tile units, one object, selection only, apply modifiers.
4. Run `patch_glb.py` on the GLB.
5. Write `<object id>.meta.json` beside it.
6. Run `validate.py`. With sprites available, pass them and `--require-iou`.

`rebuild.py` does those steps. Blender 4.2 LTS is the authoring export (`build.py`) and is required for beauty renders and for `fit.py`. The same mesh module can be written by `glb_io.py` when Blender is not installed (`--no-blender`). Both go through `patch_glb.py`. The pine GLB in `data/object-models/` is the Blender export. `--no-blender` welds the same corners and writes u16 indices; it is the fallback, not the file that ships.

OBJ export is optional and stays in the gitignored output directory. The repository gitignores `*.obj` already. Do not add an OBJ next to the reference GLB.

Beauty renders, when you ask `build.py` for `--beauty`, are perspective previews. One of them uses the Z squash and is a reference image only. Neither is required for the definition of done.

## File names and the meta schema

```text
data/object-models/<object id>.glb
data/object-models/<object id>.meta.json
```

The atlas PNG and its tile JSON are build intermediates under `tools/sprite-to-3d/out/` (gitignored). They are already inside the GLB. The Visual Studio copy and `install(DIRECTORY "data/")` copy everything in `object-models` into the data directory, so a sidecar atlas would be shipped twice and would not be read. The meta is small and the loader ignores it.

The object id is the OpenRCT2 identifier (`rct2.scenery_small.tcrp`), not the DAT name. The file name is that id plus `.glb`. Do not put models under `data/object`, which is the downloaded objects zip and is gitignored. Do not put them in a subdirectory: the loader's scan is `object-models/*.glb` and it is not recursive.

The pine's meta, already next to the flat GLB, is the record from the Blender build: units, bounds, geometry, texture tiles, and the silhouette fit. The loader ignores it. `rebuild.py` writes a superset of that record (the same bounds and triangle count, plus `object_id`, `size_class`, `export`, and `texture.justification`). The validator accepts either shape.

The tooling meta uses schema `openrct2-3d.object-model.v1`. Its fields:

| Field | Meaning |
| --- | --- |
| `schema` | `openrct2-3d.object-model.v1` |
| `tool_version` | `sprite-to-3d 1` today |
| `object_id`, `dat_name`, `display_name` | identity |
| `archetype` | `conifer` or a later module |
| `size_class` | one of the budget table keys |
| `source_sprites.image_indices` | the direction images, `[0, 1, 2, 3]` for the pine |
| `source_sprites.offsets` | `{index, width, height, x_offset, y_offset}` per image, or `null` until a local extract fills them |
| `alignment` | `sprite_shift` and `sprite_y_nudge` |
| `units` | 32 sim per tile, origin, authored Z-up, exported Y-up, `z_squash_in_file: false` |
| `bounds_blender` | min, max, size in Z-up tile units |
| `height_sim` | logical height |
| `footprint_tiles` | `[width, height]` in tiles |
| `max_overhang_tiles` | allowed foliage overhang |
| `base_sink_sim` | how far below 0 the base may go |
| `triangles` | must equal the GLB |
| `texture` | width, height, `MASK`, cutoff 0.5, `NEAREST`, `mipmaps: false`, and the tile rects |
| `iou.views` | four numbers, or the approved measurement |
| `iou.mean` | average of the four |
| `iou.threshold_per_view`, `iou.threshold_mean` | 0.65 and 0.70, the raw-mask reference levels (reported, not a gate) |
| `iou.closed` | `{views, mean, kernel_px, threshold_per_view, threshold_mean}`: the rebuild's closed-mask IoU, the 3px kernel, and the gate (0.70 per view, 0.75 mean). Written when the rebuild had local sprites. |
| `iou.raw_measured` | `{views, mean}`: the rebuild's raw IoU. When `object.json` has no `approved_iou`, `iou.views` and `iou.mean` are this measurement too. |
| `triangle_justification` | optional; why the model stands over its `soft_max_tris`. Shown on the warning. |
| `params` | the archetype inputs that rebuild the mesh (trunk, clumps, branches, quality, seeds), without palette histograms |
| `review.status` | `approved` or `pending` |
| `review.note` | who signed off, and what they looked at |

`validate.py` rejects a GLB whose file name is not `<object id>.glb`, or a meta whose name is not `<object id>.meta.json`.

## Definition of done

A model is done when all of these are true:

1. `python3 tools/sprite-to-3d/validate.py <model dir> --sprites <local sprite dir> --require-iou` exits 0.
2. Structural checks pass: bounds and footprint inside the class limits (and the trunk base for `tree_quarter`), base at z = 0 within the declared sink, height within 0.2 units of `height_sim / 32`, Y-up with identity node transform, no Z squash in the file, MASK 0.5, nearest, no mipmaps, double-sided, roughness 1, specular 0, triangle count and texture size inside the hard budget, alpha strictly 0 or 255, meta present and valid.
3. Closed-mask silhouette IoU (3×3 close on both masks) is at least **0.70 on every view** and **0.75 on average**, the area ratio is inside 0.9–1.3, and no view's tip is more than ±4 sim off its sprite. The pine clears this (closed worst view 0.734, mean 0.759; raw 0.687 and 0.725).
4. The review PNG (sprite, render, overlap) has been looked at. Green is the overlap, red is sprite-only, blue is model-only. A high IoU with the mass in the wrong place (a trunk through the crown, a pad floating off the branch) still fails review.
5. `review.status` is `approved` and the note says who approved it.
6. The commit contains no RCT1/RCT2 sprites, palettes, DAT/g1/csg files, or review images that paste the original sprites.

`validate.py --self-test` checks the committed pine without sprites: the camera matches the RCT formula, rebuilding the params reproduces the triangle count and the bounds, and the structural checks pass. It does not recompute IoU. That needs the local sprites.

The validator's own rasteriser, with the squash applied in the camera, covers 3402, 3113, 3172, and 3137 opaque pixels on the four views. The approved Blender renders were 3403, 3113, 3172, 3137. One pixel on view 0 is the whole difference, so an IoU measured here will match the approved figures for any practical purpose. The rasteriser is depth-tested: camera depth is interpolated per pixel and kept in a z-buffer, so each pixel of the review sheet shows the nearest opaque texel regardless of triangle order. MASK holes neither draw nor write depth. Which pixels are opaque does not depend on the depth test, so coverage and IoU are the same with or without it.

## Building another object in this style

1. Extract it: `python3 tools/sprite-to-3d/extract.py --rct2-data-path "$RCT2" --dat TCRP --object-id rct2.scenery_small.tcrp --out tools/sprite-to-3d/cache/rct2.scenery_small.tcrp`. Change the DAT and the id. The cache is gitignored.
2. Copy `tools/sprite-to-3d/examples/rct2.scenery_small.tcrp/` to a new folder named with the object id. Set `object.json` (id, DAT, size class, height, footprint, recolour flags, alignment).
3. If it is a conifer, edit `params.json` clumps until the silhouette fits. Run `fit.py` under Blender to search. If it is not a conifer, add an archetype module next to `conifer.py` and register it in `archetypes/__init__.py`. Keep the same units, the same material rules, and the same meta schema.
4. Count the sprite's palette indices and, if they differ from the pine, put histograms in `params.json` under `atlas` (at most 16 entries each). Histogram provenance: counts derived locally from the extracted sprite; no pixel data stored.
5. Rebuild and validate with `--sprites` pointing at the cache folder.
6. Commit the GLB, the atlas, the atlas tile JSON, and the meta. Do not commit the cache or the review PNG.

Start from something as simple as the pine's structure even when the sprite is plainer. A short bush is fewer clumps, not a different kind of object. A quarter-tile prop is a different size class and a smaller budget, and it still uses the same origin, the same materials, and the same four-view check.
