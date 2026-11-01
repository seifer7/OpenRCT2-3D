# Sprite-to-3D tools

Build RCT2 scenery GLBs in the style of the approved Corsican Pine (`rct2.scenery_small.tcrp`). The style rules are [docs/agents/sprite-to-3d-style-guide.md](../../docs/agents/sprite-to-3d-style-guide.md). The runtime plan those files feed is [docs/agents/glb-model-pipeline-recommendation.md](../../docs/agents/glb-model-pipeline-recommendation.md).

The pine example lives in `examples/rct2.scenery_small.tcrp/`. The approved GLB is the flat file `data/object-models/rct2.scenery_small.tcrp.glb` from the loader work. This pipeline does not commit another copy. The loader reads `<object id>.glb` only, in that directory, not in a subfolder. `<object id>.meta.json` beside it is for people and for the validator. The game does not read it.

## Setup

Blender **4.2 LTS**, on `PATH` as `blender`, for the authoring export, the beauty renders, and the silhouette fitter. The mesh itself is plain Python, so a machine without Blender can still export a GLB (`--no-blender`).

A virtualenv with numpy and Pillow:

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -r tools/sprite-to-3d/requirements.txt
```

The extractor reads a normal RCT2 install directory (the folder that contains `Data/g1.dat` and `ObjData/`).

You need your own RCT2 install. This repository does not contain `g1.dat`, object DATs, or sprites, and the tools must not add them.

## Rebuild the pine

From the repository root, with the venv active:

```bash
python3 tools/sprite-to-3d/rebuild.py \
  --object rct2.scenery_small.tcrp \
  --rct2-data-path /path/to/rct2
```

The default output directory is `tools/sprite-to-3d/out/` (gitignored): `<id>.glb` and `<id>.meta.json` land there, and the atlas intermediates stay in `tools/sprite-to-3d/out/build/`. That default does not touch `data/object-models/`. Replacing a GLB or meta that already ships there requires both `--out data/object-models` and `--force`. The command:

1. Extracts `TCRP.DAT` into `tools/sprite-to-3d/cache/` (gitignored).
2. Reads the palette from `Data/g1.dat` and paints the atlas. The pine's atlas matches that palette.
3. Builds the conifer mesh from `examples/rct2.scenery_small.tcrp/params.json`.
4. Exports the GLB with Blender 4.2 when `blender` is on `PATH`, otherwise with the Python writer. Pass `--no-blender` to force the Python writer. The approved file in the tree is the Blender export; `--no-blender` matches its vertex structure (welded vertices, u16 indices) and is not a second committed copy.
5. Runs `patch_glb.py` so the sampler is nearest, with no mipmaps, and the material is alpha MASK at 0.5.
6. Measures raw and closed-mask IoU against the extracted sprites, writes the flat meta (with both in its `iou` block), and runs the validator. The validator's review sheet goes to `<out>/<id>.review.png`, next to the GLB in the `--out` directory (gitignored as `*.review.png`). The gate is closed-mask IoU (3×3 close on both masks): at least 0.70 on every view and 0.75 on average. Raw IoU is reported beside it and does not gate.

Check the pipeline output, and the approved GLB when that file or the loader branch is available:

```bash
python3 tools/sprite-to-3d/validate.py --self-test
```

Pass a local sprite folder to recompute IoU:

```bash
python3 tools/sprite-to-3d/validate.py \
  data/object-models/rct2.scenery_small.tcrp.glb \
  --sprites tools/sprite-to-3d/cache/rct2.scenery_small.tcrp \
  --require-iou \
  --out tools/sprite-to-3d/out/rct2.scenery_small.tcrp.review.png
```

The review PNG pastes the original sprites beside the renders. It is gitignored (`*.review.png` and `tools/sprite-to-3d/out/`). Do not commit it. The meta must contain `alignment` (`sprite_shift` and `sprite_y_nudge`) or the validator refuses to score sprites. The pine's fitted alignment is `[-2, 0]` and `-1`. With that paste, raw IoU against the real TCRP sprites is 0.733, 0.712, 0.766, 0.687 (mean 0.725) and closed-mask IoU is 0.780, 0.737, 0.787, 0.734 (mean 0.759).

What the validator checks per size class, all read from `conventions.SIZE_CLASSES`:

- **IoU.** Closed-mask IoU gates (`IOU_CLOSE_KERNEL` 3, `IOU_CLOSED_MIN_PER_VIEW` 0.70, `IOU_CLOSED_MIN_MEAN` 0.75). Raw IoU and the 0.9–1.3 area ratio are reported; the area ratio gates.
- **Empty render.** Each view needs `min_render_px` opaque pixels: prop_quarter 100, prop_tile 250, tree_quarter 150, otherwise 500.
- **Triangles.** Over `soft_max_tris` warns, over `hard_tris` fails, under `tree_warn_below` warns (trees only). A meta `triangle_justification` is shown on the soft-cap warning.
- **Footprint.** The whole mesh within `footprint_bu`; for `tree_quarter` also the trunk base (bottom `TRUNK_BASE_BAND_SIM` = 4 sim) within `trunk_base_bu` 0.25.
- **Tips.** The model tip against the tallest sprite tip (±2 sim fails), and each view's tip against its own sprite (beyond ±2 warns, beyond ±4 fails).
- **Histograms.** None in the meta.

The validator rasteriser is depth-tested (per-pixel interpolated depth and a z-buffer), so the review sheet shows the nearest surface. Coverage and IoU do not depend on it.

### params.json

The archetype inputs. `rebuild.py` copies them into the meta's `params`, minus histograms.

| Key | Meaning |
| --- | --- |
| archetype keys (`trunk_rings`, `clumps`, `branches`, `quality`, `stems`, `parts`, ...) | geometry inputs; see the archetype module |
| `atlas.size`, `atlas.tiles` | atlas size and tile layout |
| `hist`, `*_hist` (`sprite_hist`, `top_hist`, `side_hist`, `bark_hist`, `rim_hist`, ...) | palette-index histograms that tint the atlas. Counts derived locally from the extracted sprite; no pixel data stored. At most 16 entries each (`MAX_HIST_ENTRIES`; `rebuild.py` refuses more). Not copied into the meta. |

`object.json` may also carry `approved_iou` (the reviewed raw figures, kept in `iou.views`) and `triangle_justification` (copied to the meta's top level).

```bash
python3 -m pytest tools/sprite-to-3d
```

## Other commands

Extract one object, without building a mesh:

```bash
python3 tools/sprite-to-3d/extract.py \
  --rct2-data-path /path/to/rct2 \
  --dat TCRP \
  --object-id rct2.scenery_small.tcrp \
  --out tools/sprite-to-3d/cache/rct2.scenery_small.tcrp
```

Blender export on its own (the atlas must already exist):

```bash
blender -b -P tools/sprite-to-3d/build.py -- \
  --object-id rct2.scenery_small.tcrp \
  --params tools/sprite-to-3d/examples/rct2.scenery_small.tcrp/params.json \
  --atlas tools/sprite-to-3d/out/build/rct2.scenery_small.tcrp.atlas.png \
  --atlas-json tools/sprite-to-3d/out/build/rct2.scenery_small.tcrp.atlas.json \
  --out tools/sprite-to-3d/out
```

Add `--views` for the four RCT-angle renders, or `--beauty` for the perspective previews. Both apply the Z squash only while rendering.

Fit clump positions (Blender, and a local sprite folder):

```bash
blender -b -P tools/sprite-to-3d/fit.py -- \
  --params tools/sprite-to-3d/examples/rct2.scenery_small.tcrp/params.json \
  --atlas tools/sprite-to-3d/out/build/rct2.scenery_small.tcrp.atlas.png \
  --atlas-json tools/sprite-to-3d/out/build/rct2.scenery_small.tcrp.atlas.json \
  --sprites tools/sprite-to-3d/cache/rct2.scenery_small.tcrp \
  --iters 200 --seed 1 \
  --out tools/sprite-to-3d/out/fit.json
```

Patch a GLB that Blender already wrote:

```bash
python3 tools/sprite-to-3d/patch_glb.py path/to/object.glb
```

## What not to commit

`tools/sprite-to-3d/cache/`, `tools/sprite-to-3d/out/`, and any `*.review.png`. Those hold extracted sprites, palette lookups, atlas intermediates, and comparison sheets. A committed model is only the flat `<object id>.glb`, with `<object id>.meta.json` beside it if you ship the note. The approved pine GLB is already provided by the loader work; do not add a second copy.
