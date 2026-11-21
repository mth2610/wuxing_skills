"""Metallic DEM and asset contracts for the Phú Quốc (Metal Element) map."""
import re
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "maps" / "pipeline"))

from georaster import read_dem


class PhuQuocMetallicTests(unittest.TestCase):
    def test_dem_is_projected_utm48n_and_has_island_relief(self):
        dem_path = ROOT / "assets" / "maps" / "phu_quoc" / "DEM_PhuQuoc_UTM48N.tif"
        self.assertTrue(dem_path.exists(), f"Missing {dem_path}")
        dem, valid, meta = read_dem(str(dem_path))
        self.assertEqual(meta["crs_epsg"], 32648)
        self.assertGreater(valid.sum(), 10000)
        vals = dem[valid]
        self.assertLess(vals.min(), 5.0)
        self.assertGreater(vals.max(), 500.0)

    def test_baked_mineral_mesh_and_generated_headers_exist(self):
        assets_dir = ROOT / "assets" / "maps" / "phu_quoc"
        for name in (
            "phu_quoc_heightfield.png",
            "phu_quoc_heightfield_ow.png",
            "phu_quoc_ecology_splat.png",
            "phu_quoc_debris_map.png",
            "phu_quoc_grade.png",
            "phu_quoc_water.obj",
            "phu_quoc_generated_meta.h",
        ):
            p = assets_dir / name
            self.assertTrue(p.exists(), f"Missing baked asset {p}")

        obj_text = (assets_dir / "phu_quoc_water.obj").read_text()
        self.assertIn("v ", obj_text)
        self.assertIn("vt ", obj_text)
        self.assertIn("f ", obj_text)

        rocks_h = ROOT / "maps" / "worlds" / "phu_quoc" / "phu_quoc_rocks_generated.h"
        self.assertTrue(rocks_h.exists())
        text = rocks_h.read_text()
        self.assertIn("MAP_ROCK_SLATE", text)
        self.assertIn("MAP_ORE_GOLD_VEIN", text)
        self.assertIn("MAP_ORE_SILVER_LODE", text)
        self.assertIn("PHU_QUOC_ORE_ITEMS", text)
        self.assertIn("PHU_QUOC_DEBRIS_ITEMS", text)

        m_ore = re.search(r"#define\s+PHU_QUOC_ORE_COUNT\s+(\d+)", text)
        self.assertIsNotNone(m_ore)
        self.assertGreaterEqual(int(m_ore.group(1)), 50)

        m_deb = re.search(r"#define\s+PHU_QUOC_DEBRIS_COUNT\s+(\d+)", text)
        self.assertIsNotNone(m_deb)
        self.assertGreater(int(m_deb.group(1)), 500)

    def test_seamless_metallic_textures_exist(self):
        tex_dir = ROOT / "assets" / "maps" / "phu_quoc" / "textures"
        for name in (
            "auriferous_quartz_diffuse.png",
            "auriferous_quartz_material.png",
            "argentite_slate_diffuse.png",
            "argentite_slate_material.png",
            "mineral_ore_veins.png",
        ):
            p = tex_dir / name
            self.assertTrue(p.exists(), f"Missing {p}")
            data = p.read_bytes()
            self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
            w, h = struct.unpack(">II", data[16:24])
            self.assertEqual((w, h), (512, 512))


if __name__ == "__main__":
    unittest.main()
