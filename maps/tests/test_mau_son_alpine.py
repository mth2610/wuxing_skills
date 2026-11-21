"""Alpine DEM and asset contracts for the Mẫu Sơn (Water Element) map."""
import re
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "maps" / "pipeline"))

from georaster import read_dem


class MauSonAlpineTests(unittest.TestCase):
    def test_dem_is_projected_utm48n_and_has_alpine_relief(self):
        dem_path = ROOT / "assets" / "maps" / "mau_son" / "DEM_MauSon_UTM48N.tif"
        self.assertTrue(dem_path.exists(), f"Missing {dem_path}")
        dem, valid, meta = read_dem(str(dem_path))
        self.assertEqual(meta["crs_epsg"], 32648)
        self.assertGreater(valid.sum(), 10000)
        vals = dem[valid]
        self.assertLess(vals.min(), 650.0)
        self.assertGreater(vals.max(), 1500.0)

    def test_baked_water_mesh_and_generated_headers_exist(self):
        assets_dir = ROOT / "assets" / "maps" / "mau_son"
        for name in (
            "mau_son_heightfield.png",
            "mau_son_heightfield_ow.png",
            "mau_son_ecology_splat.png",
            "mau_son_debris_map.png",
            "mau_son_ice_flow.png",
            "mau_son_grade.png",
            "mau_son_water.obj",
            "mau_son_generated_meta.h",
        ):
            p = assets_dir / name
            self.assertTrue(p.exists(), f"Missing baked asset {p}")

        obj_text = (assets_dir / "mau_son_water.obj").read_text()
        self.assertIn("v ", obj_text)
        self.assertIn("vt ", obj_text)
        self.assertIn("f ", obj_text)

        rocks_h = ROOT / "maps" / "worlds" / "mau_son" / "mau_son_rocks_generated.h"
        self.assertTrue(rocks_h.exists())
        text = rocks_h.read_text()
        self.assertIn("MAP_ROCK_CRAG", text)
        self.assertIn("MAP_ROCK_SLATE", text)
        self.assertIn("MAU_SON_DEBRIS_ITEMS", text)

        m = re.search(r"#define\s+MAU_SON_DEBRIS_COUNT\s+(\d+)", text)
        self.assertIsNotNone(m)
        self.assertGreater(int(m.group(1)), 500)

    def test_seamless_alpine_textures_exist(self):
        tex_dir = ROOT / "assets" / "maps" / "mau_son" / "textures"
        for name in (
            "alpine_snow_diffuse.png",
            "alpine_snow_material.png",
            "frozen_tundra_diffuse.png",
            "frozen_tundra_material.png",
            "glacial_ice_plates.png",
        ):
            p = tex_dir / name
            self.assertTrue(p.exists(), f"Missing {p}")
            data = p.read_bytes()
            self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
            w, h = struct.unpack(">II", data[16:24])
            self.assertEqual((w, h), (512, 512))


if __name__ == "__main__":
    unittest.main()
