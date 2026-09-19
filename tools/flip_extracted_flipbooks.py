import os
from PIL import Image

# Mapping of file name to (cols, rows)
# If not explicitly specified:
# - If in 'Sprites/' or 'BakedAtlas/': default to (8, 8)
# - If in 'Fog/' or 'Decals/' or 'Flares/': default to (1, 1)

GRID_CONFIG = {
    "T_ExplosionWide_EOO.png": (8, 16),
    "T_ExplosionWide_Normals.png": (8, 16),
    "T_MuzzleFlash.png": (4, 1),
    "T_MuzzleFlash_Side.png": (2, 1),
    "T_MuzzleFlash_Sphere.png": (1, 1),
    "T_ThinSmoke_FX.png": (8, 8),
    "T_SmokeTrail_EOO_Loop.png": (1, 16),
    "T_SmokeTrail_Normals_Loop.png": (1, 16),
    "CutoutMask_8x8.png": (8, 8),
    "BulletHole.png": (1, 1),
    "BulletHole_Normals.png": (1, 1),
    "ExplosionDecalMasks.png": (1, 1),
    "LensFlare_01.png": (1, 1),
    "LensFlare_02.png": (1, 1),
}

EXCLUDE_FILES = {
    "NumberGrid.png", # Đã đúng chiều số 1,2,3...
}

def flip_flipbook_y(img_path: str, cols: int, rows: int):
    img = Image.open(img_path)
    w, h = img.size
    cell_w = w // cols
    cell_h = h // rows

    out = Image.new("RGBA", (w, h))
    for r in range(rows):
        for c in range(cols):
            box = (c * cell_w, r * cell_h, (c + 1) * cell_w, (r + 1) * cell_h)
            cell = img.crop(box)
            cell_flipped = cell.transpose(Image.FLIP_TOP_BOTTOM)
            out.paste(cell_flipped, box)
    out.save(img_path)
    print(f"[FLIPPED] {os.path.basename(img_path)} (lưới {cols}x{rows})")

def main():
    root_dir = "/Users/mth2610/Desktop/c_games/wuxing_skills/extracted_flipbooks"
    count = 0

    for folder, _, files in os.walk(root_dir):
        cat = os.path.basename(folder)
        for f in files:
            if not f.endswith(".png") or f in EXCLUDE_FILES:
                continue

            filepath = os.path.join(folder, f)
            if f in GRID_CONFIG:
                cols, rows = GRID_CONFIG[f]
            elif cat in ("Sprites", "BakedAtlas"):
                cols, rows = 8, 8
            else:
                cols, rows = 1, 1

            flip_flipbook_y(filepath, cols, rows)
            count += 1

    print(f"\n>>> Đã lật dọc thành công {count} ảnh flipbook theo đúng chiều trọng lực! <<<")

if __name__ == '__main__':
    main()
