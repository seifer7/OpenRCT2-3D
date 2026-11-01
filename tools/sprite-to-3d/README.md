# Sprite-to-3D tools

Build RCT2 scenery GLBs in the style of the approved Corsican Pine (`rct2.scenery_small.tcrp`). The style rules are [docs/agents/sprite-to-3d-style-guide.md](../../docs/agents/sprite-to-3d-style-guide.md). The runtime plan those files feed is [docs/agents/glb-model-pipeline-recommendation.md](../../docs/agents/glb-model-pipeline-recommendation.md).

The pine example lives in `examples/rct2.scenery_small.tcrp/`. The committed reference model is `data/object-models/rct2.scenery_small.tcrp/`.

## Setup

Blender **4.2 LTS**, on `PATH` as `blender`, for the authoring export, the beauty renders, and the silhouette fitter. The mesh itself is plain Python, so a machine without Blender can still export a GLB (`--no-blender`).

A virtualenv with numpy, Pillow, and py7zr:

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -r tools/sprite-to-3d/requirements.txt
```

py7zr is there so a worker can open a `.7z` of an object pack. The extractor reads a normal RCT2 install directory (the folder that contains `Data/g1.dat` and `ObjData/`).

You need your own RCT2 install. This repository does not contain `g1.dat`, object DATs, or sprites, and the tools must not add them.

## Rebuild the pine

From the repository root, with the venv active:

```bash
python3 tools/sprite-to-3d/rebuild.py \
  --object rct2.scenery_small.tcrp \
  --rct2-data-path /path/to/rct2 \
  --out data/object-models/rct2.scenery_small.tcrp
```

That command:

1. Extracts `TCRP.DAT` into `tools/sprite-to-3d/cache/` (gitignored).
2. Reads the palette from `Data/g1.dat` and paints the 256×256 atlas.
3. Builds the conifer mesh from `examples/rct2.scenery_small.tcrp/params.json`.
4. Exports `<object id>.glb` with Blender 4.2 when `blender` is on `PATH`, otherwise with the Python writer. Pass `--no-blender` to force the Python writer.
5. Runs `patch_glb.py` so the sampler is nearest, with no mipmaps, and the material is alpha MASK at 0.5.
6. Writes `<object id>.meta.json` and runs the validator with the extracted sprites. IoU has to clear 0.65 on every view and 0.70 on average.

The reference currently in `data/object-models/` was exported with `--no-blender`. Its mesh matches the Blender path: both call `archetypes/conifer.py`.

Check the committed pine without an RCT2 install:

```bash
python3 tools/sprite-to-3d/validate.py --self-test
```

That checks scale, the Y-up orientation, materials, the triangle budget, and that rebuilding the params reproduces the GLB's bounds. It does not recompute IoU. Pass a local sprite folder to do that:

```bash
python3 tools/sprite-to-3d/validate.py \
  data/object-models/rct2.scenery_small.tcrp \
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
  --atlas data/object-models/rct2.scenery_small.tcrp/rct2.scenery_small.tcrp.atlas.png \
  --atlas-json data/object-models/rct2.scenery_small.tcrp/rct2.scenery_small.tcrp.atlas.json \
  --out data/object-models/rct2.scenery_small.tcrp
```

Add `--views` for the four RCT-angle renders, or `--beauty` for the perspective previews. Both apply the Z squash only while rendering.

Fit clump positions (Blender, and a local sprite folder):

```bash
blender -b -P tools/sprite-to-3d/fit.py -- \
  --params tools/sprite-to-3d/examples/rct2.scenery_small.tcrp/params.json \
  --atlas data/object-models/rct2.scenery_small.tcrp/rct2.scenery_small.tcrp.atlas.png \
  --atlas-json data/object-models/rct2.scenery_small.tcrp/rct2.scenery_small.tcrp.atlas.json \
  --sprites tools/sprite-to-3d/cache/rct2.scenery_small.tcrp \
  --iters 200 --seed 1 \
  --out tools/sprite-to-3d/out/fit.json
```

Patch a GLB that Blender already wrote:

```bash
python3 tools/sprite-to-3d/patch_glb.py path/to/object.glb
```

## What not to commit

`tools/sprite-to-3d/cache/`, `tools/sprite-to-3d/out/`, and any `*.review.png`. Those hold extracted sprites, palette lookups, and comparison sheets. The GLB, the painted atlas, the tile JSON, and the meta are generated art and may be committed under `data/object-models/`.
