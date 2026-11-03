# Sprite-to-3D batch process

How a batch of sprite-to-3D models is built and reviewed. The style rules themselves are in [sprite-to-3d-style-guide.md](sprite-to-3d-style-guide.md) and the QA gate is [qa/model-style-checklist.md](qa/model-style-checklist.md).

## Samples first (repo owner's rule, effective from batch 2)

1. **Build 2–3 representative samples first.** Pick them to cover the batch: different archetypes or painters, size classes (full tile and quarter tile), and the hardest silhouette. Do not build the rest of the batch yet.
2. **Send them for review.** Send each sample's review sheet (`<id>.review.png` from the validator) and one contact sheet showing all samples side by side, together with their validator reports. Review sheets paste the original sprites: share them for review only and never commit them.
3. **Wait for the repo owner's approval.** Do not build the rest of the batch until the owner approves the samples. Fold any requested changes into the samples and the shared archetype/painter code first, then resend.
4. **Then build the rest** of the batch with the approved settings, and send it through the normal QA checklist.

## One pass, concise reports (repo owner's rule)

Keep work and reports concise. Get each model right in one pass: read the sprites, offsets and DAT flags first, pick the archetype and parameters deliberately, then build and validate. Aim for one build/validate pass per object and at most two. A report states the result (tris, closed IoU mean/min, area ratios, warnings) and what changed, nothing more.

## Keep iterations minimal

Keep re-render and rebuild iterations to the minimum needed. Change parameters deliberately, rebuild only the objects affected, and re-render only to check a change, not to explore. Use `fit.py` for silhouette searches instead of rebuild loops by hand. Never run `validate.py --self-test` in a shared checkout during a batch: it writes to `tools/sprite-to-3d/out/`.

## No backwards compatibility (repo owner's rule)

Backwards compatibility is not required when the 3D view, the tools or the meta formats change. When a format changes, migrate every committed meta and example to it and drop the legacy fields, old thresholds and compat paths in the same change. Do not keep fallbacks that read an old field name or an old nested meta layout.

## QA gate: sparse crowns

A sparse crown (open foliage on visible branches, where the sprite shows sky between clumps) must not be filled in to buy IoU. Mark it with `"crown": "sparse"` in `object.json`; the validator then also requires an **area ratio ≤ 1.10 on every view** and **raw IoU ≥ 0.50 on every view** as a sanity floor (`SPARSE_CROWN_MAX_AREA_RATIO`, `SPARSE_CROWN_MIN_RAW_IOU`). The flag is opt-in because dense crowns such as the Corsican Pine (area up to 1.26) and the yew (1.12) legitimately run over 1.10. In batch 1 the flag is set on `rct2.scenery_small.tcc`.
