# Blender source models

Store the versioned, editable Blender source for each scenery model here:

`<object id>.blend`

For example, `rct2.scenery_small.tsq.blend` is the current squirrel-shaped topiary. Keep one current source file per object; Git preserves previous revisions.

- Pack authored textures into the file and save at true model scale (1 Blender unit = 32 simulation units), without comparison-only height squash.
- Do not pack original game sprites, extracted game data, or sprite comparison sheets into these files.
- Export runtime GLBs to `data/object-models/` using the relevant authoring procedure.
- Generated builds, preview scenes, and renders remain in the ignored `tools/sprite-to-3d/out/` directory. After reviewing a rebuilt model, explicitly copy its canonical `.blend` here; rebuilding must not silently overwrite an edited source file.
- Blender backup files (`*.blend1`, `*.blend2`, etc.) are ignored in this directory.

Squirrel authoring and validation instructions: [agent notes](../../../docs/agents/squirrel-topiary-model.md).
