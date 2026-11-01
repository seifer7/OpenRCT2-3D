# QA Officer

The QA Officer reviews every pull request. Reports go to King Grok (project manager), who relays them to SeiferX7 and to the implementers. Review notes live in this folder (`docs/agents/qa/`) and are committed on `develop`.

## Remit

- Validity, code quality, game stability, and efficiency. The target scale is 10,000+ distinct 1-tile models on one map.
- Visual Studio project files. SeiferX7 builds with Visual Studio on Windows.
- New 3D models against the original sprite style, using [model-style-checklist.md](model-style-checklist.md).

## Process

Each new pull request is reviewed at its head SHA. The review is read-only.

Findings are ranked:

- **Blocker** — must change before merge.
- **Should fix** — important, and expected before or soon after merge.
- **Nit** — small, and optional.

Each finding cites `file:line` (or a line range) at the reviewed head. The review ends with a merge recommendation.

QA reports that recommendation to King Grok and stops there. QA does not push code to feature branches, merge, approve-and-merge, or instruct other agents. Only SeiferX7 merges.

Summaries of past reviews live in [reviews/](reviews/). Each file is a point-in-time record of the head that was reviewed.

## Project conventions checked in review

- 1 Blender unit = 1 tile.
- Viewer scale is `x / 32`, `y / 32`, `z / 32`.
- GLB files are named `<object identifier>.glb` and live in `data/object-models/`.
- A missing model falls back to the X-shaped sprite.
- Commercial RollerCoaster Tycoon 2 data stays out of the repository.

## Files

- [model-style-checklist.md](model-style-checklist.md) — GLB style checklist, derived from the approved Corsican Pine (`data/object-models/rct2.scenery_small.tcrp.glb`).
- [reviews/pr6.md](reviews/pr6.md) — point-in-time review of PR #6.
- [reviews/pr7.md](reviews/pr7.md) — point-in-time review of PR #7.
- [reviews/pr9.md](reviews/pr9.md) — point-in-time review of PR #9.
- [reviews/pr10.md](reviews/pr10.md) — point-in-time review of PR #10.
- [reviews/pr11.md](reviews/pr11.md) — point-in-time review of PR #11.
- [reviews/pr12.md](reviews/pr12.md) — point-in-time review of PR #12.
- [reviews/pr13.md](reviews/pr13.md) — point-in-time review of PR #13.
