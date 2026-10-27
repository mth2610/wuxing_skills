#!/usr/bin/env python3
"""
Generate high-fidelity, seamless PBR texture sets (Diffuse, Normal, Roughness)
for 8 rock types: Granite, Basalt, Sandstone, Limestone, Obsidian, Slate,
River Stone, and Scree.
Pure Python + NumPy + Zlib, zero external image libraries needed.
"""

import os
import zlib
import struct
import numpy as np

def write_png(filename, img_array, color_type=2, bit_depth=8):
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

    compressed = zlib.compress(bytes(raw_data), level=6)
    idat = png_chunk(b"IDAT", compressed)
    iend = png_chunk(b"IEND", b"")

    with open(filename, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(ihdr)
        f.write(idat)
        f.write(iend)

def make_tileable_noise(size, octaves=4, seed=42):
    rng = np.random.RandomState(seed)
    noise = np.zeros((size, size), dtype=np.float32)
    freq = 1.0
    amp = 1.0
    total_amp = 0.0
    for _ in range(octaves):
        grid_size = max(2, int(freq * 8))
        rand_grid = rng.randn(grid_size, grid_size).astype(np.float32)
        # Upsample with periodic wrapping
        y = np.linspace(0, grid_size, size, endpoint=False)
        x = np.linspace(0, grid_size, size, endpoint=False)
        
        y0 = np.floor(y).astype(int) % grid_size
        y1 = (y0 + 1) % grid_size
        fy = (y - np.floor(y))[:, None]
        fy = fy * fy * (3.0 - 2.0 * fy) # smoothstep
        
        x0 = np.floor(x).astype(int) % grid_size
        x1 = (x0 + 1) % grid_size
        fx = (x - np.floor(x))[None, :]
        fx = fx * fx * (3.0 - 2.0 * fx)
        
        top = rand_grid[y0[:, None], x0[None, :]] * (1.0 - fx) + rand_grid[y0[:, None], x1[None, :]] * fx
        bot = rand_grid[y1[:, None], x0[None, :]] * (1.0 - fx) + rand_grid[y1[:, None], x1[None, :]] * fx
        layer = top * (1.0 - fy) + bot * fy
        
        noise += layer * amp
        total_amp += amp
        amp *= 0.5
        freq *= 2.0
    return (noise / total_amp)

def height_to_normal(h, strength=4.0):
    # h is 2D tileable array
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5 * strength
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5 * strength
    dz = np.ones_like(h)
    
    len_sq = dx * dx + dy * dy + dz * dz
    inv_len = 1.0 / np.sqrt(np.maximum(len_sq, 1e-8))
    
    nx = -dx * inv_len
    ny = -dy * inv_len
    nz = dz * inv_len
    
    r = np.clip((nx * 0.5 + 0.5) * 255, 0, 255).astype(np.uint8)
    g = np.clip((ny * 0.5 + 0.5) * 255, 0, 255).astype(np.uint8)
    b = np.clip((nz * 0.5 + 0.5) * 255, 0, 255).astype(np.uint8)
    return np.stack([r, g, b], axis=-1)

def generate_textures(out_dir, size=512):
    os.makedirs(out_dir, exist_ok=True)
    
    # 1. GRANITE (Coarse crystalline matrix with pink feldspar, smoky quartz, black biotite mica)
    n1 = make_tileable_noise(size, 5, seed=101)
    n2 = make_tileable_noise(size, 6, seed=102)
    n_grain = make_tileable_noise(size, 7, seed=103)
    
    # Crystals
    feldspar = np.clip(n1 * 1.5 - 0.2, 0, 1) # pinkish grains
    mica = np.clip(n2 * 2.5 - 1.2, 0, 1)     # dark mica specks
    quartz = 1.0 - np.maximum(feldspar, mica) # quartz groundmass
    
    # Base pinkish-gray granite albedo
    r_granite = 175 * quartz + 215 * feldspar + 45 * mica + n_grain * 15
    g_granite = 168 * quartz + 170 * feldspar + 48 * mica + n_grain * 15
    b_granite = 160 * quartz + 155 * feldspar + 55 * mica + n_grain * 15
    diff_granite = np.clip(np.stack([r_granite, g_granite, b_granite], axis=-1), 0, 255).astype(np.uint8)
    
    h_granite = n1 * 0.4 + n2 * 0.3 + n_grain * 0.3
    norm_granite = height_to_normal(h_granite, strength=5.5)
    
    # Quartz has lower roughness (~0.45), feldspar medium (~0.75), mica high cleavage (~0.85)
    rough_granite = np.clip((0.72 + 0.15 * n1 - 0.25 * quartz + 0.10 * mica) * 255, 0, 255).astype(np.uint8)
    rough_granite_rgb = np.stack([rough_granite, rough_granite, rough_granite], axis=-1)
    
    write_png(os.path.join(out_dir, "granite_diffuse.png"), diff_granite)
    write_png(os.path.join(out_dir, "granite_normal.png"), norm_granite)
    write_png(os.path.join(out_dir, "granite_roughness.png"), rough_granite_rgb)
    
    # 2. BASALT (Dense volcanic aphanitic charcoal, micro-pores, fine cooling micro-joints)
    nb1 = make_tileable_noise(size, 4, seed=201)
    nb2 = make_tileable_noise(size, 6, seed=202)
    vesicles = np.clip(-nb2 * 2.8 + 1.2, 0, 1) # small dark pore pits
    
    base_b = 68 + 14 * nb1 - 35 * vesicles
    diff_basalt = np.clip(np.stack([base_b * 0.98, base_b * 1.0, base_b * 1.06], axis=-1), 0, 255).astype(np.uint8)
    
    h_basalt = nb1 * 0.25 + nb2 * 0.35 - vesicles * 0.70
    norm_basalt = height_to_normal(h_basalt, strength=4.0)
    
    rough_basalt = np.clip((0.78 + 0.12 * nb1 + 0.15 * vesicles) * 255, 0, 255).astype(np.uint8)
    rough_basalt_rgb = np.stack([rough_basalt, rough_basalt, rough_basalt], axis=-1)
    
    write_png(os.path.join(out_dir, "basalt_diffuse.png"), diff_basalt)
    write_png(os.path.join(out_dir, "basalt_normal.png"), norm_basalt)
    write_png(os.path.join(out_dir, "basalt_roughness.png"), rough_basalt_rgb)
    
    # 3. SANDSTONE (Warm ochre/buff sedimentary horizontal bedding striations)
    y_coord = np.linspace(0, 2.0 * np.pi * 14.0, size, endpoint=False)[:, None]
    ns1 = make_tileable_noise(size, 4, seed=301)
    ns2 = make_tileable_noise(size, 6, seed=302)
    
    strata_wave = np.sin(y_coord + ns1 * 2.2) * 0.5 + 0.5
    fine_strata = np.sin(y_coord * 3.0 + ns2 * 1.5) * 0.5 + 0.5
    
    # Warm buff to golden-sienna bands
    r_sand = 205 + 28 * strata_wave + 12 * fine_strata + ns2 * 10
    g_sand = 162 + 25 * strata_wave + 8 * fine_strata + ns2 * 10
    b_sand = 118 + 18 * strata_wave + 5 * fine_strata + ns2 * 10
    diff_sandstone = np.clip(np.stack([r_sand, g_sand, b_sand], axis=-1), 0, 255).astype(np.uint8)
    
    h_sandstone = strata_wave * 0.55 + fine_strata * 0.25 + ns2 * 0.20
    norm_sandstone = height_to_normal(h_sandstone, strength=4.5)
    
    rough_sandstone = np.clip((0.82 + 0.08 * fine_strata - 0.06 * strata_wave) * 255, 0, 255).astype(np.uint8)
    rough_sandstone_rgb = np.stack([rough_sandstone, rough_sandstone, rough_sandstone], axis=-1)
    
    write_png(os.path.join(out_dir, "sandstone_diffuse.png"), diff_sandstone)
    write_png(os.path.join(out_dir, "sandstone_normal.png"), norm_sandstone)
    write_png(os.path.join(out_dir, "sandstone_roughness.png"), rough_sandstone_rgb)
    
    # 4. LIMESTONE (Pale ivory/bone-white chalky karst, subtle dissolution micro-etching)
    nl1 = make_tileable_noise(size, 4, seed=401)
    nl2 = make_tileable_noise(size, 6, seed=402)
    karren_groove = np.clip(np.abs(nl1) * 2.0 - 0.4, 0, 1)
    
    base_l = 215 + 22 * nl2 - 28 * karren_groove
    diff_limestone = np.clip(np.stack([base_l * 1.01, base_l * 0.99, base_l * 0.96], axis=-1), 0, 255).astype(np.uint8)
    
    h_limestone = nl2 * 0.35 - karren_groove * 0.65
    norm_limestone = height_to_normal(h_limestone, strength=3.8)
    
    rough_limestone = np.clip((0.72 + 0.16 * karren_groove) * 255, 0, 255).astype(np.uint8)
    rough_limestone_rgb = np.stack([rough_limestone, rough_limestone, rough_limestone], axis=-1)
    
    write_png(os.path.join(out_dir, "limestone_diffuse.png"), diff_limestone)
    write_png(os.path.join(out_dir, "limestone_normal.png"), norm_limestone)
    write_png(os.path.join(out_dir, "limestone_roughness.png"), rough_limestone_rgb)
    
    # 5. OBSIDIAN (Deep lustrous volcanic glass, conchoidal curved scallops, ultra-low roughness)
    no1 = make_tileable_noise(size, 3, seed=501)
    no2 = make_tileable_noise(size, 5, seed=502)
    scallop_rings = np.sin(np.sqrt(no1**2 + no2**2) * 24.0) * 0.5 + 0.5
    
    base_obs = 26 + 12 * scallop_rings + no2 * 6
    diff_obsidian = np.clip(np.stack([base_obs * 0.96, base_obs * 1.0, base_obs * 1.08], axis=-1), 0, 255).astype(np.uint8)
    
    h_obsidian = scallop_rings * 0.60 + no1 * 0.30
    norm_obsidian = height_to_normal(h_obsidian, strength=5.0)
    
    # Very shiny, glass-like (roughness 0.09 - 0.16)
    rough_obsidian = np.clip((0.11 + 0.06 * scallop_rings) * 255, 0, 255).astype(np.uint8)
    rough_obsidian_rgb = np.stack([rough_obsidian, rough_obsidian, rough_obsidian], axis=-1)
    
    write_png(os.path.join(out_dir, "obsidian_diffuse.png"), diff_obsidian)
    write_png(os.path.join(out_dir, "obsidian_normal.png"), norm_obsidian)
    write_png(os.path.join(out_dir, "obsidian_roughness.png"), rough_obsidian_rgb)
    
    # 6. SLATE (Bluish anthracite, fine parallel foliation lines, silky planar sheen)
    nsl1 = make_tileable_noise(size, 4, seed=601)
    nsl2 = make_tileable_noise(size, 7, seed=602)
    # Tilted foliation lines
    foliation = np.sin(y_coord * 2.2 + ns1 * 1.8) * 0.5 + 0.5
    
    base_sl = 105 + 18 * foliation + nsl2 * 8
    diff_slate = np.clip(np.stack([base_sl * 0.92, base_sl * 0.98, base_sl * 1.08], axis=-1), 0, 255).astype(np.uint8)
    
    h_slate = foliation * 0.55 + nsl2 * 0.25
    norm_slate = height_to_normal(h_slate, strength=4.2)
    
    rough_slate = np.clip((0.46 + 0.14 * foliation) * 255, 0, 255).astype(np.uint8)
    rough_slate_rgb = np.stack([rough_slate, rough_slate, rough_slate], axis=-1)
    
    write_png(os.path.join(out_dir, "slate_diffuse.png"), diff_slate)
    write_png(os.path.join(out_dir, "slate_normal.png"), norm_slate)
    write_png(os.path.join(out_dir, "slate_roughness.png"), rough_slate_rgb)
    
    # 7. RIVER STONE (Smooth water-tumbled alluvial stone with subtle algae/wet sheen)
    nr1 = make_tileable_noise(size, 3, seed=701)
    nr2 = make_tileable_noise(size, 5, seed=702)
    moss_vein = np.clip(nr2 * 1.8 - 0.6, 0, 1)
    
    base_r = 160 + 20 * nr1
    r_river = base_r * 0.96 - moss_vein * 24
    g_river = base_r * 1.02 + moss_vein * 18
    b_river = base_r * 0.98 - moss_vein * 30
    diff_river = np.clip(np.stack([r_river, g_river, b_river], axis=-1), 0, 255).astype(np.uint8)
    
    h_river = nr1 * 0.40 + nr2 * 0.20
    norm_river = height_to_normal(h_river, strength=2.5) # smooth water-worn surface
    
    rough_river = np.clip((0.36 + 0.18 * nr1) * 255, 0, 255).astype(np.uint8)
    rough_river_rgb = np.stack([rough_river, rough_river, rough_river], axis=-1)
    
    write_png(os.path.join(out_dir, "river_stone_diffuse.png"), diff_river)
    write_png(os.path.join(out_dir, "river_stone_normal.png"), norm_river)
    write_png(os.path.join(out_dir, "river_stone_roughness.png"), rough_river_rgb)
    
    # 8. SCREE / TALUS (Angular broken chips and gravel mix)
    nsc1 = make_tileable_noise(size, 6, seed=801)
    nsc2 = make_tileable_noise(size, 7, seed=802)
    chips = np.clip(nsc1 * 2.2 - 0.5, 0, 1)
    
    base_sc = 145 + 32 * chips + nsc2 * 12
    diff_scree = np.clip(np.stack([base_sc * 1.02, base_sc * 0.99, base_sc * 0.96], axis=-1), 0, 255).astype(np.uint8)
    
    h_scree = chips * 0.65 + nsc2 * 0.35
    norm_scree = height_to_normal(h_scree, strength=6.0) # sharp fractured facets
    
    rough_scree = np.clip((0.82 + 0.10 * nsc1) * 255, 0, 255).astype(np.uint8)
    rough_scree_rgb = np.stack([rough_scree, rough_scree, rough_scree], axis=-1)
    
    write_png(os.path.join(out_dir, "scree_diffuse.png"), diff_scree)
    write_png(os.path.join(out_dir, "scree_normal.png"), norm_scree)
    write_png(os.path.join(out_dir, "scree_roughness.png"), rough_scree_rgb)
    
    print(f"Successfully generated 8 PBR rock texture sets in '{out_dir}'.")

if __name__ == "__main__":
    generate_textures("assets/textures/rocks", 512)
