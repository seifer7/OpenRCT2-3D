# GLB models in the bgfx 3D view: recommendation

Source: Expert Coder (one of SeiferX7's assistants), 2026-11-01, asked by King Grok on SeiferX7's behalf. Question: is using `.glb` files (packed with model, textures and animations) a good way to bring 3D models into the bgfx 3D view, given ~10,000+ one-tile models per map, plus view distance and culling?

## 1. Verdict

Yes, GLB is the right *source and modding* format, but not the *only* runtime format. Use GLB everywhere for authoring, interchange and mods. At runtime, keep a cgltf loader (single header, MIT licence, fast, no allocation you don't control) behind a converted-asset cache.

- **GLB loaded directly (cgltf):** zero tooling for modders; drop-in custom objects just work. Cons: parse and reshape at load time (interleave vertices, generate tangents, flatten the node tree); embedded PNG/JPEG textures need CPU decoding via bimg/stb with no mips or GPU compression, so memory is 4–8× higher than BC/ETC.
- **Offline to geometryc `.bin` + texturec `.ktx`/`.dds`:** fastest load, mips and GPU compression, smallest VRAM. Cons: geometryc only reads OBJ/glTF meshes, not skins or animations; ties modders to your tools; bakes in a bgfx version.
- **Recommended hybrid:** ship and accept `.glb`. On first load convert each GLB into a small cache blob (`.o3d` in the user cache dir: interleaved verts and indices per material, bounds, remap mask, bones and clips, plus KTX2/BC textures via bimg's encoder), keyed by a hash of the GLB contents. Pre-bake built-in objects at build time with the same code. Modders get GLB, players get fast loads, one converter to maintain. Avoid tinygltf (JSON/STL dependencies, slower).

## 2. Rendering 10k+ instances in bgfx

10k is comfortably achievable if you draw per *object type*, never per placed object.

- **Instancing** per (mesh, material) via `bgfx::allocInstanceDataBuffer` (transient) or a dynamic vertex buffer for static props. Per-instance data: a 4×3 transform, or position + yaw + scale (RCT props rotate only in 90° steps, so pack position plus a rotation byte), three remap colours, and a flags/animation-phase value: about 32–64 bytes per instance.
- **Draw-call budget:** aim for under ~1,000 submits per frame on the OpenGL backend (the only one the VS project enables now). With ~300–600 distinct visible object types, do one instanced draw per type overall, rebuilding buffers per frame from the visible chunks (preferred over one per type per chunk).
- **Textures:** a texture array per size class (e.g. 256² or 512² layers) with a layer index per material or instance. Atlases bleed at mip levels and complicate UV wrap. Arrays need `BGFX_CAPS_TEXTURE_2D_ARRAY` (GL 3.3 has it). Lets object types sharing a mesh format share a draw; pairs with later indirect draws (`BGFX_CAPS_DRAW_INDIRECT`, GL 4.3+ only, optional).
- **Spatial chunks** of 16×16 tiles (a 256-tile map gives 256 chunks). Each chunk holds an AABB and, per object type, a list of instances. Rebuild a chunk only when its tile elements change (same dirty signal as terrain, or a per-chunk hash of tile elements each tick). Frustum-cull chunks, not individual objects. View distance is a chunk-distance cutoff with a fade.
- **LOD, three tiers:** full mesh close; a decimated mesh mid-range (optional per object, `_LOD1` in the GLB or meshopt-simplified at conversion); far: impostors. Don't reuse the original g1 sprites as impostors (paletted fixed 4-direction isometric art looks wrong from a free camera). Instead bake 8-direction impostors from the 3D meshes at conversion, or simply cull small props beyond N chunks. Keep g1 sprites as the *missing mesh* fallback only (as `docs/3d-viewer.md` already says).
- Depth pre-pass, or at least front-to-back sort of opaque draws. bgfx view order: terrain, opaque props, alpha-tested foliage, transparent.

## 3. Animations

Most RCT scenery and ride parts don't need skeletons.

- **Rigid node/transform animation** (spinning wheels, swinging arms, flags, doors) for most things: bake clips into per-node TRS tracks, evaluate on the CPU per *type* per frame, pass a per-instance phase so instances desync for free. Cheap at 10k.
- **Vertex/texture animation** for repetitive effects (fountains, water, foliage sway): in the shader via a time uniform plus per-instance phase, or vertex animation textures.
- **Skeletal** only for guests, staff and animated ride figures. GPU skinning with a bone-palette uniform limits instancing; for hundreds/thousands of guests use a **baked animation texture** (bone matrices per frame in a texture, per-instance clip and frame offset) so each guest model is one instanced draw. Don't CPU-skin per guest.
- Ride vehicles are rigid bodies with transforms from `EntityBase`: plain instances with per-frame transforms, no skinning.

## 4. Palette remap colours

Mask textures plus per-instance colour.

- In Blender, author a remap **mask**: an extra texture where R, G, B mark primary, secondary, tertiary weight, or a vertex-colour attribute (`COLOR_1`) modders can paint. Flag it with a glTF material naming convention or an `extras` field (e.g. `"rct_remap": true`).
- Shader: `albedo = mix(base, base_grey * remapColour[i], mask[i])` across the three channels; per-instance data carries the three colours. Convert RCT palette indices to linear RGB on the CPU from the game palette (remap ramps) so 3D matches 2D. Better: upload a small 1D ramp texture per remap colour and sample by base luminance to keep RCT-like shading.
- Every colour variant stays in the same instanced draw.

## 5. Risks and gotchas

- **Scale and axes:** glTF is Y-up, right-handed; the viewer is `x/32, y/32, z/32` with Z up. Pick one conversion matrix in the loader and document it for Blender exports (+Y up). 1 Blender unit = 1 tile = 32 sim units on every axis. 1 Blender unit vertically = 4 RCT height units = 32 sim Z (two land steps). `ParkView.cpp` converts heights in `worldZ` as world Z times `kLandStepHeight / 16`, with `kLandStepHeight = 0.5f`, so one land step is 0.5 viewer units and one height unit (8) is 0.25. Authors use that scale.
- **Pivot and footprint:** define the origin (tile min-corner or centre), document it, keep rotation to 90° steps about the tile centre. Large scenery spans several tiles: either one mesh for the whole footprint placed at the primary tile (simpler) or one mesh per sub-tile.
- **Object identity:** key GLBs to the object identifier (`rct2.scenery_small.xyz` or the JSON object id), not file name, so custom objects and `.parkobj` packaging can carry a GLB alongside existing images.
- **Texture memory:** 1,000 types × 512² RGBA with mips ≈ 1.3 GB uncompressed. Compress at conversion (BC7/BC1 desktop, KTX2/ETC mobile).
- **Transparency:** foliage alpha-tested, not blended, or sorting 10k instances will hurt.
- **Shader platforms:** shaderc must build per backend. Only GL is enabled now; add DX11/Vulkan/Metal binaries before relying on those backends.
- **Threading:** never read `GameState` from a render thread mid-tick. Run the extractor on the main thread after `gameStateTick()`, then hand bgfx flat instance arrays.
- **Mod safety:** cap vertex, texture and bone counts on load; validate GLBs (`cgltf_validate`). User-supplied files are untrusted input.

## Recommended pipeline

1. **Author:** Blender exports one `.glb` per object: 1 unit = 1 tile = 32 sim units on every axis, +Y up on export, origin at tile centre, remap mask (extra mask texture or `COLOR_1` vertex attribute, flagged with `extras.rct_remap`), optional `_LOD1` mesh, named animation clips. Name each file after the object identifier.
2. **Convert (runtime, cached):** load with cgltf, run `cgltf_validate` with size, vertex, texture and bone caps. meshoptimizer for vertex cache order and an auto-simplified LOD when `_LOD1` is missing. bimg encodes BC7/BC1 (desktop) or ETC/ASTC (mobile) textures with mips. Write a cached `.o3d` blob: interleaved vertices and indices per material, bounds, remap mask, node hierarchy, animation clips and bones. Key by hash of GLB contents plus a converter version. Built-in objects pre-baked at build time with the same converter.
3. **Extract:** after `gameStateTick()`, on the main thread, the SceneExtractor fills per-chunk (16×16 tiles) instance lists grouped by object type: transform or tile, rotation and height, three remap colours, animation phase. Rebuild a chunk only when its tile elements change; entities (guests, vehicles) in a separate per-frame list.
4. **Cull and LOD:** each frame, test chunk AABBs against the frustum and view-distance cutoff (with fade); pick LOD per chunk by distance: full mesh, LOD1, then baked impostor or culled.
5. **Submit:** one transient instance buffer per (object type, LOD) from visible chunks, submitted once per type. Textures from texture arrays; per-instance remap colours in the shader. View order: terrain, opaque, alpha-tested, transparent.
6. **Animate:** rigid scenery and ride parts use CPU-evaluated node transform tracks per type with per-instance phase. Water, fountains, foliage use shader animation. Guests and staff use baked bone-animation textures with per-instance clip and frame. Vehicles use rigid transforms from `EntityBase`.
7. **Fallback:** a missing mesh draws a coloured box or a camera-facing g1 sprite billboard. g1 sprites are a missing-art fallback only, not a distance LOD.

## Closing notes

- **First milestone:** instanced boxes per object type with chunk culling, before any real art, to prove the draw and culling path at 10k+ objects. Then the GLB loader and converter, then remap colours, then animation.
- Add shaderc builds for DX11, Vulkan, Metal before relying on those backends (only OpenGL is enabled now).
- Document the axis and scale conversion and pivot convention in `docs/3d-viewer.md`; that's where Blender authors will get it wrong. The authoring rules that close this (tile-centre origin, Z = 0 at the element base, the render-only sqrt(2/3) squash, MASK materials, budgets, and the Corsican Pine reference) are in `docs/agents/sprite-to-3d-style-guide.md`.

Status: the runtime plan above is still a recommendation, not an implementation. The standing scale decision is 1 Blender unit = 1 tile = 32 sim units on every axis (Z = world Z / 32, matching ParkView `worldZ`). Authoring conventions that go with it are in `docs/agents/sprite-to-3d-style-guide.md`.
