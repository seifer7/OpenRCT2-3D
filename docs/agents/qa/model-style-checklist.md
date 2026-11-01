# OpenRCT2-3D small-scenery GLB model style checklist (derived from TCRP v2)

Reference: `data/object-models/rct2.scenery_small.tcrp.glb`. The file is 85,408 B, sha256 `e2e25e2e…663c`, and matches the approved TCRP v2 export byte for byte. The handoff copies of that export are not stored in this repository.
Written 2026-11-01 AEST. **Must** items are hard requirements. **Target** items are style goals.

## 1. Naming and placement
- [ ] **Must:** the file is `data/object-models/<object identifier>.glb`, where the identifier is the exact object id, e.g. `rct2.scenery_small.tcrp`. It must pass `identifierIsSafe` (no path separators and no Windows reserved names).
- [ ] **Target:** ship `<identifier>.meta.json` next to it with bbox, triangle counts, atlas info and the silhouette stats.

## 2. Units, axes and origin
- [ ] **Must:** 1 Blender unit = 1 tile = 32 sim units on X, Y **and** Z, so one land step (16 sim Z) is 0.5 BU. Scale is applied (object scale 1.0). No render squash is baked in: the KZ `sqrt(2/3)` is render-only.
- [ ] **Must:** the export is glTF Y-up (the loader maps `x=gx, y=-gz, z=gy`). The authoring space is Z-up.
- [ ] **Must:** the origin is the tile centre in XY, with z = 0 at the tile base. **Exactly 1 node**, with an identity transform (no translation, rotation or scale on nodes).
- [ ] **Must:** direction 0 faces the same way as sprite direction 0. Directions 1-3 come from clockwise quarter turns at runtime.

## 3. Footprint and height
- [ ] **Must:** full-tile non-vegetation stays within **±0.5 BU** in XY (±16 sim).
- [ ] **Must:** quarter-tile objects are authored about their **quadrant centre** (8, 8), (8, 24), (24, 24) or (24, 8); the loader translates them to the tile centre ±8 sim by the stored quadrant. A `prop_quarter` stays within **±0.25 BU** of that origin (inside its quadrant). A `tree_quarter` keeps its **trunk base within ±0.25 BU** and its **canopy within ±0.40 BU**; the trunk base is the bottom 4 sim of the mesh (`TRUNK_BASE_BAND_SIM`), edge crossings at the top of that band included.
- [ ] **Must:** quarter-tile comparison pastes use the quadrant anchor (`SceneryQuadrantOffsets` − 1: (7, 7), (7, 23), (23, 23), (23, 7)) and the same `sprite_y_nudge` −1 as a full tile. `vOffsetCentre` applies to full-tile objects only.
- [ ] **Must:** tree canopy overhang is **≤ ±0.85 BU** (≈27 sim). TCRP reaches −0.812 / +0.690.
- [ ] **Must:** the top height matches the tallest sprite tip to within **±2 sim (±0.0625 BU)**. TCRP's tip is 4.1875 BU = 134 sim.
- [ ] **Must:** each view's tip is within **±4 sim** of that view's sprite tip. **Target:** within ±2 sim (beyond ±2 warns). Both limits are inclusive: exactly ±4 is a WARN, not a FAIL. TCRP's per-view deltas are 0, +4, +4, +3, so it sits on the fail limit and carries the warning.
- [ ] **Must:** the base sink below z = 0 is **≤ 0.1 BU** (≈3 sim). TCRP is −0.094.

## 4. Geometry budget
- [ ] **Must:** triangles ≤ the class `hard_tris`. **Target:** ≤ the class `soft_max_tris` (over it warns; a meta `triangle_justification` is shown on the warning but does not clear it), and for trees ≥ the class floor. TCRP has 864.

  | Size class | Floor | `soft_max_tris` | `hard_tris` | Max atlas |
  |---|---|---|---|---|
  | `prop_quarter` | — | 250 | 500 | 128² |
  | `prop_tile` | — | 500 | 1000 | 128² |
  | `tree_quarter` | 200 | 400 | 600 | 128² |
  | `tree_small` | 600 | 1000 | 1200 | 128² |
  | `tree_1tile` | 600 | 1200 | 1500 | 256² |
  | `tree_large` | 600 | 1200 | 1500 | 256² |
  | `large_scenery` | — | 1200 | 1500 | 256² |
- [ ] **Must:** ≤ **2500** vertices after splits. Loader limits are 65,535 vertices and 200,000 indices. TCRP has 1908.
- [ ] **Must:** u16 indices, triangle list, with POSITION, NORMAL and TEXCOORD_0 only. No skins, morphs, animations, cameras or lights.
- [ ] **Must:** **1 mesh, 1 primitive, 1 material** (loader cap is 8 primitives). Each extra primitive costs a VB, an IB, a texture and one draw call per frame.
- [ ] **Target:** no degenerate or zero-area triangles. Normals are present and normalised.

## 5. Texture
- [ ] **Must:** one embedded PNG atlas (bufferView, no external URI, because external URIs are rejected). Power of two, **≤ 256 × 256**. **Target:** 128² for simple objects, since a 256² RGBA8 is 256 KB of VRAM per model.
- [ ] **Must:** texel density is about 1 texel per sim unit (≈ **32 texels/BU**, within ±25%).
- [ ] **Must:** RGBA, with alpha **strictly 0 or 255** (no partial alpha).
- [ ] **Must:** every opaque texel is an **exact RCT2 palette colour** (`src/openrct2/drawing/ImageImporter.h`, BGR order). **Target:** ≤ 32 distinct colours. TCRP uses 19.
- [ ] **Target:** opaque coverage ≥ 20% of the atlas (TCRP 24.2%), with tiles packed on 16-px multiples.

## 6. Sampler and material
- [ ] **Must:** the sampler is mag **and** min = NEAREST (9728/9728), with wrap S/T = CLAMP_TO_EDGE (33071) and no mipmaps.
- [ ] **Must:** material alphaMode **MASK**, alphaCutoff **0.5**, doubleSided **true**, metallic **0**, specular 0 / roughness 1. No emissive or extra maps.

## 7. File size
- [ ] **Must:** GLB ≤ **128 KB**. **Target:** ≤ 100 KB. TCRP is 85.4 KB, with a 17.7 KB PNG and 1.5 KB of JSON.

## 8. Match to the 2D sprite (all 4 directions, rotation 0)
- [ ] **Must:** closed-mask silhouette IoU against the sprite (3×3 morphological close on **both** masks, `IOU_CLOSE_KERNEL` = 3) has a **mean ≥ 0.75** and is **≥ 0.70 for each view**. Raw IoU is reported beside it (the old raw reference was mean ≥ 0.70, each ≥ 0.65) but does not gate. **Only closed IoU is gated**, both the fresh measurement and the meta's recorded `iou.closed` block (views and mean); the meta's raw `iou.views` / `iou.mean` and `iou.raw_measured` are records and are not checked against any threshold. TCRP: closed 0.780 / 0.737 / 0.787 / 0.734, mean 0.759; raw mean 0.725.
- [ ] **Must:** each view renders at least the class `min_render_px` opaque pixels (prop_quarter 100, prop_tile 250, tree_quarter 150, otherwise 500).
- [ ] **Must:** the render/sprite pixel-area ratio (raw masks) is **0.9-1.3** for each view.
- [ ] **Must:** palette histograms stay in `params.json` (≤ 16 entries each; counts derived locally from the extracted sprite, no pixel data stored) and are not copied into the meta.
- [ ] **Target:** a visual check next to the sprite fallback in the 3D view and in the 2D game.

---

## PR #6 compliance (head 459d931e)

Point-in-time check of the one model shipped at that head, against the checklist as it stood then (raw IoU gate 0.65 / 0.70). The thresholds above are the standing checklist.

The PR ships one model, `rct2.scenery_small.tcrp.glb`. Values come from parsing the GLB, decoding the atlas, and the comparison stats captured with the review.

| Check | Threshold | TCRP v2 | Result |
|---|---|---|---|
| File name | `<identifier>.glb` | `rct2.scenery_small.tcrp.glb` | PASS |
| Units / axes | 1 BU = 1 tile, Y-up glTF, no squash | Uniform sim/32, Blender `export_yup` | PASS |
| Nodes / transform | 1 node, identity | 1 node, no TRS | PASS |
| Origin | tile centre, z0 = ground | Centre ≈ (−0.07, −0.02); base −0.094 | PASS |
| Footprint (tree) | ≤ ±0.85 BU | −0.812..0.676 × −0.729..0.690 | PASS. Exceeds ±0.5 as expected for a tree canopy. |
| Height | 134 sim ±2 | 4.1875 BU = 134 sim | PASS |
| Base sink | ≤ 0.1 BU | 0.094 | PASS (close to the limit) |
| Triangles | ≤ 1500 (target 600-1200) | 864 | PASS |
| Vertices | ≤ 2500 | 1908 | PASS |
| Mesh / primitive / material | 1/1/1 | 1/1/1 | PASS |
| Index type | u16 | u16, 2592 indices | PASS |
| Atlas size | pow2, ≤ 256² | 256² | PASS (at the limit; 256 KB VRAM per model) |
| Texel density | ≈ 32 texels/BU | ≈ 32 | PASS |
| Alpha | 0/255 only | 0/255 only, 24.2% opaque | PASS |
| Palette | exact RCT2 colours, ≤ 32 | 19, all exact (11-15, 34-38, 142-150) | PASS |
| Sampler | NEAREST/NEAREST, CLAMP | 9728/9728, 33071 | PASS |
| Material | MASK 0.5, double-sided, metallic 0 | MASK 0.5, double-sided, metallic 0, specular 0 | PASS |
| File size | ≤ 128 KB (target 100) | 85.4 KB | PASS |
| Silhouette IoU | mean ≥ 0.70, each ≥ 0.65 | 0.733 / 0.712 / 0.766 / 0.687, mean 0.725 | PASS (stored stats; not re-run) |
| Area ratio | 0.9-1.3 | 1.03-1.26 | PASS (stored stats) |
| In-game visual | eyeball check | PR preview PNG only | NOT VERIFIED in game |

Notes:
- There are no other GLBs in the PR to check.
- The IoU and area figures come from comparison stats captured during the review. The comparison script, its render output, and the sprite source directory were local review files and are not in this repository, so the figures were not re-run from the tree.
- At scale, the 256² atlas is the main VRAM cost (about 3.3 GB for 10k distinct models). Recommend 128² for most new objects, or a shared palette atlas, until the renderer pools textures.
