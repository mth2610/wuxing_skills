"""Modular Wuxia Beard, Mustache & Sideburns collection (6 distinct styles, normalized via FaceLandmarks)."""

import math
import numpy as np
from .core import FaceProfile, HeadSurfaceSampler, MAT_HAIR, MAT_STUBBLE, MeshBuilder, head_weights


def _add_sideburns(mb: MeshBuilder, head: HeadSurfaceSampler, bidx: dict,
                   tint: np.ndarray, length_frac: float = 0.293, width: float = 0.0065):
    lm = head.landmarks
    nb = mb.num_bones
    for s in (1.0, -1.0):
        n_sb = 7
        spine = np.zeros((n_sb, 3), dtype=np.float64)
        rx = np.zeros(n_sb, dtype=np.float64)
        rz = np.zeros(n_sb, dtype=np.float64)
        for k in range(n_sb):
            t = k / (n_sb - 1)
            y = lm.y(0.664 - length_frac * t)
            x = s * (lm.x(0.96) - lm.m(0.006) * t)
            z = lm.center[2] + lm.m(0.030 - 0.006 * t)
            d_vec = np.array([x, y, z], dtype=np.float64) - head.center
            pt, _ = head.sample_skull_dir(d_vec, offset=lm.m(0.0022))
            spine[k] = pt
            taper = 1.0 - 0.78 * (t ** 1.6)
            rx[k] = max(lm.m(0.0010), lm.m(width) * taper)
            rz[k] = max(lm.m(0.0008), lm.m(0.0022) * taper)
        mb.add_tapered_tube(spine, rx, rz, np.tile(tint * 0.85, (n_sb, 1)), head_weights(nb, bidx, n_sb), MAT_HAIR, segs=8)


def _add_mustache(mb: MeshBuilder, head: HeadSurfaceSampler, prof: FaceProfile, bidx: dict,
                  tint: np.ndarray, x_end: float = 0.031, drop: float = 0.0115,
                  rx_base: float = 0.0030, rz_base: float = 0.0013,
                  upturn: float = 0.0, layers=None):
    lm = head.landmarks
    nb = mb.num_bones
    y_lip = lm.y(prof.lip_upper_y_frac)
    if layers is None:
        z_l = 0.0028 if x_end > 0.038 else 0.0018
        layers = [(0.0, 1.00, z_l, x_end), (0.0016, 0.68, z_l - 0.0002, x_end * 0.87)]
    for s in (1.0, -1.0):
        for dy_m, w_sc, z_ex, xe in layers:
            n_m = 12
            spine = np.zeros((n_m, 3), dtype=np.float64)
            out_h = np.zeros((n_m, 3), dtype=np.float64)
            rx = np.zeros(n_m, dtype=np.float64)
            rz = np.zeros(n_m, dtype=np.float64)
            col_r = np.zeros((n_m, 3), dtype=np.float64)
            for k in range(n_m):
                t = k / (n_m - 1)
                x = s * lm.m(0.0020 + t * (xe - 0.0020))
                up = upturn * max(0.0, (t - 0.65) / 0.35) ** 1.8
                y = y_lip + lm.m(0.0012 * math.sin(t * math.pi * 0.65) - drop * (t ** 1.9) + up + dy_m * (1.0 - 0.55 * t))
                pt, nrm = head.sample_face_xy(x, y, z_offset=lm.m(z_ex), radius=lm.m(0.0055))
                spine[k] = pt
                out_h[k] = nrm
                p = (0.45 + 0.55 * math.sin(t * math.pi * 0.85)) * (1.0 - 0.86 * (t ** 2.0))
                rx[k] = max(lm.m(0.0007), lm.m(rx_base) * w_sc * p)
                rz[k] = max(lm.m(0.0005), lm.m(rz_base) * w_sc * p)
                col_r[k] = np.clip(tint * (0.82 + 0.16 * (1.0 - t)), 0.2, 1.0)
            mb.add_tapered_tube(spine, rx, rz, col_r, head_weights(nb, bidx, n_m), MAT_HAIR, segs=8, uv_v_scale=1.5, out_hint=out_h)


def _add_chin_pad(mb: MeshBuilder, head: HeadSurfaceSampler, prof: FaceProfile, bidx: dict,
                  tint: np.ndarray, half_w: float = 0.032, top_ext: float = 0.0055,
                  bot_ext: float = 0.0075, z_lift: float = 0.0042):
    """Smooth analytical chin beard patch guaranteed to sit cleanly proud of the chin skin."""
    lm = head.landmarks
    nb = mb.num_bones
    b_head = bidx["mixamorig:Head"]
    y_chin_top_ref = lm.y(prof.chin_top_y_frac)
    y_chin_bot_ref = lm.y(prof.chin_bot_y_frac)
    # Sample the actual chin prominence (mid-chin, not the recessed labiomental fold!) so the pad sits cleanly proud
    y_chin_mid = 0.55 * y_chin_top_ref + 0.45 * y_chin_bot_ref
    chin_peak_pt, _ = head.sample_face_xy(0.0, y_chin_mid, z_offset=0.0, radius=lm.m(0.007))
    z_chin_peak = float(chin_peak_pt[2]) + lm.m(z_lift)

    n_r, n_c = 9, 15
    Pb = np.zeros((n_r, n_c, 3), dtype=np.float64)
    UVb = np.zeros((n_r, n_c, 2), dtype=np.float64)
    COLb = np.ones((n_r, n_c, 3), dtype=np.float64)
    Wb = np.zeros((n_r, n_c, nb), dtype=np.float64)
    Wb[:, :, b_head] = 1.0

    for i in range(n_c):
        u = i / (n_c - 1)
        su = 2.0 * u - 1.0
        env = max(0.0, 1.0 - su * su)
        y_top = y_chin_top_ref + lm.m(top_ext) * (env ** 1.5)
        y_bot = y_chin_bot_ref - lm.m(bot_ext) * (env ** 1.15)
        for r in range(n_r):
            v = r / (n_r - 1)
            t_down = 1.0 - v
            y = y_top * v + y_bot * t_down
            x = su * lm.m(half_w) * (1.0 - 0.22 * (t_down ** 1.3))
            flute = lm.m(0.0005) * math.sin(6.0 * math.pi * u) * env
            edge_taper = math.sin(u * math.pi) ** 0.65
            relief = lm.m(0.0010 + 0.0018 * math.sin(v * math.pi * 0.85)) * edge_taper + flute
            dx_n = x / lm.scale
            dy_dn = max(0.0, (y_chin_mid - y) / lm.scale)
            dy_up = max(0.0, (y - y_chin_mid) / lm.scale)
            z_base = z_chin_peak - lm.m(11.5 * (dx_n * dx_n) + 22.0 * (dy_dn ** 1.60) + 10.0 * (dy_up ** 2))
            Pb[r, i] = [x, y, z_base + relief]
            UVb[r, i] = (u * 2.2, t_down * 1.4)
            COLb[r, i] = np.clip(tint * (0.78 + 0.14 * v), 0.45, 0.98)

    mb.add_solidified_grid(
        Pb, UVb, COLb, Wb, np.full((n_r - 1, n_c - 1), MAT_HAIR, dtype=np.int32),
        thickness=lm.m(0.0015), flip=False, inner_shade=0.72,
    )


def _add_flowing_chin_tufts(mb: MeshBuilder, head: HeadSurfaceSampler, prof: FaceProfile, bidx: dict,
                            tint: np.ndarray, length: float = 0.075, spread_x: float = 0.022, n_locks: int = 5):
    lm = head.landmarks
    nb = mb.num_bones
    y_top = lm.y(prof.chin_top_y_frac) - lm.m(0.002)
    chin_pt, _ = head.sample_face_xy(0.0, y_top, z_offset=lm.m(0.0038), radius=lm.m(0.006))
    z0 = float(chin_pt[2])

    sus = np.linspace(-1.0, 1.0, n_locks)
    n_k = 12
    for idx, su in enumerate(sus):
        spine = np.zeros((n_k, 3), dtype=np.float64)
        out_h = np.zeros((n_k, 3), dtype=np.float64)
        rx = np.zeros(n_k, dtype=np.float64)
        rz = np.zeros(n_k, dtype=np.float64)
        col_r = np.zeros((n_k, 3), dtype=np.float64)
        len_i = length * (1.0 - 0.24 * (su ** 2))
        hw = lm.m(0.0115 - 0.0025 * abs(su))
        for k in range(n_k):
            t = k / (n_k - 1)
            x = su * lm.m(spread_x) * (1.0 - 0.65 * (t ** 1.2)) + lm.m(0.0025) * math.sin(3.5 * t + idx) * t
            y = y_top - lm.m(len_i) * t
            # Curves gently forward then drapes cleanly down clear of neck/collar
            z = z0 - lm.m(0.006 * (su ** 2) + 0.010 * (t ** 1.3) - 0.006 * math.sin(t * math.pi))
            spine[k] = [x, y, z]
            out_h[k] = [0.35 * su, 0.08, 0.92]
            taper = (0.55 + 0.45 * math.sin(t * math.pi * 0.68)) * (1.0 - 0.90 * (t ** 1.65))
            rx[k] = max(lm.m(0.0012), hw * taper)
            rz[k] = max(lm.m(0.0010), hw * 0.50 * taper)
            col_r[k] = np.clip(tint * (0.82 + 0.18 * math.sin(t * math.pi * 0.8)), 0.35, 1.0)
        mb.add_tapered_tube(spine, rx, rz, col_r, head_weights(nb, bidx, n_k), MAT_HAIR, segs=8, uv_v_scale=2.2, out_hint=out_h)


def build_kiem_khach_trim(mb: MeshBuilder, head: HeadSurfaceSampler, prof: FaceProfile, bidx: dict):
    """1. Râu Kiếm Khách Gọn — sleek close-cropped mustache + contoured chin trim + sideburns."""
    tint = np.array([0.82, 0.72, 0.66], dtype=np.float64)
    _add_mustache(mb, head, prof, bidx, tint, x_end=0.031, drop=0.0115, rx_base=0.0030, rz_base=0.0013)
    _add_chin_pad(mb, head, prof, bidx, tint, half_w=0.031, top_ext=0.0055, bot_ext=0.0075, z_lift=0.0044)
    _add_sideburns(mb, head, bidx, tint)


def build_lang_tu_light(mb: MeshBuilder, head: HeadSurfaceSampler, prof: FaceProfile, bidx: dict):
    """2. Râu Lãng Tử Nhẹ — understated thin wanderer mustache + sleek small chin tuft (youthful hero)."""
    tint = np.array([0.78, 0.70, 0.64], dtype=np.float64)
    _add_mustache(mb, head, prof, bidx, tint, x_end=0.028, drop=0.0095, rx_base=0.0023, rz_base=0.0011,
                  layers=[(0.0, 1.00, 0.0014, 0.028)])
    _add_chin_pad(mb, head, prof, bidx, tint, half_w=0.020, top_ext=0.0035, bot_ext=0.0055, z_lift=0.0040)
    _add_sideburns(mb, head, bidx, tint, length_frac=0.23, width=0.0052)


def build_tam_chom_quan_tu(mb: MeshBuilder, head: HeadSurfaceSampler, prof: FaceProfile, bidx: dict):
    """3. Râu Tam Chòm Quân Tử — classic Wuxia strategist/nobleman sweeping mustache + medium tapered chin beard."""
    tint = np.array([0.74, 0.66, 0.62], dtype=np.float64)
    _add_mustache(mb, head, prof, bidx, tint, x_end=0.044, drop=0.0220, rx_base=0.0034, rz_base=0.0015)
    _add_chin_pad(mb, head, prof, bidx, tint, half_w=0.026, top_ext=0.0045, bot_ext=0.0065, z_lift=0.0042)
    _add_flowing_chin_tufts(mb, head, prof, bidx, tint, length=0.072, spread_x=0.018, n_locks=5)
    _add_sideburns(mb, head, bidx, tint, length_frac=0.34, width=0.0070)


def build_quai_non_vo_tuong(mb: MeshBuilder, head: HeadSurfaceSampler, prof: FaceProfile, bidx: dict):
    """4. Râu Quai Nón Lún Phún (3-Day Freshly-Grown Stubble / 5-O'Clock Shadow).

    Extracts the exact lower-face / jaw / cheek / upper-lip surface from the target body GLB
    (+0.8 mm along body normals) and maps a 512x512 soft-feathered alpha-blended follicle
    stubble texture over it -> natural freshly-shaved / newly-grown stubble with ZERO hard edge!
    """
    lm = head.landmarks
    nb = mb.num_bones
    V_face, _, F_face = head.extract_lower_face_mesh(offset=lm.m(0.0008))

    # Map each vertex into normalized stubble UV:
    #   u = 0.5 + atan2(x, z - center_z) / (2 * 1.42)  -> 0.5 at chin/lip center, 0/1 at ears
    #   v = (y - (chin_y - 0.018)) / (y_sideburn - (chin_y - 0.018))
    phi = np.arctan2(V_face[:, 0], np.maximum(V_face[:, 2] - lm.center[2], 1e-4))
    u = np.clip(0.5 + phi / (2.0 * 1.42), 0.0, 1.0)
    y_lo = lm.chin_y - lm.m(0.018)
    y_hi = lm.y(0.54)
    v = np.clip((V_face[:, 1] - y_lo) / max(y_hi - y_lo, 1e-5), 0.0, 1.0)

    UV = np.column_stack([u, v])
    COL = np.ones((len(V_face), 3), dtype=np.float64)
    W = head_weights(nb, bidx, len(V_face))
    mb.add_indexed_mesh(V_face, UV, COL, W, F_face, MAT_STUBBLE)


def build_tien_ong_truong_tu(mb: MeshBuilder, head: HeadSurfaceSampler, prof: FaceProfile, bidx: dict):
    """5. Râu Trường Tu Tiên Phong — iconic long flowing silver-white Daoist grandmaster beard & whiskers."""
    tint = np.array([0.94, 0.94, 0.96], dtype=np.float64)
    _add_mustache(mb, head, prof, bidx, tint, x_end=0.056, drop=0.0440, rx_base=0.0040, rz_base=0.0018)
    _add_chin_pad(mb, head, prof, bidx, tint, half_w=0.032, top_ext=0.0055, bot_ext=0.0080, z_lift=0.0045)
    _add_flowing_chin_tufts(mb, head, prof, bidx, tint, length=0.165, spread_x=0.028, n_locks=7)
    _add_sideburns(mb, head, bidx, tint, length_frac=0.38, width=0.0072)


def build_son_duong_hiep_si(mb: MeshBuilder, head: HeadSurfaceSampler, prof: FaceProfile, bidx: dict):
    """6. Râu Sơn Dương Hiệp Sĩ — upturned aristocratic mustache tips + sharp pointed Van-Dyke chin tuft."""
    tint = np.array([0.76, 0.68, 0.62], dtype=np.float64)
    _add_mustache(mb, head, prof, bidx, tint, x_end=0.038, drop=0.0075, rx_base=0.0032, rz_base=0.0014, upturn=0.0065)
    _add_chin_pad(mb, head, prof, bidx, tint, half_w=0.024, top_ext=0.0065, bot_ext=0.0065, z_lift=0.0042)
    _add_flowing_chin_tufts(mb, head, prof, bidx, tint, length=0.042, spread_x=0.012, n_locks=3)
    _add_sideburns(mb, head, bidx, tint, length_frac=0.26, width=0.0058)


STYLE_BUILDERS = {
    "kiem_khach_trim": build_kiem_khach_trim,
    "lang_tu_light": build_lang_tu_light,
    "tam_chom_quan_tu": build_tam_chom_quan_tu,
    "quai_non_vo_tuong": build_quai_non_vo_tuong,
    "tien_ong_truong_tu": build_tien_ong_truong_tu,
    "son_duong_hiep_si": build_son_duong_hiep_si,
}
