# Agent notes

Project notes, memories, decisions, surveys, and other working documents live in this folder (`docs/agents/`) on `develop` and must be committed here. Write new notes in this folder. The Agent Store is not a substitute.

- `notes.md` — task list
- `project-context.md` — project context, repo facts, and decisions
- `repo-setup.md` — repository setup record
- `scenery-sprites-angles-and-3d-view-survey.md` — scenery sprites, angles, and 3D view survey
- `scenery-3d-milestone.md` — first scenery milestone: GLB loader, Corsican Pine, sprite-cross fallback
- `scenery-3d-scale.md` — chunked scenery, async cache loads, shared GPU buffers, startup-only validation
- `glb-model-pipeline-recommendation.md` — GLB model pipeline recommendation
- `sprite-to-3d-style-guide.md` — how to build scenery GLBs in the Corsican Pine style
- [`qa/`](qa/README.md) — QA Officer: review process, model style checklist, and PR reviews

## CI

`ci.yml` and `clang-tidy.yml` run on a pull request only when it carries the `CI Request` label. King Grok applies that label to pull requests that change game code. Docs, tools, and model-only pull requests skip CI.

Pushes to `develop` always run CI. `workflow_dispatch` runs the CI workflow by hand.

To trigger or retrigger a run, add the `CI Request` label, or remove it and add it again, or push a commit while the label is on, or use `workflow_dispatch`. Adding any other label still starts a workflow, and every job in that run is skipped. Removing `CI Request` does not cancel a run already in progress: `unlabeled` is not a trigger, and a different label is a separate concurrency group so it does not cancel the build.
