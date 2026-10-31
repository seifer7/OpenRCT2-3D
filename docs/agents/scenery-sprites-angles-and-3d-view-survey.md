# Scenery sprites, angles, and 3D view survey

Checked against `develop` at `cba2102` (this branch’s parent) and the files already on this branch. Commercial RCT2 data and the official objects zip are not in the repository. Paths such as `/opt/rct2` and `/tmp/orct2-objects` are example locations from an old VM, not paths in this repo. Numbers taken from those files are marked unverified.

## 1. Where object / scenery sprites live

### Runtime locations

| Source | Path / pattern | Format |
| --- | --- | --- |
| Legacy RCT2 objects | `{rct2}/ObjData/*.DAT` (example: `/opt/rct2/ObjData/`) | 16-byte `RCTObjectEntry` (`Object.h`, comment `size: 0x10`) plus a Sawyer chunk (`ObjectFactory.cpp`). For small scenery, `SmallSceneryObject::ReadLegacy` reads the image table last, after the string table, scenery group, and optional frame offsets |
| OpenRCT2 object catalog | `objects.zip` | `assets.json` points at `https://github.com/OpenRCT2/objects/releases/download/v1.8.0/objects.zip`. The zip’s layout is **unverified** (not in this repo) |
| Official / custom park objects | `*.parkobj` | A zip. `CreateObjectFromZipFile` requires `object.json`. Image strings may use `$LGX:` (`ImageTable.cpp`). Whether that archive is always named `images.dat` is **unverified** |
| OpenRCT2 install objects dir | `DirBase::openrct2` + `DirId::objects` | scanned as `*.dat;*.pob;*.json;*.parkobj` (`ObjectRepository.cpp`, `kPattern`) |
| User objects | `DirBase::user` + `DirId::objects` | Same patterns. On Linux the user base is `$XDG_CONFIG_HOME/OpenRCT2`, or `~/.config/OpenRCT2` when that variable is unset (`Platform.Linux.cpp`, `CreatePlatformEnvironment`), then `object` |
| Global UI / terrain sprites | `{rct2}/Data/g1.dat`, OpenRCT2 `resources/g2/` | `g1.dat` is install data, not in this repo. `resources/g2/` is in the repo. These are not per-object scenery art |

`PlatformEnvironment.cpp` maps `DirId` object under `DirBase::rct2` to `ObjData`.

### In-memory format

- `ImageTable` (`src/openrct2/object/ImageTable.h`) holds `std::vector<G1Element>`. `G1Element` (`src/openrct2/drawing/G1Element.h`) has width, height, `xOffset`, `yOffset`, flags, `zoomedOffset`, and a pointer to the pixel bytes.
- `ImageTable::ParseImages` accepts `$RCT2:OBJDATA/…`, `$LGX:…`, `$G1`, `$CSG`, or another string loaded with `Imaging::ReadFromBuffer` and `ImageImporter` (PNG is one import format in that file).
- Small scenery object type: `ObjectType::smallScenery`. JSON object type string `"scenery_small"` (`ObjectFactory.cpp`). Loader: `SmallSceneryObject`.

### Quadrant scenery and four directions

`SmallSceneryObject::ReadJson` sets footprint flags from `properties.shape`: `"4/4"` sets `occupiesFullTile`, `"2/4"` also sets `occupiesHalfTile`, `"3/4"` also sets `occupiesThreeQuarters`. A shape that matches none of those, including `"1/4"`, leaves `occupiesFullTile` unset. The tile painter then uses the quadrant offset path (`Paint.SmallScenery.cpp`).

For an object that is not withering, glass, or animated, the painter uses `sceneryEntry->image + direction` with `direction` in 0..3, so that path needs four images. Whether most official JSON files contain exactly four images is **unverified** (those files are not in this repo).

## 2. Four-angle sprites

### Direction mapping

`TileElementBase.h` defines, in order, starting at 0:

| Index | Name |
| --- | --- |
| 0 | `TILE_ELEMENT_DIRECTION_WEST` |
| 1 | `TILE_ELEMENT_DIRECTION_NORTH` |
| 2 | `TILE_ELEMENT_DIRECTION_EAST` |
| 3 | `TILE_ELEMENT_DIRECTION_SOUTH` |

`Paint.TileElement.cpp` sets `direction = tile_element->getDirectionWithOffset(rotation)` and passes it to `PaintSmallScenery`. `getDirectionWithOffset` adds the offset and masks with `kTileElementDirectionMask` (2 bits). `PaintSmallSceneryBody` then uses `baseImageIndex = sceneryEntry->image + direction`. Viewport rotation therefore selects which of the four images is drawn for a placed facing.

`DirectionFromTo` in `Location.hpp` returns 0 for negative X, 2 for positive X, 3 for negative Y, and 1 for positive Y, which are those same indices.

Extra images, from `Paint.SmallScenery.cpp` unless noted:

- Wither (`canWither`): `+4` at the first age threshold, and another `+4` at the second, on top of `image + direction`.
- Glass (`hasGlass`): child image at `baseImageIndex + 4`, drawn with `ImageId::WithTransparency(primaryColour)`.
- Animation strides are larger than `+4` for some flags. A fountain uses `image + 4` plus a 0..15 frame. A clock’s hour frame is `image + 68 + index` with `index` reduced into 0..47, so the highest hour offset in this function is `image + 115`. The minute hand uses `image + 8` plus a minute offset reduced into 0..59.
- Overlay (`hasOverlayImage`) is not painted in `Paint.SmallScenery.cpp`. `SmallSceneryObject::DrawPreview` draws `image + 4` when that flag is set. The scenery window checks the same flag.

### Example sprite sizes (unverified)

The sizes below were decoded on an old VM from example paths `/opt/rct2/ObjData` and an unpacked objects zip at `/tmp/orct2-objects`. Those files are not in this repo, so every object id, display name, height, pixel size, and flag in this table is **unverified**.

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

The claim that sample G1 flags were typically `5` (`hasTransparency` plus `hasRLECompression`) is **unverified**. The flag names exist on `G1Flag`. `hasTransparency` is the first enumerator and `hasRLECompression` is the third.

There are no per-angle PNG filenames in this repo for vanilla RCT2 scenery. Example locations, not repo paths:

- `/tmp/orct2-objects/rct2/scenery_small/rct2.scenery_small.tbn.json`
- `/opt/rct2/ObjData/TBN.DAT`

## 3. 3D rendering on `develop`

All 3D work in this repository is on `develop`. There is no `3d` branch.

### `src/openrct2-3d/`

These are on `develop`:

- `docs/3d-viewer.md` is the implementation plan. It says the 3D mode is a second view of `GameState`, bgfx draws the meshes, and `OpenGLDrawingEngine` (`src/openrct2-ui/drawing/engines/opengl/`) stays the 2D paletted compositor. It also says track and scenery have no 3D meshes today, and that a missing piece should be a coloured box or a camera-facing sprite from the loaded `g1` images until real art exists.
- `src/openrct2-3d/README.md` says this directory holds the viewer and will also hold the extractor, meshes, and the UI composite.
- `src/openrct2-3d/src/ParkView.cpp` and `ParkView.hpp` (`OpenRCT2::View3D::Park`) draw terrain, water, and path tiles. A comment in `ParkView.cpp` says tracks are not meshed yet. No scenery mesh draw was found in `src/openrct2-3d/`.
- `src/openrct2-3d/src/BgfxRenderer.cpp` and `BgfxRenderer.hpp` only define `View3D::bgfxLibraryName()`.
- `src/openrct2-3d/src/View3D.cpp` and `src/openrct2-ui/drawing/View3D.h` are the View menu toggle, bgfx present, and the paletted UI composite. `docs/3d-viewer.md` names `View3D::present()`.

The name `SceneExtractor` appears in `docs/3d-viewer.md` and not in `src/openrct2-3d/`. Terrain and path meshing is inside `ParkView.cpp`.

The coloured-box and `g1` billboard placeholders are the plan in `docs/3d-viewer.md`. They were not found as draws in `src/openrct2-3d/`.

### OpenGL drawing engine

`src/openrct2-ui/drawing/engines/opengl/` is the 2D paletted compositor. `docs/3d-viewer.md` says not to hang the 3D world on `OpenGLDrawingEngine`.

No GLB or glTF loader was found in this tree (no loader type or `.glb` / glTF reference under `src/`).

## 4. Grid units, pixels, and heights

From `src/openrct2/world/MapLimits.h`:

```text
kCoordsXYStep      = 32
kCoordsXYHalfTile  = kCoordsXYStep / 2    // 16
kCoordsZStep       = 8
kLandHeightStep    = 2 * kCoordsZStep     // 16
kWaterHeightStep   = 2 * kCoordsZStep     // 16
kCoordsZPerTinyZ   = 16
```

`Translate3DTo2DWithZ` in `src/openrct2/interface/Viewport.cpp`:

```text
rotated = pos.rotate(rotation)
screen = (rotated.y - rotated.x, ((rotated.x + rotated.y) >> 1) - pos.z)
```

World Z is subtracted 1:1 from screen Y. For a tile of `kCoordsXYStep` (32) at rotation 0, the four corners span 64 pixels in screen X and 32 pixels in screen Y.

`SmallSceneryEntry::height` is a `uint8_t` loaded from JSON `properties.height` (`SmallSceneryObject::ReadJson`). `SetSupportHeights` adds that field to the element’s base Z before calling `PaintUtilSetGeneralSupportHeight`, so the field is in the same Z units as paint `baseZ`. If a height value is 32, that is `4 * kCoordsZStep` and `2 * kLandHeightStep`. The bone height 32 in the table above is **unverified**.

RCT1 `Limits::kCoordsZStep` is 4 in `src/openrct2/rct1/Limits.h`. `S4Importer.cpp` uses it when converting imported heights.

### Scale

3D scenery and object models use **1 Blender unit per game tile**. That matches the viewer space in `docs/3d-viewer.md`: sim `x / 32`, `y / 32`, `z / 16`. One tile is 1 unit, and one land step (`kLandHeightStep` = 16) is 1 unit.

`ParkView.cpp` places the camera target at `map.x / 32` and `map.y / 32` (one tile = 1 unit). Mesh heights go through `worldZ`, which multiplies world Z by `kLandStepHeight / 16` with `kLandStepHeight = 0.5f`. One land step is 0.5 viewer units in that mesh (world Z / 32). The comment there says a full tile of height (`1.0`) read as too steep. That 0.5 factor is the current terrain mesh, not the model scale.

## 5. Remappable colours and transparency

### Transparency

- `PaletteIndex::transparent = 0` (`PaletteIndex.h`). `docs/3d-viewer.md` uses that index as the UI hole.
- `G1Flag::hasTransparency` is commented in `G1Element.h` as transparent pixels `0xFF` which are not rendered. `Drawing.Sprite.h` also says a pixel value of 0 is transparent on some blit paths. RLE is `G1Flag::hasRLECompression`.
- Glass scenery: `ImageId(baseImageIndex + 4).WithTransparency(primaryColour)` in `Paint.SmallScenery.cpp`.

### Colour remaps

`PaletteIndex.h` and `ImageImporter::GetPaletteIndexType` agree:

| Role | Palette indices |
| --- | --- |
| Primary remap | 243–254 (`primaryRemap0` … `primaryRemap11`) |
| Secondary remap | 202–213 (`secondaryRemap0` … `secondaryRemap11`) |
| Tertiary remap | 46–57 (`tertiaryRemap0` … `tertiaryRemap11`) |

`SmallSceneryEntry.h` has `hasPrimaryColour`, `hasSecondaryColour`, `hasTertiaryColour`, `hasGlass`, and `supportsHavePrimaryColour`.

`PaintSmallSceneryBody` applies `WithPrimary`, `WithSecondary`, and `WithTertiary` when `imageTemplate.IsRemap()` is false. A ghost or a selected element sets a remap template first, so those pixels are not recoloured on top of that template. Supports use `WithPrimary` when `supportsHavePrimaryColour` is set, unless the image template is already a remap (`PaintSmallScenerySupports`).

A byte-count comparison of decoded `BRBASE` against `TBN` / `TERS` is **unverified** (the DAT files are not in this repo).

Consequences of the palette and paint code for a later mesh conversion:

1. Paletted G1 has to be decoded with the game palette. Index 0 is the named transparent index; the `hasTransparency` comment names `0xFF` for that flag.
2. Remap-range pixels are palette slots, not a finished RGB colour.
3. Glass, overlay, animated, and withering objects use extra images beyond the four direction slots.

## 6. Small conversion candidates

Prefer objects whose paint path is `image + direction` only: no animation, glass, or overlay, no primary remap, and shape `1/4` or `4/4`. The rows below are the unverified examples from section 2. They are not confirmed against files in this repo.

| Priority | Object ID | Example path | Why (unverified sizes) |
| --- | --- | --- | --- |
| 1 | `rct2.scenery_small.tbn` | example `TBN.DAT` under `/opt/rct2/ObjData` | Tiny asymmetric bone; 4 views; ~12–17×30–35 |
| 2 | `rct2.scenery_small.tml` | example `TML.DAT` | Logs; ~23×18 |
| 3 | `rct2.scenery_small.ters` | example `TERS.DAT` | Ruined statue; ~30×20 |
| 4 | `rct2.scenery_small.tcd` | example `TCD.DAT` | Cauldron; ~24×24 |
| 5 | `rct2.scenery_small.tdm` | example `TDM.DAT` | Small prop; ~17×32. Not in the size table above; **unverified** |
| 6 | `rct2.scenery_small.tq1` | example `TQ1.DAT` | Chess queen; identical 20×49 frames |
| Optional harder | `rct2.scenery_small.ts0` / `tm0` / `tl0` | example `TS0.DAT` / `TM0.DAT` / `TL0.DAT` | Trees; larger |
| Remap test (later) | `official.scenery_small.support_structure_half` | parkobj inside objects.zip (**unverified**) | 64×48 and `hasPrimaryColour` |

The clock paint path indexes through `image + 115`. Which object id that is, and whether it stores 116 images, is **unverified**.

## Key source files

- `src/openrct2/world/MapLimits.h` — coordinate constants
- `src/openrct2/object/SmallSceneryObject.cpp`, `SmallSceneryEntry.h`
- `src/openrct2/object/ImageTable.cpp` — image ref syntax and G1 load
- `src/openrct2/paint/tile_element/Paint.SmallScenery.cpp` — `image + direction`
- `src/openrct2/paint/tile_element/Paint.TileElement.cpp` — `getDirectionWithOffset(rotation)`
- `src/openrct2/drawing/G1Element.h`, `PaletteIndex.h`, `ImageId.hpp`
- `src/openrct2/interface/Viewport.cpp` — `Translate3DTo2DWithZ`
- `docs/3d-viewer.md`, `src/openrct2-3d/` — 3D view on `develop`
