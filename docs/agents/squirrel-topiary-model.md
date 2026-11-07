# Squirrel Shaped Tree — TSQ

## Current status: revision 2 installed for the build

The owner reported that revision 2 looks much better and requested removal of revision 1. The obsolete conifer parameters, revision-1 model/metadata, atlas, fitted parameters, fitter script, Blender file and previews have been deleted. Revision-1-only tests were removed; installed-model checks now live in `test_squirrel_revision2.py`. The shared object definition and preview script remain because revision 2 uses them. Original game sprites and revision-2 runtime/build files were preserved.

The owner rejected the original GLB's anatomy and greens in the game, Blender and previews. The historical deliverables and metrics below describe that **rejected first model**, not artistic approval. At the owner's request, revision 2 and its matching metadata now replace the old files in `data/object-models/`. The existing `CopyObjectModels` build target picks up the flat GLB automatically; no build-script changes are required. Installation is not a claim of visual approval.

Revision 2 source is `tools/sprite-to-3d/examples/rct2.scenery_small.tsq/revision2/` (`params.json`, `author.py`, `sculpt.py`). It uses Blender 5.2.1 to fuse symmetric anatomical volumes and a swept curled tail, smooth the joins, and decimate the resulting single closed surface. It has a muzzle, paired ears, thighs, feet and tucked forepaws; it does not use independently silhouette-optimized conifer lumps. The atlas is fully opaque small foliage clusters with the source's nine-green histogram balanced per local UV patch, avoiding conifer skirt dark rims.

Measured source sprite RGB mean is `(93.0, 130.6, 54.2)`. The rejected model's software-rendered mean was `(82.8, 121.8, 49.8)`; the revised mean is `(88.2, 127.6, 52.6)`. Palette entries were correct in both cases; the error was disproportionately dark sampled texture regions. This color comparison is flat albedo, not a guarantee of in-game lighting appearance.

Local candidate outputs in `tools/sprite-to-3d/out/squirrel-revision2/`:

- `rct2.scenery_small.tsq.glb` and `.meta.json`: revision-2 source outputs, also installed in `data/object-models/`.
- `rct2.scenery_small.tsq.blend`: editable connected model with packed texture.
- `rct2.scenery_small.tsq.review.png`: sprite comparison, local only.
- `previews/beauty.png`: neutral studio-lit perspective.
- `previews/albedo.png`: flat-texture perspective for color comparison.
- `previews/view0.png` through `view3.png`: true-scale model-only views.

Rebuild from the repo root with a Python environment containing NumPy and Pillow:

```powershell
python tools/sprite-to-3d/examples/rct2.scenery_small.tsq/revision2/author.py --blender "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" --sprites tools/sprite-to-3d/cache/rct2.scenery_small.tsq
& "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b -P tools/sprite-to-3d/examples/rct2.scenery_small.tsq/preview.py -- --glb tools/sprite-to-3d/out/squirrel-revision2/rct2.scenery_small.tsq.glb --out tools/sprite-to-3d/out/squirrel-revision2/previews
```

Omit `--sprites` to build without source-game data (no IoU measurement). The script writes only to the ignored candidate folder. Blender version is recorded accurately in metadata. Geometry decimation can vary with Blender version; repeat with 5.2.1 for this candidate. The optional export tests skip when no local candidate exists; texture/anatomy input tests run without Blender or commercial data.

Revision-2 validation: **580 triangles, 579 exported vertices, 128×128 atlas**; one connected closed surface (each mesh edge has two incident faces). Closed IoU is `0.809, 0.835, 0.770, 0.777`, mean `0.798`. All hard validator gates pass. Remaining warnings are the 400-triangle soft cap and view 0's +3 sim projected tip offset (hard fail is beyond 4). No thresholds were changed. **126 tests and 12 subtests passed; full workspace build passed.** Six Blender preview renders succeeded. The game was not launched. Human recognition and color approval are still outstanding; metrics must not be described as visual approval.

## Historical revision-1 record (rejected)

Archive only: the revision-1 paths and rebuild instructions below are no longer available and must not be used. Use the revision-2 commands above.

## Deliverables

- `data/object-models/rct2.scenery_small.tsq.glb`: flat runtime asset, matching the existing model loader naming contract.
- `data/object-models/rct2.scenery_small.tsq.meta.json`: measured metadata; review status is **pending**, not approved.
- `tools/sprite-to-3d/examples/rct2.scenery_small.tsq/object.json`: placement, alignment, review state and triangle-budget justification.
- `tools/sprite-to-3d/examples/rct2.scenery_small.tsq/params.json`: complete reproducible geometry and procedural atlas inputs.
- `tools/sprite-to-3d/examples/rct2.scenery_small.tsq/preview.py`: optional Blender scene/preview generation from the exported GLB.
- `tools/sprite-to-3d/test_squirrel.py`: deterministic geometry/atlas, footprint, palette and shipped-asset regression checks. No commercial data required.

The object is the RCT1-list entry `rct2.scenery_small.tsq`, display name **Squirrel Shaped Tree**. All four PNGs under the user's `RCT-Scenery/rct1/small_scenery/rct2.scenery_small.tsq/` were verified pixel-identical to the corresponding TSQ.DAT images. Authoritative offsets and placement came from the user's installed TSQ.DAT via the existing extractor, into the ignored cache. The supplied `RCT-Scenery` directory was left untouched and untracked.

## Construction and conventions

Uses the existing conifer archetype's **closed foliage lumps**, not its conifer cards: seated body, chest, head, two pointed ears, paired raised forepaws, two feet and an upright broad tail. A short green topiary stem meets Z=0. The model faces Blender -X and includes small asymmetric proportion adjustments fitted jointly to the four sprite silhouettes. No renderer, shared archetype or validator changes were needed.

- Quarter-tile bush/tree class, origin at quadrant centre; 1 Blender unit = 32 sim units.
- Logical DAT height 36 sim; mesh tip 36.126 sim. True-scale Y-up positions, identity node transform; no baked comparison squash.
- Sprite shift `[0, 0]`, anchor Y nudge `-1`; no per-view alignment overrides.
- Procedural 128×128 atlas, nine greens (palette indices 72–80), approximately 32 texels/unit. No original sprite pixels copied or embedded.
- MASK 0.5, nearest/clamp sampling, no mipmaps, double-sided, roughness 1, metallic/specular 0.
- TSQ has no colour-remap flags. Its few incidental remap-range source pixels do not make it recolourable; the authored atlas uses fixed greens.
- Deterministic local silhouette refinement used seed 736; fitted values are stored directly in params, so no optimizer is required to rebuild.

## Rebuild

Follow `tools/sprite-to-3d/README.md` to prepare Python with NumPy and Pillow. From the repository root:

```powershell
python tools/sprite-to-3d/rebuild.py --object rct2.scenery_small.tsq --rct2-data-path "C:\Source\RCT Source Files\RollerCoaster Tycoon 2 Triple Thrill Pack" --no-blender
```

That writes to ignored `tools/sprite-to-3d/out/` and regenerates authoritative local sprite offsets. Substitute the local game installation path as needed. To rebuild without game data, omit `--rct2-data-path` and supply `--palette-header`; this reproduces geometry/atlas but cannot remeasure silhouette IoU.

An already prepared cache can be used with `--sprites tools/sprite-to-3d/cache/rct2.scenery_small.tsq --palette-header`. The original RCT-Scenery PNG-only folder lacks `offsets.json`, so it cannot be passed directly to the validator. Installing over the delivered GLB requires explicit `--out data/object-models --force`, as required by the existing overwrite protection.

The runtime GLB uses the guide-supported Python writer. The locally available Blender is **5.2.1 LTS**, not the guide's reference 4.2. Blender was used only to import the finished asset, save an editable source scene and render previews; no claim is made that the asset was exported by Blender 4.2.

```powershell
& "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b -P tools/sprite-to-3d/examples/rct2.scenery_small.tsq/preview.py
```

Local-only generated files:

- `tools/sprite-to-3d/out/rct2.scenery_small.tsq/rct2.scenery_small.tsq.blend`: editable mesh at true scale with packed texture, without preview cameras.
- `tools/sprite-to-3d/out/rct2.scenery_small.tsq/beauty.png`: perspective preview.
- `tools/sprite-to-3d/out/rct2.scenery_small.tsq/view0.png` through `view3.png`: true-height, model-only orthographic previews, not IoU measurement images.
- `data/object-models/rct2.scenery_small.tsq.review.png`: final sprite/render comparison sheet, generated by the validator with render-only Z squash. Contains source sprites; **do not commit**.

The ignored virtual environment at `tools/sprite-to-3d/out/venv` was created from Blender's bundled Python 3.13 and used its NumPy plus locally installed Pillow/pytest. Blender's sitecustomize emits a nonfatal missing `blender.shared` warning in that relocated venv; all commands and tests completed successfully. A normal standalone Python environment avoids that Blender-specific startup warning.

## Validation results

| Check | Result |
| --- | --- |
| Triangles / vertices | 472 / 1100 |
| GLB bytes | 47,536 |
| Closed-mask IoU, views 0–3 | 0.897, 0.883, 0.884, 0.897 |
| Mean closed-mask IoU | 0.890 (required 0.750) |
| Raw-mask IoU, views 0–3 | 0.877, 0.868, 0.875, 0.878 |
| Mean raw-mask IoU | 0.875 |
| Render/source area ratios | 1.01, 1.00, 0.99, 1.02 |
| Per-view projected tip deltas | +1, -1, 0, +1 sim |
| Maximum canopy extent | 0.398 units; class limit 0.400 |
| Trunk-base extent | 0.067 units; class limit 0.250 |
| Final asset validator | Pass, one documented soft-cap warning |
| Pipeline tests | 120 passed, 12 subtests passed |
| Validator self-test | Pass, including approved pine |
| Full Visual Studio workspace build | Pass |
| Blender import / scene save / five renders | Pass |

The one warning is 472 triangles above the tree_quarter soft cap of 400; the 600-triangle hard limit is respected. Separate anatomical volumes justify the extra triangles. No thresholds were relaxed and no approval figures were fabricated.

**Owner review remains required** for appearance, orientation, placement and lighting in the Filament view. Numerical silhouette fitting and successful Blender renders are not artistic or in-game approval. The game was not launched. No commits or pushes were made for this task.
