"""Numerical contracts for the DEM bake; run with /usr/bin/python3."""
import importlib.util
import sys
import unittest
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'pipeline'))
from dem_pipeline import delineate_basins, compute_d8_flow, largest_component, resample_grid, write_water_mesh
import tempfile
import struct
from georaster import read_dem

class DrainageTests(unittest.TestCase):
    def test_geotiff_endianness_units_and_nodata(self):
        for endian,mark in (('<',b'II'),('>',b'MM')):
            # Tiny real TIFF directory, including inline SHORT arrays and GeoKeys.
            keys=[1,1,0,4,1024,0,1,1,1025,0,1,1,3072,0,1,32648,3076,0,1,9001]
            values={256:(3,[2]),257:(3,[2]),258:(3,[16]),259:(3,[1]),
                273:(4,[0]),277:(3,[1]),278:(4,[2]),279:(4,[8]),339:(3,[2]),
                33550:(12,[30.,60.,0.]),33922:(12,[0.,0.,0.,100.,200.,0.]),
                34735:(3,keys),42113:(2,b'-32768\0')}
            cursor=8+2+12*len(values)+4; extras=bytearray();entries=[]
            for tag,(kind,value) in sorted(values.items()):
                raw=value if kind==2 else struct.pack(endian+str(len(value))+{3:'H',4:'I',12:'d'}[kind],*value)
                if len(raw)>4:
                    field=struct.pack(endian+'I',cursor+len(extras));extras.extend(raw)
                else: field=raw.ljust(4,b'\0')
                entries.append((tag,kind,len(value),field))
            data_offset=cursor+len(extras)
            entries=[(t,k,n,struct.pack(endian+'I',data_offset) if t==273 else v) for t,k,n,v in entries]
            blob=mark+struct.pack(endian+'HIH',42,8,len(entries))
            blob+=b''.join(struct.pack(endian+'HHI4s',*e) for e in entries)+b'\0'*4+extras
            blob+=struct.pack(endian+'4h',45,71,-32768,1240)
            with tempfile.TemporaryDirectory() as d:
                path=Path(d)/'dem.tif';path.write_bytes(blob)
                dem,valid,meta=read_dem(path)
                self.assertEqual(dem[1,1],1240)
                self.assertFalse(valid[1,0])
                self.assertEqual(meta['pixel_size_m'],[30,60])
                self.assertEqual(meta['valid_area_m2'],5400)
                self.assertEqual(meta['crs_epsg'],32648)

    def test_flat_routes_to_boundary_and_conserves_area(self):
        valid=np.ones((17,23),bool)
        direction,accum,filled=compute_d8_flow(np.ones(valid.shape)*71,valid,30,60)
        dy=np.array([0,-1,-1,-1,0,1,1,1]); dx=np.array([1,1,0,-1,-1,-1,0,1])
        for y,x in zip(*np.where(direction>=0)):
            d=direction[y,x]
            self.assertLess(filled[y+dy[d],x+dx[d]],filled[y,x])
        self.assertTrue(np.all(direction[1:-1,1:-1]>=0))
        self.assertEqual(accum[direction<0].sum(),valid.sum())

    def test_nodata_hole_is_not_a_synthetic_deep_channel(self):
        valid=np.ones((11,11),bool);valid[4:7,4:7]=False
        dem=np.indices(valid.shape)[0].astype(float);dem[~valid]=-32768
        direction,accum,filled=compute_d8_flow(dem,valid)
        self.assertTrue(np.all(direction[~valid]==-1))
        self.assertEqual(accum[(direction<0)&valid].sum(),valid.sum())

    def test_basin_area_matches_outlet_accumulation(self):
        y,x=np.indices((13,19))
        dem=10-np.abs(x-9)+y*0.1
        valid=np.ones(dem.shape,bool)
        direction,accum,filled=compute_d8_flow(dem,valid)
        basin=delineate_basins(direction,filled,valid)
        self.assertTrue(np.all(basin>0))
        for r,c in zip(*np.where(direction<0)):
            self.assertEqual((basin==basin[r,c]).sum(),accum[r,c])

    def test_disconnected_flats_are_not_merged(self):
        mask=np.zeros((10,10),bool);mask[1:4,1:4]=True;mask[6:8,6:8]=True
        lake=largest_component(mask)
        self.assertEqual(lake.sum(),9)
        self.assertFalse(lake[6,6])

    def test_resampler_includes_last_terrain_vertex(self):
        result=resample_grid(np.array([[0.,1.],[2.,3.]]),7,9)
        self.assertEqual(result[-1,-1],3)
        self.assertEqual(result[3,4],1.5)

    def test_water_mesh_clips_at_zero_depth(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'water.obj'
            count=write_water_mesh(path,np.array([[0.,2.],[0.,2.]]),np.ones((2,2)),1,1)
            self.assertGreater(count,0)
            verts=[list(map(float,line.split()[1:])) for line in path.read_text().splitlines() if line.startswith('v ')]
            uv=[float(line.split()[1]) for line in path.read_text().splitlines() if line.startswith('vt ')]
            self.assertTrue(all(v[0]<=0.5 for v in verts))
            self.assertTrue(all(v[1]==1 for v in verts))
            self.assertIn(0,uv)
            self.assertTrue(all(0<=d<=1 for d in uv))

if __name__=='__main__': unittest.main()
