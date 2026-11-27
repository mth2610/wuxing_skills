"""Modular Wuxia Eyebrow + Eyelash collection (6 distinct styles, normalized for male & female heads)."""

import math
import numpy as np
from .core import FaceProfile, HeadSurfaceSampler, MAT_HAIR, MeshBuilder, head_weights


def _add_lashes(mb: MeshBuilder, head: HeadSurfaceSampler, prof: FaceProfile, bidx: dict,
                length_mult: float = 1.0, curl_boost: float = 0.0, tint_scale: float = 1.0):
    lm = head.landmarks
    nb = mb.num_bones
    x0 = lm.x(prof.eye_x0_frac)
    dx = lm.x(prof.eye_x1_frac) - x0
    yu_base = lm.y(prof.eye_upper_y_frac)
    yl_base = lm.y(prof.eye_lower_y_frac)
    l_scale = lm.scale * prof.lash_length_scale * length_mult

    for s in (1.0, -1.0):
        n_u = 11
        u_spine = np.zeros((n_u, 3), dtype=np.float64)
        u_rx = np.zeros(n_u, dtype=np.float64)
        u_rz = np.zeros(n_u, dtype=np.float64)
        for k in range(n_u):
            t = k / (n_u - 1)
            x = s * (x0 + t * dx)
            y = yu_base + lm.m(0.0040) * math.sin(t * math.pi) + lm.m(0.0012) * t
            pt, _ = head.sample_face_xy(x, y, z_offset=lm.m(0.0018), radius=lm.m(0.004))
            u_spine[k] = pt
            thk = lm.m(0.00075 + 0.00060 * math.sin(t * math.pi))
            u_rx[k] = thk
            u_rz[k] = thk * 0.85
        mb.add_tapered_tube(u_spine, u_rx, u_rz, np.full((n_u, 3), 0.48 * tint_scale), head_weights(nb, bidx, n_u), MAT_HAIR, segs=8)

        for j in range(10):
            t = 0.10 + 0.84 * (j / 9.0)
            x = s * (x0 + t * dx)
            y = yu_base + lm.m(0.0040) * math.sin(t * math.pi) + lm.m(0.0012) * t
            p0, _ = head.sample_face_xy(x, y, z_offset=lm.m(0.0017), radius=lm.m(0.004))
            length = (0.0028 + 0.0015 * (t ** 1.1)) * l_scale
            dir_out = np.array([s * (0.18 + 0.38 * t), 0.28 + 0.22 * t + curl_boost, 0.90], dtype=np.float64)
            dir_out /= np.linalg.norm(dir_out)
            sp = np.stack([p0, p0 + dir_out * (length * 0.55), p0 + dir_out * length + np.array([0.0, lm.m(0.0007 + 0.0005 * curl_boost), 0.0])], axis=0)
            r_l = np.array([0.00060, 0.00040, 0.00014]) * lm.scale
            mb.add_tapered_tube(sp, r_l, r_l, np.full((3, 3), 0.44 * tint_scale), head_weights(nb, bidx, 3), MAT_HAIR, segs=8)

        n_lo = 9
        l_spine = np.zeros((n_lo, 3), dtype=np.float64)
        l_r = np.zeros(n_lo, dtype=np.float64)
        for k in range(n_lo):
            t = k / (n_lo - 1)
            x = s * (x0 + lm.m(0.0015) + t * (dx - lm.m(0.0030)))
            y = yl_base - lm.m(0.0020) * math.sin(t * math.pi) + lm.m(0.0010) * t
            pt, _ = head.sample_face_xy(x, y, z_offset=lm.m(0.0013), radius=lm.m(0.004))
            l_spine[k] = pt
            l_r[k] = lm.m(0.00040 + 0.00025 * math.sin(t * math.pi))
        mb.add_tapered_tube(l_spine, l_r, l_r, np.full((n_lo, 3), 0.56 * tint_scale), head_weights(nb, bidx, n_lo), MAT_HAIR, segs=8)


def _add_brow_ribbon(mb: MeshBuilder, head: HeadSurfaceSampler, prof: FaceProfile, bidx: dict,
                     arch_fn, prof_fn, base_rx: float, base_rz: float, brow_tint: np.ndarray,
                     layers=None, x_pad_in: float = 0.0, x_pad_out: float = 0.0, n_pts: int = 13):
    lm = head.landmarks
    nb = mb.num_bones
    x0 = lm.x(prof.brow_x0_frac) + lm.m(x_pad_in)
    x1 = lm.x(prof.brow_x1_frac) + lm.m(x_pad_out)
    dx = x1 - x0
    y_base = lm.y(prof.brow_y_frac)
    w_s = lm.scale * prof.brow_width_scale
    a_s = lm.scale * prof.brow_arch_scale
    if layers is None:
        layers = [(0.0, 1.00, 0.0036), (0.0014, 0.72, 0.0034)]

    for s in (1.0, -1.0):
        for dy_off, scale_w, z_ex in layers:
            spine = np.zeros((n_pts, 3), dtype=np.float64)
            out_h = np.zeros((n_pts, 3), dtype=np.float64)
            rx = np.zeros(n_pts, dtype=np.float64)
            rz = np.zeros(n_pts, dtype=np.float64)
            col_r = np.zeros((n_pts, 3), dtype=np.float64)
            for k in range(n_pts):
                t = k / (n_pts - 1)
                x = s * (x0 + t * dx)
                y = y_base + arch_fn(t) * a_s + lm.m(dy_off) * (1.0 - 0.65 * t)
                pt, nrm = head.sample_face_xy(x, y, z_offset=lm.m(z_ex), radius=lm.m(0.0055))
                spine[k] = pt
                out_h[k] = nrm
                p = prof_fn(t)
                rx[k] = max(lm.m(0.0005), base_rx * w_s * scale_w * p)
                rz[k] = max(lm.m(0.0004), base_rz * w_s * scale_w * p)
                col_r[k] = np.clip(brow_tint * (0.88 + 0.12 * (1.0 - math.sin(t * math.pi))), 0.2, 1.0)
            mb.add_tapered_tube(spine, rx, rz, col_r, head_weights(nb, bidx, n_pts), MAT_HAIR, segs=8, uv_v_scale=1.2, out_hint=out_h)


def build_kiem_mi(mb: MeshBuilder, head: HeadSurfaceSampler, prof: FaceProfile, bidx: dict):
    """1. Kiếm Mi Hiệp Khách — classic sharp rising Wuxia sword-brows + masculine lashes."""
    _add_brow_ribbon(
        mb, head, prof, bidx,
        arch_fn=lambda t: 0.0054 * (t ** 0.80) - 0.0032 * max(0.0, (t - 0.68) / 0.32) ** 1.6,
        prof_fn=lambda t: 0.48 + 0.52 * math.sin(min(1.0, t * 1.25) * math.pi * 0.5) * (1.0 - 0.85 * (t ** 1.9)),
        base_rx=0.0044, base_rz=0.0018,
        brow_tint=np.array([0.74, 0.66, 0.60]),
    )
    _add_lashes(mb, head, prof, bidx, length_mult=1.0, curl_boost=0.0)


def build_tuong_quan_mi(mb: MeshBuilder, head: HeadSurfaceSampler, prof: FaceProfile, bidx: dict):
    """2. Ngọa Tằm Tướng Quân Mi — thick, bold, fierce warrior brows with strong squared inner head."""
    _add_brow_ribbon(
        mb, head, prof, bidx,
        arch_fn=lambda t: 0.0042 * (t ** 0.72) + 0.0012 * math.sin(t * math.pi) - 0.0018 * max(0.0, (t - 0.76) / 0.24) ** 1.5,
        prof_fn=lambda t: (0.78 + 0.22 * math.sin(t * math.pi * 0.8)) * (1.0 - 0.68 * (t ** 2.2)),
        base_rx=0.0056, base_rz=0.0022,
        brow_tint=np.array([0.62, 0.56, 0.52]),
        layers=[(0.0, 1.00, 0.0038), (0.0020, 0.82, 0.0036), (-0.0014, 0.68, 0.0035)],
        x_pad_in=-0.0020, x_pad_out=0.0025,
    )
    _add_lashes(mb, head, prof, bidx, length_mult=0.95, curl_boost=0.0)


def build_thu_sinh_mi(mb: MeshBuilder, head: HeadSurfaceSampler, prof: FaceProfile, bidx: dict):
    """3. Thư Sinh Thanh Tú Mi — refined, clean, gently arched scholar/nobleman brows."""
    _add_brow_ribbon(
        mb, head, prof, bidx,
        arch_fn=lambda t: 0.0044 * math.sin(t * math.pi * 0.86) - 0.0016 * (t ** 2.1),
        prof_fn=lambda t: 0.42 + 0.58 * math.sin(min(1.0, t * 1.35) * math.pi * 0.5) * (1.0 - 0.88 * (t ** 1.75)),
        base_rx=0.0035, base_rz=0.0014,
        brow_tint=np.array([0.78, 0.70, 0.64]),
        layers=[(0.0, 1.00, 0.0032), (0.0010, 0.62, 0.0030)],
        x_pad_in=0.0005, x_pad_out=0.0010,
    )
    _add_lashes(mb, head, prof, bidx, length_mult=1.05, curl_boost=0.06)


def build_dao_nhan_mi(mb: MeshBuilder, head: HeadSurfaceSampler, prof: FaceProfile, bidx: dict):
    """4. Đạo Nhân Trường Mi — wise sage brows with gently descending outer tail & longer silver-charcoal wisps."""
    _add_brow_ribbon(
        mb, head, prof, bidx,
        arch_fn=lambda t: 0.0038 * math.sin(t * math.pi * 0.78) - 0.0038 * (t ** 2.2),
        prof_fn=lambda t: (0.64 + 0.36 * math.sin(t * math.pi * 0.75)) * (1.0 - 0.74 * (t ** 2.1)),
        base_rx=0.0046, base_rz=0.0022,
        brow_tint=np.array([0.92, 0.90, 0.92]),
        layers=[(0.0, 1.00, 0.0046), (0.0014, 0.75, 0.0044)],
        x_pad_in=-0.0010, x_pad_out=0.0055, n_pts=15,
    )
    _add_lashes(mb, head, prof, bidx, length_mult=0.90, curl_boost=0.0, tint_scale=1.2)


def build_lieu_diep_mi(mb: MeshBuilder, head: HeadSurfaceSampler, prof: FaceProfile, bidx: dict):
    """5. Liễu Diệp Mi — graceful feminine willow-leaf arched brows + longer curled eyelashes."""
    _add_brow_ribbon(
        mb, head, prof, bidx,
        arch_fn=lambda t: 0.0066 * math.sin(t * math.pi * 0.82) - 0.0024 * (t ** 2.4),
        prof_fn=lambda t: 0.38 + 0.62 * math.sin(min(1.0, t * 1.40) * math.pi * 0.5) * (1.0 - 0.90 * (t ** 1.7)),
        base_rx=0.0028, base_rz=0.0012,
        brow_tint=np.array([0.70, 0.62, 0.58]),
        layers=[(0.0, 1.00, 0.0030)],
        x_pad_in=0.0005, x_pad_out=0.0020, n_pts=14,
    )
    _add_lashes(mb, head, prof, bidx, length_mult=1.28, curl_boost=0.22)


def build_phuong_vi_mi(mb: MeshBuilder, head: HeadSurfaceSampler, prof: FaceProfile, bidx: dict):
    """6. Phượng Vĩ Mi — high-sweeping noble phoenix-tail brows + lush curled eyelashes."""
    _add_brow_ribbon(
        mb, head, prof, bidx,
        arch_fn=lambda t: 0.0082 * (t ** 0.88) + 0.0015 * math.sin(t * math.pi) - 0.0010 * max(0.0, (t - 0.82) / 0.18) ** 2.0,
        prof_fn=lambda t: 0.36 + 0.64 * math.sin(min(1.0, t * 1.35) * math.pi * 0.5) * (1.0 - 0.92 * (t ** 1.65)),
        base_rx=0.0031, base_rz=0.0013,
        brow_tint=np.array([0.66, 0.58, 0.56]),
        layers=[(0.0, 1.00, 0.0032), (0.0009, 0.55, 0.0030)],
        x_pad_in=0.0000, x_pad_out=0.0040, n_pts=14,
    )
    _add_lashes(mb, head, prof, bidx, length_mult=1.35, curl_boost=0.28)


STYLE_BUILDERS = {
    "kiem_mi": build_kiem_mi,
    "tuong_quan_mi": build_tuong_quan_mi,
    "thu_sinh_mi": build_thu_sinh_mi,
    "dao_nhan_mi": build_dao_nhan_mi,
    "lieu_diep_mi": build_lieu_diep_mi,
    "phuong_vi_mi": build_phuong_vi_mi,
}
