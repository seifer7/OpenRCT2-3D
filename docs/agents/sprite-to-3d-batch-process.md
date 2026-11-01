# Sprite-to-3D batch process

How a batch of sprite-to-3D models is built and reviewed. The style rules themselves are in [sprite-to-3d-style-guide.md](sprite-to-3d-style-guide.md) and the QA gate is [qa/model-style-checklist.md](qa/model-style-checklist.md).

## Samples first (repo owner's rule, effective from batch 2)

1. **Build 2–3 representative samples first.** Pick them to cover the batch: different archetypes or painters, size classes (full tile and quarter tile), and the hardest silhouette. Do not build the rest of the batch yet.
2. **Send them for review.** Send each sample's review sheet (`<id>.review.png` from the validator) and one contact sheet showing all samples side by side, together with their validator reports. Review sheets paste the original sprites: share them for review only and never commit them.
3. **Wait for the repo owner's approval.** Do not build the rest of the batch until the owner approves the samples. Fold any requested changes into the samples and the shared archetype/painter code first, then resend.
4. **Then build the rest** of the batch with the approved settings, and send it through the normal QA checklist.

## Keep iterations minimal

Keep re-render and rebuild iterations to the minimum needed. Change parameters deliberately, rebuild only the objects affected, and re-render only to check a change, not to explore. Use `fit.py` for silhouette searches instead of rebuild loops by hand. Never run `validate.py --self-test` in a shared checkout during a batch: it writes to `tools/sprite-to-3d/out/`.
