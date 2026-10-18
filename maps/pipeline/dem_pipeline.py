#!/usr/bin/env python3
"""Projected DEM bake: source metrics, game terrain, hydrology and habitat proxies.

Python + NumPy only. Geology, vegetation and bathymetry inferred from terrain
are explicitly distinguished from observations in the generated manifest.
"""

import sys
import os
import struct
import zlib
import json
import argparse
import numpy as np

# --- 1. Pure Python + Zlib PNG Writer ---
def write_png(filename, img_array, color_type=0, bit_depth=8):
    """
    Writes a 2D or 3D numpy array to a valid PNG file.
    color_type: 0 for Grayscale, 2 for RGB, 6 for RGBA.
    bit_depth: 8 or 16.
    """
    h, w = img_array.shape[:2]
    
    def png_chunk(chunk_type, data):
        length = len(data)
        crc = zlib.crc32(chunk_type + data) & 0xffffffff
        return struct.pack(">I", length) + chunk_type + data + struct.pack(">I", crc)

    ihdr_data = struct.pack(">IIBBBBB", w, h, bit_depth, color_type, 0, 0, 0)
    ihdr = png_chunk(b"IHDR", ihdr_data)

    raw_data = bytearray()
    if bit_depth == 8:
        for row in range(h):
            raw_data.append(0) # filter type 0 (None)
            raw_data.extend(img_array[row].astype(np.uint8).tobytes())
    elif bit_depth == 16:
        arr_be = img_array.astype(">u2")
        for row in range(h):
            raw_data.append(0)
            raw_data.extend(arr_be[row].tobytes())

    compressed = zlib.compress(bytes(raw_data), level=6)
    idat = png_chunk(b"IDAT", compressed)
    iend = png_chunk(b"IEND", b"")

    with open(filename, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(ihdr)
        f.write(idat)
        f.write(iend)

# GeoTIFF georeferencing and NoData are part of the input contract.
try:
    from .georaster import read_dem
except ImportError:
    from georaster import read_dem


def read_geotiff(dem_path):
    return read_dem(dem_path)[0]

# --- 3. Bilinear Grid Resampler ---
def resample_grid(src, dst_h, dst_w):
    src_h, src_w = src.shape
    y_coords = np.linspace(0, src_h - 1, dst_h)
    x_coords = np.linspace(0, src_w - 1, dst_w)
    
    y0 = np.floor(y_coords).astype(int)
    y1 = np.clip(y0 + 1, 0, src_h - 1)
    x0 = np.floor(x_coords).astype(int)
    x1 = np.clip(x0 + 1, 0, src_w - 1)
    
    wy = (y_coords - y0).reshape(-1, 1)
    wx = (x_coords - x0).reshape(1, -1)
    
    return (src[y0, :][:, x0] * (1 - wx) * (1 - wy) +
            src[y0, :][:, x1] * wx * (1 - wy) +
            src[y1, :][:, x0] * (1 - wx) * wy +
            src[y1, :][:, x1] * wx * wy)

def gaussian_filter_2d(arr, sigma=3.0):
    """Fast, boundary-preserving separable 2D Gaussian convolution in pure NumPy."""
    k_size = int(round(sigma * 3)) * 2 + 1
    x = np.arange(k_size) - (k_size // 2)
    k1d = np.exp(-0.5 * (x / sigma)**2)
    k1d /= k1d.sum()
    
    pad_h = np.pad(arr, ((0, 0), (k_size//2, k_size//2)), mode='edge')
    tmp = np.apply_along_axis(lambda m: np.convolve(m, k1d, mode='valid'), axis=1, arr=pad_h)
    pad_v = np.pad(tmp, ((k_size//2, k_size//2), (0, 0)), mode='edge')
    out = np.apply_along_axis(lambda m: np.convolve(m, k1d, mode='valid'), axis=0, arr=pad_v)
    return out

# --- 4. Hydrology: Priority Flood Depression Filling & D8 Flow Routing ---
import heapq

def priority_flood(elevation, valid_mask):
    """
    Barns et al. Priority-Flood algorithm.
    Guarantees monotonic drainage to watershed boundary seeds, completely
    eliminating artificial sinks without iterative heuristics.
    """
    h, w = elevation.shape
    dem = elevation.astype(np.float64).copy()
    dem[~valid_mask] = -1e9

    pq = []
    visited = np.zeros((h, w), dtype=bool)
    dy = [-1, -1, -1,  0,  0,  1,  1,  1]
    dx = [-1,  0,  1, -1,  1, -1,  0,  1]

    # Boundary cells of valid_mask act as drainage sinks/seeds
    for r in range(h):
        for c in range(w):
            if valid_mask[r, c]:
                is_boundary = any(
                    not (0 <= r + dy[i] < h and 0 <= c + dx[i] < w) or
                    not valid_mask[r + dy[i], c + dx[i]]
                    for i in range(8)
                )
                if is_boundary:
                    heapq.heappush(pq, (float(dem[r, c]), r, c))
                    visited[r, c] = True

    while pq:
        z, r, c = heapq.heappop(pq)
        for i in range(8):
            nr, nc = r + dy[i], c + dx[i]
            if 0 <= nr < h and 0 <= nc < w and valid_mask[nr, nc] and not visited[nr, nc]:
                visited[nr, nc] = True
                if dem[nr, nc] <= z:
                    dem[nr, nc] = np.nextafter(z, np.inf)  # Elevate sink to spillover threshold
                heapq.heappush(pq, (float(dem[nr, nc]), nr, nc))

    return dem

def compute_d8_flow(elevation, valid_mask, cell_x=1.0, cell_y=1.0):
    """Computes D8 steepest descent direction and continuous flow accumulation using Priority-Flood."""
    dem_flow = priority_flood(elevation, valid_mask)
    h, w = dem_flow.shape
    dy = np.array([0, -1, -1, -1,  0,  1, 1, 1])
    dx = np.array([1,  1,  0, -1, -1, -1, 0, 1])
    dist = np.hypot(dx * cell_x, dy * cell_y)

    flow_dir = np.full((h, w), -1, dtype=np.int8)
    elev_copy = dem_flow.copy()
    elev_copy[~valid_mask] = 1e9

    elev_padded = np.pad(elev_copy, 1, mode='edge')
    max_slope = np.zeros((h, w), dtype=np.float64)
    center = elev_padded[1:-1, 1:-1]

    for i in range(8):
        neighbor = elev_padded[1+dy[i]:1+dy[i]+h, 1+dx[i]:1+dx[i]+w]
        drop = center - neighbor
        slope = drop / dist[i]
        steeper = (slope > max_slope) & (drop > 0) & valid_mask
        max_slope[steeper] = slope[steeper]
        flow_dir[steeper] = i

    flat_indices = np.argsort(-dem_flow.ravel())
    accum = np.ones((h, w), dtype=np.float64)
    accum[~valid_mask] = 0.0

    for idx in flat_indices:
        r, c = divmod(idx, w)
        if not valid_mask[r, c]:
            continue
        d = flow_dir[r, c]
        if d >= 0:
            nr = r + dy[d]
            nc = c + dx[d]
            if 0 <= nr < h and 0 <= nc < w and valid_mask[nr, nc]:
                accum[nr, nc] += accum[r, c]

    return flow_dir, accum, dem_flow


def delineate_basins(directions, conditioned, valid):
    """Assign every valid cell to its D8 terminal outlet, low to high."""
    labels = np.zeros(valid.shape, dtype=np.int32)
    offsets = [(0,1),(-1,1),(-1,0),(-1,-1),(0,-1),(1,-1),(1,0),(1,1)]
    next_id = 0
    width = valid.shape[1]
    for i in np.argsort(conditioned, axis=None):
        y,x = divmod(int(i),width)
        if not valid[y,x]: continue
        d = directions[y,x]
        if d < 0:
            next_id += 1
            labels[y,x] = next_id
        else:
            dy,dx = offsets[d]
            labels[y,x] = labels[y+dy,x+dx]
            if labels[y,x] == 0:
                raise ValueError('Drainage is not a strictly descending acyclic graph')
    return labels


def largest_component(mask):
    """Four-connected footprint; isolated flats must not become one giant lake."""
    seen = np.zeros_like(mask)
    best = []
    h, w = mask.shape
    for r, c in zip(*np.where(mask)):
        if seen[r, c]:
            continue
        todo = [(r, c)]
        seen[r, c] = True
        for y, x in todo:
            for yy, xx in ((y-1,x), (y+1,x), (y,x-1), (y,x+1)):
                if 0 <= yy < h and 0 <= xx < w and mask[yy,xx] and not seen[yy,xx]:
                    seen[yy,xx] = True
                    todo.append((yy,xx))
        if len(todo) > len(best):
            best = todo
    result = np.zeros_like(mask)
    if best:
        yy, xx = np.array(best).T
        result[yy, xx] = True
    return result


def normalized_smooth(values, valid, sigma):
    return gaussian_filter_2d(np.where(valid, values, 0), sigma) / np.maximum(
        gaussian_filter_2d(valid.astype(float), sigma), 1e-12)


def distance_inside(mask, step_x, step_y):
    """Eight-neighbor metric distance to dry/invalid nodes, including raster edge."""
    h, w = mask.shape
    dist = np.where(mask, np.inf, 0.0)
    queue = []
    for y, x in zip(*np.where(mask)):
        if y == 0 or x == 0 or y == h-1 or x == w-1:
            dist[y,x] = min(step_x, step_y)
            heapq.heappush(queue, (dist[y,x], y, x))
        elif not (mask[y-1,x] and mask[y+1,x] and mask[y,x-1] and mask[y,x+1]):
            dist[y,x] = min(step_x if not (mask[y,x-1] and mask[y,x+1]) else np.inf,
                            step_y if not (mask[y-1,x] and mask[y+1,x]) else np.inf)
            heapq.heappush(queue, (dist[y,x], y, x))
    neighbors = [(dy,dx,np.hypot(dx*step_x,dy*step_y)) for dy in (-1,0,1) for dx in (-1,0,1) if dx or dy]
    while queue:
        d,y,x = heapq.heappop(queue)
        if d != dist[y,x]:
            continue
        for dy,dx,step in neighbors:
            yy,xx=y+dy,x+dx
            if 0<=yy<h and 0<=xx<w and mask[yy,xx] and d+step<dist[yy,xx]:
                dist[yy,xx]=d+step
                heapq.heappush(queue,(d+step,yy,xx))
    return dist


def write_water_mesh(path, ground, surface, sx, sz):
    """Clip the terrain's two triangles per cell to positive water depth.

    UV.x carries vertical depth in metres, independent of camera/depth buffer.
    OBJ vertices are world-space. Shore intersections have exactly zero depth.
    """
    h,w=ground.shape
    count=0
    with open(path,'w') as f:
        f.write('# DEM terrain water; vt.x = depth metres\n')
        f.write('vn 0.000000 1.000000 0.000000\n')
        for r in range(h-1):
            for c in range(w-1):
                nodes=[(r,c),(r+1,c),(r,c+1),(r+1,c+1)]
                verts=[np.array([x*sx,surface[y,x],y*sz,surface[y,x]-ground[y,x]]) for y,x in nodes]
                for ids in ((0,1,2),(2,1,3)):
                    polygon=[]
                    for a,b in zip(ids,(ids[1],ids[2],ids[0])):
                        va,vb=verts[a],verts[b]
                        if va[3]>0: polygon.append(va)
                        if (va[3]>0)!=(vb[3]>0):
                            t=va[3]/(va[3]-vb[3])
                            v=va+(vb-va)*t; v[3]=0
                            polygon.append(v)
                    for i in range(1,len(polygon)-1):
                        for v in (polygon[0],polygon[i],polygon[i+1]):
                            f.write(f'v {v[0]:.6f} {v[1]:.6f} {v[2]:.6f}\nvt {max(0,v[3]):.6f} 0\n')
                        f.write(f'f {count+1}/{count+1}/1 {count+2}/{count+2}/1 {count+3}/{count+3}/1\n')
                        count+=3
    return count//3


def process_dem(dem_path, max_dim=300.0, height_range=16.0, cliff_depth=8.0,
                cliff_margin=8.5, res_scale=1.0, stream_ratio=0.01,
                prefix='map', out_dir='assets/maps', lake_level=None):
    import hashlib
    import re
    import time
    started=time.perf_counter()
    if not re.fullmatch(r'[a-z][a-z0-9_]*',prefix):
        raise ValueError('prefix must be a lowercase C identifier')
    if not all(np.isfinite(v) and v>0 for v in (max_dim,height_range,cliff_depth,cliff_margin,res_scale)) or not 0<stream_ratio<1:
        raise ValueError('Invalid dimensions or stream ratio')
    raw,valid_raw,geo=read_dem(dem_path)
    if not valid_raw.any(): raise ValueError('No valid elevations')
    yy,xx=np.where(valid_raw)
    crop=(slice(yy.min(),yy.max()+1),slice(xx.min(),xx.max()+1))
    source=raw[crop]; valid_source=valid_raw[crop]
    px,py=geo['pixel_size_m']
    print(f'[DEM] EPSG:{geo["crs_epsg"]}, {px:g} x {py:g} m; valid footprint {geo["valid_area_m2"]/1e6:.4f} km2',flush=True)
    # Source metrics precede artistic compression. NoData is never an elevation.
    clean=normalized_smooth(source,valid_source,0.8)
    gy,gx=np.gradient(clean,py,px)
    slope=np.hypot(gx,gy)
    slope_deg=np.degrees(np.arctan(slope))
    aspect=np.mod(np.degrees(np.arctan2(-gx,gy)),360)
    tpi=clean-normalized_smooth(source,valid_source,5.0)
    curvature=np.gradient(gx,px,axis=1)+np.gradient(gy,py,axis=0)
    source_dir,source_accum,source_filled=compute_d8_flow(source,valid_source,px,py)
    source_basins=delineate_basins(source_dir,source_filled,valid_source)
    main_outlet=np.unravel_index(np.argmax(source_accum),source.shape)
    main_basin=source_basins==source_basins[main_outlet]
    area=source_accum*px*py
    twi=np.log(np.maximum(area/np.sqrt(px*py),1e-9)/np.maximum(slope,0.001))
    lo,hi=float(source[valid_source].min()),float(source[valid_source].max())
    # Large connected low-elevation plateaus are candidates, not surveyed water.
    low=valid_source & (source<=lo+(hi-lo)*0.30)
    if lake_level is None:
        levels,counts=np.unique(source[low],return_counts=True)
        lake_level=float(levels[np.argmax(counts)])
        lake_origin='inferred largest connected lowland plateau; unverified water extent'
    else:
        lake_origin='user supplied source elevation; connected flat footprint'
    lake_source=largest_component(valid_source & (np.abs(source-lake_level)<=0.5) & (slope_deg<4.0))
    if lake_source.sum()<20: lake_source[:]=False
    physical_w=(source.shape[1]-1)*px; physical_h=(source.shape[0]-1)*py
    scale=max_dim/max(physical_w,physical_h)
    width,depth=physical_w*scale,physical_h*scale
    w,h=max(8,round(width*res_scale)+1),max(8,round(depth*res_scale)+1)
    if w*h>65535: raise ValueError('Runtime heightfield supports at most 65535 nodes; reduce resolution')
    sx,sz=width/(w-1),depth/(h-1)
    sample=lambda a: resample_grid(a,h,w)
    valid=sample(valid_source.astype(float))>0.75
    valid[:2]=False; valid[-2:]=False; valid[:,:2]=False; valid[:,-2:]=False
    dem=sample(clean)
    # Smooth macro rolling relief while preserving drainage valleys and ridges
    rolling=normalized_smooth(dem,valid,max(0.8,2.4/min(sx,sz)))
    normalized=np.clip((rolling-lo)/max(hi-lo,1e-9),0,1)
    terrain=height_range*np.power(normalized,1.15)
    perimeter=distance_inside(valid,sx,sz)
    cliff=np.clip(perimeter/cliff_margin,0,1); cliff=cliff*cliff*(3-2*cliff)
    cliff=gaussian_filter_2d(cliff, 1.4)
    lake_fraction=sample(lake_source.astype(float))
    lake=valid & (lake_fraction>0.25) & (perimeter>cliff_margin)
    lake_y=float(height_range*np.clip((lake_level-lo)/max(hi-lo,1e-9),0,1)**1.15)
    lake_inside=distance_inside(lake,sx,sz)
    lake_blend=gaussian_filter_2d(lake.astype(float),2.6)
    w_lake=np.clip((lake_blend-0.02)/0.90,0.0,1.0)
    w_lake=w_lake*w_lake*(3.0-2.0*w_lake)
    # Smooth C2 bowl bathymetry with convex bank blending: eliminates 1m grid-step sawtooth walls
    lake_depth_profile = gaussian_filter_2d(np.clip(lake_inside / 7.5, 0.0, 1.0), 1.8)
    lake_carve = 0.08 * w_lake + 1.25 * np.power(lake_depth_profile, 1.25)
    target_lake_bed = np.minimum(terrain, lake_y - lake_carve)
    terrain = terrain * (1.0 - w_lake) + target_lake_bed * w_lake
    # Re-route after compression; retain every branch over the area threshold.
    directions,accum,conditioned=compute_d8_flow(terrain,valid,sx,sz)
    threshold=max(12,float(valid.sum())*stream_ratio)
    streams=valid & (accum>=threshold) & (perimeter>cliff_margin)
    dd=[(0,1),(-1,1),(-1,0),(-1,-1),(0,-1),(1,-1),(1,0),(1,1)]
    X,Z=np.meshgrid(np.arange(w)*sx,np.arange(h)*sz)
    surface=terrain-0.12
    corridor=np.zeros_like(terrain)
    stream_surface_num=np.zeros_like(terrain)
    stream_surface_den=np.zeros_like(terrain)
    # Vectorized segment distance over local bounding boxes, not grid x all segments.
    for r,c in zip(*np.where(streams & ~lake)):
        d=int(directions[r,c]); dr,dc=dd[d] if d>=0 else (0,0)
        nr,nc=r+dr,c+dc
        if d<0 or not valid[nr,nc] or perimeter[nr,nc]<=cliff_margin: continue
        ax,az=c*sx,r*sz; bx,bz=nc*sx,nr*sz
        half=float(np.clip(0.85+0.024*np.sqrt(accum[r,c]),0.95,2.6))
        radius=half+max(sx,sz)*1.85
        x0=max(0,int((min(ax,bx)-radius*1.4)/sx)); x1=min(w,int((max(ax,bx)+radius*1.4)/sx)+2)
        z0=max(0,int((min(az,bz)-radius*1.4)/sz)); z1=min(h,int((max(az,bz)+radius*1.4)/sz)+2)
        sl=np.s_[z0:z1,x0:x1]
        t=np.clip(((X[sl]-ax)*(bx-ax)+(Z[sl]-az)*(bz-az))/max((bx-ax)**2+(bz-az)**2,1e-9),0,1)
        dist=np.hypot(X[sl]-(ax+t*(bx-ax)),Z[sl]-(az+t*(bz-az)))
        w_raw=0.5+0.5*np.cos(np.pi*np.clip(dist/radius,0,1))
        weight=w_raw*np.sqrt(np.maximum(w_raw,0.0))
        corridor[sl]=np.maximum(corridor[sl],weight)
        level=conditioned[r,c]*(1-t)+conditioned[nr,nc]*t
        # Carve a smooth U-shaped bed with convex bank interpolation across cells
        bed=np.minimum(terrain[sl],level-0.52*weight)
        terrain[sl]=terrain[sl]*(1.0-weight)+bed*weight
        w_surf=np.clip((radius*1.35-dist)/max(radius*1.35,1e-6),0,1)
        stream_surface_num[sl]+=w_surf*(level-0.08)
        stream_surface_den[sl]+=w_surf
    has_stream_surf = stream_surface_den > 1e-4
    avg_stream_surf = np.where(has_stream_surf, stream_surface_num / np.maximum(stream_surface_den, 1e-4), surface)
    # Smooth bed & bank transitions inside wet corridors to eliminate diagonal grid facets
    bed_smooth = normalized_smooth(terrain, valid, 1.35)
    smooth_mask = np.clip(corridor * 0.85 + w_lake * 0.90, 0.0, 0.90)
    terrain = terrain * (1.0 - smooth_mask) + bed_smooth * smooth_mask
    # Continuous water_plane field (extends smoothly onto adjacent dry banks for exact zero-depth contour clipping)
    lake_plane_w = np.clip(lake_blend * 2.2, 0.0, 1.0)
    stream_plane = np.where(has_stream_surf, avg_stream_surf, terrain - 0.25)
    water_plane = stream_plane * (1.0 - lake_plane_w) + np.maximum(stream_plane, lake_y) * lake_plane_w
    wet_corridor = valid & (perimeter > cliff_margin) & ((lake_blend > 0.01) | has_stream_surf)
    if wet_corridor.any():
        plane_smooth = normalized_smooth(water_plane, wet_corridor, 1.65)
        water_plane = np.where(wet_corridor & (lake_blend < 0.25), plane_smooth, water_plane)
    terrain=terrain*cliff-(1-cliff)*cliff_depth
    terrain[~valid]=-cliff_depth
    # Quantize terrain heightfield first, then clip water_plane against quantized ground
    total=height_range+cliff_depth
    height_code=np.rint(np.clip((terrain+cliff_depth)/total,0,1)*65535).astype(np.uint16)
    ground=height_code.astype(float)/65535*total-cliff_depth
    encoded=np.rint(height_code.astype(float)/257).astype(np.uint8)
    water_plane=np.where(valid & (perimeter>cliff_margin),water_plane,ground-0.25)
    surface=np.where(water_plane>ground+0.005,water_plane,ground-0.12)
    water_code=np.rint(np.clip((surface+cliff_depth)/total,0,1)*65535).astype(np.uint16)
    surface=water_code.astype(float)/65535*total-cliff_depth
    water_depth=np.maximum(0,water_plane-ground)
    wet=water_depth>0.005
    moisture=np.clip((sample(twi)-6)/8,0,1)
    shore=gaussian_filter_2d(wet.astype(float),max(0.7,2/min(sx,sz)))
    moisture=np.maximum(moisture,shore)
    slope_s = sample(slope_deg)
    tpi_s = sample(tpi)
    twi_s = sample(twi)

    # Rock scree emerges only on steeper cliffs (> 17 deg), keeping rolling hills green
    rock_slope = np.clip((slope_s - 17.0) / 15.0, 0.0, 1.0)
    rock_ridge = np.clip((tpi_s - 5.5) / 14.0, 0.0, 1.0) * np.clip((ground - 3.5) / 8.0, 0.0, 1.0)
    rock = np.maximum(np.maximum(rock_slope, rock_ridge), 1.0 - cliff)
    rock[water_depth > 0.01] = 0.0

    wet = water_depth > 0.005
    moisture = np.clip((twi_s - 5.5) / 8.0, 0.0, 1.0)
    shore = gaussian_filter_2d(wet.astype(float), max(0.7, 2.0 / min(sx, sz)))
    moisture = np.maximum(moisture, shore)
    depositional = np.clip(-tpi_s / 25.0, 0.0, 1.0) * moisture
    growth = np.clip(0.45 + 0.55 * moisture, 0.0, 1.0) * (1.0 - rock)

    # Lush valley & hillside meadow coverage
    valley_boost = np.clip((18.0 - slope_s) / 18.0, 0.0, 1.0) * np.clip((12.0 - ground) / 12.0, 0.0, 1.0)
    coverage = np.clip((1.0 - rock * 0.85) * (1.0 - wet) * (0.76 + 0.24 * growth + 0.22 * valley_boost), 0.0, 1.0)
    coverage[~valid] = 0.0

    # Bare dirt / soil substrate along margins and transitions
    soil = np.clip((1.0 - coverage) * (1.0 - rock) * (1.0 - wet.astype(float)) * 1.15, 0.0, 1.0)
    wet_mud = np.clip(np.maximum(shore * 0.75, wet.astype(float)), 0.0, 1.0)

    # Debris Layers:
    # 1. Leaf Litter (R): sheltered swales, under growth canopy, gentle ground (excluded near shoreline so shore isn't rusty red)
    leaf_litter = np.clip(growth * 0.65 + depositional * 0.50, 0.0, 1.0) * np.clip((16.0 - slope_s) / 16.0, 0.0, 1.0) * np.clip(1.0 - shore * 1.8, 0.0, 1.0) * (1.0 - wet.astype(float)) * (1.0 - rock * 0.8)
    # 2. Scree Gravel & River Shingle (G): talus zones below cliffs + natural pebble shingle along stream/lake shores
    scree_gravel = np.clip((slope_s - 10.0) / 12.0, 0.0, 1.0) * np.clip((28.0 - slope_s) / 12.0, 0.0, 1.0) * (0.35 + 0.65 * rock)
    scree_gravel = np.maximum(scree_gravel, np.clip(rock * (1.0 - wet.astype(float)), 0.0, 1.0) * 0.70)
    scree_gravel = np.maximum(scree_gravel, np.clip(shore * (1.0 - wet.astype(float)) * 0.85, 0.0, 1.0))
    # 3. Fallen Twigs & Wood (B): woodland margins
    twigs = np.clip(growth * 0.55 * (1.0 - slope_s / 20.0), 0.0, 1.0) * (1.0 - wet.astype(float))
    # 4. Moss & Lichen (A): moist shady swales, damp rock crevices, lush riverbanks
    moss = np.clip(moisture * 0.90 * (0.45 + 0.55 * rock), 0.0, 1.0) * (1.0 - wet.astype(float))

    # Wildflowers in sunny gentle knolls
    flowers = np.clip(coverage * (1.0 - moisture * 0.6) * (1.0 - slope_s / 18.0) * np.clip((tpi_s + 5.0) / 10.0, 0.0, 1.0), 0.0, 1.0)
    splat = np.stack((coverage, rock, flowers, np.maximum(shore, wet)), axis=-1)
    splat[~valid] = (0, 1, 0, 0)
    habitat = np.stack((coverage, growth, moisture, 1.0 - moisture), axis=-1)
    habitat[~valid] = 0.0

    debris_map = np.stack((leaf_litter, scree_gravel, twigs, moss), axis=-1)
    debris_map[~valid] = 0

    ground_material = np.stack((coverage, soil, wet_mud, rock), axis=-1)
    ground_material[~valid] = (0, 0, 0, 255)

    # Macro ground color tint (RGB)
    sunny = np.clip(tpi_s / 15.0 + 0.5, 0.0, 1.0)
    r_tint = np.clip(0.32 + 0.30 * sunny + 0.16 * soil + 0.12 * rock, 0.0, 1.0)
    g_tint = np.clip(0.46 + 0.45 * coverage * (1.0 - rock * 0.6) + 0.15 * moss, 0.0, 1.0)
    b_tint = np.clip(0.24 + 0.22 * rock + 0.28 * wet_mud, 0.0, 1.0)
    tint_map = np.stack((r_tint, g_tint, b_tint), axis=-1)
    tint_map[~valid] = (0.3, 0.3, 0.3)

    # Classification is morphological, not rock type or species observation.
    landform=np.where(sample(tpi)<-8,1,np.where(sample(tpi)>8,3,2)).astype(np.uint8)
    landform[~valid]=0
    landform[lake]=4
    os.makedirs(out_dir,exist_ok=True)
    path=lambda suffix:os.path.join(out_dir,prefix+'_'+suffix)
    write_png(path('island_heightmap.png'),encoded)
    height_rgba=np.stack((height_code>>8,height_code&255,np.zeros_like(height_code),np.full_like(height_code,255)),axis=-1)
    write_png(path('heightfield.png'),height_rgba,6)
    write_png(path('ecology_splat.png'),np.rint(splat*255),6)
    write_png(path('ground_material.png'),np.rint(ground_material*255),6)
    write_png(path('debris_map.png'),np.rint(debris_map*255),6)
    write_png(path('ground_tint.png'),np.rint(tint_map*255),2)
    write_png(path('habitat.png'),np.rint(habitat*255),6)
    write_png(path('stream_mask.png'),streams.astype(np.uint8)*255)
    write_png(path('main_catchment_mask.png'),(sample(main_basin.astype(float))>0.5).astype(np.uint8)*255)
    write_png(path('lake_mask.png'),lake.astype(np.uint8)*255)
    write_png(path('landform.png'),landform)
    write_png(path('substrate_proxy.png'),np.stack((rock,depositional,moisture),axis=-1)*255,2)
    write_png(path('terrain_metrics.png'),np.stack((np.clip(sample(slope_deg)/60,0,1),
        np.clip((sample(twi)-4)/12,0,1),np.clip(sample(tpi)/100+0.5,0,1)),axis=-1)*255,2)
    water_rgba=np.stack((water_code>>8,water_code&255,wet*255,lake*255),axis=-1).astype(np.uint8)
    write_png(path('water_height.png'),water_rgba,6)
    # Road distance is +16 m (no authored road); shore signed distance is metres.
    signed_shore=distance_inside(~wet,sx,sz)-distance_inside(wet,sx,sz)
    shore_code=np.rint((np.clip(signed_shore/16,-1,1)*0.5+0.5)*65535).astype(np.uint16)
    distance=np.stack((np.full_like(shore_code,255),np.full_like(shore_code,255),shore_code>>8,shore_code&255),axis=-1)
    write_png(path('habitat_distance.png'),distance,6)
    triangles=write_water_mesh(path('water.obj'),ground,water_plane,sx,sz)
    fields=dict(valid=valid,source_elevation_m=sample(clean).astype('f4'),slope_deg=sample(slope_deg).astype('f4'),
                aspect_deg=np.mod(np.degrees(np.arctan2(sample(np.sin(np.radians(aspect))),
                sample(np.cos(np.radians(aspect))))),360).astype('f4'),curvature_per_m=sample(curvature).astype('f4'),
                tpi_m=sample(tpi).astype('f4'),contributing_area_m2=sample(area).astype('f4'),twi=sample(twi).astype('f4'),
                flow_direction=directions,accumulation_cells=accum.astype('f4'),ground_y_m=ground.astype('f4'),
                water_y_m=surface.astype('f4'),water_depth_m=water_depth.astype('f4'),
                exposed_substrate=rock.astype('f4'),deposition_potential=depositional.astype('f4'),landform=landform)
    np.savez_compressed(path('analysis.npz'),**fields)
    # Preserve native source-grid metrics for scientific use and future rebakes.
    native=lambda a:np.where(valid_source,a,np.nan).astype('f4')
    np.savez_compressed(path('source_analysis.npz'),valid=valid_source,
        elevation_m=native(source),slope_deg=native(slope_deg),aspect_deg=native(aspect),
        tpi_m=native(tpi),curvature_per_m=native(curvature),twi=native(twi),
        contributing_area_m2=native(area),flow_direction=source_dir,basin_id=source_basins,
        main_catchment=main_basin,lake_candidate=lake_source)
    outlet=np.unravel_index(np.argmax(accum),accum.shape)
    peak=np.unravel_index(np.argmax(np.where(valid,ground,-np.inf)),ground.shape)
    lys,lxs=np.where(lake)
    cx=float(lxs.mean()*sx) if lxs.size else 0.0; cz=float(lys.mean()*sz) if lys.size else 0.0
    rx=float((lxs.max()-lxs.min()+1)*sx/2) if lxs.size else 0.0
    rz=float((lys.max()-lys.min()+1)*sz/2) if lys.size else 0.0

    # Geomorphological rock and flora feature extraction
    # Restrict large ridge crags strictly to outer perimeter cliffs so interior meadow hills stay clean
    max_g = max(float(ground[valid].max()), 1e-4) if valid.any() else 1.0
    crag_score = np.where(valid & (perimeter > cliff_margin + 1) & (perimeter < cliff_margin + 16) &
                          (water_depth < 0.01) & (ground > 3.2) & (slope_s > 16.0),
                          (ground / max_g) * 0.65 + np.clip((slope_s - 16.0) / 24.0, 0, 1) * 0.35, 0.0)
    crags = []
    for idx in np.argsort(-crag_score.ravel()):
        r, c = divmod(int(idx), w)
        if crag_score[r, c] <= 0.30: break
        x, z = float(c * sx), float(r * sz)
        if all(np.hypot(x - cx, z - cz) >= 24.0 for cx, cz, cy in crags):
            crags.append((x, z, float(ground[r, c])))
            if len(crags) >= 8: break

    # Place boulders strictly along rocky banks, shoreline benches, and cliff toes (never on open grass hilltops)
    talus_score = np.where(valid & (perimeter > cliff_margin + 3) & (water_depth < 0.01) & (ground > -1.5) & (ground < 3.4) &
                           (((shore > 0.14) & (shore < 0.65)) | ((slope_s > 12.0) & (slope_s < 24.0) & (tpi_s < -2.0))),
                           shore * 0.70 + np.clip((24.0 - slope_s) / 15.0, 0, 1) * 0.30, 0.0)
    boulders = []
    for idx in np.argsort(-talus_score.ravel()):
        r, c = divmod(int(idx), w)
        if talus_score[r, c] <= 0.25: break
        x, z = float(c * sx), float(r * sz)
        if all(np.hypot(x - cx, z - cz) >= 18.0 for cx, cz, cy in boulders):
            boulders.append((x, z, float(ground[r, c])))
            if len(boulders) >= 10: break

    river_score = np.where(valid & (perimeter > cliff_margin) & streams & ~lake & (water_depth > 0.02) & (water_depth < 0.35),
                           water_depth, 0.0)
    river_stones = []
    for idx in np.argsort(-river_score.ravel()):
        r, c = divmod(int(idx), w)
        if river_score[r, c] <= 0.02: break
        x, z = float(c * sx), float(r * sz)
        if all(np.hypot(x - cx, z - cz) >= 9.0 for cx, cz, cy in river_stones):
            river_stones.append((x, z, float(ground[r, c])))
            if len(river_stones) >= 16: break

    flower_score = np.where(valid & (perimeter > cliff_margin + 4) & (water_depth < 0.01) & (ground > 0.0) & (ground < 8.5) & (slope_s < 12.0),
                            (1.0 - slope_s / 12.0) * 0.6 + np.clip((tpi_s + 10.0) / 20.0, 0, 1) * 0.4, 0.0)
    flower_colonies = []
    for idx in np.argsort(-flower_score.ravel()):
        r, c = divmod(int(idx), w)
        if flower_score[r, c] <= 0.35: break
        x, z = float(c * sx), float(r * sz)
        if all(np.hypot(x - cx, z - cz) >= 16.0 for cx, cz, cy in flower_colonies):
            flower_colonies.append((x, z, float(ground[r, c])))
            if len(flower_colonies) >= 14: break

    # Build rock items array: crags, boulder clusters with companions, river stones
    rock_items = []
    # 1. Outer Rim Ridge Crags
    for i, (x, z, y) in enumerate(crags):
        rScale = 0.95 + 0.38 * ((i * 17) % 10) / 10.0
        hScale = 0.90 + 0.42 * ((i * 31) % 10) / 10.0
        rot = float((i * 73) % 360)
        rock_items.append(dict(x=x, y=y, z=z, rScale=rScale, hScale=hScale, rot=rot, archetype=1))
    # 2. Boulder clusters (low-profile, grounded along banks & talus toes)
    for i, (x, z, y) in enumerate(boulders):
        rScale = 0.64 + 0.28 * ((i * 13) % 10) / 10.0
        hScale = 0.36 + 0.18 * ((i * 29) % 10) / 10.0
        rot = float((i * 47) % 360)
        rock_items.append(dict(x=x, y=y, z=z, rScale=rScale, hScale=hScale, rot=rot, archetype=0))
        # 2 companions
        for c_idx in range(2):
            ang = float((i * 120 + c_idx * 160) % 360) * np.pi / 180.0
            dist = 1.35 + 0.80 * c_idx
            cx_pos = x + np.cos(ang) * dist
            cz_pos = z + np.sin(ang) * dist
            c_rScale = rScale * (0.42 + 0.20 * c_idx)
            c_hScale = hScale * (0.42 + 0.20 * c_idx)
            rock_items.append(dict(x=cx_pos, y=y, z=cz_pos, rScale=c_rScale, hScale=c_hScale, rot=rot + 30 * c_idx, archetype=0))
    # 3. River stones
    for i, (x, z, y) in enumerate(river_stones):
        rScale = 0.45 + 0.26 * ((i * 19) % 10) / 10.0
        hScale = 0.26 + 0.16 * ((i * 23) % 10) / 10.0
        rot = float((i * 61) % 360)
        rock_items.append(dict(x=x, y=y, z=z, rScale=rScale, hScale=hScale, rot=rot, archetype=2))

    # Write rock data C header
    with open(path('rock_data.h'), 'w') as f:
        guard = prefix.upper() + '_ROCK_DATA_H'
        f.write(f'// Generated by maps/pipeline/dem_pipeline.py\n#ifndef {guard}\n#define {guard}\n\n')
        f.write('#include "maps/toolkit/map_props.h"\n\n')
        f.write(f'static const MapRockItem {prefix.upper()}_ROCK_ITEMS[] = {{\n')
        for item in rock_items:
            arch_str = 'MAP_ROCK_CRAG' if item['archetype'] == 1 else ('MAP_ROCK_RIVER_STONE' if item['archetype'] == 2 else 'MAP_ROCK_BOULDER')
            f.write(f'    {{{{{item["x"]:.2f}f, {item["y"]:.2f}f, {item["z"]:.2f}f}}, {item["rScale"]:.2f}f, {item["hScale"]:.2f}f, {item["rot"]:.1f}f, {arch_str}}},\n')
        f.write('};\n')
        f.write(f'#define {prefix.upper()}_ROCK_ITEM_COUNT (int)(sizeof({prefix.upper()}_ROCK_ITEMS) / sizeof({prefix.upper()}_ROCK_ITEMS[0]))\n\n')
        f.write(f'#endif // {guard}\n')

    # Write flora data C header (Procedural 3D flower clusters)
    with open(path('flora_data.h'), 'w') as f:
        guard = prefix.upper() + '_FLORA_DATA_H'
        f.write(f'// Generated by maps/pipeline/dem_pipeline.py\n#ifndef {guard}\n#define {guard}\n\n')
        f.write('#include "maps/toolkit/map_props.h"\n\n')
        f.write(f'static const MapFloraFlowerCluster {prefix.upper()}_FLOWER_CLUSTERS[] = {{\n')
        total_flowers = 0
        for i, (x, z, y) in enumerate(flower_colonies):
            count = 135 + 15 * (i % 4)
            total_flowers += count
            species = i % 8
            sp0, sp1, sp2, sp3 = species, (species + 2) % 8, (species + 4) % 8, (species + 6) % 8
            f.write(f'    {{{{{x:.2f}f, {y:.2f}f, {z:.2f}f}}, {{12.5f, 12.5f}}, {count}, {species}, {{{sp0}, {sp1}, {sp2}, {sp3}}}, {{0}}, {{0}}, {{0}}}},\n')
        f.write('};\n')
        f.write(f'#define {prefix.upper()}_FLOWER_CLUSTER_COUNT (int)(sizeof({prefix.upper()}_FLOWER_CLUSTERS) / sizeof({prefix.upper()}_FLOWER_CLUSTERS[0]))\n')
        f.write(f'#define {prefix.upper()}_TOTAL_FLOWERS {total_flowers}\n\n')
        f.write(f'#endif // {guard}\n')

    # Extract 3D physical debris placements
    debris_items = []
    # 1. Fallen leaves
    leaf_cands = np.where(valid & (perimeter > cliff_margin) & (water_depth < 0.01) & (leaf_litter > 0.32))
    if len(leaf_cands[0]) > 0:
        step = max(1, len(leaf_cands[0]) // 400)
        for idx in range(0, len(leaf_cands[0]), step):
            r, c = int(leaf_cands[0][idx]), int(leaf_cands[1][idx])
            lx = float(c * sx) + float((idx * 17) % 7 - 3) * 0.15
            lz = float(r * sz) + float((idx * 31) % 7 - 3) * 0.15
            ly = float(ground[r, c])
            rot = float((idx * 73) % 360)
            scale = 0.85 + 0.35 * ((idx * 11) % 10) / 10.0
            debris_items.append(dict(x=lx, y=ly, z=lz, rot=rot, scale=scale, type=0))
            if len(debris_items) >= 400: break

    # 2. Scree pebbles
    pebble_cands = np.where(valid & (perimeter > cliff_margin) & (scree_gravel > 0.32))
    if len(pebble_cands[0]) > 0:
        step = max(1, len(pebble_cands[0]) // 250)
        for idx in range(0, len(pebble_cands[0]), step):
            r, c = int(pebble_cands[0][idx]), int(pebble_cands[1][idx])
            lx = float(c * sx) + float((idx * 23) % 7 - 3) * 0.15
            lz = float(r * sz) + float((idx * 43) % 7 - 3) * 0.15
            ly = float(ground[r, c])
            rot = float((idx * 67) % 360)
            scale = 0.80 + 0.40 * ((idx * 19) % 10) / 10.0
            debris_items.append(dict(x=lx, y=ly, z=lz, rot=rot, scale=scale, type=1))
            if len(debris_items) >= 650: break

    # 3. Forest twigs
    twig_cands = np.where(valid & (perimeter > cliff_margin) & (water_depth < 0.01) & (twigs > 0.28))
    if len(twig_cands[0]) > 0:
        step = max(1, len(twig_cands[0]) // 150)
        for idx in range(0, len(twig_cands[0]), step):
            r, c = int(twig_cands[0][idx]), int(twig_cands[1][idx])
            lx = float(c * sx) + float((idx * 19) % 7 - 3) * 0.15
            lz = float(r * sz) + float((idx * 37) % 7 - 3) * 0.15
            ly = float(ground[r, c])
            rot = float((idx * 89) % 360)
            scale = 0.90 + 0.30 * ((idx * 13) % 10) / 10.0
            debris_items.append(dict(x=lx, y=ly, z=lz, rot=rot, scale=scale, type=2))
            if len(debris_items) >= 800: break

    # Write debris data C header
    with open(path('debris_data.h'), 'w') as f:
        guard = prefix.upper() + '_DEBRIS_DATA_H'
        f.write(f'// Generated by maps/pipeline/dem_pipeline.py\n#ifndef {guard}\n#define {guard}\n\n')
        f.write('#include "maps/toolkit/map_props.h"\n\n')
        f.write(f'static const MapDebrisPlacement {prefix.upper()}_DEBRIS_ITEMS[] = {{\n')
        for item in debris_items:
            f.write(f'    {{{{{item["x"]:.2f}f, {item["y"]:.2f}f, {item["z"]:.2f}f}}, {item["rot"]:.1f}f, {item["scale"]:.2f}f, {{255, 255, 255, 255}}, {item["type"]}}},\n')
        f.write('};\n')
        f.write(f'#define {prefix.upper()}_DEBRIS_ITEM_COUNT (int)(sizeof({prefix.upper()}_DEBRIS_ITEMS) / sizeof({prefix.upper()}_DEBRIS_ITEMS[0]))\n\n')
        f.write(f'#endif // {guard}\n')

    # Save JSON files
    with open(path('rock_placements.json'), 'w') as f: json.dump(rock_items, f, indent=2)
    with open(path('flora_clusters.json'), 'w') as f: json.dump([dict(x=x, z=z, y=y) for x, z, y in flower_colonies], f, indent=2)
    with open(path('debris_placements.json'), 'w') as f: json.dump(debris_items, f, indent=2)

    # Compatibility metadata; runtime uses the water raster/mesh, not these bounds.
    values=dict(MAP_WIDTH=width,MAP_DEPTH=depth,HEIGHT_RANGE=height_range,CLIFF_DEPTH=cliff_depth,
                OUTLET_X=outlet[1]*sx,OUTLET_Z=outlet[0]*sz,PEAK_X=peak[1]*sx,PEAK_Z=peak[0]*sz,
                LAKE_CENTER_X=cx,LAKE_CENTER_Z=cz,LAKE_RADIUS_X=rx,LAKE_RADIUS_Z=rz,LAKE_WATER_Y=lake_y,
                LAKE_BED_MIN_Y=float(ground[lake].min()) if lake.any() else 0)
    with open(path('generated_meta.h'),'w') as f:
        guard=prefix.upper()+'_GENERATED_META_H'; f.write(f'// Generated by maps/pipeline/dem_pipeline.py\n#ifndef {guard}\n#define {guard}\n')
        for k,v in values.items(): f.write(f'#define {prefix.upper()}_{k} {v:.6f}f\n')
        f.write(f'#include "{prefix}_rock_data.h"\n')
        f.write(f'#include "{prefix}_flora_data.h"\n')
        f.write(f'#include "{prefix}_debris_data.h"\n')
        f.write(f'#endif // {guard}\n')
    with open(dem_path,'rb') as source_file: digest=hashlib.sha256(source_file.read()).hexdigest()
    manifest=dict(schema=2,source=dict(geo,path=os.path.abspath(dem_path),sha256=digest,
        crop_pixels=[int(xx.min()),int(yy.min()),int(source.shape[1]),int(source.shape[0])]),
        game=dict(width_m=width,depth_m=depth,grid=[h,w],height_range_m=height_range,cliff_depth_m=cliff_depth,
                  horizontal_scale=scale,vertical_transform='normalized elevation power 1.12',water_triangles=triangles),
        inference=dict(lake=lake_origin,lake_source_level_m=lake_level,lake_source_area_m2=float(lake_source.sum()*px*py),
                       geology='unknown; exposed substrate and deposition are terrain proxies only',
                       biology='habitat suitability only; no surveyed species or land-cover input',
                       drainage='D8 after priority flood; raster boundary outlets may truncate upstream area',
                       landform_classes={'0':'outside','1':'valley','2':'slope or plain','3':'ridge','4':'candidate lake'},
                       bathymetry='procedural game-scale reconstruction; not measured lake depth'),
        source_statistics=dict(elevation_min_m=lo,elevation_max_m=hi,source_outlets=int(np.count_nonzero(valid_source & (source_dir<0))),
            main_catchment_area_m2=float(main_basin.sum()*px*py),
            main_outlet_source_pixel=[int(main_outlet[1]+xx.min()),int(main_outlet[0]+yy.min())]),
        parameters=dict(max_dim=max_dim,height_range=height_range,cliff_depth=cliff_depth,cliff_margin=cliff_margin,
                        resolution_scale=res_scale,stream_ratio=stream_ratio),
        metrics=dict(bake_seconds=time.perf_counter()-started,wet_nodes=int(wet.sum()),lake_nodes=int(lake.sum())))
    with open(path('manifest.json'),'w') as f: json.dump(manifest,f,indent=2,allow_nan=False)
    print(f'[DEM] {w}x{h} nodes; {triangles} water triangles; {wet.sum()} wet nodes; {time.perf_counter()-started:.2f}s',flush=True)
    return manifest


if __name__=='__main__':
    parser=argparse.ArgumentParser(description='Projected DEM to terrain, drainage and inferred habitat')
    parser.add_argument('dem_path')
    parser.add_argument('--max-dim',type=float,default=300)
    parser.add_argument('--height-range',type=float,default=16)
    parser.add_argument('--cliff-depth',type=float,default=8)
    parser.add_argument('--cliff-margin',type=float,default=8.5)
    parser.add_argument('--resolution-scale',type=float,default=1.0)
    parser.add_argument('--stream-ratio',type=float,default=0.01)
    parser.add_argument('--lake-level',type=float,help='Candidate lake source elevation in metres; default detects a lowland plateau')
    parser.add_argument('--prefix',default='song_quao')
    parser.add_argument('--out-dir',default='assets/maps/song_quao')
    args=parser.parse_args()
    process_dem(args.dem_path,args.max_dim,args.height_range,args.cliff_depth,args.cliff_margin,
                args.resolution_scale,args.stream_ratio,args.prefix,args.out_dir,args.lake_level)
