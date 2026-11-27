"""Modular Wuxia Hair builders (conformal to any male or female body GLB via FaceLandmarks)."""

import math
import numpy as np
from .core import (
    FaceProfile,
    HeadSurfaceSampler,
    MAT_GOLD,
    MAT_HAIR,
    MAT_JADE,
    MeshBuilder,
    head_weights,
    smoothstep,
)


def _make_back_hair_weights(nb: int, bidx: dict, t: float, su: float) -> np.ndarray:
    w = np.zeros(nb, dtype=np.float64)
    w_h = 1.0 - float(smoothstep(0.14, 0.34, t))
    w_c = 1.0 - w_h
    w2 = float(smoothstep(0.40, 0.78, t))
    w_side = float(smoothstep(0.18, 0.74, abs(su)))
    w_mid = 1.0 - w_side
    is_l = 1.0 if su >= 0.0 else 0.0
    w[bidx["mixamorig:Head"]] = w_h
    w[bidx["cloth_hair_c_01"]] = w_c * w_mid * (1.0 - w2)
    w[bidx["cloth_hair_c_02"]] = w_c * w_mid * w2
    w[bidx["cloth_hair_l_01"]] = w_c * w_side * is_l * (1.0 - w2)
    w[bidx["cloth_hair_l_02"]] = w_c * w_side * is_l * w2
    w[bidx["cloth_hair_r_01"]] = w_c * w_side * (1.0 - is_l) * (1.0 - w2)
    w[bidx["cloth_hair_r_02"]] = w_c * w_side * (1.0 - is_l) * w2
    return w


def _add_sculpted_crown(mb: MeshBuilder, head: HeadSurfaceSampler, prof: FaceProfile, bidx: dict,
                        part_phi: float = 0.20, loft_mult: float = 1.0):
    lm = head.landmarks
    nb = mb.num_bones
    b_head = bidx["mixamorig:Head"]
    n_phi, n_v = 64, 20
    P = np.zeros((n_v, n_phi, 3), dtype=np.float64)
    UV = np.zeros((n_v, n_phi, 2), dtype=np.float64)
    COL = np.ones((n_v, n_phi, 3), dtype=np.float64)
    W = np.zeros((n_v, n_phi, nb), dtype=np.float64)
    W[:, :, b_head] = 1.0

    loft_s = lm.scale * prof.crown_loft_scale * loft_mult
    for iv in range(n_v):
        v = iv / (n_v - 1)
        for ip in range(n_phi):
            phi = (ip / n_phi) * 2.0 * math.pi
            phi_s = phi - 2.0 * math.pi if phi > math.pi else phi
            c, s = math.cos(phi_s), math.sin(phi_s)
            front_w, back_w, side_w = max(0.0, c), max(0.0, -c), abs(s)

            widow_peak = -0.045 * math.exp(-((phi_s / 0.22) ** 2))
            temple_bay = 0.115 * math.exp(-(((abs(phi_s) - 0.64) / 0.26) ** 2))
            ear_arch = 0.155 * math.exp(-(((abs(phi_s) - 1.40) / 0.26) ** 2))
            elev_bot = prof.hairline_front_elev * (front_w ** 1.15) - 0.62 * (back_w ** 0.85) + widow_peak + temple_bay + ear_arch
            elev = elev_bot * (1.0 - v) + 1.53 * v

            d_vec = np.array([math.cos(elev) * s, math.sin(elev), math.cos(elev) * c], dtype=np.float64)
            env_v = math.sin(min(1.0, v * 1.18) * math.pi * 0.86)
            base_loft = lm.m(0.0025) + (0.0115 + 0.0032 * front_w + 0.0028 * back_w + 0.0018 * side_w) * loft_s * env_v

            part_dist = abs(phi_s - part_phi)
            part_groove = -lm.m(0.0026) * math.exp(-((part_dist / 0.14) ** 2)) * front_w * math.sin(v * math.pi)
            wave_env = math.sin(v * math.pi) * (1.0 - 0.35 * (v ** 2))
            silk_wave = lm.m(
                0.0011 * math.sin(9.0 * phi_s + 2.2 * v)
                + 0.0006 * math.cos(15.0 * phi_s - 3.0 * v)
                + 0.0004 * math.sin(5.0 * phi_s + 1.4 * v)
            ) * wave_env

            vol = max(lm.m(0.0018), base_loft + part_groove + silk_wave)
            pt, _ = head.sample_skull_dir(d_vec, offset=vol)
            P[iv, ip] = pt
            UV[iv, ip] = (ip / n_phi * 7.0, v * 2.8)
            strand_mod = 0.06 * math.sin(14.0 * phi_s + 1.8 * v) + 0.04 * math.cos(23.0 * phi_s)
            part_ao = -0.10 * math.exp(-((part_dist / 0.12) ** 2)) * front_w * math.sin(v * math.pi)
            COL[iv, ip] = np.clip(0.76 + 0.18 * math.sin(v * math.pi * 0.85) + strand_mod + part_ao, 0.58, 0.98)

    mb.add_solidified_grid(
        P, UV, COL, W, np.full((n_v - 1, n_phi), MAT_HAIR, dtype=np.int32),
        thickness=lm.m(0.0028), wrap_u=True, flip=False, inner_shade=0.68,
    )


def build_kiem_hiep_ponytail(mb: MeshBuilder, head: HeadSurfaceSampler, prof: FaceProfile, bidx: dict):
    """Heroic Wuxia Swordsman hair: sculpted crown + 7 windswept bangs + tilted gold-jade coronet + cascading ponytail."""
    lm = head.landmarks
    nb = mb.num_bones
    b_head = bidx["mixamorig:Head"]

    # 1. Lush continuous sculpted crown shell
    _add_sculpted_crown(mb, head, prof, bidx, part_phi=0.20, loft_mult=1.0)

    # 2. 7 Dynamic Windswept Front Bangs (Tóc Mái Kiếm Khách)
    cheek_y = lm.y(prof.cheek_y_frac)
    yr = lm.skull_top_y - lm.m(0.020)
    bang_specs = [
        # (xr_f, xm_f, xt_f, y_tip_frac, z_l, hw, sway_bone)
        ( 0.19,  0.61,  0.93, 0.440, 0.014, 0.017, "cloth_hair_bang_l"),
        ( 0.13,  0.39,  0.64, 0.543, 0.016, 0.013, None),
        ( 0.21, -0.19, -0.64, 0.491, 0.015, 0.018, None),
        ( 0.08, -0.51, -0.93, 0.414, 0.014, 0.017, "cloth_hair_bang_r"),
        ( 0.29,  0.03, -0.21, 0.603, 0.016, 0.012, None),
        ( 0.77,  0.99,  0.96, 0.009, 0.012, 0.014, "cloth_hair_bang_l"),
        (-0.77, -0.99, -0.96, 0.009, 0.012, 0.014, "cloth_hair_bang_r"),
    ]
    n_b = 11
    for xrf, xmf, xtf, ytf, z_l, hw, sw_bone in bang_specs:
        xr, xm, xt = lm.x(xrf), lm.x(xmf), lm.x(xtf)
        yt = lm.y(ytf)
        spine = np.zeros((n_b, 3), dtype=np.float64)
        out_h = np.zeros((n_b, 3), dtype=np.float64)
        rx = np.zeros(n_b, dtype=np.float64)
        rz = np.zeros(n_b, dtype=np.float64)
        col_r = np.ones((n_b, 3), dtype=np.float64)
        w_r = np.zeros((n_b, nb), dtype=np.float64)

        for k in range(n_b):
            t = k / (n_b - 1)
            x = (1 - t) ** 2 * xr + 2 * (1 - t) * t * xm + t ** 2 * xt
            y = yr * (1.0 - t ** 0.92) + yt * (t ** 0.92)
            if y >= cheek_y:
                d_v = np.array([x, y, lm.forehead_z - 0.55 * abs(x)], dtype=np.float64) - head.center
                off = lm.m(0.0085 * (1.0 - t) + z_l * math.sin(t * math.pi * 0.85) + 0.0025)
                pt, nrm = head.sample_skull_dir(d_v, offset=off)
            else:
                s_x = 1.0 if x >= 0 else -1.0
                pt = np.array([x + s_x * lm.m(0.006) * (t - 0.5), y, lm.center[2] + lm.m(0.046 - 0.012 * t)], dtype=np.float64)
                nrm = np.array([s_x * 0.65, 0.0, 0.76], dtype=np.float64)
            spine[k] = pt
            out_h[k] = nrm
            taper = (0.48 + 0.52 * math.sin(t * math.pi * 0.78)) * (1.0 - 0.88 * (t ** 1.65))
            rx[k] = max(lm.m(0.0014), lm.m(hw) * taper)
            rz[k] = max(lm.m(0.0010), lm.m(hw) * 0.30 * taper)
            col_r[k] = 0.80 + 0.20 * math.sin(t * math.pi * 0.8)

            if sw_bone is not None:
                w_sw = float(smoothstep(0.32, 0.80, t)) * 0.78
                w_r[k, b_head] = 1.0 - w_sw
                w_r[k, bidx[sw_bone]] = w_sw
            else:
                w_r[k, b_head] = 1.0

        mb.add_tapered_tube(spine, rx, rz, col_r, w_r, MAT_HAIR, segs=8, uv_v_scale=2.0, out_hint=out_h)

    # 3. Backward-Tilted Imperial Antique-Gold & Celadon-Jade Hair Coronet (Phát Quán) + Jade Pin
    crown_anchor = np.array([0.0, lm.skull_top_y - lm.m(0.014), lm.back_z + lm.m(0.035)], dtype=np.float64)
    tilt = math.radians(-22.0)
    ct, st = math.cos(tilt), math.sin(tilt)

    def crown_pt(lx, ly, lz):
        lx, ly, lz = lm.m(lx), lm.m(ly), lm.m(lz)
        return crown_anchor + np.array([lx, ly * ct - lz * st, ly * st + lz * ct], dtype=np.float64)

    n_bv, n_bu = 10, 20
    Pb = np.zeros((n_bv, n_bu, 3), dtype=np.float64)
    UVb = np.zeros((n_bv, n_bu, 2), dtype=np.float64)
    COLb = np.ones((n_bv, n_bu, 3), dtype=np.float64)
    Wb = np.zeros((n_bv, n_bu, nb), dtype=np.float64)
    Wb[:, :, b_head] = 1.0
    for r in range(n_bv):
        v = r / (n_bv - 1)
        rad = max(0.004, 0.022 * (1.0 - 0.12 * v) * (1.0 - 0.72 * (v ** 3.5)))
        ly = -0.004 + v * 0.044
        for i in range(n_bu):
            th = (i / n_bu) * 2.0 * math.pi
            Pb[r, i] = crown_pt(rad * math.sin(th), ly, rad * math.cos(th))
            UVb[r, i] = (i / n_bu * 3.0, v * 1.5)
            COLb[r, i] = 0.78 + 0.18 * v
    mb.add_grid(Pb, UVb, COLb, Wb, np.full((n_bv - 1, n_bu), MAT_HAIR, dtype=np.int32), wrap_u=True, flip=False)

    n_cv, n_cu = 7, 28
    Pc = np.zeros((n_cv, n_cu, 3), dtype=np.float64)
    UVc = np.zeros((n_cv, n_cu, 2), dtype=np.float64)
    COLc = np.ones((n_cv, n_cu, 3), dtype=np.float64)
    Wc = np.zeros((n_cv, n_cu, nb), dtype=np.float64)
    Wc[:, :, b_head] = 1.0
    for r in range(n_cv):
        v = r / (n_cv - 1)
        ly = v * 0.028
        flare = 0.0028 * ((2.0 * v - 1.0) ** 2)
        for i in range(n_cu):
            th = (i / n_cu) * 2.0 * math.pi
            crest = 0.009 * max(0.0, math.cos(th)) ** 2.4 * v
            rr = 0.031 + flare + 0.0012 * math.cos(4.0 * th)
            Pc[r, i] = crown_pt(rr * math.sin(th), ly + crest, rr * math.cos(th))
            UVc[r, i] = (i / n_cu * 4.0, v)
            COLc[r, i] = 0.90 + 0.10 * math.sin(v * math.pi)
    fm_c = np.full((n_cv - 1, n_cu), MAT_GOLD, dtype=np.int32)
    for i in range(n_cu):
        th = ((i + 0.5) / n_cu) * 2.0 * math.pi
        if math.cos(th) > 0.88:
            fm_c[1:-1, i] = MAT_JADE
    mb.add_solidified_grid(Pc, UVc, COLc, Wc, fm_c, thickness=lm.m(0.0024), wrap_u=True, flip=False, inner_shade=0.78)

    n_p = 12
    xs = np.linspace(-0.068, 0.068, n_p)
    sp_p = np.array([crown_pt(float(xx), 0.032, 0.0) for xx in xs])
    rad_p = np.array([0.0038, 0.0046, 0.0032] + [0.0027] * (n_p - 6) + [0.0032, 0.0046, 0.0038]) * lm.scale
    mb.add_tapered_tube(sp_p, rad_p, rad_p, np.full((n_p, 3), 0.96), head_weights(nb, bidx, n_p), MAT_JADE, segs=8)
    for sgn in (-1.0, 1.0):
        ex = np.linspace(sgn * 0.054, sgn * 0.074, 5)
        if sgn < 0:
            ex = ex[::-1]
        sp_e = np.array([crown_pt(float(xx), 0.032, 0.0) for xx in ex])
        re = np.array([0.0034, 0.0054, 0.0060, 0.0046, 0.0020]) * lm.scale
        mb.add_tapered_tube(sp_e, re, re, np.full((5, 3), 0.98), head_weights(nb, bidx, 5), MAT_GOLD, segs=8)

    # 4. Cascading High Ponytail + Cohesive S-Curved Back-Mane Under-Sheet + Outer Accent Locks
    hy = float(lm.head_bone_pos[1])
    ponytail_specs = [
        ( 0.00, hy - lm.m(0.375),  0.000, -0.024, 0.030, 0.0),
        ( 0.30, hy - lm.m(0.350),  0.038, -0.018, 0.026, 0.8),
        (-0.30, hy - lm.m(0.350), -0.038, -0.018, 0.026, 1.6),
        ( 0.60, hy - lm.m(0.310),  0.068, -0.010, 0.023, 2.4),
        (-0.60, hy - lm.m(0.310), -0.068, -0.010, 0.023, 3.2),
        ( 0.16, hy - lm.m(0.325),  0.020, -0.030, 0.022, 1.1),
        (-0.16, hy - lm.m(0.325), -0.020, -0.030, 0.022, 2.1),
    ]
    n_pt = 15
    y_pt_start = crown_anchor[1] + lm.m(0.024)
    z_pt_start = crown_anchor[2] - lm.m(0.010)
    for su, y_bot, x_sp, z_arc, hw, ph in ponytail_specs:
        spine = np.zeros((n_pt, 3), dtype=np.float64)
        out_h = np.zeros((n_pt, 3), dtype=np.float64)
        rx = np.zeros(n_pt, dtype=np.float64)
        rz = np.zeros(n_pt, dtype=np.float64)
        col_r = np.ones((n_pt, 3), dtype=np.float64)
        w_r = np.zeros((n_pt, nb), dtype=np.float64)
        for k in range(n_pt):
            t = k / (n_pt - 1)
            y = y_pt_start * (1.0 - t ** 1.15) + y_bot * (t ** 1.15) + lm.m(0.034) * math.sin(min(1.0, t * 2.2) * math.pi) * (1.0 - t)
            x = su * lm.m(0.012) * (1.0 - t) + lm.m(x_sp) * (t ** 0.72) + lm.m(0.007) * math.sin(4.2 * t + ph) * t
            z = z_pt_start + lm.m(z_arc) * math.sin(t * math.pi * 0.85) - lm.m(0.118) * (t ** 0.62) + lm.m(0.006) * math.cos(3.6 * t + ph) * t
            spine[k] = [x, y, z]
            out_h[k] = [0.3 * su, 0.25 * (1.0 - t), -0.95]
            taper = (0.48 + 0.52 * math.sin(t * math.pi * 0.75)) * (1.0 - 0.88 * (t ** 1.8))
            rx[k] = max(lm.m(0.0020), lm.m(hw) * taper)
            rz[k] = max(lm.m(0.0014), lm.m(hw) * 0.28 * taper)
            col_r[k] = 0.80 + 0.20 * math.sin(t * math.pi * 0.8)
            w_r[k] = _make_back_hair_weights(nb, bidx, t, su)
        mb.add_tapered_tube(spine, rx, rz, col_r, w_r, MAT_HAIR, segs=8, uv_v_scale=2.6, out_hint=out_h)

    n_mr, n_mc = 15, 19
    Pm = np.zeros((n_mr, n_mc, 3), dtype=np.float64)
    UVm = np.zeros((n_mr, n_mc, 2), dtype=np.float64)
    COLm = np.ones((n_mr, n_mc, 3), dtype=np.float64)
    Wm = np.zeros((n_mr, n_mc, nb), dtype=np.float64)
    for ir in range(n_mr):
        t = ir / (n_mr - 1)
        for ic in range(n_mc):
            u = ic / (n_mc - 1)
            su = 2.0 * u - 1.0
            y_top = hy + lm.m(0.012) - lm.m(0.016) * (su ** 2)
            y_bot = hy - lm.m(0.325) + lm.m(0.085) * (su ** 2) + lm.m(0.014) * math.sin(5.0 * math.pi * u)
            y = y_top * (1.0 - t) + y_bot * t
            half_w = lm.x(0.90) + lm.m(0.044) * math.sin(t * math.pi * 0.78) - lm.m(0.018) * (t ** 2)
            x = su * half_w
            flute = lm.m(0.0022) * math.sin(7.0 * math.pi * u + 1.2 * t) * math.sin(t * math.pi)
            z = lm.back_z + lm.m(0.010) - lm.m(0.084) * (t ** 0.74) + lm.m(0.032) * (su ** 2) * (1.0 - 0.45 * t) - flute
            Pm[ir, ic] = [x, y, z]
            UVm[ir, ic] = (u * 3.5, t * 2.8)
            COLm[ir, ic] = 0.70 + 0.18 * math.sin(t * math.pi * 0.85) + 0.04 * math.sin(7.0 * math.pi * u)
            Wm[ir, ic] = _make_back_hair_weights(nb, bidx, t, su)
    mb.add_solidified_grid(
        Pm, UVm, COLm, Wm, np.full((n_mr - 1, n_mc - 1), MAT_HAIR, dtype=np.int32),
        thickness=lm.m(0.0040), wrap_u=False, flip=True, inner_shade=0.65,
    )

    mane_specs = [
        ( 0.00, hy + lm.m(0.005), hy - lm.m(0.345), -0.005, 0.026, 0.2),
        ( 0.54, hy + lm.m(0.000), hy - lm.m(0.300),  0.000, 0.022, 1.8),
        (-0.54, hy + lm.m(0.000), hy - lm.m(0.300),  0.000, 0.022, 2.6),
        ( 0.90, hy - lm.m(0.008), hy - lm.m(0.235),  0.014, 0.018, 0.8),
        (-0.90, hy - lm.m(0.008), hy - lm.m(0.235),  0.014, 0.018, 2.0),
    ]
    n_m = 13
    for su, y_top, y_bot, z_ex, hw, ph in mane_specs:
        spine = np.zeros((n_m, 3), dtype=np.float64)
        out_h = np.zeros((n_m, 3), dtype=np.float64)
        rx = np.zeros(n_m, dtype=np.float64)
        rz = np.zeros(n_m, dtype=np.float64)
        col_r = np.ones((n_m, 3), dtype=np.float64)
        w_r = np.zeros((n_m, nb), dtype=np.float64)
        for k in range(n_m):
            t = k / (n_m - 1)
            y = y_top * (1.0 - t) + y_bot * t
            x = su * (lm.x(0.88) + lm.m(0.044) * math.sin(t * math.pi * 0.80)) + lm.m(0.005) * math.sin(3.6 * t + ph) * t
            z = lm.back_z + lm.m(0.008 + z_ex) - lm.m(0.084) * (t ** 0.74) + lm.m(0.030) * (su ** 2) * (1.0 - 0.45 * t) + lm.m(0.005) * math.cos(3.2 * t + ph) * t
            spine[k] = [x, y, z]
            out_h[k] = [0.45 * su, 0.1, -0.88]
            taper = (0.65 + 0.35 * math.sin(t * math.pi * 0.8)) * (1.0 - 0.88 * (t ** 1.75))
            rx[k] = max(lm.m(0.0018), lm.m(hw) * taper)
            rz[k] = max(lm.m(0.0012), lm.m(hw) * 0.28 * taper)
            col_r[k] = 0.76 + 0.20 * math.sin(t * math.pi * 0.8)
            w_r[k] = _make_back_hair_weights(nb, bidx, t, su)
        mb.add_tapered_tube(spine, rx, rz, col_r, w_r, MAT_HAIR, segs=8, uv_v_scale=2.4, out_hint=out_h)


def build_nu_hiep_long(mb: MeshBuilder, head: HeadSurfaceSampler, prof: FaceProfile, bidx: dict):
    """Feminine Wuxia heroine hair: center-parted soft bangs + graceful high chignon with jade pin + waist-length flowing back hair."""
    lm = head.landmarks
    nb = mb.num_bones
    b_head = bidx["mixamorig:Head"]

    _add_sculpted_crown(mb, head, prof, bidx, part_phi=0.0, loft_mult=1.08)

    # Graceful center-parted framing bangs + long shoulder-draping cheek locks
    cheek_y = lm.y(prof.cheek_y_frac)
    yr = lm.skull_top_y - lm.m(0.020)
    for sgn in (1.0, -1.0):
        sw_bone = "cloth_hair_bang_l" if sgn > 0 else "cloth_hair_bang_r"
        for xrf, xmf, xtf, ytf, hw, use_sw in [
            (0.06, 0.42, 0.76, 0.48, 0.015, False),
            (0.14, 0.68, 0.94, 0.20, 0.015, True),
            (0.62, 0.96, 0.92, -0.35, 0.014, True),
        ]:
            xr, xm, xt = sgn * lm.x(xrf), sgn * lm.x(xmf), sgn * lm.x(xtf)
            yt = lm.y(ytf)
            n_b = 11
            spine = np.zeros((n_b, 3), dtype=np.float64)
            out_h = np.zeros((n_b, 3), dtype=np.float64)
            rx = np.zeros(n_b, dtype=np.float64)
            rz = np.zeros(n_b, dtype=np.float64)
            col_r = np.ones((n_b, 3), dtype=np.float64)
            w_r = np.zeros((n_b, nb), dtype=np.float64)
            for k in range(n_b):
                t = k / (n_b - 1)
                x = (1 - t) ** 2 * xr + 2 * (1 - t) * t * xm + t ** 2 * xt
                y = yr * (1.0 - t ** 0.90) + yt * (t ** 0.90)
                if y >= cheek_y:
                    d_v = np.array([x, y, lm.forehead_z - 0.50 * abs(x)], dtype=np.float64) - head.center
                    pt, nrm = head.sample_skull_dir(d_v, offset=lm.m(0.010 + 0.010 * math.sin(t * math.pi * 0.85)))
                else:
                    pt = np.array([x + sgn * lm.m(0.008) * (t - 0.4), y, lm.center[2] + lm.m(0.048 - 0.010 * t)], dtype=np.float64)
                    nrm = np.array([sgn * 0.65, 0.0, 0.76], dtype=np.float64)
                spine[k] = pt
                out_h[k] = nrm
                taper = (0.45 + 0.55 * math.sin(t * math.pi * 0.78)) * (1.0 - 0.88 * (t ** 1.65))
                rx[k] = max(lm.m(0.0014), lm.m(hw) * taper)
                rz[k] = max(lm.m(0.0010), lm.m(hw) * 0.28 * taper)
                col_r[k] = 0.82 + 0.18 * math.sin(t * math.pi * 0.8)
                if use_sw:
                    w_sw = float(smoothstep(0.28, 0.78, t)) * 0.82
                    w_r[k, b_head] = 1.0 - w_sw
                    w_r[k, bidx[sw_bone]] = w_sw
                else:
                    w_r[k, b_head] = 1.0
            mb.add_tapered_tube(spine, rx, rz, col_r, w_r, MAT_HAIR, segs=8, uv_v_scale=2.2, out_hint=out_h)

    # High graceful chignon bun + celadon-jade hairpin
    bun_c = np.array([0.0, lm.skull_top_y + lm.m(0.008), lm.back_z + lm.m(0.044)], dtype=np.float64)
    n_bv, n_bu = 12, 24
    Pb = np.zeros((n_bv, n_bu, 3), dtype=np.float64)
    UVb = np.zeros((n_bv, n_bu, 2), dtype=np.float64)
    COLb = np.ones((n_bv, n_bu, 3), dtype=np.float64)
    Wb = np.zeros((n_bv, n_bu, nb), dtype=np.float64)
    Wb[:, :, b_head] = 1.0
    for r in range(n_bv):
        v = r / (n_bv - 1)
        phi_v = (v - 0.5) * math.pi * 0.92
        rad = lm.m(0.036) * math.cos(phi_v)
        ly = lm.m(0.026) * math.sin(phi_v)
        for i in range(n_bu):
            th = (i / n_bu) * 2.0 * math.pi
            Pb[r, i] = bun_c + np.array([rad * math.sin(th), ly, rad * math.cos(th)])
            UVb[r, i] = (i / n_bu * 3.0, v * 1.5)
            COLb[r, i] = 0.78 + 0.18 * v
    mb.add_grid(Pb, UVb, COLb, Wb, np.full((n_bv - 1, n_bu), MAT_HAIR, dtype=np.int32), wrap_u=True, flip=False)

    n_p = 12
    xs = np.linspace(-0.082, 0.082, n_p) * lm.scale
    sp_p = np.array([bun_c + np.array([float(xx), lm.m(0.004), 0.0]) for xx in xs])
    rad_p = np.array([0.0038, 0.0044, 0.0028] + [0.0024] * (n_p - 6) + [0.0028, 0.0044, 0.0038]) * lm.scale
    mb.add_tapered_tube(sp_p, rad_p, rad_p, np.full((n_p, 3), 0.96), head_weights(nb, bidx, n_p), MAT_JADE, segs=8)

    # Flowing waist-length silky back cape
    hy = float(lm.head_bone_pos[1])
    n_mr, n_mc = 17, 21
    Pm = np.zeros((n_mr, n_mc, 3), dtype=np.float64)
    UVm = np.zeros((n_mr, n_mc, 2), dtype=np.float64)
    COLm = np.ones((n_mr, n_mc, 3), dtype=np.float64)
    Wm = np.zeros((n_mr, n_mc, nb), dtype=np.float64)
    for ir in range(n_mr):
        t = ir / (n_mr - 1)
        for ic in range(n_mc):
            u = ic / (n_mc - 1)
            su = 2.0 * u - 1.0
            y_top = hy + lm.m(0.015) - lm.m(0.018) * (su ** 2)
            y_bot = hy - lm.m(0.460) + lm.m(0.095) * (su ** 2) + lm.m(0.016) * math.sin(6.0 * math.pi * u)
            y = y_top * (1.0 - t) + y_bot * t
            half_w = lm.x(0.92) + lm.m(0.058) * math.sin(t * math.pi * 0.72)
            x = su * half_w
            flute = lm.m(0.0026) * math.sin(8.0 * math.pi * u + 1.5 * t) * math.sin(t * math.pi)
            z = lm.back_z + lm.m(0.010) - lm.m(0.095) * (t ** 0.70) + lm.m(0.032) * (su ** 2) * (1.0 - 0.45 * t) - flute
            Pm[ir, ic] = [x, y, z]
            UVm[ir, ic] = (u * 4.0, t * 3.6)
            COLm[ir, ic] = 0.72 + 0.20 * math.sin(t * math.pi * 0.85) + 0.04 * math.sin(8.0 * math.pi * u)
            Wm[ir, ic] = _make_back_hair_weights(nb, bidx, t, su)
    mb.add_solidified_grid(
        Pm, UVm, COLm, Wm, np.full((n_mr - 1, n_mc - 1), MAT_HAIR, dtype=np.int32),
        thickness=lm.m(0.0038), wrap_u=False, flip=True, inner_shade=0.65,
    )


STYLE_BUILDERS = {
    "kiem_hiep_ponytail": build_kiem_hiep_ponytail,
    "nu_hiep_long": build_nu_hiep_long,
}
