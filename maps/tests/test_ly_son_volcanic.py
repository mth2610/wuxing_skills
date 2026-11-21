"""Volcanic DEM and asset contracts for the Lý Sơn (Fire Element) map."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "maps" / "pipeline"))

from georaster import read_dem


class LySonVolcanicTests(unittest.TestCase):
    def test_dem_is_projected_utm49n_and_has_caldera_relief(self):
        dem_path = ROOT / "assets" / "maps" / "ly_son" / "DEM_LySon_UTM49N.tif"
        self.assertTrue(dem_path.exists(), f"Missing {dem_path}")
        dem, valid, meta = read_dem(str(dem_path))
        self.assertEqual(meta["crs_epsg"], 32649)
        self.assertGreater(valid.sum(), 10000)
        vals = dem[valid]
        self.assertLess(vals.min(), 5.0)
        self.assertGreater(vals.max(), 140.0)

    def test_baked_magma_mesh_and_generated_headers_exist(self):
        assets_dir = ROOT / "assets" / "maps" / "ly_son"
        for name in (
            "ly_son_heightfield.png",
            "ly_son_heightfield_ow.png",
            "ly_son_ecology_splat.png",
            "ly_son_debris_map.png",
            "ly_son_grade.png",
            "ly_son_water.obj",
            "ly_son_generated_meta.h",
        ):
            p = assets_dir / name
            self.assertTrue(p.exists(), f"Missing baked asset {p}")

        obj_text = (assets_dir / "ly_son_water.obj").read_text()
        self.assertIn("v ", obj_text)
        self.assertIn("vt ", obj_text)
        self.assertIn("f ", obj_text)

        rocks_h = ROOT / "maps" / "worlds" / "ly_son" / "ly_son_rocks_generated.h"
        self.assertTrue(rocks_h.exists())
        text = rocks_h.read_text()
        self.assertIn("MAP_ROCK_BASALT", text)
        self.assertIn("MAP_ROCK_OBSIDIAN", text)
        self.assertIn("LY_SON_VOLCANO_VENTS", text)


if __name__ == "__main__":
    unittest.main()
