# QA review summary — PR #7

Point-in-time review. This note records the review as it stood when it was written. It is not a live status of the pull request.

- Pull request: [#7](https://github.com/seifer7/OpenRCT2-3D/pull/7) "Add a reproducible sprite-to-3D pipeline, with the Corsican Pine as the reference".
- **Head reviewed:** `277b0b70b2b5905f7e19b29ec88deb00f507dce8` (recorded in the review as `277b0b70b2`). Base `develop` at `ba627e2808`. 26 files, +4176/−2.
- Scope: Python tooling (`tools/sprite-to-3d`), docs, `.gitignore`, and a regenerated pine GLB, atlas, and meta under `data/object-models/rct2.scenery_small.tcrp/`. No C++ or Visual Studio project changes.
- Checked at review time: `validate.py --self-test` passed. `rebuild.py` from a local RCT2 install (`--no-blender`) reproduced the committed GLB and atlas byte for byte (sha `fa12b10a` / `5495c60f`). Silhouette IoU 0.733 / 0.712 / 0.766 / 0.687, mean 0.725, matches the approved figures. Sprite cache and output stayed gitignored. Format, clang-tidy, changelog, and Docker had passed; platform builds were still running.

## Recommendation

**Request changes.** Merge after blockers 1 and 2, and ideally should-fix 3. The tooling is reproducible. No runtime code was touched, so the 2D renderer, saves, and the Visual Studio build are unchanged.

## Blockers

1. **Layout conflicts with the loader in PR #6.** The GLB is at `data/object-models/<id>/<id>.glb` (`style-guide.md:254-257`, tools README line 5). PR #6 looks up `<dir>/<id>.glb` with a non-recursive `*.glb` scan (`ModelCache.cpp:558` and `:629`), so this model would never load and the view would show the X fallback. The repo convention and checklist item 1 say `data/object-models/<id>.glb`. Keep the GLB flat (sidecars may sit beside it), or change the loader in one place on purpose.
2. **The reference GLB is no longer the approved TCRP v2 file.** The committed GLB is 112,696 B (sha `fa12b10a`). The approved Blender export is 85,408 B (sha `e2e25e2e`, PR #6). Triangle count, bounds, and atlas match (864 triangles), but the mesh is unwelded: 2592 vertices (checklist must ≤ 2500), UINT32 indices (must be u16), and the file is over the 100 KB target. PR #6 also adds `data/object-models/rct2.scenery_small.tcrp.glb`, so merging both leaves two copies. Weld vertices and write u16 indices in `glb_io.py` (`glb_io.py:26` and `:82`), or keep the approved file as the reference.

## Should fix

3. **`validate.py` skips several checklist musts**, which is why blocker 2 still passes: vertex count, u16 indices, exactly 1 mesh / 1 primitive / 1 material / 1 node, exact RCT2 palette colours, an embedded image only (no external URI), and GLB size ≤ 128 KB.
4. **Size-class budgets sit above the checklist.** `conventions.py:43-44` allow `tree_large` 4000 triangles and a 512² atlas, and `large_scenery` 8000 triangles and 512². The checklist musts are ≤ 1500 triangles and ≤ 256². A 512² RGBA texture is 1 MB of VRAM, which is costly at 10,000+ distinct models. Align the budgets with the checklist, or justify them per tile.
5. **`py7zr` is unused.** `requirements.txt:3` lists it. No script imports it (mentioned only at README line 19). Drop it, or use it.
6. **DAT parsing has no length guards.** The RLE and chunk loops in `rct2dat.py` (about lines 50-67 and 154-185) can raise `IndexError` or spin on a truncated DAT. The tool should stop with a clear error.

## Nits

7. Meta `size_class` `tree_1tile` uses different wording from the checklist. Document the class list in the style-guide table.
8. The node name is the object id. README line 43 says the reference was exported with `--no-blender`; the meta should say it is not the Blender export.
