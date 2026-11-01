# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Force the authoring material conventions onto a GLB after export.

Blender 4.2's glTF exporter writes minFilter NEAREST_MIPMAP_NEAREST and
alphaMode BLEND for this kind of cut-out material. The viewer wants the
pixel art to stay crisp: nearest filtering, no mipmaps, alpha MASK at 0.5,
double-sided, roughness 1, no specular. Run this on every export, including
files that a Python writer already filled in (the patch is idempotent).
"""

from __future__ import annotations

import json
import struct
import sys

from conventions import CLAMP_TO_EDGE, NEAREST

GLB_MAGIC = 0x46546C67
GLB_JSON = 0x4E4F534A


def patch(path):
    data = open(path, "rb").read()
    magic, version, _length = struct.unpack_from("<III", data, 0)
    if magic != GLB_MAGIC or version != 2:
        raise ValueError(f"{path} is not a glTF 2.0 GLB")
    json_len = struct.unpack_from("<I", data, 12)[0]
    gltf = json.loads(data[20 : 20 + json_len])
    rest = data[20 + json_len :]

    gltf["samplers"] = [
        {
            "magFilter": NEAREST,
            "minFilter": NEAREST,
            "wrapS": CLAMP_TO_EDGE,
            "wrapT": CLAMP_TO_EDGE,
        }
    ]
    for texture in gltf.get("textures", []):
        texture["sampler"] = 0
    used = list(gltf.get("extensionsUsed") or [])
    if "KHR_materials_specular" not in used:
        used.append("KHR_materials_specular")
    gltf["extensionsUsed"] = used
    for material in gltf.get("materials", []):
        # Cut-out foliage and cards. Opaque materials keep their alpha mode
        # but still lose mipmaps via the shared sampler above.
        if material.get("alphaMode", "OPAQUE") != "OPAQUE":
            material["alphaMode"] = "MASK"
            material["alphaCutoff"] = 0.5
            material["doubleSided"] = True
        pbr = material.setdefault("pbrMetallicRoughness", {})
        pbr["metallicFactor"] = 0.0
        pbr["roughnessFactor"] = 1.0
        ext = material.setdefault("extensions", {})
        ext["KHR_materials_specular"] = {"specularFactor": 0.0}
    extras = gltf.setdefault("extras", {})
    info = extras.setdefault("openrct2_3d", {})
    info["z_squash"] = False
    info["exported_up"] = "Y"

    js = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    js += b" " * ((4 - len(js) % 4) % 4)
    total = 20 + len(js) + len(rest)
    with open(path, "wb") as fh:
        fh.write(struct.pack("<III", GLB_MAGIC, 2, total))
        fh.write(struct.pack("<II", len(js), GLB_JSON))
        fh.write(js)
        fh.write(rest)
    return path


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: patch_glb.py model.glb")
    patch(sys.argv[1])
    print(sys.argv[1])
