# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Connected squirrel candidate checks; original sprite data is never required."""

from collections import Counter
import importlib.util
import json
from pathlib import Path
import unittest

import numpy as np

from glb_io import load_mesh
from rebuild import check_histograms, strip_histograms
from render_views import rasterize
from rct2dat import load_standard_palette
from validate import trunk_base_extent

HERE = Path(__file__).resolve().parent
SOURCE = HERE / "examples/rct2.scenery_small.tsq/revision2"
OUT = HERE / "out/squirrel-revision2"
OBJECT_ID = "rct2.scenery_small.tsq"
spec = importlib.util.spec_from_file_location("squirrel_revision2_author", SOURCE / "author.py")
author = importlib.util.module_from_spec(spec)
spec.loader.exec_module(author)


class RevisedTopiaryInputsTest(unittest.TestCase):
    def setUp(self):
        self.params = json.loads((SOURCE / "params.json").read_text())

    def test_texture_is_deterministic_opaque_and_palette_matched(self):
        check_histograms(self.params)
        rgba = author.paint(self.params)
        np.testing.assert_array_equal(rgba, author.paint(self.params))
        self.assertEqual(rgba.shape, (128,128,4))
        self.assertTrue(np.all(rgba[:,:,3] == 255))
        colours = {tuple(c) for c in rgba[:,:,:3].reshape(-1,3)}
        self.assertEqual(colours, {tuple(c) for c in load_standard_palette()[72:81]})

    def test_installed_model_uses_revised_texture(self):
        path = HERE.parents[1] / "data/object-models" / (OBJECT_ID + ".glb")
        mesh = load_mesh(str(path))
        np.testing.assert_array_equal(mesh["image"], author.paint(self.params))

    def test_installed_model_matches_revised_metadata(self):
        root = HERE.parents[1] / "data/object-models"
        mesh = load_mesh(str(root / (OBJECT_ID + ".glb")))
        meta = json.loads((root / (OBJECT_ID + ".meta.json")).read_text())
        self.assertEqual(meta["archetype"], "squirrel_topiary")
        self.assertEqual(meta["params"], strip_histograms(self.params))
        self.assertEqual(mesh["triangles"], 580)
        self.assertEqual(mesh["triangles"], meta["triangles"])
        positions = mesh["positions"][:, [0, 2, 1]].copy()
        positions[:, 1] *= -1
        np.testing.assert_allclose(positions.min(axis=0), meta["bounds_blender"]["min"], atol=1e-6)
        np.testing.assert_allclose(positions.max(axis=0), meta["bounds_blender"]["max"], atol=1e-6)
        self.assertLessEqual(trunk_base_extent(mesh["positions"], mesh["indices"]), 0.25)

    def test_local_patches_keep_source_green_distribution(self):
        rgba = author.paint(self.params)
        hist = self.params["atlas"]["foliage_hist"]
        palette = load_standard_palette()
        total = sum(hist.values())
        target = sum(palette[int(k)].astype(float)*v for k,v in hist.items())/total
        for y in range(0,128,16):
            for x in range(0,128,16):
                mean = rgba[y:y+16,x:x+16,:3].mean(axis=(0,1))
                np.testing.assert_allclose(mean,target,atol=0.5)

    def test_paired_anatomy_and_tail_curl_are_explicit(self):
        parts = {p["name"]:p for p in self.params["parts"]}
        for name in ("ears","thighs","feet","elbows","forearms","paws"):
            self.assertTrue(parts[name]["pair"])
        self.assertLess(parts["muzzle"]["center"][0], parts["head"]["center"][0])
        tail = np.array(self.params["tail"]["rings"])
        self.assertGreater(tail[:,0].max()-tail[-1,0],4)
        self.assertGreater(tail[:,2].max()-tail[-1,2],1)


@unittest.skipUnless((OUT / "geometry.json").exists(), "Build the local revision-2 candidate first")
class RevisedTopiaryExportTest(unittest.TestCase):
    def test_sculpt_is_one_closed_connected_surface(self):
        geometry = json.loads((OUT / "geometry.json").read_text())
        edges = Counter(tuple(sorted((f[i],f[(i+1)%len(f)]))) for f in geometry["F"] for i in range(len(f)))
        self.assertEqual(set(edges.values()), {2})
        neighbours = {}
        for a,b in edges:
            neighbours.setdefault(a,set()).add(b)
            neighbours.setdefault(b,set()).add(a)
        seen, todo = set(), [next(iter(neighbours))]
        while todo:
            i = todo.pop()
            if i not in seen:
                seen.add(i)
                todo.extend(neighbours[i]-seen)
        self.assertEqual(len(seen), len(geometry["V"]))

    def test_export_preserves_foliage_and_material_contract(self):
        mesh = load_mesh(str(OUT/(OBJECT_ID+".glb")))
        params = json.loads((SOURCE/"params.json").read_text())
        np.testing.assert_array_equal(mesh["image"], author.paint(params))
        self.assertLessEqual(mesh["triangles"],600)
        self.assertLessEqual(len(mesh["positions"]),2500)
        material = mesh["gltf"]["materials"][0]
        self.assertEqual(material["alphaMode"],"MASK")
        self.assertEqual(material["alphaCutoff"],0.5)
        self.assertTrue(material["doubleSided"])

    def test_sampled_render_greens_track_source_histogram(self):
        mesh = load_mesh(str(OUT/(OBJECT_ID+".glb")))
        pixels = np.concatenate([rasterize(mesh["positions"],mesh["uvs"],mesh["indices"],mesh["image"],view).reshape(-1,4) for view in range(4)])
        mean = pixels[pixels[:,3]>127,:3].mean(axis=0)
        params = json.loads((SOURCE/"params.json").read_text())
        hist = params["atlas"]["foliage_hist"]
        palette = load_standard_palette()
        target = sum(palette[int(k)].astype(float)*v for k,v in hist.items())/sum(hist.values())
        np.testing.assert_allclose(mean,target,atol=5)


if __name__ == "__main__":
    unittest.main()
