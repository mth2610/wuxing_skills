"""
process_decal_textures.py — Auto-process raw decal textures for Wuxing Skills engine.

Usage:
    python scripts/process_decal_textures.py <input_dir> [--output <output_dir>] [--size 256] [--preview]

    input_dir : folder chứa raw images (PNG/JPG/WEBP, bất kỳ tên, bất kỳ size)
    --output  : output dir, mặc định assets/textures/
    --size    : output size, mặc định 256
    --preview : mở preview từng ảnh sau khi xử lý (debug)

Pipeline mỗi file:
    1. Load ảnh, convert sang RGBA
    2. Remove background (chroma key black/white, hoặc rembg nếu cài)
    3. Crop tight quanh content, pad về hình vuông, resize về --size
    4. Normalize brightness alpha channel (clamp ≥ 8 để tránh mất detail mỏng)
    5. Rename theo DECAL_EXPECTED_MAP nếu tên file khớp keyword
    6. Validate: cảnh báo nếu content quá nhỏ, không centered, alpha trống
    7. Ghi PNG sang output_dir

Dependencies:
    pip install Pillow
    pip install rembg   (optional, dùng AI background removal nếu ảnh phức tạp)
"""

import argparse
import os
import sys
from pathlib import Path

try:
    from PIL import Image, ImageFilter, ImageOps
except ImportError:
    print("[ERROR] Pillow chưa được cài. Chạy: pip install Pillow")
    sys.exit(1)

# ---------------------------------------------------------------------------
# Danh sách 13 decal cần thiết — script sẽ rename nếu keyword khớp filename
# ---------------------------------------------------------------------------
DECAL_EXPECTED = [
    "decal_impact_ring.png",
    "decal_glow_circle.png",
    "decal_shadow_blob.png",
    "decal_water_ripple.png",
    "decal_water_puddle.png",
    "decal_wood_roots.png",
    "decal_fire_scorch.png",
    "decal_earth_crater.png",
    "decal_earth_rune.png",
    "decal_metal_slash.png",
    "decal_metal_rune.png",
    "decal_taiji_symbol.png",
    "decal_taiji_rune.png",
]

# Keyword → target filename (lowercase match)
DECAL_KEYWORD_MAP = {
    "impact_ring":    "decal_impact_ring.png",
    "impact ring":    "decal_impact_ring.png",
    "glow_circle":    "decal_glow_circle.png",
    "glow circle":    "decal_glow_circle.png",
    "shadow_blob":    "decal_shadow_blob.png",
    "shadow blob":    "decal_shadow_blob.png",
    "water_ripple":   "decal_water_ripple.png",
    "water ripple":   "decal_water_ripple.png",
    "water_puddle":   "decal_water_puddle.png",
    "water puddle":   "decal_water_puddle.png",
    "wood_roots":     "decal_wood_roots.png",
    "wood roots":     "decal_wood_roots.png",
    "fire_scorch":    "decal_fire_scorch.png",
    "fire scorch":    "decal_fire_scorch.png",
    "earth_crater":   "decal_earth_crater.png",
    "earth crater":   "decal_earth_crater.png",
    "earth_rune":     "decal_earth_rune.png",
    "earth rune":     "decal_earth_rune.png",
    "metal_slash":    "decal_metal_slash.png",
    "metal slash":    "decal_metal_slash.png",
    "metal_rune":     "decal_metal_rune.png",
    "metal rune":     "decal_metal_rune.png",
    "taiji_symbol":   "decal_taiji_symbol.png",
    "taiji symbol":   "decal_taiji_symbol.png",
    "yin_yang":       "decal_taiji_symbol.png",
    "taiji_rune":     "decal_taiji_rune.png",
    "taiji rune":     "decal_taiji_rune.png",
}

# Các file nên dùng BLEND_ADDITIVE → artwork cần đủ sáng (white-based)
ADDITIVE_FILES = {
    "decal_impact_ring.png",
    "decal_glow_circle.png",
    "decal_water_ripple.png",
    "decal_earth_rune.png",
    "decal_metal_rune.png",
    "decal_taiji_symbol.png",
    "decal_taiji_rune.png",
}

SUPPORTED_EXTS = {".png", ".jpg", ".jpeg", ".webp", ".bmp", ".tga"}

# ---------------------------------------------------------------------------

def try_rembg(img: Image.Image) -> Image.Image | None:
    """Dùng rembg AI nếu có cài, trả None nếu không."""
    try:
        from rembg import remove as rembg_remove
        import io
        buf = io.BytesIO()
        img.save(buf, format="PNG")
        out_bytes = rembg_remove(buf.getvalue())
        return Image.open(io.BytesIO(out_bytes)).convert("RGBA")
    except ImportError:
        return None


def remove_background_simple(img: Image.Image, threshold: int = 20) -> Image.Image:
    """
    Xóa background đơn giản: pixel nào gần đen hoặc gần trắng thuần → transparent.
    Phù hợp với ảnh AI gen có nền đen hoặc trắng đồng nhất.
    """
    img = img.convert("RGBA")
    pixels = img.load()
    w, h = img.size

    # Sample 4 góc để đoán màu background
    corners = [
        pixels[0, 0], pixels[w - 1, 0],
        pixels[0, h - 1], pixels[w - 1, h - 1],
    ]
    avg_r = sum(c[0] for c in corners) // 4
    avg_g = sum(c[1] for c in corners) // 4
    avg_b = sum(c[2] for c in corners) // 4

    is_dark_bg  = (avg_r < 40 and avg_g < 40 and avg_b < 40)
    is_light_bg = (avg_r > 215 and avg_g > 215 and avg_b > 215)

    if not is_dark_bg and not is_light_bg:
        # Không detect được bg rõ ràng — giữ nguyên, để caller dùng rembg
        return img

    for y in range(h):
        for x in range(w):
            r, g, b, a = pixels[x, y]
            if is_dark_bg:
                brightness = (r + g + b) / 3
                if brightness < threshold:
                    pixels[x, y] = (r, g, b, 0)
            else:  # light bg
                darkness = 255 - (r + g + b) / 3
                if darkness < threshold:
                    pixels[x, y] = (r, g, b, 0)
    return img


def crop_and_center(img: Image.Image, target_size: int, padding_ratio: float = 0.08) -> Image.Image:
    """
    Crop tight quanh vùng có alpha > 0, thêm padding, scale vừa target_size.
    Kết quả luôn là ảnh vuông target_size × target_size.
    """
    bbox = img.getbbox()  # (left, upper, right, lower) của vùng non-transparent
    if bbox is None:
        # Ảnh trống hoàn toàn — trả về canvas trong suốt
        return Image.new("RGBA", (target_size, target_size), (0, 0, 0, 0))

    cropped = img.crop(bbox)
    cw, ch = cropped.size
    side = max(cw, ch)

    # Tạo canvas vuông
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    paste_x = (side - cw) // 2
    paste_y = (side - ch) // 2
    square.paste(cropped, (paste_x, paste_y), cropped)

    # Thêm padding rìa để tránh bị clip
    pad_px = int(side * padding_ratio)
    padded_size = side + pad_px * 2
    padded = Image.new("RGBA", (padded_size, padded_size), (0, 0, 0, 0))
    padded.paste(square, (pad_px, pad_px), square)

    # Resize về target
    result = padded.resize((target_size, target_size), Image.LANCZOS)
    return result


def normalize_alpha(img: Image.Image, min_alpha: int = 8) -> Image.Image:
    """
    Clamp alpha: pixel có alpha > 0 nhưng < min_alpha → nâng lên min_alpha
    để tránh mất detail đường mỏng khi render.
    """
    r, g, b, a = img.split()
    pixels = list(a.getdata())
    pixels = [min_alpha if 0 < p < min_alpha else p for p in pixels]
    a.putdata(pixels)
    return Image.merge("RGBA", (r, g, b, a))


def boost_brightness_for_additive(img: Image.Image, target_filename: str) -> Image.Image:
    """
    Với BLEND_ADDITIVE textures: nếu artwork quá tối, nâng RGB lên
    vì additive blend nhân màu × tint — artwork tối sẽ bị tối thêm.
    """
    if target_filename not in ADDITIVE_FILES:
        return img

    r, g, b, a = img.split()
    import statistics
    r_vals = [v for v, alpha in zip(r.getdata(), a.getdata()) if alpha > 30]
    if not r_vals:
        return img

    median_brightness = statistics.median(r_vals)
    if median_brightness >= 160:
        return img  # đã đủ sáng

    # Scale RGB lên tỷ lệ để median → ~200
    factor = min(200.0 / max(median_brightness, 1), 3.0)
    boosted = img.point(lambda p: min(int(p * factor), 255))
    # Giữ lại alpha channel gốc (point áp lên cả alpha nếu dùng trực tiếp)
    rb, gb, bb, _ = boosted.split()
    return Image.merge("RGBA", (rb, gb, bb, a))


def resolve_target_name(src_path: Path) -> str:
    """
    Tìm tên decal target từ filename. Ưu tiên exact match trước, sau đó keyword.
    Trả về None nếu không khớp → giữ tên gốc (với prefix decal_ nếu chưa có).
    """
    stem = src_path.stem.lower().replace("-", "_")

    # Exact match với expected list
    candidate = f"decal_{stem}.png" if not stem.startswith("decal_") else f"{stem}.png"
    if candidate in DECAL_EXPECTED:
        return candidate

    # Keyword match
    for keyword, target in DECAL_KEYWORD_MAP.items():
        if keyword.replace(" ", "_") in stem or keyword.replace("_", " ") in stem.replace("_", " "):
            return target

    # Fallback: giữ tên gốc với prefix
    if not stem.startswith("decal_"):
        return f"decal_{stem}.png"
    return f"{stem}.png"


def validate(img: Image.Image, target_name: str) -> list[str]:
    """Trả danh sách cảnh báo (không block xử lý)."""
    warnings = []
    _, _, _, a = img.split()
    alpha_data = list(a.getdata())
    non_transparent = sum(1 for v in alpha_data if v > 30)
    total = len(alpha_data)
    coverage = non_transparent / total

    if coverage < 0.02:
        warnings.append(f"Alpha coverage {coverage:.1%} — ảnh gần như trống, kiểm tra lại background removal")
    if coverage > 0.85:
        warnings.append(f"Alpha coverage {coverage:.1%} — background có thể chưa được xóa hết")

    if target_name in ADDITIVE_FILES:
        rgb_vals = [r for r, alpha in zip(img.split()[0].getdata(), alpha_data) if alpha > 30]
        if rgb_vals:
            avg_r = sum(rgb_vals) / len(rgb_vals)
            if avg_r < 80:
                warnings.append(f"Brightness thấp ({avg_r:.0f}/255) cho BLEND_ADDITIVE texture — artwork có thể quá tối")

    return warnings


def process_file(src: Path, output_dir: Path, target_size: int, use_rembg: bool, preview: bool) -> bool:
    print(f"\n  → {src.name}")

    try:
        img = Image.open(src).convert("RGBA")
    except Exception as e:
        print(f"     [ERROR] Không đọc được file: {e}")
        return False

    target_name = resolve_target_name(src)
    print(f"     target : {target_name}")

    # Background removal
    result = None
    if use_rembg:
        result = try_rembg(img)
        if result:
            print("     bg_rm  : rembg AI")

    if result is None:
        result = remove_background_simple(img)
        _, _, _, a = result.split()
        non_transparent = sum(1 for v in a.getdata() if v > 30)
        if non_transparent / (result.size[0] * result.size[1]) < 0.01:
            # Cơ chế đơn giản xóa hết ảnh — ảnh có nền phức tạp
            print("     bg_rm  : simple (cảnh báo: kết quả có thể cần kiểm tra)")
            result = img  # fallback: giữ nguyên, user tự xóa bg
        else:
            print("     bg_rm  : simple (black/white chroma key)")

    # Crop, center, resize
    result = crop_and_center(result, target_size)

    # Normalize alpha
    result = normalize_alpha(result, min_alpha=8)

    # Boost brightness cho additive textures
    result = boost_brightness_for_additive(result, target_name)

    # Validate
    warnings = validate(result, target_name)
    for w in warnings:
        print(f"     [WARN] {w}")

    if preview:
        result.show(title=target_name)

    # Ghi file
    out_path = output_dir / target_name
    result.save(out_path, "PNG", optimize=False)
    print(f"     saved  : {out_path}  ({result.size[0]}×{result.size[1]})")
    return True


def main():
    parser = argparse.ArgumentParser(description="Process raw decal textures for Wuxing Skills engine")
    parser.add_argument("input_dir", help="Folder chứa raw images")
    parser.add_argument("--output", default=None, help="Output dir (mặc định: assets/textures/)")
    parser.add_argument("--size", type=int, default=256, help="Output size px (mặc định: 256)")
    parser.add_argument("--rembg", action="store_true", help="Dùng rembg AI để xóa background")
    parser.add_argument("--preview", action="store_true", help="Mở preview sau khi xử lý mỗi ảnh")
    args = parser.parse_args()

    # Resolve paths từ project root (script nằm trong scripts/)
    script_dir = Path(__file__).parent
    project_root = script_dir.parent

    input_dir = Path(args.input_dir)
    if not input_dir.is_absolute():
        input_dir = project_root / input_dir
    if not input_dir.exists():
        print(f"[ERROR] Input dir không tồn tại: {input_dir}")
        sys.exit(1)

    output_dir = Path(args.output) if args.output else project_root / "assets" / "textures"
    if not output_dir.is_absolute():
        output_dir = project_root / output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    # Tìm tất cả ảnh
    sources = [f for f in sorted(input_dir.iterdir()) if f.suffix.lower() in SUPPORTED_EXTS]
    if not sources:
        print(f"[ERROR] Không tìm thấy ảnh nào trong {input_dir}")
        sys.exit(1)

    print(f"[INFO] Tìm thấy {len(sources)} file trong {input_dir}")
    print(f"[INFO] Output → {output_dir}  (size: {args.size}×{args.size})")
    if args.rembg:
        print("[INFO] Dùng rembg AI background removal")

    ok = 0
    for src in sources:
        if process_file(src, output_dir, args.size, args.rembg, args.preview):
            ok += 1

    print(f"\n[DONE] Xử lý {ok}/{len(sources)} file thành công.")

    # Kiểm tra còn thiếu file nào
    produced = {f.name for f in output_dir.glob("decal_*.png")}
    missing = [f for f in DECAL_EXPECTED if f not in produced]
    if missing:
        print(f"\n[CHECK] {len(missing)} decal chưa có trong output:")
        for m in missing:
            print(f"         • {m}")
    else:
        print("[CHECK] Đủ 13/13 decal texture ✓")


if __name__ == "__main__":
    main()
