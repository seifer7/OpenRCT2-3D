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
  --rct2-data-path /path/to/rct2 \
  --out data/object-models
```

That writes `data/object-models/rct2.scenery_small.tcrp.glb` and `rct2.scenery_small.tcrp.meta.json`. The atlas PNG and tile JSON stay in `tools/sprite-to-3d/out/build/` (gitignored). The command:

1. Extracts `TCRP.DAT` into `tools/sprite-to-3d/cache/` (gitignored).
2. Reads the palette from `Data/g1.dat` and paints the atlas. The pine's atlas matches that palette.
3. Builds the conifer mesh from `examples/rct2.scenery_small.tcrp/params.json`.
4. Exports the GLB with Blender 4.2 when `blender` is on `PATH`, otherwise with the Python writer. Pass `--no-blender` to force the Python writer. The approved file in the tree is the Blender export; `--no-blender` matches its vertex structure (welded vertices, u16 indices) and is not a second committed copy.
5. Runs `patch_glb.py` so the sampler is nearest, with no mipmaps, and the material is alpha MASK at 0.5.
6. Writes the flat meta and runs the validator with the extracted sprites. IoU has to clear 0.65 on every view and 0.70 on average.

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

The review PNG pastes the original sprites beside the renders. It is gitignored (`*.review.png` and `tools/sprite-to-3d/out/`). Do not commit it.

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
  --out data/object-models
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
