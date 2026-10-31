---
cursor:
  subagentId: "bc-e129bd64-f4c7-58b8-bdff-f1aefd6cd124"
---

# Scenery sprites, angles, and 3D view survey

Read-only exploration of `/workspace` (OpenRCT2 fork) plus local RCT2 install at `/opt/rct2` and official objects zip v1.7.11.

## 1. Where object / scenery sprites live

### Runtime locations

| Source | Path / pattern | Format |
| --- | --- | --- |
| Legacy RCT2 objects | `{rct2}/ObjData/*.DAT` (e.g. `/opt/rct2/ObjData/`) | 16-byte `RCTObjectEntry` + Sawyer-chunked payload; image table at end of decoded chunk |
| OpenRCT2 object catalog | `objects.zip` → `rct2/scenery_small/*.json` (and ww/tt/…) | JSON metadata; images usually reference DAT via `$RCT2:OBJDATA/NAME.DAT[0..N]` |
| Official / custom park objects | `official/**/*.parkobj` (zip) | `object.json` + `images.dat` (LGX/G1 archive) via `$LGX:images.dat[0..N]` |
| OpenRCT2 install objects dir | `DirBase::openrct2` + `DirId::objects` | scanned as `*.dat;*.pob;*.json;*.parkobj` (`ObjectRepository.cpp`) |
| User objects | `DirBase::user` + `DirId::objects` (e.g. `~/.config/OpenRCT2/object/`) | same patterns |
| Global UI / terrain sprites | `{rct2}/Data/g1.dat`, OpenRCT2 `resources/g2/` | G1/GX; **not** per-object scenery art |

`PlatformEnvironment` maps `DirBase::rct2` + `DirId::objects` → `ObjData`.

`assets.json` pulls official extras from `https://github.com/OpenRCT2/objects/releases/.../objects.zip` (JSON stubs + `.parkobj`, not a full DAT dump).

### In-memory format

- `ImageTable` (`src/openrct2/object/ImageTable.{h,cpp}`) holds `G1Element` entries (`src/openrct2/drawing/G1Element.h`): width/height, x/y offsets, flags, zoom, paletted pixel data.
- JSON image refs parsed in `ImageTable::ParseImages`: `$RCT2:OBJDATA/…`, `$LGX:…`, `$G1[…]`, `$CSG[…]`, or raw PNG paths (imported via `ImageImporter`).
- Small scenery object type: `ObjectType::smallScenery` / JSON `"scenery_small"`; loader `SmallSceneryObject`.

### Typical 1×1 / quadrant scenery with 4 angles

Most static small scenery JSON uses exactly four images:

```json
"images": [ "$RCT2:OBJDATA/TBN.DAT[0..3]" ]
```

Paint selects with `sceneryEntry->image + direction` (`Paint.SmallScenery.cpp`). Shape `"1/4"` = quadrant (not full tile); `"4/4"` = full tile.

---

## 2. Example: four-angle sprites

### Angle / direction mapping

Directions (`TileElementBase.h` / `DirectionFromTo` in `Location.hpp`):

| Index | Compass | Used as |
| --- | --- | --- |
| 0 | West | `TILE_ELEMENT_DIRECTION_WEST` |
| 1 | North | `TILE_ELEMENT_DIRECTION_NORTH` |
| 2 | East | `TILE_ELEMENT_DIRECTION_EAST` |
| 3 | South | `TILE_ELEMENT_DIRECTION_SOUTH` |

Paint path: `direction = tile_element->getDirectionWithOffset(session.CurrentRotation)` then `baseImageIndex = sceneryEntry->image + direction`. Viewport rotation cycles which of the four sprites is shown for a given placement facing.

Wither / overlay / glass / animation add further +4 (or larger) strides on top of the base quartet (`Paint.SmallScenery.cpp`). Clocks use `[0..115]` etc.—avoid those for first GLB conversions.

**Note:** `seifer7-image-to-blender` composite sheets put “front” at the **top of the sheet** and match other views by landmarks—not by assuming DAT index order is clockwise FRBL.

### Concrete sizes (decoded from `/opt/rct2/ObjData`)

| Object ID | DAT | Name | Height (world Z) | Sprite sizes (dirs 0–3) | Notes |
| --- | --- | --- | --- | --- | --- |
| `rct2.scenery_small.tbn` | `TBN.DAT` (1670 B) | Bone | 32 | 12×35, 17×31, 12×30, 17×32 | 1/4, rotatable, no colour remap |
| `rct2.scenery_small.ters` | `TERS.DAT` | Ruined Statue | 8 | 29×21, 30×23, 29×20, 30×18 | 1/4, small |
| `rct2.scenery_small.tml` | `TML.DAT` | Logs | 8 | 24×18, 23×18, 24×19, 23×19 | 1/4 |
| `rct2.scenery_small.tcd` | `TCD.DAT` | Cauldron | 16 | 24×24, 25×24, 24×23, 24×23 | 1/4 |
| `rct2.scenery_small.tq1` | `TQ1.DAT` | White Queen | 40 | 20×49 ×4 | 1/4 chess piece |
| `rct2.scenery_small.ts0` | `TS0.DAT` | (small tree) | 46 | ~32×46–52 | 1/4-ish footprint flags |
| `rct2.scenery_small.tm0` | `TM0.DAT` | (medium tree) | 70 | ~53–57 × 77–87 | 4/4 |
| `rct2.scenery_small.tl0` | `TL0.DAT` | Tree | 134 | ~56–64 × 135–139 | 4/4 tall tree |
| `official.scenery_small.support_structure_half` | parkobj `images.dat` | Support Structure | 16 | 64×48 ×4 (xOff=-32,yOff=-20) | `$LGX:`; **hasPrimaryColour** |

G1 flags on samples were typically `5` = `hasTransparency` + `hasRLECompression`.

There are no per-angle PNG filenames for vanilla RCT2 scenery—only DAT/LGX indices. Official JSON path examples:

- `/tmp/orct2-objects/rct2/scenery_small/rct2.scenery_small.tbn.json` (from objects.zip)
- Game file: `/opt/rct2/ObjData/TBN.DAT`

---

## 3. Existing 3D rendering / model work

**Not on current `develop` checkout**—lives on fork branches:

### A. `origin/3d` + PR [#3](https://github.com/seifer7/openrct2/pull/3) (“Draw 3D terrain and water…”)

- Docs: `docs/3d-viewer.md`, `src/openrct2-3d/README.md`
- Code: `src/openrct2-3d/` (bgfx/bx/bimg submodules, `ParkView`, `BgfxRenderer`, `View3D`)
- Goal: View-menu toggle; keep 2D default; UI composited over 3D; scenery/track still placeholders (boxes or billboards from g1) until meshes exist
- Notes.md task **RCT-2** tracks this PR

### B. `origin/view3d`

- Earlier integrated module under `src/openrct2-ui/view3d/` (`OpenGLRenderer`, `MapAdapter`, `TileTexture`, `TerrainGeometry`, README)
- Reuses loaded terrain/path sprites as textures; translucent water; placeholders for other objects
- Tests under `test/tests/view3d/`

### C. `origin/seifer7-image-to-blender`

- Offline asset pipeline (not engine): `.github/agents/image-to-blender.agent.md`, `scripts/image-to-blender/`
- Four-view isometric → Blender → paired `.blend` / `.glb` (4 Blender units per tile)
- Style refs: `references/isometric sample.png`, `ParkEntrance.glb`

### D. Upstream OpenGL (on develop)

- `src/openrct2-ui/drawing/engines/opengl/` — **2D paletted** compositor, not a 3D park renderer (`docs/3d-viewer.md` on `3d` branch explicitly says so)

No GLB/glTF **loader** in the game tree on develop; GLB is an export target of the Blender agent only so far.

---

## 4. Grid units ↔ pixels / heights

From `src/openrct2/world/MapLimits.h`:

```text
kCoordsXYStep      = 32   // one tile edge in world XY
kCoordsXYHalfTile  = 16
kCoordsZStep       = 8    // one height unit in world Z
kLandHeightStep    = 16   // 2 * kCoordsZStep; one slope corner rise
kWaterHeightStep   = 16
kCoordsZPerTinyZ   = 16
```

Isometric projection (`Viewport.cpp` `Translate3DTo2DWithZ`):

```text
screen = (y' - x', ((x' + y') >> 1) - z)   after rotating by viewport
```

One tile → screen diamond **64×32** pixels. World Z subtracts 1:1 from screen Y.

Small-scenery `properties.height` / `SmallSceneryEntry::height` is in **world Z units** (same space as paint `baseZ`), not tile-index steps. Example: bone height 32 = 4×`kCoordsZStep` = 2×`kLandHeightStep`.

**3D viewer doc scale** (`docs/3d-viewer.md` on `3d`): viewer space `x/32, y/32, z/16` → 1 tile = 1 unit, 1 land step = 1 unit. `view3d` `TerrainGeometry` builds vertices with raw `kCoordsXYStep` / corner heights in world Z.

**Image-to-Blender:** 1 source tile = **4 Blender units**; native +Z up, −Y front, +X right; GLB export Y-up via glTF exporter.

RCT1 uses `kCoordsZStep = 4` in `rct1/Limits.h` (import conversion only).

---

## 5. Remappable colours / transparency (GLB impact)

### Transparency

- `PaletteIndex::transparent = 0` (UI / viewport hole).
- `G1Flag::hasTransparency`: documentation says transparent pixels are `0xFF` in non-RLE data; RLE sprites encode skips (flags bit pattern `5` = transparency + RLE on samples).
- Glass scenery: extra image at `base+4` drawn with `ImageId::WithTransparency(primaryColour)` (blend remap, not opaque RGB).

### Colour remaps (`PaletteIndex.h` / `ImageImporter.h`)

| Role | Palette indices |
| --- | --- |
| Primary remap | 243–254 (`primaryRemap0`…) |
| Secondary remap | 202–213 (`secondaryRemap0` / hot pink range) |
| Tertiary remap | 46–57 (yellow range) |

Object flags: `hasPrimaryColour`, `hasSecondaryColour`, `hasTertiaryColour`, `hasGlass`, `supportsHavePrimaryColour` (`SmallSceneryEntry.h` / JSON).

Paint applies remaps via `ImageId.WithPrimary/Secondary/Tertiary` when not already a ghost/remap template.

Decoded DAT heuristic: `BRBASE` (remap flagged) has ~1537 primary-range bytes per sprite; `TBN`/`TERS` (no remap flags) have ~0–2—safe “baked colour” candidates.

**GLB conversion implications:**

1. Must decode paletted G1 → RGBA using the game palette; treat 0 / RLE gaps as alpha.
2. Remap-index pixels are **placeholders**, not final colours—either bake a default colour map or keep remap masks/materials for runtime tinting.
3. Glass / overlay / animated / withering objects need extra images or layers—poor first conversion targets.
4. Nearest-neighbour / sprite-scale fidelity matters (Blender agent guidance).

---

## 6. Good small conversion candidates

Prefer: `[0..3]` only, no animation/glass/overlay, no primary remap, small bounding boxes, `shape` `1/4` or simple `4/4`.

| Priority | Object ID | Path(s) | Why |
| --- | --- | --- | --- |
| 1 | `rct2.scenery_small.tbn` | JSON `rct2/scenery_small/rct2.scenery_small.tbn.json`; DAT `/opt/rct2/ObjData/TBN.DAT` | Tiny asymmetric bone; 4 clear views; ~12–17×30–35 |
| 2 | `rct2.scenery_small.tml` | `…/rct2.scenery_small.tml.json`; `TML.DAT` | Logs; ~23×18 |
| 3 | `rct2.scenery_small.ters` | `…ters.json`; `TERS.DAT` | Ruined statue; ~30×20 |
| 4 | `rct2.scenery_small.tcd` | `…tcd.json`; `TCD.DAT` | Cauldron; ~24×24 |
| 5 | `rct2.scenery_small.tdm` | `…tdm.json`; `TDM.DAT` | Small prop; ~17×32 |
| 6 | `rct2.scenery_small.tq1` | `…tq1.json`; `TQ1.DAT` | Chess queen; identical 20×49 frames—good symmetry check |
| Optional harder | `rct2.scenery_small.ts0` / `tm0` / `tl0` | `TS0.DAT` / `TM0.DAT` / `TL0.DAT` | Trees; larger; organic silhouette |
| Remap test (later) | `official.scenery_small.support_structure_half` | parkobj in objects.zip | Clean 64×48 LGX + `hasPrimaryColour` |

Avoid initially: `tck` (clock, 116 images), fountains, glass, wither trees, anything with `hasPrimaryColour` until remap strategy exists.

---

## Key source files

- `src/openrct2/world/MapLimits.h` — coordinate constants  
- `src/openrct2/object/SmallSceneryObject.cpp`, `SmallSceneryEntry.h`  
- `src/openrct2/object/ImageTable.cpp` — image ref syntax / G1 load  
- `src/openrct2/paint/tile_element/Paint.SmallScenery.cpp` — `image + direction`  
- `src/openrct2/drawing/G1Element.h`, `PaletteIndex.h`, `ImageId.hpp`  
- `src/openrct2/interface/Viewport.cpp` — `Translate3DTo2DWithZ`  
- Branches: `origin/3d`, `origin/view3d`, `origin/seifer7-image-to-blender`
