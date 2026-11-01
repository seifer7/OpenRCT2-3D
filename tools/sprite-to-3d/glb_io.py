# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Minimal glTF 2.0 binary reader and writer for one mesh and one texture.

The writer emits Y-up positions from a Z-up mesh in sim units:
gltf (x, y, z) = (sim_x / 32, sim_z / 32, -sim_y / 32).
UVs use the glTF top-left origin, so a texel (u, v) from the painter
(top-left of the atlas) is stored as (u / width, v / height).
"""

from __future__ import annotations

import json
import struct

import numpy as np
from PIL import Image

from conventions import CLAMP_TO_EDGE, NEAREST, SIM_PER_UNIT

GLB_MAGIC = 0x46546C67
GLB_JSON = 0x4E4F534A
GLB_BIN = 0x004E4942
FLOAT = 5126
UINT = 5125
ARRAY = 34962
ELEMENT = 34963


def _pad(blob, pad_byte):
    return blob + pad_byte * ((4 - len(blob) % 4) % 4)


def sim_to_gltf(point):
    x, y, z = point
    return (x / SIM_PER_UNIT, z / SIM_PER_UNIT, -y / SIM_PER_UNIT)


def gltf_to_blender(point):
    """Inverse of the export conversion. Blender is Z-up, one unit per tile."""
    x, y, z = point
    return (x, -z, y)


def triangle_corners(mesh):
    """Expand faces to triangles. Quads split as (0, 1, 2) + (0, 2, 3)."""
    for face in mesh.F:
        if len(face) == 3:
            yield face
        elif len(face) == 4:
            yield (face[0], face[1], face[2])
            yield (face[0], face[2], face[3])
        else:
            for i in range(1, len(face) - 1):
                yield (face[0], face[i], face[i + 1])


def write_glb(path, mesh, png_bytes, object_id, atlas_size):
    """Write one double-sided MASK material, nearest, no mipmaps, roughness 1."""
    width, height = atlas_size
    positions = []
    normals = []
    uvs = []
    indices = []
    for a, b, c in triangle_corners(mesh):
        pa, pb, pc = (sim_to_gltf(mesh.V[i]) for i in (a, b, c))
        base = len(positions)
        positions.extend((pa, pb, pc))
        n = np.cross(np.subtract(pb, pa), np.subtract(pc, pa))
        length = float(np.linalg.norm(n))
        n = (0.0, 1.0, 0.0) if length < 1e-12 else tuple(float(v) / length for v in n)
        normals.extend((n, n, n))
        for i in (a, b, c):
            u, v = mesh.UV[i]
            uvs.append((u / width, v / height))
        indices.extend((base, base + 1, base + 2))

    pos = np.array(positions, np.float32)
    nrm = np.array(normals, np.float32)
    uv = np.array(uvs, np.float32)
    idx = np.array(indices, np.uint32)

    parts = [pos.tobytes(), nrm.tobytes(), uv.tobytes(), idx.tobytes(), png_bytes]
    views = []
    blob = b""
    targets = (
        (ARRAY, 12),
        (ARRAY, 12),
        (ARRAY, 8),
        (ELEMENT, 4),
        (ARRAY, 1),
    )
    for data, (target, align) in zip(parts, targets):
        if len(blob) % align:
            blob += b"\x00" * (align - len(blob) % align)
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), "target": target})
        blob += data
    # The image bufferView does not need a target. Drop it.
    views[-1].pop("target")
    blob = _pad(blob, b"\x00")

    def accessor(view, count, type_, component, mins=None, maxs=None):
        acc = {"bufferView": view, "componentType": component, "count": count, "type": type_}
        if mins is not None:
            acc["min"] = mins
            acc["max"] = maxs
        return acc

    def vec_min(arr):
        return [float(v) for v in arr.min(axis=0)]

    def vec_max(arr):
        return [float(v) for v in arr.max(axis=0)]

    gltf = {
        "asset": {
            "version": "2.0",
            "generator": "openrct2-3d sprite-to-3d 1",
        },
        "extensionsUsed": ["KHR_materials_specular"],
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": object_id, "mesh": 0}],
        "meshes": [
            {
                "name": object_id,
                "primitives": [
                    {
                        "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                        "indices": 3,
                        "material": 0,
                    }
                ],
            }
        ],
        "accessors": [
            accessor(0, len(pos), "VEC3", FLOAT, vec_min(pos), vec_max(pos)),
            accessor(1, len(nrm), "VEC3", FLOAT),
            accessor(2, len(uv), "VEC2", FLOAT),
            accessor(3, len(idx), "SCALAR", UINT),
        ],
        "bufferViews": views,
        "buffers": [{"byteLength": len(blob)}],
        "images": [{"mimeType": "image/png", "bufferView": 4}],
        "samplers": [
            {
                "magFilter": NEAREST,
                "minFilter": NEAREST,
                "wrapS": CLAMP_TO_EDGE,
                "wrapT": CLAMP_TO_EDGE,
            }
        ],
        "textures": [{"sampler": 0, "source": 0}],
        "materials": [
            {
                "name": object_id + "_mat",
                "doubleSided": True,
                "alphaMode": "MASK",
                "alphaCutoff": 0.5,
                "pbrMetallicRoughness": {
                    "baseColorTexture": {"index": 0},
                    "metallicFactor": 0.0,
                    "roughnessFactor": 1.0,
                },
                "extensions": {"KHR_materials_specular": {"specularFactor": 0.0}},
            }
        ],
        "extras": {
            "openrct2_3d": {
                "object_id": object_id,
                "units": "1 unit = 1 tile = 32 sim units on every axis",
                "authored_up": "Z",
                "exported_up": "Y",
                "z_squash": False,
            }
        },
    }
    js = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    js = _pad(js, b" ")
    header = struct.pack("<III", GLB_MAGIC, 2, 12 + 8 + len(js) + 8 + len(blob))
    out = (
        header
        + struct.pack("<II", len(js), GLB_JSON)
        + js
        + struct.pack("<II", len(blob), GLB_BIN)
        + blob
    )
    with open(path, "wb") as fh:
        fh.write(out)
    return {"triangles": len(idx) // 3, "vertices": len(pos)}


def read_glb(path):
    """Return the JSON dict, the binary chunk, and the raw file bytes."""
    data = open(path, "rb").read()
    magic, version, length = struct.unpack_from("<III", data, 0)
    if magic != GLB_MAGIC:
        raise ValueError(f"{path} is not a GLB file")
    if version != 2:
        raise ValueError(f"{path} is glTF version {version}, expected 2")
    if length != len(data):
        raise ValueError(f"{path} header length {length} does not match the file ({len(data)})")
    offset = 12
    js = None
    blob = b""
    while offset + 8 <= len(data):
        chunk_len, chunk_type = struct.unpack_from("<II", data, offset)
        offset += 8
        chunk = data[offset : offset + chunk_len]
        offset += chunk_len
        if chunk_type == GLB_JSON:
            js = json.loads(chunk.decode("utf-8"))
        elif chunk_type == GLB_BIN:
            blob = chunk
    if js is None:
        raise ValueError(f"{path} has no JSON chunk")
    return js, blob


def _view_bytes(gltf, blob, view_index):
    view = gltf["bufferViews"][view_index]
    start = view.get("byteOffset", 0)
    end = start + view["byteLength"]
    return blob[start:end]


def accessor_array(gltf, blob, index):
    acc = gltf["accessors"][index]
    raw = _view_bytes(gltf, blob, acc["bufferView"])
    raw = raw[acc.get("byteOffset", 0) :]
    components = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[acc["type"]]
    dtype = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}[
        acc["componentType"]
    ]
    count = acc["count"] * components
    return np.frombuffer(raw, dtype=dtype, count=count).reshape(acc["count"], components)


def load_mesh(path):
    """Positions in glTF Y-up, triangle indices, UVs, the RGBA image, the JSON."""
    gltf, blob = read_glb(path)
    mesh = gltf["meshes"][0]
    prim = mesh["primitives"][0]
    attrs = prim["attributes"]
    positions = accessor_array(gltf, blob, attrs["POSITION"]).astype(np.float64)
    uvs = accessor_array(gltf, blob, attrs["TEXCOORD_0"]).astype(np.float64) if "TEXCOORD_0" in attrs else None
    indices = accessor_array(gltf, blob, prim["indices"]).reshape(-1).astype(np.int64)
    image = None
    if gltf.get("images"):
        png = _view_bytes(gltf, blob, gltf["images"][0]["bufferView"])
        image = np.array(Image.open(__import__("io").BytesIO(png)).convert("RGBA"))
    return {
        "gltf": gltf,
        "positions": positions,
        "uvs": uvs,
        "indices": indices,
        "image": image,
        "triangles": len(indices) // 3,
    }
