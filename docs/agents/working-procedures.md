# Agent working procedures and handoff

## Repository is the source of truth

All project-specific agent notes, procedures, plans, decisions, investigation findings and handoffs belong in `docs/agents/`. Do not leave their only copy in chat, an Agent Store, a temporary directory, or a VM. Update the [index](README.md) when adding a document.

Tool-managed plans may also be written to external temporary storage automatically. Record their actionable steps, decisions, completed validation and remaining work here or in the relevant topic document before ending a task. A temporary tool snapshot is not the durable project record. Keep reusable scripts with their tools and link to them from the notes; do not duplicate executable code in an agent store.

`AGENTS.md` and `.github/copilot-instructions.md` remain at their conventional repository locations so assistants can discover the rules. Public tool READMEs remain beside their tools. All of these are already inside the repository.

Repository-owned does not mean automatically committed: include relevant notes when the user requests a commit, and report uncommitted documentation honestly. Never stage unrelated user changes or commercial game data as part of a notes cleanup.

## Start and finish a task

1. Read [AGENTS.md](../../AGENTS.md), the [agent index](README.md), and the applicable topic document.
2. Inspect workspace status; preserve existing user edits. Check actual files rather than assuming old handoffs still describe the current checkout.
3. For multi-phase work, keep a durable plan and progress in the topic document, even when a tool also maintains a temporary plan.
4. Make scoped changes and run appropriate validation. Distinguish source checks, unit tests, build success and human visual approval.
5. Update the topic document with paths, reproducible commands, measured results, warnings and pending work. Report what was not tested.
6. Provide a short completion summary and signal task completion. Do not commit or push without authorization.

## Sprite-to-3D procedure

Use the [style guide](sprite-to-3d-style-guide.md), [batch procedure](sprite-to-3d-batch-process.md), [QA procedure](qa/README.md), and [tool README](../../tools/sprite-to-3d/README.md).

- Prioritize recognizable anatomy, faithful source colors and the intended material. Small details absent from the low-resolution sprites may be added to express the original intent. The squirrel must read as a squirrel-shaped hedge, not a realistic animal or a collection of generic foliage lumps.
- Silhouette overlap is a validation measurement, not visual approval. Do not distort anatomy just to increase overlap or lower thresholds to pass an unsuitable model.
- Check sampled/rendered colors as well as atlas palette membership. Correct palette entries can still produce wrong-looking colors if UVs sample overly dark regions.
- Keep original game sprites and sprite-containing review sheets local and excluded from commits. Do not embed copied sprite pixels in procedural textures.
- Export at 1 Blender unit = 32 simulation units, Y-up, identity transforms, with comparison-only height squash kept out of the model.
- Put the installed model at `data/object-models/<object id>.glb`, with matching `.meta.json`. The Visual Studio `CopyObjectModels` target copies the GLB to `bin/data/object-models/`; no per-model project entry is needed.
- Keep review candidates separate until installation is requested. Preserve the editable authoring inputs and document the `.blend` location. Generated assets are not agent notes and are not moved merely to satisfy the documentation policy.
- Run pipeline tests and the asset validator, then build when installing. Verify the copied GLB matches the source. In-game visual checks remain the owner's task; do not launch the game for agent validation.

## Local modeling environment

At the last inspection, `python` resolved to Python 3.15 without NumPy/Pillow. Blender 5.2.1 LTS was installed at `C:\Program Files\Blender Foundation\Blender 5.2\blender.exe` but was not on PATH. Its bundled Python 3.13 supplied NumPy, but not Pillow.

A local ignored environment at `tools/sprite-to-3d/out/venv` was created from Blender's Python, with Pillow and pytest installed. It works but emits a nonfatal `sitecustomize` missing `blender.shared` warning because Blender's startup logic assumes its own directory layout. Prefer a standalone Python 3.13 virtual environment with dependencies from `tools/sprite-to-3d/requirements.txt`; do not modify Blender's installation to silence that warning.

The general guide references Blender 4.2 LTS. The squirrel's revision-2 scripts explicitly use Blender 5.2.1 and record the actual version. Do not label newer Blender exports as 4.2. Adding the Blender installation directory to user PATH is optional; full executable paths work. Restart Visual Studio after changing PATH.

## Current squirrel handoff

See [Squirrel Shaped Tree](squirrel-topiary-model.md) for authoring commands and measurements.

- Active source: `tools/sprite-to-3d/examples/rct2.scenery_small.tsq/revision2/`.
- Shared definition and preview script: its parent `rct2.scenery_small.tsq/` folder.
- Installed GLB/meta: `data/object-models/rct2.scenery_small.tsq.*`.
- Editable Blender source: `tools/sprite-to-3d/out/squirrel-revision2/rct2.scenery_small.tsq.blend` (ignored local output).
- Comparison sheet: `tools/sprite-to-3d/out/squirrel-revision2/rct2.scenery_small.tsq.review.png` (contains sprites; do not commit).
- Model-only previews: `tools/sprite-to-3d/out/squirrel-revision2/previews/`.
- The owner reported revision 2 looks much better and requested removal of revision 1. Old geometry parameters, fitted outputs, atlas, GLB, Blender file, previews and revision-1-only tests were removed. Original sprites were preserved.
- Latest post-cleanup validation: 124 tests and 12 subtests passed; workspace build passed; installed GLB hash matched revision 2. Two existing validator warnings remain: 580 triangles above the 400 soft cap (below 600 hard cap), and one view's +3 sim tip offset (hard failure is beyond 4).
- No modeling changes were committed or pushed by the assistant. Check Git status for subsequent user changes.

Example validation with the existing local environment, from the repository root:

```powershell
& tools/sprite-to-3d/out/venv/Scripts/python.exe -m pytest tools/sprite-to-3d -q
& tools/sprite-to-3d/out/venv/Scripts/python.exe tools/sprite-to-3d/validate.py --glb data/object-models/rct2.scenery_small.tsq.glb --meta data/object-models/rct2.scenery_small.tsq.meta.json --sprites tools/sprite-to-3d/cache/rct2.scenery_small.tsq --require-iou --out data/object-models/rct2.scenery_small.tsq.review.png
```

## Documentation consolidation record

- Existing architecture, modeling, QA and setup documents were already inside `docs/agents/`; retain them rather than create competing copies.
- Replaced external-only navigation in [notes.md](notes.md) with local links. The former `/cursor/stores/self/docs/project-context.md` reference has a repository equivalent at [project-context.md](project-context.md).
- The historical Agent Store identifier `bc-a3eabb6a-577f-567f-87c5-c3d28db75196` has no accessible export in this environment. Its unknown contents have **not** been recovered. Import an export into this folder if one becomes available; do not invent its contents or claim a complete external-store migration.
- Recent plan outcomes and setup details available in this conversation are preserved above and in the squirrel topic document. No project notes were found in the inspected ignored modeling-output directory.
- Existing staged `.gitignore` changes and unrelated assets were left untouched. Generated models, game data, Copilot application logs, and installer caches are outside this documentation-only consolidation.
