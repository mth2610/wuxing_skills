#!/usr/bin/env python3
"""Male Body V6 — Upgraded Anatomical Sculpt & Game-Ready 20-Bone Mixamo Pipeline.

RUN: python male_body_standalone_v6.py
DEPS: python -m pip install numpy scipy
OPTIONAL: python -m pip install vtk pillow  (for --preview)

No external source GLB, JSON, or project Python file is required.
Source mesh: user-provided male-base-mesh-base_male.glb, Innerscene Corp,
CC0 1.0 per its glTF asset copyright field. Original bytes embedded losslessly.
Garment reference: only the necessary original skeleton metadata and inverse
bind matrices are embedded; garment vertices are not used by this pipeline.

EDITING WITH AI: all editable geometry, material, skinning and export logic
is above the EMBEDDED DATA marker. Run --print-code to extract that part
without the large source payload. Do not edit the encoded mesh by hand.
The default builds anatomy + V6 + 20-bone Mixamo T-Pose; --version all also
builds V4 and V5 for comparison.
"""
import argparse, base64, copy, hashlib, json, lzma, struct, sys, tempfile
from pathlib import Path
import numpy as np

_HERE = Path(__file__).resolve().parent
if str(_HERE) not in sys.path:
    sys.path.insert(0, str(_HERE))

from male_body_data import (
    GAME_MIXAMO_20,
    REFERENCE_INVERSE_BIND,
    REFERENCE_RIG_JSON,
    SOURCE_MESH_B85,
    SOURCE_SHA256,
)


def _quat_to_mat(q):
    x, y, z, w = q
    n = (x*x + y*y + z*z + w*w) ** 0.5
    if n > 1e-12:
        x, y, z, w = x/n, y/n, z/n, w/n
    return np.array([
        [1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
        [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
        [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)],
    ], dtype=float)


def _mat_to_quat(R):
    tr = R[0, 0] + R[1, 1] + R[2, 2]
    if tr > 0:
        s = 2.0 * (tr + 1.0) ** 0.5
        w = 0.25 * s
        x = (R[2, 1] - R[1, 2]) / s
        y = (R[0, 2] - R[2, 0]) / s
        z = (R[1, 0] - R[0, 1]) / s
    elif R[0, 0] > R[1, 1] and R[0, 0] > R[2, 2]:
        s = 2.0 * (1.0 + R[0, 0] - R[1, 1] - R[2, 2]) ** 0.5
        w = (R[2, 1] - R[1, 2]) / s
        x = 0.25 * s
        y = (R[0, 1] + R[1, 0]) / s
        z = (R[0, 2] + R[2, 0]) / s
    elif R[1, 1] > R[2, 2]:
        s = 2.0 * (1.0 + R[1, 1] - R[0, 0] - R[2, 2]) ** 0.5
        w = (R[0, 2] - R[2, 0]) / s
        x = (R[0, 1] + R[1, 0]) / s
        y = 0.25 * s
        z = (R[1, 2] + R[2, 1]) / s
    else:
        s = 2.0 * (1.0 + R[2, 2] - R[0, 0] - R[1, 1]) ** 0.5
        w = (R[1, 0] - R[0, 1]) / s
        x = (R[0, 2] + R[2, 0]) / s
        y = (R[1, 2] + R[2, 1]) / s
        z = 0.25 * s
    q = np.array([x, y, z, w], dtype=float)
    q /= max(np.linalg.norm(q), 1e-12)
    return q


def _quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return np.array([
        aw*bx + ax*bw + ay*bz - az*by,
        aw*by - ax*bz + ay*bw + az*bx,
        aw*bz + ax*by - ay*bx + az*bw,
        aw*bw - ax*bx - ay*by - az*bz,
    ], dtype=float)


def _smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _ga(x, c, s):
    return np.exp(-0.5 * ((x - c) / s) ** 2)


def _recompute_normals_split_safe(v, v_ref, n_ref, faces):
    """Area-weighted vertex normals preserving authored hard edges and seamless UV welds."""
    f0, f1, f2 = faces[:, 0], faces[:, 1], faces[:, 2]
    cross = np.cross(v[f1] - v[f0], v[f2] - v[f0])
    n = np.zeros_like(v, dtype=float)
    np.add.at(n, f0, cross)
    np.add.at(n, f1, cross)
    np.add.at(n, f2, cross)
    key = np.round(np.c_[v_ref, n_ref], 4)
    _, ii = np.unique(key, axis=0, return_inverse=True)
    nn = np.zeros((int(ii.max()) + 1, 3), dtype=float)
    np.add.at(nn, ii, n)
    n = nn[ii]
    lens = np.linalg.norm(n, axis=1)
    bad = lens < 1e-12
    n[bad] = n_ref[bad]
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)


def _taubin_smooth_masked(v, v_ref, faces, mask, iters=3, lam=0.35, mu=-0.36):
    """Volume-preserving Taubin smoothing over welded seam graph, strictly gated by mask."""
    if float(np.max(mask)) < 1e-4 or len(faces) == 0:
        return v
    _, inv = np.unique(np.round(v_ref, 6), axis=0, return_inverse=True)
    n_u = int(inv.max()) + 1
    cnt_u = np.maximum(np.bincount(inv, minlength=n_u), 1).astype(float)
    pos_u = np.zeros((n_u, 3), dtype=float)
    np.add.at(pos_u, inv, v)
    pos_u /= cnt_u[:, None]

    m_u = np.zeros(n_u, dtype=float)
    np.add.at(m_u, inv, mask)
    m_u = np.clip(m_u / cnt_u, 0.0, 1.0)[:, None]

    fu = inv[faces]
    edges = np.vstack([fu[:, [0, 1]], fu[:, [1, 2]], fu[:, [2, 0]]])
    edges = np.sort(edges, axis=1)
    edges = edges[edges[:, 0] != edges[:, 1]]
    edges = np.unique(edges, axis=0)
    u0, u1 = edges[:, 0], edges[:, 1]
    deg = np.maximum(
        np.bincount(u0, minlength=n_u) + np.bincount(u1, minlength=n_u), 1
    ).astype(float)[:, None]

    cur = pos_u.copy()
    for _ in range(iters):
        for factor in (lam, mu):
            nb = np.zeros_like(cur)
            np.add.at(nb, u0, cur[u1])
            np.add.at(nb, u1, cur[u0])
            lap = nb / deg - cur
            cur = cur + (factor * m_u) * lap
    return v + (cur - pos_u)[inv]


CT = {5120: 'i1', 5121: 'u1', 5122: '<i2', 5123: '<u2', 5125: '<u4', 5126: '<f4'}
DIM = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


class GLB:
    def __init__(self, path):
        b = bytes(path) if isinstance(path, (bytes, bytearray, memoryview)) else Path(path).read_bytes()
        magic, ver, size = struct.unpack_from('<4sII', b)
        if magic != b'glTF' or ver != 2 or size != len(b):
            raise ValueError('Not a valid GLB 2.0')
        self.d = {}
        self.b = b''
        p = 12
        while p < len(b):
            n, t = struct.unpack_from('<I4s', b, p)
            p += 8
            if t == b'JSON':
                self.d = json.loads(b[p:p+n])
            elif t == b'BIN\0':
                self.b = b[p:p+n]
            p += n
        self.parents = {c: i for i, n in enumerate(self.d['nodes']) for c in n.get('children', [])}
        self.world = {}

    def matrix(self, i):
        if i in self.world:
            return self.world[i]
        n = self.d['nodes'][i]
        if 'matrix' in n:
            m = np.array(n['matrix']).reshape(4, 4).T
        else:
            m = np.eye(4)
            m[:3, :3] = _quat_to_mat(n.get('rotation', [0, 0, 0, 1])) @ np.diag(n.get('scale', [1, 1, 1]))
            m[:3, 3] = n.get('translation', [0, 0, 0])
        if i in self.parents:
            m = self.matrix(self.parents[i]) @ m
        self.world[i] = m
        return m

    def read(self, i, normalize=False):
        a = self.d['accessors'][i]
        v = self.d['bufferViews'][a['bufferView']]
        dt = np.dtype(CT[a['componentType']])
        k = DIM[a['type']]
        if a.get('sparse') or v.get('buffer', 0) != 0:
            raise ValueError('Unsupported sparse/external accessor')
        x = np.ndarray(
            (a['count'], k), dtype=dt, buffer=self.b,
            offset=v.get('byteOffset', 0) + a.get('byteOffset', 0),
            strides=(v.get('byteStride', k * dt.itemsize), dt.itemsize)
        ).copy()
        if normalize and a.get('normalized') and dt.kind in 'iu':
            x = np.maximum(-1, x.astype(float) / np.iinfo(dt).max)
        return x


def _build_relaxed_hand_deltas(src):
    """Build per-joint 4x4 DQS deltas on source rig to relax wide-splayed hands into a natural martial-arts pose."""
    ss = src.d['skins'][0]
    sn = {n['name']: i for i, n in enumerate(src.d['nodes'])}
    joint_idx = {src.d['nodes'][nid]['name']: idx for idx, nid in enumerate(ss['joints'])}
    S = lambda k: src.matrix(sn[k])[:3, 3].copy()
    deltas = np.tile(np.eye(4, dtype=float), (len(ss['joints']), 1, 1))

    # Standard Mixamo flat/open T-pose hand: gentle adduction to align fingers parallel, 0 deg curl
    # so 52-bone Mixamo animations control fists, sword grips, and open palms without double-curling.
    finger_specs = {
        2: ( +4.0,  0.0,  0.0,  0.0),  # Index: open straight
        3: (  0.0,  0.0,  0.0,  0.0),  # Middle: open straight
        4: ( -2.5,  0.0,  0.0,  0.0),  # Ring: open straight
        5: ( -4.5,  0.0,  0.0,  0.0),  # Pinky: open straight
    }

    for side, s in (('L', 1.0), ('R', -1.0)):
        wr = S('wrist.' + side)
        f2 = S('finger2-1.' + side)
        f3 = S('finger3-1.' + side)
        f5 = S('finger5-1.' + side)
        d3 = f3 - wr
        d3 /= max(np.linalg.norm(d3), 1e-12)
        tp = f5 - f2
        tp /= max(np.linalg.norm(tp), 1e-12)
        palm_n = np.cross(tp, d3) if s > 0 else np.cross(d3, tp)
        palm_n /= max(np.linalg.norm(palm_n), 1e-12)
        abd_axis = -s * palm_n  # +angle sweeps finger toward pinky on both L and R

        # 4 fingers (Index..Pinky): planar hinge curl around flex1_ax across all 3 phalanges!
        for k, (abd_deg, c1, c2, c3) in finger_specs.items():
            b1, b2, b3 = f'finger{k}-1.{side}', f'finger{k}-2.{side}', f'finger{k}-3.{side}'
            if not (b1 in joint_idx and b2 in joint_idx and b3 in joint_idx):
                continue
            p1, p2, p3 = S(b1), S(b2), S(b3)
            u1 = p2 - p1
            u1 /= max(np.linalg.norm(u1), 1e-12)

            # Joint 1 (MCP): adduction in palm plane, then palmar flexion around constant hinge flex_ax
            R_abd = _rot_axis_angle(abd_axis, np.radians(abd_deg))
            u1_a = R_abd @ u1
            flex_ax = np.cross(u1_a, palm_n)
            flex_ax /= max(np.linalg.norm(flex_ax), 1e-12)
            R1 = _rot_axis_angle(flex_ax, np.radians(c1)) @ R_abd
            D1 = _rigid_delta(R1, p1, p1)

            # Joint 2 (PIP): same planar hinge axis flex_ax (no cross-product singularity!)
            p2_new = (D1[:3, :3] @ p2) + D1[:3, 3]
            R2 = _rot_axis_angle(flex_ax, np.radians(c2)) @ R1
            D2 = _rigid_delta(R2, p2, p2_new)

            # Joint 3 (DIP): same planar hinge axis flex_ax
            p3_new = (D2[:3, :3] @ p3) + D2[:3, 3]
            R3 = _rot_axis_angle(flex_ax, np.radians(c3)) @ R2
            D3 = _rigid_delta(R3, p3, p3_new)

            deltas[joint_idx[b1]] = D1
            deltas[joint_idx[b2]] = D2
            deltas[joint_idx[b3]] = D3

        # Thumb (k=1): keep natural open Mixamo T-pose (0 deg curl; animations drive grip/opposition)
    return deltas


def build_anatomy(source, reference, ROOT):
    src = GLB(source)
    ref = GLB(reference)
    ss = src.d['skins'][0]
    rs = ref.d['skins'][0]
    ib = ref.read(rs['inverseBindMatrices'])
    rm = np.linalg.inv(ib.reshape(-1, 4, 4).transpose(0, 2, 1))
    rn = {ref.d['nodes'][i]['name'].split(':')[-1]: j for j, i in enumerate(rs['joints'])}
    sn = {n['name']: i for i, n in enumerate(src.d['nodes'])}
    S = lambda k: src.matrix(sn[k])[:3, 3]

    mp = {
        'root': 'Hips', 'pelvis.L': 'Hips', 'pelvis.R': 'Hips', 'spine05': 'Hips',
        'spine04': 'Spine', 'spine03': 'Spine1', 'spine02': 'Spine1', 'spine01': 'Spine2',
        'breast.L': 'Spine2', 'breast.R': 'Spine2',
        'neck01': 'Neck', 'neck02': 'Neck', 'neck03': 'Neck', 'head': 'Head',
    }
    for s, side in [('L', 'Left'), ('R', 'Right')]:
        for a, b in [
            ('clavicle', 'Shoulder'), ('shoulder01', 'Shoulder'),
            ('upperarm01', 'Arm'), ('upperarm02', 'Arm'),
            ('lowerarm01', 'ForeArm'), ('lowerarm02', 'ForeArm'),
            ('wrist', 'Hand'),
            ('upperleg01', 'UpLeg'), ('upperleg02', 'UpLeg'),
            ('lowerleg01', 'Leg'), ('lowerleg02', 'Leg'),
            ('foot', 'Foot'),
        ]:
            mp[a + '.' + s] = side + b

    reverse = {sn[k]: v for k, v in mp.items()}

    def mapnode(i):
        while i not in reverse:
            i = src.parents[i]
        return reverse[i]

    jsrc = np.array([rn[mapnode(i)] for i in ss['joints']])
    finger_deltas = _build_relaxed_hand_deltas(src)

    parts = []
    for m in src.d['meshes']:
        for p in m['primitives']:
            at = p['attributes']
            v = src.read(at['POSITION']).astype(float)
            uv = src.read(at['TEXCOORD_0'])
            n = src.read(at['NORMAL'])
            f = src.read(p['indices']).reshape(-1, 3).astype(int)
            j = src.read(at['JOINTS_0'])
            w = src.read(at['WEIGHTS_0'], True)
            weights = np.zeros((len(v), 40))
            for c in range(4):
                np.add.at(weights, (np.arange(len(v)), jsrc[j[:, c]]), w[:, c])
            weights /= np.maximum(weights.sum(1, keepdims=True), 1e-12)
            # Blend neck/head weights smoothly below the jaw; preserve facial geometry.
            q = np.clip((v[:, 1] - 1.49) / (1.575 - 1.49), 0, 1)
            q = q * q * (3 - 2 * q)
            weights *= 1 - q[:, None]
            weights[:, rn['Head']] += q
            # Relax wide-splayed fingers via source phalanx rig (moves skin + fingernails together)
            w_src = w.astype(float)
            w_src /= np.maximum(w_src.sum(axis=1, keepdims=True), 1e-12)
            v_posed, _ = _dqs_skin(v, n.astype(float), j.astype(int), w_src, finger_deltas)
            parts.append(dict(v=v, out=v_posed * 0.92 + np.array([0, 0, -0.025]), uv=uv, n=n, f=f, w=weights))

    # Weld positional/weight seams across UV duplicates without altering mesh connectivity.
    vall = np.concatenate([p['v'] for p in parts])
    outall = np.concatenate([p['out'] for p in parts])
    wall = np.concatenate([p['w'] for p in parts])
    _, inv = np.unique(np.round(vall, 6), axis=0, return_inverse=True)
    cnt = np.bincount(inv)
    avg = np.zeros((len(cnt), 3))
    aw = np.zeros((len(cnt), 40))
    np.add.at(avg, inv, outall)
    np.add.at(aw, inv, wall)
    avg /= cnt[:, None]
    aw /= cnt[:, None]
    pos = 0
    for p in parts:
        l = len(p['v'])
        p['out'] = avg[inv[pos:pos+l]]
        p['w'] = aw[inv[pos:pos+l]]
        pos += l

    # Recompute surface normals after retargeting, respecting source normal splits.
    for p in parts:
        p['norm'] = _recompute_normals_split_safe(p['out'], p['v'], p['n'], p['f'])

    # Body-led skeleton aligned to source anatomy pivots.
    newM = rm.copy()
    targets = {'Hips': 'spine05', 'Spine': 'spine04', 'Spine1': 'spine03', 'Spine2': 'spine01', 'Neck': 'neck01', 'Head': 'head'}
    for side, s in [('Left', 'L'), ('Right', 'R')]:
        for a, b in [
            ('Shoulder', 'clavicle'), ('Arm', 'upperarm01'), ('ForeArm', 'lowerarm01'), ('Hand', 'wrist'),
            ('UpLeg', 'upperleg01'), ('Leg', 'lowerleg01'), ('Foot', 'foot'),
        ]:
            targets[side + a] = b + '.' + s
    for k, v in targets.items():
        newM[rn[k], :3, 3] = S(v) * 0.92 + np.array([0, 0, -0.025])
    jointidx = {n: i for i, n in enumerate(rs['joints'])}
    for i, n in enumerate(rs['joints']):
        if i in [rn[k] for k in targets]:
            continue
        p = ref.parents.get(n)
        if p in jointidx:
            newM[i, :3, 3] = newM[jointidx[p], :3, 3] + rm[i, :3, 3] - rm[jointidx[p], :3, 3]
    newIB = np.linalg.inv(newM).transpose(0, 2, 1).reshape(-1, 16).astype('f4')

    # Tailored shorts with subtle waistband ridge, hem cuff, and natural gluteal/groin bridge.
    def garment(p):
        v = p['out']; f = p['f']; u = p['uv']; n = p['norm']; w = p['w']
        a = np.c_[v, n, u, w, p['v']]
        yc = p['v'][f, 1]; xx = np.abs(p['v'][f, 0])
        chosen = f[(yc.max(1) > 0.745) & (yc.min(1) < 1.06) & (xx.max(1) < 0.27)]

        def clip(poly, h, sgn):
            res = []
            for i, x in enumerate(poly):
                y = poly[(i + 1) % len(poly)]
                d = (x[-2] - h) * sgn
                e = (y[-2] - h) * sgn
                if d >= 0:
                    res.append(x)
                if (d >= 0) != (e >= 0):
                    res.append(x + (y - x) * d / (d - e))
            return res

        arr = []; faces = []
        for tri in chosen:
            poly = clip(a[tri], 0.745, 1)
            if len(poly) < 3:
                continue
            poly = clip(poly, 1.06, -1)
            if len(poly) < 3:
                continue
            j = len(arr)
            arr.extend(poly)
            faces.extend((j, j + k, j + k + 1) for k in range(1, len(poly) - 1))
        arr = np.array(arr)
        faces = np.array(faces)
        _, ii, iv = np.unique(np.round(arr[:, :3], 6), axis=0, return_index=True, return_inverse=True)
        arr = arr[ii]
        faces = iv[faces]
        faces = faces[(faces[:, 0] != faces[:, 1]) & (faces[:, 1] != faces[:, 2]) & (faces[:, 2] != faces[:, 0])]

        norm = arr[:, 3:6]
        norm /= np.maximum(np.linalg.norm(norm, axis=1, keepdims=True), 1e-9)
        vy = arr[:, -2]  # unscaled Y in [0.745, 1.06]
        vx = np.abs(arr[:, -3])
        vz = arr[:, -1]

        # Base ease + waistband ridge + bottom hem cuff so shorts read as tailored cloth.
        waistband = _smooth(1.022, 1.045, vy) * 0.0024
        leg_hem = (1.0 - _smooth(0.752, 0.774, vy)) * 0.0014
        ease = 0.0052 + waistband + leg_hem
        out = arr[:, :3] + norm * ease[:, None]

        # Gently bridge deep gluteal cleft & front groin crease so fabric does not vacuum-seal.
        glute_bridge = _ga(vx, 0.0, 0.036) * (1.0 - _smooth(0.95, 1.02, vy)) * (1.0 - _smooth(-0.02, 0.02, vz))
        groin_bridge = _ga(vx, 0.0, 0.032) * (1.0 - _smooth(0.89, 0.96, vy)) * _smooth(0.02, 0.07, vz)
        out[:, 2] -= 0.0065 * glute_bridge
        out[:, 2] += 0.0040 * groin_bridge

        norm_new = _recompute_normals_split_safe(out, arr[:, -3:], norm, faces)
        return {'out': out, 'v': arr[:, -3:], 'norm': norm_new, 'uv': arr[:, 6:8], 'w': arr[:, 8:48], 'f': faces}

    def export(path, shorts=False):
        d = copy.deepcopy(ref.d)
        d['asset']['generator'] = 'Source human mesh fit / original 40-joint rig'
        d['asset']['extras'] = {'source': 'User-provided male-base-mesh-base_male.glb', 'reference': 'ao_giao_linh_4.glb'}
        d['meshes'] = []; d['accessors'] = []; d['bufferViews'] = []; d['buffers'] = []
        d.pop('animations', None)
        data = bytearray()

        def add(a, typ, ct, target=None, bounds=False):
            a = np.ascontiguousarray(a, dtype=CT[ct])
            data.extend(b'\0' * ((-len(data)) % 4))
            off = len(data)
            data.extend(a.tobytes())
            view = {'buffer': 0, 'byteOffset': off, 'byteLength': a.nbytes}
            if target:
                view['target'] = target
            d['bufferViews'].append(view)
            acc = {'bufferView': len(d['bufferViews']) - 1, 'componentType': ct, 'count': len(a), 'type': typ}
            if bounds:
                acc.update(min=a.min(0).tolist(), max=a.max(0).tolist())
            d['accessors'].append(acc)
            return len(d['accessors']) - 1

        d['skins'][0]['inverseBindMatrices'] = add(newIB, 'MAT4', 5126)
        for i, node in enumerate(rs['joints']):
            parent = ref.parents.get(node)
            pm = newM[jointidx[parent]] if parent in jointidx else (ref.matrix(parent) if parent is not None else np.eye(4))
            local = np.linalg.inv(pm) @ newM[i]
            n = d['nodes'][node]
            n.pop('matrix', None)
            n['translation'] = local[:3, 3].tolist()

        def mat(name, color, rough=0.72, double_sided=False):
            return {
                'name': name,
                'doubleSided': double_sided,
                'pbrMetallicRoughness': {
                    'baseColorFactor': [*color, 1],
                    'metallicFactor': 0,
                    'roughnessFactor': rough,
                },
            }

        # Closed anatomical primitives use doubleSided=False for clean backface culling.
        d['materials'] = [
            mat('Neutral warm skin', [0.54, 0.39, 0.30], 0.70, False),
            mat('Nails', [0.56, 0.40, 0.34], 0.42, False),
            mat('Lips / mouth sculpt', [0.47, 0.30, 0.25], 0.56, False),
            mat('Eye sclera', [0.64, 0.61, 0.57], 0.24, False),
            mat('Athletic shorts - charcoal indigo', [0.032, 0.044, 0.068], 0.88, True),
            mat('Iris dark brown', [0.062, 0.030, 0.017], 0.18, False),
            mat('Pupil', [0.008, 0.008, 0.008], 0.14, False),
        ]

        prims = []; totalv = totalt = 0
        for i, p in enumerate(parts + ([garment(parts[0])] if shorts else [])):
            f = p['f']; v = p['out']; w = p['w']
            j = np.argsort(-w, axis=1)[:, :4]
            ww = np.take_along_axis(w, j, axis=1)
            ww /= np.maximum(ww.sum(1, keepdims=True), 1e-12)
            j[ww == 0] = 0
            mi = [0, 0, 2, 1, 1, 0, 3, 4][i]
            groups = [(mi, f)]
            if i == 6:
                c = p['v'][f].mean(1)
                rad = np.sqrt((np.abs(c[:, 0]) - 0.0308) ** 2 + (c[:, 1] - 1.6751) ** 2)
                front = c[:, 2] > 0.138
                iris = front & (rad < 0.0061)
                pupil = front & (rad < 0.00265)
                groups = [(3, f[~iris]), (5, f[iris & ~pupil]), (6, f[pupil])]
            for material, faces in groups:
                if not len(faces):
                    continue
                ids, idx = np.unique(faces.ravel(), return_inverse=True)
                assert len(ids) < 65536
                attrs = {
                    'POSITION': add(v[ids], 'VEC3', 5126, 34962, True),
                    'NORMAL': add(p['norm'][ids], 'VEC3', 5126, 34962),
                    'TEXCOORD_0': add(p['uv'][ids], 'VEC2', 5126, 34962),
                    'JOINTS_0': add(j[ids], 'VEC4', 5121, 34962),
                    'WEIGHTS_0': add(ww[ids], 'VEC4', 5126, 34962),
                }
                prims.append({'attributes': attrs, 'indices': add(idx, 'SCALAR', 5123, 34963), 'material': material, 'mode': 4})
                totalv += len(ids)
                totalt += len(faces)

        d['meshes'] = [{'name': 'MaleBody_AnatomyPrototype', 'primitives': prims}]
        for n in d['nodes']:
            if 'mesh' in n:
                n['name'] = 'MaleBody_AnatomyPrototype'
                n['mesh'] = 0
        d['buffers'] = [{'byteLength': len(data)}]
        js = json.dumps(d, separators=(',', ':')).encode()
        js += b' ' * ((-len(js)) % 4)
        data.extend(b'\0' * ((-len(data)) % 4))
        size = 28 + len(js) + len(data)
        Path(path).write_bytes(
            struct.pack('<4sII', b'glTF', 2, size)
            + struct.pack('<I4s', len(js), b'JSON') + js
            + struct.pack('<I4s', len(data), b'BIN\0') + data
        )

        g = GLB(path)
        assert g.d['skins'][0]['joints'] == rs['joints']
        assert np.array_equal(g.read(g.d['skins'][0]['inverseBindMatrices']), newIB)
        assert all(g.d['nodes'][i]['name'] == ref.d['nodes'][i]['name'] for i in rs['joints'])
        print(
            path, totalv, totalt, 'bounds',
            np.min(np.concatenate([p['out'] for p in parts]), 0),
            np.max(np.concatenate([p['out'] for p in parts]), 0),
        )
        for p in g.d['meshes'][0]['primitives']:
            a = p['attributes']
            v = g.read(a['POSITION']); w = g.read(a['WEIGHTS_0']); j = g.read(a['JOINTS_0']); f = g.read(p['indices']).reshape(-1, 3)
            assert np.isfinite(v).all() and np.isfinite(w).all() and (j < 40).all() and f.max() < len(v) and np.max(np.abs(w.sum(1) - 1)) < 1e-6

    export(ROOT / 'male_body_anatomy.glb')
    export(ROOT / 'male_body_anatomy_shorts.glb', True)


def build_athletic(D):
    """Legacy V4 athletic sculpt (kept for --version v4 / all comparison)."""
    smooth, ga = _smooth, _ga

    def sculpt(v):
        x, y, z = v.T; a = np.abs(x); out = v.copy().astype(float)
        torso = 1 - smooth(.16, .27, a)
        front = smooth(.022, .075, z); back = 1 - smooth(-.07, -.015, z)
        broad = ga(y, 1.265, .085) * ga(a, .14, .10)
        out[:, 0] += np.sign(x) * (.013 * broad + .004 * ga(y, 1.17, .09) * ga(a, .12, .065)) * smooth(.018, .065, a)
        pec = (ga(x, .084, .047) + ga(x, -.084, .047)) * ga(y, 1.238, .037) * (1 - smooth(.15, .20, a))
        lowerline = ga(y, 1.187 + .13 * (a - .075), .010) * ga(a, .085, .048) * smooth(.02, .04, a)
        out[:, 2] += front * (.030 * pec - .0025 * lowerline)
        out[:, 2] -= back * torso * (.012 * ga(y, 1.25, .085) * ga(a, .10, .07) + .005 * ga(y, 1.13, .075) * ga(a, .09, .05))
        absprofile = sum(h * ga(y, c, .0155) for c, h in [(1.158, .0145), (1.115, .014), (1.073, .0115)])
        abx = (ga(x, .035, .022) + ga(x, -.035, .022)) * (1 - smooth(.075, .11, a))
        oblique = .007 * ga(a, .099, .027) * ga(y, 1.10, .060)
        belly = .004 * ga(y, 1.07, .079) * ga(a, .045, .057)
        out[:, 2] += front * (abx * absprofile + oblique + belly - .0015 * ga(x, 0, .012) * ga(y, 1.115, .055))
        nr = ((a - .085) / .0085) ** 2 + ((y - 1.205) / .0075) ** 2
        nipple = .0018 * np.exp(-.5 * nr) + .0032 * np.exp(-.5 * (((a - .085) / .0027) ** 2 + ((y - 1.205) / .0026) ** 2))
        nav = ((x) / .0075) ** 2 + ((y - 1.041) / .0055) ** 2
        out[:, 2] += front * (nipple - .0045 * np.exp(-.5 * nav) + .0015 * ga(x, 0, .011) * ga(y, 1.049, .0045))
        for s in [-1, 1]:
            for A, B, amount, c, w in [
                ([s * .187, 1.315, -.008], [s * .344, 1.133, -.009], .22, .30, .25),
                ([s * .344, 1.133, -.009], [s * .471, 1.012, .172], .13, .35, .25),
                ([s * .102, .883, -.032], [s * .14, .48, 0], .115, .32, .28),
                ([s * .14, .48, 0], [s * .186, .068, -.012], .14, .34, .20),
            ]:
                A = np.array(A); B = np.array(B); ab = B - A; t = (v - A) @ ab / (ab @ ab); radial = v - A - t[:, None] * ab
                gate = smooth(-.10, .10, t) * (1 - smooth(.78, 1.02, t)) * ga(t, c, w)
                if A[1] > 1:
                    gate *= smooth(.155, .235, a) * (x * s > 0) * (1 - smooth(.075, .14, np.linalg.norm(radial, axis=1)))
                else:
                    gate *= smooth(.018, .065, x * s) * (1 - smooth(.09, .15, np.linalg.norm(radial, axis=1)))
                out += radial * (amount * gate)[:, None]
        return out

    def colors(v):
        x, y, z = v.T; a = abs(x); front = smooth(.025, .075, z)
        r = ((a - .085) / .009) ** 2 + ((y - 1.205) / .008) ** 2
        areola = np.exp(-.5 * r) ** 1.3 * front
        tip = ga(a, .085, .003) * ga(y, 1.205, .003) * front
        navel = ga(x, 0, .006) * ga(y, 1.041, .004) * front
        col = np.ones((len(v), 3))
        col -= areola[:, None] * np.array([.13, .30, .33])
        col -= tip[:, None] * np.array([.065, .10, .10])
        col -= navel[:, None] * np.array([.23, .27, .30])
        return np.clip(col, 0, 1)

    _export_sculpted_pair(
        D, sculpt, colors,
        ('male_body_athletic.glb', 'male_body_athletic_shorts.glb'),
        'MaleBody_Athletic_Master40', 'Male Anatomy athletic surface sculpt (V4)',
    )


def build_refined(D):
    """Legacy V5 refined sculpt (kept for --version v5 / all comparison)."""
    smooth, ga = _smooth, _ga

    def sculpt(v):
        x, y, z = v.T; a = np.abs(x); out = v.copy().astype(float)
        torso = 1 - smooth(.16, .27, a); front = smooth(.022, .075, z); back = 1 - smooth(-.07, -.015, z)
        out[:, 0] += np.sign(x) * (.014 * ga(y, 1.285, .077) * ga(a, .155, .071) + .006 * ga(y, 1.17, .09) * ga(a, .12, .07)) * smooth(.02, .065, a)
        pec = (ga(x, .092, .058) + ga(x, -.092, .058)) * ga(y, 1.245, .054) * (1 - smooth(.16, .23, a))
        lowerline = ga(y, 1.188 + .12 * (a - .09), .014) * ga(a, .095, .055) * smooth(.02, .06, a)
        sternum = ga(x, 0, .016) * ga(y, 1.24, .05)
        out[:, 2] += front * (.035 * pec - .004 * lowerline - .004 * sternum)
        out[:, 2] += front * (.005 * ga(y, 1.323 - .12 * a, .019) * ga(a, .087, .066) + .007 * ga(y, 1.30, .041) * ga(a, .192, .046))
        out[:, 2] -= back * torso * (.018 * ga(y, 1.25, .09) * ga(a, .105, .07) + .007 * ga(y, 1.15, .078) * ga(a, .10, .06))
        ay = y - .09 * (a - .035)
        absprofile = sum(h * ga(ay, c, w) for c, h, w in [(1.157, .013, .0175), (1.112, .012, .0175), (1.067, .009, .0185)])
        abx = (ga(x, .041, .025) + ga(x, -.041, .025)) * (1 - smooth(.079, .125, a))
        oblique = .008 * ga(a, .103, .030) * ga(y, 1.105, .072)
        belly = .006 * ga(y, 1.088, .081) * ga(a, .045, .065)
        out[:, 2] += front * (abx * absprofile + oblique + belly - .0017 * ga(x, 0, .012) * ga(y, 1.111, .058))
        nipple = .0010 * ga(a, .091, .008) * ga(y, 1.205, .007) + .0010 * ga(a, .091, .003) * ga(y, 1.205, .003)
        nav = ga(x, 0, .0075) * ga(y, 1.041, .0055)
        out[:, 2] += front * (nipple - .0033 * nav + .0010 * ga(x, 0, .011) * ga(y, 1.049, .005))
        for s in [-1, 1]:
            for A, B, amount, c, w in [
                ([s * .187, 1.315, -.008], [s * .344, 1.133, -.009], .27, .37, .27),
                ([s * .344, 1.133, -.009], [s * .471, 1.012, .172], .085, .27, .26),
                ([s * .102, .883, -.032], [s * .14, .48, 0], .10, .36, .28),
                ([s * .14, .48, 0], [s * .186, .068, -.012], .07, .30, .23),
            ]:
                A = np.array(A); B = np.array(B); ab = B - A; t = (v - A) @ ab / (ab @ ab); radial = v - A - t[:, None] * ab
                gate = smooth(-.14, .10, t) * (1 - smooth(.75, 1.06, t)) * ga(t, c, w)
                if A[1] > 1:
                    gate *= smooth(.15, .23, a) * (x * s > 0) * (1 - smooth(.075, .14, np.linalg.norm(radial, axis=1)))
                else:
                    gate *= smooth(.018, .065, x * s) * (1 - smooth(.09, .15, np.linalg.norm(radial, axis=1)))
                out += radial * (amount * gate)[:, None]
        out[:, 0] += np.sign(x) * .0035 * ga(a, .045, .014) * ga(y, 1.466, .024) * smooth(.035, .08, z)
        return out

    def colors(v):
        x, y, z = v.T; a = abs(x); front = smooth(.025, .075, z)
        col = np.ones((len(v), 3))
        areola = ga(a, .091, .008) * ga(y, 1.205, .007) * front
        tip = ga(a, .091, .003) * ga(y, 1.205, .003) * front
        navel = ga(x, 0, .005) * ga(y, 1.041, .004) * front
        col -= areola[:, None] * np.array([.14, .29, .32])
        col -= tip[:, None] * np.array([.04, .07, .07])
        col -= navel[:, None] * np.array([.15, .19, .21])
        blush = ga(a, .043, .018) * ga(y, 1.512, .021) * smooth(.068, .10, z) + .4 * ga(a, 0, .016) * ga(y, 1.509, .022) * smooth(.09, .12, z)
        col -= blush[:, None] * np.array([.008, .055, .067])
        browy = 1.559 + .08 * (a - .028) - 5.0 * (a - .029) ** 2
        brow = ga(y, browy, .0026) * smooth(.009, .018, a) * (1 - smooth(.041, .053, a)) * smooth(.075, .097, z)
        col -= brow[:, None] * np.array([.69, .76, .79])
        return np.clip(col, 0, 1)

    _export_sculpted_pair(
        D, sculpt, colors,
        ('male_body_refined.glb', 'male_body_refined_shorts.glb'),
        'MaleBody_Refined_Master40', 'Male Anatomy refined surface sculpt (V5)',
        mat_overrides={
            0: {'baseColorFactor': [0.57, 0.395, 0.30, 1], 'roughnessFactor': 0.82},
            2: {'baseColorFactor': [0.50, 0.30, 0.25, 1]},
            3: {'baseColorFactor': [0.51, 0.49, 0.45, 1]},
        },
    )


def build_v6(D):
    """V6 Anatomical Martial-Athlete Sculpt (Refined):
    - Balanced athletic proportions: natural V-taper, iliac/gluteus-medius hip flow (no violin-hip pinch)
    - Broad, flat-fronted trapezoidal pectorals (no overhang), upward S-curve clavicles, jugular notch & SCM cords
    - Anatomically positioned Adonis belt (V-cut), refined 6-pack, obliques & serratus anterior
    - Sculpted back (trapezius slope, lats, scapula, erector spinae) & firm glutes
    - Stronger balanced arms (3-head deltoid, biceps/brachialis/triceps, brachioradialis/flexors)
    - Sleek powerful legs (controlled quads/VMO/hamstrings, diamond gastrocnemius, crisp Achilles taper)
    - Eye-safe masculine facial structure + soft anatomically registered micro-AO & natural warm skin shading
    """
    smooth, ga = _smooth, _ga

    def _limb_basis(A, B, lat_hint, fwd_hint):
        ab = B - A
        L = np.linalg.norm(ab)
        u = ab / max(L, 1e-12)
        lat = lat_hint - (lat_hint @ u) * u
        lat /= max(np.linalg.norm(lat), 1e-12)
        fwd = fwd_hint - (fwd_hint @ u) * u
        fwd /= max(np.linalg.norm(fwd), 1e-12)
        return ab, u, lat, fwd

    def sculpt(v):
        x, y, z = v.T
        a = np.abs(x)
        sx = np.sign(x)
        out = v.copy().astype(float)

        torso = 1.0 - smooth(0.165, 0.255, a)
        front = smooth(0.014, 0.064, z)
        back = 1.0 - smooth(-0.064, -0.014, z)
        # Strict orbital protection so eyelids never deform into eyeballs
        eye_protect = 1.0 - ga(a, 0.0285, 0.016) * ga(y, 1.541, 0.016) * smooth(0.06, 0.10, z)

        # 1. Balanced Martial-Athlete Silhouette: clean V-taper + natural iliac/trochanter hip flow
        upper_broad = ga(y, 1.275, 0.072) * ga(a, 0.148, 0.065)
        lat_flare = ga(y, 1.190, 0.058) * ga(a, 0.135, 0.052) * (1.0 - 0.50 * front)
        waist_trim = ga(y, 1.062, 0.044) * ga(a, 0.122, 0.040) * smooth(0.055, 0.105, a)
        iliac_shelf = ga(y, 0.975, 0.042) * ga(a, 0.132, 0.038) * smooth(0.045, 0.095, a)
        glute_med_lat = (
            ga(y, 0.885, 0.052) * ga(a, 0.145, 0.040)
            * smooth(0.06, 0.11, a) * (1.0 - smooth(0.195, 0.245, a))
        )
        out[:, 0] += sx * (
            (0.0095 * upper_broad + 0.0085 * lat_flare) * smooth(0.02, 0.065, a)
            - 0.0024 * waist_trim
            + 0.0044 * iliac_shelf
            + 0.0058 * glute_med_lat
        )

        # 2. Continuous Trapezius-to-Acromion Slope, Athletic Neck & SCM Cords + Suprasternal Notch
        # Flows seamlessly all the way out to the acromion (a=0.20) with zero mid-shoulder bump or notch
        trap_y = 1.402 - 0.34 * np.clip(a - 0.045, 0.0, 0.16)
        trap_mask = (
            ga(y, trap_y, 0.028)
            * smooth(0.032, 0.062, a) * (1.0 - smooth(0.150, 0.205, a))
            * (1.0 - smooth(0.025, 0.068, z))
        )
        out[:, 1] += 0.0040 * trap_mask
        out[:, 2] -= 0.0042 * trap_mask * (1.0 - smooth(-0.015, 0.030, z))

        # Subtle neck athletic solidity (sternocleidomastoid / splenius width)
        neck_lat = (
            ga(a, 0.054, 0.016)
            * smooth(1.340, 1.370, y) * (1.0 - smooth(1.425, 1.455, y))
            * (1.0 - smooth(0.055, 0.090, z))
        )
        out[:, 0] += sx * 0.0024 * neck_lat

        # V-shaped SCM cords from behind ear down to sternoclavicular joint
        scm_x = 0.015 + 0.34 * np.clip(y - 1.324, 0.0, 0.105)
        scm = (
            ga(a, scm_x, 0.0105)
            * smooth(1.320, 1.342, y)
            * (1.0 - smooth(1.408, 1.438, y))
            * smooth(0.002, 0.042, z)
        )
        out[:, 2] += 0.0022 * scm
        out[:, 0] += sx * 0.0014 * scm

        # Suprasternal (jugular) notch hollow between medial clavicle heads
        jugular = ga(x, 0.0, 0.015) * ga(y, 1.323, 0.013) * front
        out[:, 2] -= 0.0024 * jugular

        # 3. Upward S-Curve Clavicle & Broad, Flat-Fronted Trapezoidal Pectoralis Major
        # Real clavicles slope gently UPWARD from suprasternal notch (1.322) to acromion (1.350)
        clav_y = 1.322 + 0.175 * np.clip(a - 0.018, 0.0, 0.16)
        clav_bone = (
            ga(y, clav_y, 0.0105)
            * smooth(0.018, 0.042, a) * (1.0 - smooth(0.140, 0.178, a))
        )
        supra_clav = (
            ga(y, clav_y + 0.016, 0.013)
            * smooth(0.028, 0.052, a) * (1.0 - smooth(0.085, 0.118, a))
            * smooth(0.005, 0.048, z)
        )
        infra_clav = ga(y, clav_y - 0.017, 0.014) * ga(a, 0.082, 0.044)

        # Lower pec edge sits comfortably ~2.5 cm below nipple (y=1.205), sweeping smoothly into anterior deltoid
        pec_bot = 1.180 + 0.04 * np.clip(a - 0.03, 0.0, 0.08) + 2.0 * np.maximum(a - 0.105, 0.0) ** 2
        pec_top = clav_y - 0.012
        pec_y = smooth(pec_bot - 0.022, pec_bot + 0.026, y) * (1.0 - smooth(pec_top - 0.034, pec_top + 0.024, y))
        pec_x = smooth(0.009, 0.036, a) * (1.0 - smooth(0.158, 0.212, a))
        # Broad athletic plate profile (fills upper-inner and mid chest evenly, avoiding conical peaks)
        pec_plate = 0.85 + 0.20 * ga(y, 1.242, 0.044) * ga(a, 0.088, 0.060)
        pec = pec_x * pec_y * pec_plate

        # Solid thoracic ribcage foundation behind pecs so side profile looks barrel-chested, not pasted on
        ribcage = ga(y, 1.240, 0.068) * (1.0 - smooth(0.115, 0.175, a))

        # Soften inferior pectoral groove before reaching lateral axilla to avoid outer-corner pinch
        pec_fold = ga(y, pec_bot - 0.004, 0.015) * smooth(0.024, 0.055, a) * (1.0 - smooth(0.120, 0.155, a))
        sternum = ga(x, 0.0, 0.015) * ga(y, 1.248, 0.052)
        # Subtle diagonal deltopectoral groove from clavicular fossa down-outward to anterior axilla
        deltopec_x = 0.150 + 0.30 * np.clip(1.310 - y, -0.04, 0.08)
        deltopec = ga(a, deltopec_x, 0.015) * smooth(1.238, 1.262, y) * (1.0 - smooth(1.315, 1.345, y))

        out[:, 2] += front * (
            0.0175 * pec
            + 0.0048 * ribcage
            - 0.0017 * pec_fold
            - 0.0024 * sternum
            + 0.0030 * clav_bone
            - 0.0014 * supra_clav
            - 0.0014 * infra_clav
            - 0.0010 * deltopec
        )

        # 4. Refined Abdominal Wall, External Obliques, Serratus Anterior & Anatomically True Adonis Belt
        ay = y - 0.06 * (a - 0.036)
        ab_rows = (
            0.0090 * ga(ay, 1.158, 0.0175)
            + 0.0092 * ga(ay, 1.112, 0.0175)
            + 0.0078 * ga(ay, 1.066, 0.0185)
            + 0.0052 * ga(y, 1.005, 0.032)
        )
        ab_x = (ga(x, 0.038, 0.024) + ga(x, -0.038, 0.024)) * (1.0 - smooth(0.072, 0.112, a))
        linea_alba = ga(x, 0.0, 0.0115) * smooth(1.032, 1.058, y) * (1.0 - smooth(1.165, 1.192, y))
        semilunaris = ga(a, 0.076, 0.014) * smooth(0.995, 1.030, y) * (1.0 - smooth(1.155, 1.185, y))
        # External oblique flank pad thickening down over iliac crest
        oblique = 0.0070 * ga(a, 0.106, 0.030) * ga(y, 1.055, 0.072)
        belly = 0.0035 * ga(y, 1.060, 0.085) * ga(a, 0.045, 0.065)

        # Anatomically accurate Adonis belt / inguinal V-line:
        # slopes from ASIS (a=0.118, y=0.968) down-inward to pubic symphysis (a=0.035, y=0.875)
        vcut_y = 0.875 + 1.12 * np.clip(a - 0.035, 0.0, 0.090)
        vcut_span = smooth(0.032, 0.062, a) * (1.0 - smooth(0.112, 0.145, a))
        vcut_ridge = 0.0032 * ga(y, vcut_y + 0.016, 0.017) * vcut_span
        vcut_groove = 0.0015 * ga(y, vcut_y - 0.005, 0.014) * vcut_span

        serr_y = y + 0.34 * (a - 0.128)
        serr = (
            0.0022 * (ga(serr_y, 1.178, 0.0105) + ga(serr_y, 1.152, 0.0105) + ga(serr_y, 1.126, 0.0105))
            * ga(a, 0.132, 0.022)
            * smooth(-0.02, 0.04, z)
        )
        out[:, 0] += sx * serr * 0.70
        out[:, 2] += front * (
            ab_x * ab_rows + oblique + belly + vcut_ridge - vcut_groove
            - 0.0016 * linea_alba - 0.0014 * semilunaris
        ) + serr * 0.58

        # Subtle source-registered areola & umbilicus refinement
        nipple = (
            0.0007 * ga(a, 0.091, 0.0075) * ga(y, 1.205, 0.0068)
            + 0.0008 * ga(a, 0.091, 0.0028) * ga(y, 1.205, 0.0028)
        )
        nav = ga(x, 0.0, 0.0070) * ga(y, 1.041, 0.0050)
        nav_hood = ga(x, 0.0, 0.0105) * ga(y, 1.048, 0.0045)
        out[:, 2] += front * (nipple - 0.0028 * nav + 0.0010 * nav_hood)

        # 5. Back Anatomy (Spine furrow, Erector spinae, Lats, Scapula) & Grounded Masculine Glutes
        spine_furrow = 0.0040 * ga(x, 0.0, 0.016) * smooth(0.94, 1.00, y) * (1.0 - smooth(1.31, 1.37, y))
        erector = (
            0.0062 * (ga(x, 0.036, 0.024) + ga(x, -0.036, 0.024))
            * smooth(0.96, 1.02, y) * (1.0 - smooth(1.19, 1.26, y))
        )
        lats = 0.0135 * ga(y, 1.228, 0.075) * ga(a, 0.115, 0.056)
        scapula = 0.0085 * ga(y, 1.288, 0.044) * ga(a, 0.088, 0.048)
        out[:, 2] -= back * torso * (erector + lats + scapula - spine_furrow)

        glute_lobe = (
            (ga(x, 0.068, 0.048) + ga(x, -0.068, 0.048))
            * ga(y, 0.892, 0.054)
            * (1.0 - smooth(0.140, 0.190, a))
        )
        glute_cleft = ga(x, 0.0, 0.014) * ga(y, 0.882, 0.052)
        glute_fold = ga(y, 0.822 + 0.08 * a, 0.014) * ga(a, 0.065, 0.046)
        out[:, 2] -= back * (0.0080 * glute_lobe - 0.0028 * glute_cleft - 0.0018 * glute_fold)

        # 6. Anatomically Directional Limb Sculpting (Left & Right mirror-symmetric)
        for s in (-1.0, 1.0):
            # --- Upper Arm: Seamless Acromion-to-Deltoid Curve + Cohesive Biceps/Brachialis/Triceps ---
            A = np.array([s * 0.187, 1.315, -0.008])
            B = np.array([s * 0.344, 1.133, -0.009])
            ab, _, lat, fwd = _limb_basis(A, B, np.array([0.0, 1.0, 0.0]), np.array([0.0, 0.0, 1.0]))
            t = (v - A) @ ab / (ab @ ab)
            radial = v - A - t[:, None] * ab
            rlen = np.linalg.norm(radial, axis=1)
            rhat = radial / np.maximum(rlen[:, None], 1e-6)
            c_lat = rhat @ lat
            c_fwd = rhat @ fwd
            # Broad smooth overlap over acromion with restrained superior bulge so trap -> deltoid is one continuous arc
            arm_mask = smooth(0.138, 0.210, a) * (x * s > 0) * (1.0 - smooth(0.085, 0.148, rlen))
            end_gate = smooth(-0.22, 0.08, t) * (1.0 - smooth(0.86, 1.06, t))
            delt_lat = 0.115 * ga(t, 0.21, 0.21) * smooth(-0.20, 0.78, c_lat)
            delt_ant = 0.072 * ga(t, 0.15, 0.18) * smooth(-0.05, 0.85, c_fwd) * (1.0 - 0.35 * smooth(0.45, 0.95, c_lat))
            delt_post = 0.088 * ga(t, 0.17, 0.19) * smooth(-0.05, 0.85, -c_fwd) * (1.0 - 0.35 * smooth(0.45, 0.95, c_lat))
            biceps = 0.215 * ga(t, 0.54, 0.22) * smooth(-0.18, 0.78, c_fwd)
            brachialis = 0.085 * ga(t, 0.60, 0.20) * smooth(-0.10, 0.80, c_lat)
            triceps = 0.230 * ga(t, 0.48, 0.22) * smooth(-0.18, 0.78, -c_fwd) * (0.68 + 0.32 * smooth(-0.3, 0.6, c_lat))
            base_arm = 0.062 * ga(t, 0.48, 0.34)
            out += radial * (
                arm_mask * end_gate * (delt_lat + delt_ant + delt_post + biceps + brachialis + triceps + base_arm)
            )[:, None]

            # --- Forearm: Athletic Brachioradialis + Extensor/Flexor Mass, Smooth Elbow & Crisp Wrist Taper ---
            A = np.array([s * 0.344, 1.133, -0.009])
            B = np.array([s * 0.471, 1.012, 0.172])
            ab, _, lat, fwd = _limb_basis(A, B, np.array([0.0, 1.0, 0.0]), np.array([0.0, 0.0, 1.0]))
            t = (v - A) @ ab / (ab @ ab)
            radial = v - A - t[:, None] * ab
            rlen = np.linalg.norm(radial, axis=1)
            rhat = radial / np.maximum(rlen[:, None], 1e-6)
            c_lat = rhat @ lat
            c_fwd = rhat @ fwd
            fa_mask = smooth(0.24, 0.31, a) * (x * s > 0) * (1.0 - smooth(0.068, 0.125, rlen))
            fa_gate = smooth(-0.14, 0.14, t) * (1.0 - smooth(0.64, 0.92, t))
            brachio = 0.175 * ga(t, 0.27, 0.22) * smooth(-0.22, 0.78, c_lat)
            flexor = 0.145 * ga(t, 0.31, 0.23) * smooth(-0.22, 0.78, -c_fwd)
            base_fa = 0.055 * ga(t, 0.29, 0.26)
            out += radial * (fa_mask * fa_gate * (brachio + flexor + base_fa))[:, None]

            # --- Thigh: Controlled Athletic Quads + TFL Hip Tie-in + VMO Teardrop + Hamstrings ---
            A = np.array([s * 0.102, 0.883, -0.032])
            B = np.array([s * 0.140, 0.480, 0.000])
            ab, _, lat, fwd = _limb_basis(A, B, np.array([s, 0.0, 0.0]), np.array([0.0, 0.0, 1.0]))
            t = (v - A) @ ab / (ab @ ab)
            radial = v - A - t[:, None] * ab
            rlen = np.linalg.norm(radial, axis=1)
            rhat = radial / np.maximum(rlen[:, None], 1e-6)
            c_lat = rhat @ lat
            c_fwd = rhat @ fwd
            th_mask = smooth(0.018, 0.062, x * s) * (1.0 - smooth(0.095, 0.155, rlen))
            th_gate = smooth(-0.08, 0.08, t) * (1.0 - smooth(0.84, 1.00, t))
            # Prevent inner-upper thigh rubbing
            inner_guard = 1.0 - 0.85 * smooth(0.10, 0.75, -c_lat) * (1.0 - smooth(0.32, 0.56, t))
            # Smooth upper-outer hip/TFL tie-in prevents "violin-hip" pinch above outer quad
            tfl_tie = 0.042 * ga(t, 0.10, 0.14) * smooth(0.15, 0.85, c_lat)
            vastus_lat = 0.055 * ga(t, 0.36, 0.24) * smooth(-0.10, 0.85, c_lat)
            rectus_fem = 0.068 * ga(t, 0.42, 0.25) * smooth(-0.05, 0.85, c_fwd)
            vmo_drop = 0.062 * ga(t, 0.74, 0.12) * smooth(0.0, 0.80, c_fwd) * smooth(-0.10, 0.75, -c_lat)
            hamstring = 0.056 * ga(t, 0.44, 0.24) * smooth(-0.10, 0.85, -c_fwd)
            out += radial * (th_mask * th_gate * inner_guard * (tfl_tie + vastus_lat + rectus_fem + vmo_drop + hamstring))[:, None]

            # --- Lower Leg: Crisp Diamond Gastrocnemius + Tibialis Anterior + Lean Achilles Taper ---
            A = np.array([s * 0.140, 0.480, 0.000])
            B = np.array([s * 0.186, 0.068, -0.012])
            ab, _, lat, fwd = _limb_basis(A, B, np.array([s, 0.0, 0.0]), np.array([0.0, 0.0, 1.0]))
            t = (v - A) @ ab / (ab @ ab)
            radial = v - A - t[:, None] * ab
            rlen = np.linalg.norm(radial, axis=1)
            rhat = radial / np.maximum(rlen[:, None], 1e-6)
            c_lat = rhat @ lat
            c_fwd = rhat @ fwd
            cf_mask = smooth(0.030, 0.075, x * s) * (1.0 - smooth(0.078, 0.130, rlen))
            cf_gate = smooth(0.02, 0.14, t) * (1.0 - smooth(0.84, 0.98, t))
            gastroc_med = 0.092 * ga(t, 0.35, 0.15) * smooth(0.05, 0.85, -c_fwd) * smooth(-0.30, 0.70, -c_lat)
            gastroc_lat = 0.074 * ga(t, 0.27, 0.14) * smooth(0.05, 0.85, -c_fwd) * smooth(-0.30, 0.70, c_lat)
            tibialis_ant = 0.026 * ga(t, 0.38, 0.20) * smooth(0.15, 0.85, c_lat) * smooth(0.05, 0.75, c_fwd)
            achilles_trim = -0.024 * ga(t, 0.80, 0.11) * (1.0 - smooth(0.15, 0.75, c_fwd)) * (1.0 - smooth(0.55, 0.95, -c_fwd))
            out += radial * (cf_mask * cf_gate * (gastroc_med + gastroc_lat + tibialis_ant + achilles_trim))[:, None]

        # 7. Handsome Wuxia Hero Facial Structure (chiseled jaw, refined nose, slim masculine lips, hunter-eye brow)
        #    All displacements are smooth diffeomorphisms (det(J) > 0) with strict eyeball protection.
        face_front = smooth(0.060, 0.095, z)

        # 7a. Lips (mat=2 sits at y in [1.461..1.485], |x|<=0.0258, center y_mouth=1.4745):
        #     Slim bloated lip height (-26%), tuck protruding lower lip (-2.8mm Z), narrow corners (-8% X),
        #     and subtly lift mouth corners (+1.4mm Y) for a poised, confident neutral expression.
        y_mouth = 1.4745
        lip_zone = ga(y, y_mouth, 0.0125) * ga(a, 0.0, 0.024) * smooth(0.092, 0.112, z)
        lower_lip = ga(y, 1.4675, 0.0075) * ga(a, 0.0, 0.022) * smooth(0.095, 0.114, z)
        upper_lip = ga(y, 1.4810, 0.0065) * ga(a, 0.0, 0.021) * smooth(0.098, 0.116, z)
        mouth_corner = ga(a, 0.0235, 0.0075) * ga(y, 1.4730, 0.0085) * smooth(0.092, 0.112, z)
        mentolabial = ga(y, 1.4565, 0.0075) * ga(a, 0.0, 0.022) * smooth(0.092, 0.112, z)
        out[:, 1] += -0.24 * (y - y_mouth) * lip_zone + 0.0014 * mouth_corner
        out[:, 0] -= 0.06 * x * lip_zone
        out[:, 2] += -0.0024 * lower_lip - 0.0008 * upper_lip - 0.0016 * mentolabial

        # 7b. High, Straight, Refined Heroic Nose (nasion y=1.545 -> tip y=1.505):
        #     Narrow wide nostrils/alae (-18% X) and bulbous tip (-15% X), raise & straighten bridge (+2.8mm Z).
        nose_alae = ga(y, 1.502, 0.011) * smooth(0.005, 0.014, a) * (1.0 - smooth(0.020, 0.032, a)) * smooth(0.100, 0.118, z)
        nose_tip = ga(y, 1.505, 0.010) * ga(a, 0.0, 0.013) * smooth(0.110, 0.126, z)
        nose_bridge = smooth(1.504, 1.514, y) * (1.0 - smooth(1.540, 1.552, y)) * ga(a, 0.0, 0.0095) * smooth(0.106, 0.122, z)
        nose_sides = smooth(1.505, 1.516, y) * (1.0 - smooth(1.538, 1.550, y)) * ga(a, 0.011, 0.007) * smooth(0.104, 0.120, z)
        out[:, 0] -= 0.18 * x * nose_alae + 0.14 * x * nose_tip + 0.12 * x * nose_sides
        out[:, 2] += 0.0028 * nose_bridge + 0.0015 * nose_tip
        out[:, 1] += 0.0012 * nose_tip

        # 7c. Chiseled Masculine Jawline, High Cheekbones, Lean Buccal Hollows & Balanced Chin:
        #     Trim chubby buccal lower cheeks, elevate sculpted zygomatic arches, define jaw angle & crisp chin.
        buccal = ga(a, 0.048, 0.015) * ga(y, 1.484, 0.019) * smooth(0.048, 0.088, z) * eye_protect
        zygo = ga(a, 0.056, 0.014) * ga(y, 1.524, 0.014) * smooth(0.046, 0.086, z) * eye_protect
        jaw_angle = ga(a, 0.056, 0.014) * ga(y, 1.464, 0.018) * smooth(0.018, 0.058, z) * eye_protect
        chin_body = ga(y, 1.445, 0.015) * ga(a, 0.0, 0.032) * smooth(0.072, 0.105, z)
        chin_tip = ga(y, 1.444, 0.012) * ga(a, 0.0, 0.016) * smooth(0.085, 0.110, z)
        out[:, 0] += sx * (0.0022 * zygo + 0.0024 * jaw_angle - 0.0032 * buccal) - 0.10 * x * chin_body
        out[:, 2] += 0.0018 * zygo - 0.0022 * buccal + 0.0032 * chin_tip
        out[:, 1] += 0.0020 * chin_body

        # 7d. Deep-Set Hunter-Eye Brow Ridge, Positive Outer Orbital Lift, Smooth Under-Eye & Balanced Cranium:
        brow_ridge = ga(y, 1.561, 0.009) * ga(a, 0.024, 0.022) * smooth(0.078, 0.108, z) * eye_protect
        outer_orbit = ga(a, 0.046, 0.009) * ga(y, 1.543, 0.009) * smooth(0.075, 0.102, z) * eye_protect
        under_eye_bag = ga(a, 0.028, 0.014) * ga(y, 1.527, 0.007) * smooth(0.085, 0.108, z) * eye_protect
        temple_broad = ga(a, 0.068, 0.016) * ga(y, 1.584, 0.026) * smooth(0.010, 0.075, z)
        skull_top = smooth(1.605, 1.638, y) * (1.0 - smooth(0.045, 0.080, a))
        out[:, 2] += 0.0030 * brow_ridge - 0.0018 * under_eye_bag
        out[:, 1] += 0.0014 * outer_orbit - 0.0028 * skull_top
        out[:, 0] += sx * 0.0024 * temple_broad
        return out

    def colors(v):
        x, y, z = v.T
        a = np.abs(x)
        front = smooth(0.018, 0.068, z)
        back = 1.0 - smooth(-0.068, -0.018, z)
        col = np.ones((len(v), 3), dtype=float)

        # Areola, nipple tip & umbilicus (registered to source topology, soft feathered warmth)
        areola = ga(a, 0.091, 0.0076) * ga(y, 1.205, 0.0068) * front
        tip = ga(a, 0.091, 0.0028) * ga(y, 1.205, 0.0028) * front
        navel = ga(x, 0.0, 0.0052) * ga(y, 1.041, 0.0040) * front
        col -= areola[:, None] * np.array([0.10, 0.22, 0.25])
        col -= tip[:, None] * np.array([0.04, 0.065, 0.065])
        col -= navel[:, None] * np.array([0.13, 0.16, 0.18])

        # Clean noble Wuxia complexion: subtle warm cheeks, nose bridge & cheekbones (no blotchy patches!)
        blush = (
            0.55 * ga(a, 0.046, 0.018) * ga(y, 1.518, 0.018) * smooth(0.068, 0.100, z)
            + 0.25 * ga(a, 0.0, 0.012) * ga(y, 1.512, 0.018) * smooth(0.095, 0.122, z)
            + 0.35 * ga(a, 0.078, 0.014) * ga(y, 1.518, 0.022) * (1.0 - smooth(0.02, 0.06, z))
            + 0.28 * ga(a, 0.142, 0.028) * ga(y, 0.475, 0.032) * smooth(0.005, 0.045, z)
            + 0.24 * ga(a, 0.345, 0.030) * ga(y, 1.130, 0.032) * (1.0 - smooth(-0.01, 0.03, z))
        )
        col -= blush[:, None] * np.array([0.004, 0.034, 0.044])

        # Clean brow-ridge skin (removed heavy painted brown brow smears so 3D modular eyebrows shine cleanly!)
        # Keep only a subtle, ultra-soft natural supraorbital shadow + crisp lash-line definition.
        brow_y = 1.560 + 0.08 * (a - 0.028) - 4.5 * (a - 0.029) ** 2
        brow_soft = ga(y, brow_y, 0.0040) * smooth(0.009, 0.018, a) * (1.0 - smooth(0.040, 0.052, a)) * smooth(0.074, 0.098, z)
        col -= brow_soft[:, None] * np.array([0.05, 0.06, 0.07])

        # Deep-set upper eyelid crease & crisp dark lash-rim framing for sharp heroic eyes
        lash_y = 1.547 - 1.4 * (a - 0.0285) ** 2
        lash = ga(y, lash_y, 0.0020) * smooth(0.014, 0.020, a) * (1.0 - smooth(0.036, 0.044, a)) * smooth(0.080, 0.098, z)
        col -= lash[:, None] * np.array([0.28, 0.32, 0.35])

        # Subtle aristocratic facial plane sculpting (soft cheekbone hollow, nose-side shadow, clean nose-bridge light)
        nose_hi = ga(a, 0.0, 0.0065) * smooth(1.504, 1.514, y) * (1.0 - smooth(1.542, 1.554, y)) * smooth(0.110, 0.125, z)
        col += nose_hi[:, None] * np.array([0.035, 0.032, 0.028])

        # Subtle baked anatomical micro-AO (soft warm flesh shadow — never harsh dark stripes)
        ao = np.zeros(len(v), dtype=float)
        # Soft buccal cheek hollow, orbital socket depth, nose-wing crease & mentolabial shadow
        ao += 0.042 * ga(a, 0.048, 0.016) * ga(y, 1.484, 0.018) * smooth(0.045, 0.085, z)
        ao += 0.055 * ga(a, 0.022, 0.013) * ga(y, 1.551, 0.009) * smooth(0.082, 0.105, z)
        ao += 0.045 * ga(a, 0.016, 0.006) * ga(y, 1.500, 0.009) * smooth(0.100, 0.118, z)
        ao += 0.048 * ga(a, 0.0, 0.020) * ga(y, 1.456, 0.007) * smooth(0.090, 0.112, z)
        # Under-chin / upper neck shadow
        ao += 0.095 * ga(a, 0.0, 0.048) * ga(y, 1.430, 0.022) * smooth(0.015, 0.065, z)
        # Suprasternal notch & medial supraclavicular hollow (stays cleanly medial of acromion)
        clav_y = 1.322 + 0.175 * np.clip(a - 0.018, 0.0, 0.16)
        ao += 0.042 * ga(x, 0.0, 0.016) * ga(y, 1.323, 0.014) * front
        ao += 0.028 * ga(y, clav_y + 0.016, 0.013) * smooth(0.028, 0.052, a) * (1.0 - smooth(0.085, 0.118, a)) * smooth(0.005, 0.048, z)
        # Soft sternal cleft, gentle inferior pec shadow, linea alba & anatomically true V-cut
        ao += 0.036 * ga(x, 0.0, 0.016) * ga(y, 1.246, 0.052) * front
        pec_bot = 1.180 + 0.04 * np.clip(a - 0.03, 0.0, 0.08) + 2.0 * np.maximum(a - 0.105, 0.0) ** 2
        ao += 0.028 * ga(y, pec_bot - 0.004, 0.017) * smooth(0.024, 0.055, a) * (1.0 - smooth(0.118, 0.152, a)) * front
        ao += 0.030 * ga(x, 0.0, 0.013) * smooth(1.032, 1.058, y) * (1.0 - smooth(1.165, 1.192, y)) * front
        vcut_y = 0.875 + 1.12 * np.clip(a - 0.035, 0.0, 0.090)
        ao += 0.024 * ga(y, vcut_y - 0.005, 0.015) * smooth(0.032, 0.062, a) * (1.0 - smooth(0.112, 0.145, a)) * front
        # Armpit / axilla cavity AO (tucked strictly under arm, never bleeding onto front pec corner)
        ao += 0.075 * ga(a, 0.175, 0.024) * ga(y, 1.190, 0.030) * ga(z, -0.015, 0.036)
        # Spinal furrow, gluteal fold & subtle back-of-knee (popliteal) crease
        ao += 0.055 * ga(x, 0.0, 0.017) * smooth(0.94, 1.00, y) * (1.0 - smooth(1.31, 1.37, y)) * back
        ao += 0.058 * ga(y, 0.822 + 0.08 * a, 0.015) * ga(a, 0.065, 0.048) * back
        ao += 0.032 * ga(a, 0.140, 0.026) * ga(y, 0.475, 0.024) * back

        ao = np.clip(ao, 0.0, 0.16)
        # Warm skin-shadow attenuation (preserves healthy warm hue in creases)
        col -= ao[:, None] * np.array([0.74, 0.88, 0.96])
        return np.clip(col, 0.0, 1.0)

    _export_sculpted_pair(
        D, sculpt, colors,
        ('male_body_v6.glb', 'male_body_v6_shorts.glb'),
        'MaleBody_V6_Master40', 'Male Anatomy V6 directional anatomical sculpt',
        mat_overrides={
            0: {'baseColorFactor': [0.62, 0.440, 0.325, 1], 'roughnessFactor': 0.78},
            1: {'baseColorFactor': [0.60, 0.43, 0.35, 1], 'roughnessFactor': 0.44},
            2: {'baseColorFactor': [0.55, 0.355, 0.285, 1], 'roughnessFactor': 0.62},
            3: {'baseColorFactor': [0.68, 0.67, 0.65, 1], 'roughnessFactor': 0.22},
            5: {'baseColorFactor': [0.095, 0.052, 0.028, 1], 'roughnessFactor': 0.16},
            6: {'baseColorFactor': [0.008, 0.008, 0.008, 1], 'roughnessFactor': 0.14},
        },
        skip_sculpt_materials={3, 5, 6},  # Never distort eyeballs (sclera/iris/pupil)
    )


def _export_sculpted_pair(D, sculpt_fn, colors_fn, out_names, mesh_name, generator, mat_overrides=None, skip_sculpt_materials=None):
    skip_mats = set(skip_sculpt_materials or ())

    def export(src, dst):
        g = GLB(src)
        d = copy.deepcopy(g.d)
        data = bytearray(g.b)

        def add(a, typ, ct=5126, target=34962, bounds=False):
            a = np.ascontiguousarray(a, dtype={5126: '<f4', 5121: 'u1'}[ct])
            data.extend(b'\0' * ((-len(data)) % 4))
            off = len(data)
            data.extend(a.tobytes())
            d['bufferViews'].append({'buffer': 0, 'byteOffset': off, 'byteLength': a.nbytes, 'target': target})
            ac = {'bufferView': len(d['bufferViews']) - 1, 'componentType': ct, 'count': len(a), 'type': typ}
            if bounds:
                ac.update(min=a.min(0).tolist(), max=a.max(0).tolist())
            if ct == 5121:
                ac['normalized'] = True
            d['accessors'].append(ac)
            return len(d['accessors']) - 1

        for p in d['meshes'][0]['primitives']:
            at = p['attributes']
            v = g.read(at['POSITION']).astype(float)
            n = g.read(at['NORMAL']).astype(float)
            if p['material'] in skip_mats:
                new, nn = v, n
            else:
                new = sculpt_fn(v)
                eps = 0.00015
                J = np.stack(
                    [(sculpt_fn(v + np.eye(3)[i] * eps) - sculpt_fn(v - np.eye(3)[i] * eps)) / (2 * eps) for i in range(3)],
                    axis=2,
                )
                det = np.linalg.det(J)
                assert np.isfinite(new).all() and (det > 0).all(), f'Invalid Jacobian det min={det.min()}'
                nn = np.linalg.solve(J.transpose(0, 2, 1), n[:, :, None])[:, :, 0]
                nn /= np.maximum(np.linalg.norm(nn, axis=1, keepdims=True), 1e-12)
                assert np.isfinite(nn).all()
            at['POSITION'] = add(new, 'VEC3', bounds=True)
            at['NORMAL'] = add(nn, 'VEC3')
            if p['material'] == 0:
                at['COLOR_0'] = add(np.round(colors_fn(v) * 255), 'VEC3', 5121)

        if mat_overrides:
            for mi, props in mat_overrides.items():
                if mi < len(d['materials']):
                    d['materials'][mi]['pbrMetallicRoughness'].update(props)

        d['meshes'][0]['name'] = mesh_name
        for n in d['nodes']:
            if 'mesh' in n:
                n['name'] = mesh_name
        d['asset']['generator'] = generator
        d['asset']['extras']['rigVersion'] = 'Master40 anatomy bind pose'
        d['buffers'][0]['byteLength'] = len(data)
        js = json.dumps(d, separators=(',', ':')).encode()
        js += b' ' * ((-len(js)) % 4)
        data.extend(b'\0' * ((-len(data)) % 4))
        Path(dst).write_bytes(
            struct.pack('<4sII', b'glTF', 2, 28 + len(js) + len(data))
            + struct.pack('<I4s', len(js), b'JSON') + js
            + struct.pack('<I4s', len(data), b'BIN\0') + data
        )

        out = GLB(dst)
        assert g.d['skins'] == out.d['skins']
        assert np.array_equal(g.read(g.d['skins'][0]['inverseBindMatrices']), out.read(out.d['skins'][0]['inverseBindMatrices']))
        for j in g.d['skins'][0]['joints']:
            assert g.d['nodes'][j] == out.d['nodes'][j]
        print(dst, len(data), sum(out.d['accessors'][p['indices']]['count'] // 3 for p in out.d['meshes'][0]['primitives']))

    export(D / 'male_body_anatomy.glb', D / out_names[0])
    export(D / 'male_body_anatomy_shorts.glb', D / out_names[1])


def render_preview(D, glb_name='male_body_v6_shorts.glb', out_name='male_body_v6_preview.png'):
    import vtk
    from vtk.util.numpy_support import numpy_to_vtk, numpy_to_vtkIdTypeArray
    from PIL import Image, ImageDraw, ImageFont

    g = GLB(D / glb_name)
    r = vtk.vtkRenderer()
    r.SetBackground(0.11, 0.14, 0.18)
    win = vtk.vtkRenderWindow()
    win.SetOffScreenRendering(1)
    win.AddRenderer(r)
    win.SetSize(600, 920)
    win.SetMultiSamples(4)

    all_v = []
    for p in g.d['meshes'][0]['primitives']:
        a = p['attributes']
        v = g.read(a['POSITION']); n = g.read(a['NORMAL']); f = g.read(p['indices']).reshape(-1, 3)
        all_v.append(v)
        pts = vtk.vtkPoints(); pts.SetData(numpy_to_vtk(v, deep=True))
        cells = vtk.vtkCellArray()
        cells.SetCells(len(f), numpy_to_vtkIdTypeArray(np.c_[np.full(len(f), 3), f].astype(np.int64).ravel(), deep=True))
        poly = vtk.vtkPolyData(); poly.SetPoints(pts); poly.SetPolys(cells)
        poly.GetPointData().SetNormals(numpy_to_vtk(n, deep=True))
        mapper = vtk.vtkPolyDataMapper(); mapper.SetInputData(poly)
        act = vtk.vtkActor(); act.SetMapper(mapper)
        col = g.d['materials'][p['material']]['pbrMetallicRoughness']['baseColorFactor'][:3]
        act.GetProperty().SetColor(*np.power(col, 1 / 2.2))
        act.GetProperty().SetSpecular(0.10)
        act.GetProperty().SetSpecularPower(40)
        if 'COLOR_0' in a:
            c_arr = g.read(a['COLOR_0'], True)[:, :3]
            rgb = np.clip(np.power(c_arr * np.array(col), 1 / 2.2) * 255, 0, 255).astype(np.uint8)
            poly.GetPointData().SetScalars(numpy_to_vtk(rgb, deep=True))
            mapper.SetColorModeToDirectScalars()
            mapper.ScalarVisibilityOn()
            act.GetProperty().SetColor(1, 1, 1)
        r.AddActor(act)

    bounds_v = np.concatenate(all_v, axis=0)
    cy = 0.5 * (bounds_v[:, 1].min() + bounds_v[:, 1].max())
    height = bounds_v[:, 1].max() - bounds_v[:, 1].min()
    sc_full = 0.54 * height
    chest_y = bounds_v[:, 1].min() + 0.72 * height

    for xyz, intensity in [((2.4, 3.0, 4.0), 0.90), ((-3.0, 1.4, 2.2), 0.45), ((0.0, 2.2, -3.6), 0.75)]:
        l = vtk.vtkLight()
        l.SetLightTypeToSceneLight()
        l.SetPosition(*xyz)
        l.SetFocalPoint(0, cy, 0)
        l.SetIntensity(intensity)
        r.AddLight(l)

    cam = r.GetActiveCamera()
    cam.ParallelProjectionOn()
    frames = []
    views = [
        ('FRONT', (0, cy, 4.2), (0, cy, 0), sc_full),
        ('3/4 VIEW', (2.6, cy + 0.12, 3.8), (0, cy, 0), sc_full),
        ('BACK', (0, cy, -4.2), (0, cy, 0), sc_full),
        ('TORSO DETAIL', (0.35, chest_y, 2.2), (0, chest_y, 0.015), 0.165 * height),
    ]
    tmp_png = D / '_bodyview.png'
    for name, eye, look, sc in views:
        cam.SetPosition(*eye)
        cam.SetFocalPoint(*look)
        cam.SetViewUp(0, 1, 0)
        cam.SetParallelScale(sc)
        r.ResetCameraClippingRange()
        win.Render()
        w = vtk.vtkWindowToImageFilter(); w.SetInput(win); w.Update()
        wr = vtk.vtkPNGWriter(); wr.SetFileName(str(tmp_png)); wr.SetInputConnection(w.GetOutputPort()); wr.Write()
        frames.append((name, Image.open(tmp_png).copy()))
    if tmp_png.exists():
        tmp_png.unlink()

    out = Image.new('RGB', (600 * len(frames), 980), (28, 35, 46))
    draw = ImageDraw.Draw(out)
    try:
        font = ImageFont.truetype('DejaVuSans.ttf', 24)
    except OSError:
        font = ImageFont.load_default()
    for i, (name, img) in enumerate(frames):
        out.paste(img, (600 * i, 50))
        draw.text((600 * i + 22, 12), name, font=font, fill='white')
    out.save(D / out_name)
    print('Preview saved:', D / out_name)


def _make_bone_frame(y_dir, hint_vec, hint_axis='z'):
    y = y_dir / max(np.linalg.norm(y_dir), 1e-12)
    h = hint_vec - (hint_vec @ y) * y
    h = h / max(np.linalg.norm(h), 1e-12)
    if hint_axis == 'z':
        z = h
        x = np.cross(y, z)
        x = x / max(np.linalg.norm(x), 1e-12)
        z = np.cross(x, y)
    else:
        x = h
        z = np.cross(x, y)
        z = z / max(np.linalg.norm(z), 1e-12)
        x = np.cross(y, z)
    return np.column_stack([x, y, z])


def _dqs_skin(v, n, j4, w4, delta_mats):
    """Dual-quaternion skinning for artifact-free A-pose -> T-pose baking."""
    nb = len(delta_mats)
    qr = np.zeros((nb, 4), dtype=float)
    qd = np.zeros((nb, 4), dtype=float)
    for b in range(nb):
        M = delta_mats[b]
        q = _mat_to_quat(M[:3, :3])
        t = M[:3, 3]
        tq = np.array([t[0], t[1], t[2], 0.0])
        qr[b] = q
        qd[b] = 0.5 * _quat_mul(tq, q)
    q0 = qr[j4[:, 0]]
    br = np.zeros((len(v), 4), dtype=float)
    bd = np.zeros((len(v), 4), dtype=float)
    for c in range(4):
        qc = qr[j4[:, c]]
        dc = qd[j4[:, c]]
        sgn = np.where((q0 * qc).sum(axis=1, keepdims=True) < 0.0, -1.0, 1.0)
        wc = w4[:, c:c+1] * sgn
        br += wc * qc
        bd += wc * dc
    ln = np.maximum(np.linalg.norm(br, axis=1, keepdims=True), 1e-12)
    br /= ln
    bd /= ln
    rx, ry, rz, rw = br.T
    dx, dy, dz, dw = bd.T
    tx = 2.0 * (-dw*rx + dx*rw - dy*rz + dz*ry)
    ty = 2.0 * (-dw*ry + dx*rz + dy*rw - dz*rx)
    tz = 2.0 * (-dw*rz - dx*ry + dy*rx + dz*rw)
    t_out = np.column_stack([tx, ty, tz])
    rv = br[:, :3]
    rw_c = br[:, 3:4]

    def qrot(vec):
        c1 = np.cross(rv, vec) + rw_c * vec
        return vec + 2.0 * np.cross(rv, c1)

    v_out = qrot(v) + t_out
    n_out = qrot(n)
    n_out /= np.maximum(np.linalg.norm(n_out, axis=1, keepdims=True), 1e-12)
    return v_out, n_out


def _rot_between(v_from, v_to):
    """Shortest-arc 3x3 rotation matrix aligning unit vector v_from to v_to."""
    a = v_from / max(np.linalg.norm(v_from), 1e-12)
    b = v_to / max(np.linalg.norm(v_to), 1e-12)
    c = float(np.clip(a @ b, -1.0, 1.0))
    if c > 1.0 - 1e-10:
        return np.eye(3)
    if c < -1.0 + 1e-10:
        orth = np.array([1.0, 0.0, 0.0]) if abs(a[0]) < 0.9 else np.array([0.0, 1.0, 0.0])
        ax = np.cross(a, orth)
        ax /= max(np.linalg.norm(ax), 1e-12)
        return -np.eye(3) + 2.0 * np.outer(ax, ax)
    v = np.cross(a, b)
    s = np.linalg.norm(v)
    K = np.array([[0.0, -v[2], v[1]], [v[2], 0.0, -v[0]], [-v[1], v[0], 0.0]])
    return np.eye(3) + K + (K @ K) * ((1.0 - c) / max(s * s, 1e-12))


def _rot_axis_angle(axis, rad):
    ax = np.asarray(axis, dtype=float)
    ax = ax / max(np.linalg.norm(ax), 1e-12)
    c, s = np.cos(rad), np.sin(rad)
    K = np.array([[0.0, -ax[2], ax[1]], [ax[2], 0.0, -ax[0]], [-ax[1], ax[0], 0.0]])
    return np.eye(3) + s * K + (1.0 - c) * (K @ K)


def _rigid_delta(R, p_src, p_dst):
    """4x4 rigid transform mapping p_src -> p_dst with rotation R."""
    M = np.eye(4, dtype=float)
    M[:3, :3] = R
    M[:3, 3] = p_dst - R @ p_src
    return M


def export_game_tpose_20(sculpted_glb_path, dst_glb_path, source_bytes):
    """Retarget sculpted body into exact 20-bone Mixamo T-Pose matching humanoid_anims.glb.

    Anatomically faithful, length-preserving retargeting:
    - Rigid Z-centered torso/pelvis/neck/head (zero vertical torso stretch, zero clavicle/neck crush).
    - Length-preserving hip-socket rotation for legs with flat level feet (zero leg compression or groin tear).
    - Natural scapulohumeral clavicle lift + zero-compression arm chain + smooth forearm axial length fit
      so hands land on exact GAME_MIXAMO_20 weapon sockets [+/-0.72, 1.48, 0] with zero wrist/elbow shear.
    - Deduplicated 20-joint weights, gentle axilla relaxation, cross-primitive seam-safe normals,
      and VEC4 UINT8 normalized COLOR_0.
    """
    src_raw = GLB(source_bytes)
    sn = {n['name']: i for i, n in enumerate(src_raw.d['nodes'])}
    S = lambda k: src_raw.matrix(sn[k])[:3, 3].copy()

    # Exact target Mixamo 20-bone bind skeleton (must match humanoid_anims.glb)
    M_tpose = np.zeros((20, 4, 4), dtype=float)
    for i, (_, par, tr, rot) in enumerate(GAME_MIXAMO_20):
        loc = np.eye(4)
        loc[:3, :3] = _quat_to_mat(rot)
        loc[:3, 3] = tr
        M_tpose[i] = M_tpose[par] @ loc if par >= 0 else loc

    # Global rigid Z-centering aligns source spine/hip/knee centerline onto Z=0 with zero deformation
    z_shift = np.array([0.0, 0.0, -0.020], dtype=float)
    delta_mats = np.tile(np.eye(4, dtype=float), (20, 1, 1))

    # 1. Torso, Pelvis, Neck, Head (bones 0..5): rigid Z alignment + gentle upright posture retraction on Neck/Head
    # Moving Head rigidly in delta_mats[5] shifts skull, skin, lips, ears, and eyeballs together with 0 socket offset.
    for idx in range(4):
        delta_mats[idx, :3, 3] = z_shift
    delta_mats[4, :3, 3] = np.array([0.0, 0.0, -0.0245], dtype=float)
    delta_mats[5, :3, 3] = np.array([0.0, 0.0, -0.0285], dtype=float)

    # 2. Arms & Shoulders (bones 6..13): anatomical scapulohumeral lift + length-preserving arm chain
    forearm_fits = []
    for side, s, base in [('L', 1.0, 6), ('R', -1.0, 10)]:
        ua_src = S('upperarm01.' + side)
        fa_src = S('lowerarm01.' + side)
        wr_src = S('wrist.' + side)
        f2_src = S('finger2-1.' + side)
        f3_src = S('finger3-1.' + side)
        f5_src = S('finger5-1.' + side)

        # (a) Clavicle: subtle +3.0 deg scapulohumeral elevation -> preserves smooth continuous trapezius slope
        clav_anchor_src = np.array([s * 0.045, 1.455, 0.020], dtype=float)
        clav_anchor_dst = clav_anchor_src + z_shift
        R_clav = _rot_axis_angle([0.0, 0.0, s], np.radians(3.0))
        delta_mats[base + 0] = _rigid_delta(R_clav, clav_anchor_src, clav_anchor_dst)

        # (b) Upper arm: exact kinematic continuity with rotated clavicle endpoint (zero step/notch at acromion!)
        ua_dst = clav_anchor_dst + R_clav @ (ua_src - clav_anchor_src)
        ua_dst = np.array([ua_dst[0], ua_dst[1], 0.0], dtype=float)

        # Preserve exact anatomical humerus length (~0.260m), rising gently toward elbow at y=1.464
        len_ua = float(np.linalg.norm(fa_src - ua_src))
        dy_ua = 1.464 - ua_dst[1]
        dx_ua = np.sqrt(max(len_ua * len_ua - dy_ua * dy_ua, 1e-6))
        fa_dst = np.array([ua_dst[0] + s * dx_ua, 1.464, 0.0], dtype=float)

        F_ua_src = _make_bone_frame(fa_src - ua_src, np.array([0.0, 1.0, 0.0]), 'z')
        F_ua_dst = _make_bone_frame(fa_dst - ua_dst, np.array([0.0, 1.0, 0.0]), 'z')
        R_ua = F_ua_dst @ F_ua_src.T
        delta_mats[base + 1] = _rigid_delta(R_ua, ua_src, ua_dst)

        # (c) Forearm: smoothly fit forearm shaft length (only ~8-9%) pre-DQS so wrist lands EXACTLY at [+/-0.72, 1.48, 0]
        wr_dst = np.array([s * 0.720, 1.480, 0.0], dtype=float)
        len_fa_src = float(np.linalg.norm(wr_src - fa_src))
        len_fa_dst = float(np.linalg.norm(wr_dst - fa_dst))
        fa_axis_src = (wr_src - fa_src) / max(len_fa_src, 1e-12)
        # Shift effective wrist source anchor along forearm axis after smooth pre-DQS shaft fit
        wr_src_fit = fa_src + fa_axis_src * len_fa_dst
        forearm_fits.append((side, s, fa_src, wr_src, fa_axis_src, len_fa_src, len_fa_dst))

        F_fa_src = _make_bone_frame(wr_src - fa_src, F_ua_src[:, 2], 'z')
        F_fa_dst = _make_bone_frame(wr_dst - fa_dst, np.array([0.0, 1.0, 0.0]), 'z')
        R_fa = F_fa_dst @ F_fa_src.T
        delta_mats[base + 2] = _rigid_delta(R_fa, fa_src, fa_dst)

        # (d) Hand: exact anatomical palm-down (-Y), thumb-forward (+Z) orientation at [+/-0.72, 1.48, 0]
        # Left: thumb-side (f2-f5) -> +Z (+X_bone); Right: pinky-side (f5-f2) -> -Z (+X_bone)
        thumb_hint = (f2_src - f5_src) if s > 0 else (f5_src - f2_src)
        F_ha_src = _make_bone_frame(f3_src - wr_src, thumb_hint, 'x')
        F_ha_dst = _make_bone_frame(np.array([s, 0.0, 0.0]), np.array([0.0, 0.0, s]), 'x')
        R_ha = F_ha_dst @ F_ha_src.T
        delta_mats[base + 3] = _rigid_delta(R_ha, wr_src_fit, wr_dst)

    # 3. Legs (bones 14..19): pure rotation around true femoral head [+/-0.10, 0.90, 0] -> zero leg crushing
    for side, s, base in [('L', 1.0, 14), ('R', -1.0, 17)]:
        hip_piv_src = np.array([s * 0.100, 0.900, 0.020], dtype=float)
        hip_piv_dst = np.array([s * 0.100, 0.900, 0.000], dtype=float)
        foot_src = S('foot.' + side)
        # Rotate whole leg cleanly from A-stance (~6 deg splay) to vertical along x = +/-0.10
        leg_vec_src = foot_src - hip_piv_src
        R_leg = _rot_between(leg_vec_src, np.array([0.0, -1.0, 0.0]))
        delta_leg = _rigid_delta(R_leg, hip_piv_src, hip_piv_dst)
        delta_mats[base + 0] = delta_leg  # UpLeg
        delta_mats[base + 1] = delta_leg  # Leg (continuous through knee -> zero knee shear/compression)
        # Foot: keep sole strictly level (identity rotation, natural 1.5 deg toe turnout) at rotated ankle
        ankle_dst = hip_piv_dst + R_leg @ (foot_src - hip_piv_src)
        delta_mats[base + 2] = _rigid_delta(np.eye(3), foot_src, ankle_dst)

    g = GLB(sculpted_glb_path)
    old_joints = g.d['skins'][0]['joints']
    name_to_new = {name: i for i, (name, _, _, _) in enumerate(GAME_MIXAMO_20)}
    old_to_new = np.array([name_to_new.get(g.d['nodes'][j]['name'], 0) for j in old_joints], dtype=int)

    # ---------- 52-Bone Standard Mixamo Extension (30 Fingers + 2 ToeBases) ----------
    # Bones 0..19: exact GAME_MIXAMO_20; 20..34: LeftHand 5x3 fingers; 35..49: RightHand 5x3 fingers; 50..51: ToeBases
    FINGER_ORDER = [
        ('1', 'Thumb'),
        ('2', 'Index'),
        ('3', 'Middle'),
        ('4', 'Ring'),
        ('5', 'Pinky'),
    ]
    EXTRA_BONES = []  # list of (name, parent_idx, src_bone_names_list, parent_base20_idx, side_sign)
    for side_s, side_name, hand_idx, s_sign in [('L', 'Left', 9, 1.0), ('R', 'Right', 13, -1.0)]:
        for f_num, f_name in FINGER_ORDER:
            par_i = hand_idx
            for phal in (1, 2, 3):
                idx_52 = 20 + len(EXTRA_BONES)
                EXTRA_BONES.append((
                    f'mixamorig:{side_name}Hand{f_name}{phal}',
                    par_i,
                    [f'finger{f_num}-{phal}.{side_s}'],
                    hand_idx,
                    s_sign,
                ))
                par_i = idx_52
    for side_s, side_name, foot_idx, s_sign in [('L', 'Left', 16, 1.0), ('R', 'Right', 19, -1.0)]:
        toe_src_names = [
            f'toe{t}-{p}.{side_s}'
            for t in range(1, 6)
            for p in range(1, 4 if t > 1 else 3)
        ]
        EXTRA_BONES.append((
            f'mixamorig:{side_name}ToeBase',
            foot_idx,
            toe_src_names,
            foot_idx,
            s_sign,
        ))
    NUM_BONES_52 = 20 + len(EXTRA_BONES)  # 52

    # Build source 163-joint -> 52-bone mapping for fingers and toes
    src_skin_joints = src_raw.d['skins'][0]['joints']
    src_name_to_52 = {}
    for eb_off, (_, _, src_list, _, _) in enumerate(EXTRA_BONES):
        for s_nm in src_list:
            src_name_to_52[s_nm] = 20 + eb_off
    # Palm / wrist / metacarpals stay on Hand (9 or 13); foot stays on Foot (16 or 19)
    for side_s, hand_idx, foot_idx in [('L', 9, 16), ('R', 13, 19)]:
        src_name_to_52[f'wrist.{side_s}'] = hand_idx
        for m_i in range(1, 5):
            src_name_to_52[f'metacarpal{m_i}.{side_s}'] = hand_idx
        src_name_to_52[f'foot.{side_s}'] = foot_idx

    src_joint_to_52 = np.full(len(src_skin_joints), -1, dtype=int)
    for sj_i, node_idx in enumerate(src_skin_joints):
        nm = src_raw.d['nodes'][node_idx].get('name', '')
        if nm in src_name_to_52:
            src_joint_to_52[sj_i] = src_name_to_52[nm]

    # Collect per-primitive source weights for prim 0..5 (1-to-1 vertex correspondence with src_raw mesh 0)
    src_mesh0_prims = src_raw.d['meshes'][0]['primitives']
    src_finger_deltas = _build_relaxed_hand_deltas(src_raw)

    deformed_prims = []
    min_y = 1e9
    for pi, p in enumerate(g.d['meshes'][0]['primitives']):
        at = p['attributes']
        v_unscaled = (g.read(at['POSITION']).astype(float) - np.array([0.0, 0.0, -0.025])) / 0.92
        n_orig = g.read(at['NORMAL']).astype(float)
        uv = g.read(at['TEXCOORD_0']).astype(np.float32)
        j_old = g.read(at['JOINTS_0']).astype(int)
        w_old = g.read(at['WEIGHTS_0']).astype(float)
        idx = g.read(p['indices']).reshape(-1).astype(np.uint16)
        faces = idx.reshape(-1, 3).astype(int)
        col = g.read(at['COLOR_0']) if 'COLOR_0' in at else None

        # Consolidate into exact 20-joint weights to prevent duplicate joint entries in JOINTS_0
        W20 = np.zeros((len(v_unscaled), 20), dtype=float)
        rows = np.arange(len(v_unscaled))
        for c in range(4):
            np.add.at(W20, (rows, old_to_new[j_old[:, c]]), w_old[:, c])
        W20 /= np.maximum(W20.sum(axis=1, keepdims=True), 1e-12)

        # Clean & smooth shoulder/trapezius/axilla/elbow weights ONCE BEFORE DQS, and export the
        # EXACT SAME weights to the GLB. Using identical weights for A->T bake and runtime T->Idle
        # guarantees exact round-trip continuity across trapezius -> acromion -> deltoid (zero notch!).
        if p['material'] == 0:
            ax_u = np.abs(v_unscaled[:, 0])
            ay_u = v_unscaled[:, 1]
            # Superior trapezius/acromion: keep Arm weight out of vertices medial of the shoulder pivot
            # (x < 0.175) and ramp smoothly across acromion -> deltoid so neither A->T nor T->Idle notches.
            top_zone = _smooth(1.385, 1.455, ay_u)
            cap_top = _smooth(0.168, 0.242, ax_u)
            # Underarm / axillary fold: prevent Arm weight medial of x=0.22 from cramming pec/lat inward
            ax_zone = (1.0 - _smooth(1.365, 1.415, ay_u)) * _smooth(1.20, 1.28, ay_u)
            cap_ax = _smooth(0.168, 0.238, ax_u)
            sp2_frac = _smooth(1.28, 1.37, ay_u)
            to_shd_frac = _smooth(0.085, 0.155, ax_u)
            for b_shd, b_arm in ((6, 7), (10, 11)):
                excess_top = np.maximum(W20[:, b_arm] - cap_top, 0.0) * top_zone
                W20[:, b_arm] -= excess_top
                W20[:, b_shd] += excess_top * (0.20 + 0.60 * to_shd_frac)
                W20[:, 3] += excess_top * (0.80 - 0.60 * to_shd_frac)

                excess_ax = np.maximum(W20[:, b_arm] - cap_ax, 0.0) * ax_zone
                W20[:, b_arm] -= excess_ax
                W20[:, 3] += excess_ax * (0.35 + 0.55 * sp2_frac)
                W20[:, 2] += excess_ax * (0.65 - 0.55 * sp2_frac)

            # Keep Shoulder bone cleanly out of immediate neck column (|x| < 0.085, y > 1.46)
            neck_guard = _smooth(1.455, 1.505, ay_u) * (1.0 - _smooth(0.068, 0.115, ax_u))
            for b_shd in (6, 10):
                n_leak = W20[:, b_shd] * neck_guard
                W20[:, b_shd] -= n_leak
                W20[:, 3] += n_leak * 0.55
                W20[:, 4] += n_leak * 0.45

            # Graph-Laplacian weight smoothing across shoulder/trapezius/axilla AND elbow belts
            w_arm_tr = W20[:, 7] + W20[:, 11]
            w_fa_tr = W20[:, 8] + W20[:, 12]
            w_shd_tr = W20[:, 6] + W20[:, 10]
            w_tor_tr = W20[:, 3] + W20[:, 2] + W20[:, 4]
            w_blend_mask = np.maximum(
                np.maximum(
                    np.clip(4.0 * w_arm_tr * (w_shd_tr + w_tor_tr), 0.0, 1.0),
                    np.clip(4.0 * w_shd_tr * w_tor_tr, 0.0, 1.0) * 0.85,
                ),
                np.clip(4.0 * w_arm_tr * w_fa_tr, 0.0, 1.0),
            )
            if float(w_blend_mask.max()) > 1e-3 and len(faces) > 0:
                _, inv_w = np.unique(np.round(v_unscaled, 6), axis=0, return_inverse=True)
                n_uw = int(inv_w.max()) + 1
                cnt_uw = np.maximum(np.bincount(inv_w, minlength=n_uw), 1).astype(float)
                W_u = np.zeros((n_uw, 20), dtype=float)
                for b_idx in range(20):
                    np.add.at(W_u[:, b_idx], inv_w, W20[:, b_idx])
                W_u /= cnt_uw[:, None]
                m_uw = np.zeros(n_uw, dtype=float)
                np.add.at(m_uw, inv_w, w_blend_mask)
                m_uw = np.clip(m_uw / cnt_uw, 0.0, 1.0)[:, None]
                fu = inv_w[faces]
                edges = np.vstack([fu[:, [0, 1]], fu[:, [1, 2]], fu[:, [2, 0]]])
                edges = np.unique(np.sort(edges, axis=1), axis=0)
                edges = edges[edges[:, 0] != edges[:, 1]]
                u0, u1 = edges[:, 0], edges[:, 1]
                deg = np.maximum(np.bincount(u0, minlength=n_uw) + np.bincount(u1, minlength=n_uw), 1).astype(float)[:, None]
                for _ in range(7):
                    nb = np.zeros_like(W_u)
                    np.add.at(nb, u0, W_u[u1])
                    np.add.at(nb, u1, W_u[u0])
                    W_u = W_u + (0.42 * m_uw) * (nb / deg - W_u)
                W20 = np.maximum(W_u[inv_w], 0.0)
            W20 /= np.maximum(W20.sum(axis=1, keepdims=True), 1e-12)

        j_new = np.argsort(-W20, axis=1)[:, :4]
        w_new = np.take_along_axis(W20, j_new, axis=1)
        w_new /= np.maximum(w_new.sum(axis=1, keepdims=True), 1e-12)
        j_new[w_new <= 1e-7] = 0

        # Smooth pre-DQS axial length fit strictly along forearm shaft (hand translates rigidly -> zero hand/wrist distortion)
        v_pre = v_unscaled.copy()
        for side, s, fa_src, wr_src, fa_axis, len_src, len_dst in forearm_fits:
            b_fa = 8 if s > 0 else 12
            b_ha = 9 if s > 0 else 13
            w_distal = W20[:, b_fa] + W20[:, b_ha]
            if float(w_distal.max()) < 1e-4:
                continue
            t_fa = ((v_unscaled - fa_src) @ fa_axis) / max(len_src, 1e-12)
            # Smooth ramp from 0 at elbow (t<=0.06) to 1 at wrist/hand (t>=0.94)
            ramp = _smooth(0.06, 0.94, t_fa)
            shift_len = (len_dst - len_src) * ramp * w_distal
            v_pre += shift_len[:, None] * fa_axis[None, :]

        v_tpose, n_tpose = _dqs_skin(v_pre, n_orig, j_new, w_new, delta_mats)

        # Gentle localized volume-preserving Taubin relaxation on shoulder/axilla, trapezius, elbow & finger webs
        if p['material'] == 0:
            w_arm = W20[:, 7] + W20[:, 11]
            w_fa = W20[:, 8] + W20[:, 12]
            w_shd = W20[:, 6] + W20[:, 10]
            w_torso = W20[:, 3] + W20[:, 4] + W20[:, 2]
            arm_crease = np.clip(4.0 * w_arm * (w_shd + W20[:, 3] + W20[:, 2]), 0.0, 1.0) * 0.70
            trap_crease = np.clip(4.0 * w_shd * w_torso, 0.0, 1.0) * 0.65
            elbow_crease = np.clip(4.0 * w_arm * w_fa, 0.0, 1.0) * 0.55
            web_crease = _smooth(0.755, 0.790, np.abs(v_tpose[:, 0])) * (1.0 - _smooth(0.815, 0.840, np.abs(v_tpose[:, 0]))) * 0.35
            crease_mask = np.maximum(np.maximum(arm_crease, trap_crease), np.maximum(elbow_crease, web_crease))
            v_tpose = _taubin_smooth_masked(v_tpose, v_unscaled, faces, crease_mask, iters=5, lam=0.34, mu=-0.35)

        # Recompute exact normals on deformed T-pose triangles (skip rigid eyeballs)
        if p['material'] not in (3, 5, 6):
            n_tpose = _recompute_normals_split_safe(v_tpose, v_unscaled, n_orig, faces)

        # Expand W20 -> W52 by splitting Hand (9,13) and Foot (16,19) weights using exact source MakeHuman finger/toe weights
        W52 = np.zeros((len(v_unscaled), NUM_BONES_52), dtype=float)
        W52[:, :20] = W20
        v_src_tpose_ref = None
        if pi < len(src_mesh0_prims):
            sp = src_mesh0_prims[pi]
            sv_raw = src_raw.read(sp['attributes']['POSITION']).astype(float)
            if len(sv_raw) == len(v_unscaled):
                sj_raw = src_raw.read(sp['attributes']['JOINTS_0']).astype(int)
                sw_raw = src_raw.read(sp['attributes']['WEIGHTS_0'], True).astype(float)
                sw_raw /= np.maximum(sw_raw.sum(axis=1, keepdims=True), 1e-12)
                W_src_hf = np.zeros((len(v_unscaled), NUM_BONES_52), dtype=float)
                for c in range(4):
                    mapped = src_joint_to_52[sj_raw[:, c]]
                    valid = mapped >= 0
                    if np.any(valid):
                        np.add.at(W_src_hf, (rows[valid], mapped[valid]), sw_raw[valid, c])

                # Split LeftHand(9), RightHand(13), LeftFoot(16), RightFoot(19) proportionally
                split_groups = [
                    (9,  [9]  + list(range(20, 35))),
                    (13, [13] + list(range(35, 50))),
                    (16, [16, 50]),
                    (19, [19, 51]),
                ]
                for base_b, grp in split_groups:
                    grp_sum = W_src_hf[:, grp].sum(axis=1)
                    mask = (W20[:, base_b] > 1e-6) & (grp_sum > 1e-6)
                    if np.any(mask):
                        scale_f = W20[mask, base_b] / grp_sum[mask]
                        W52[mask, base_b] = 0.0
                        for gb in grp:
                            W52[mask, gb] += W_src_hf[mask, gb] * scale_f
                W52 /= np.maximum(W52.sum(axis=1, keepdims=True), 1e-12)

                if pi == 0:
                    # Reference un-sculpted T-pose vertex positions (for exact male/female finger & toe pivot tracking)
                    sn_raw = src_raw.read(sp['attributes']['NORMAL']).astype(float)
                    sv_rel, _ = _dqs_skin(sv_raw, sn_raw, sj_raw, sw_raw, src_finger_deltas)
                    sv_pre = sv_rel.copy()
                    for _, s_f, fa_src_f, _, fa_ax_f, len_s_f, len_d_f in forearm_fits:
                        b_fa_f = 8 if s_f > 0 else 12
                        b_ha_f = 9 if s_f > 0 else 13
                        w_df = W20[:, b_fa_f] + W20[:, b_ha_f]
                        t_faf = ((sv_rel - fa_src_f) @ fa_ax_f) / max(len_s_f, 1e-12)
                        sv_pre += ((len_d_f - len_s_f) * _smooth(0.06, 0.94, t_faf) * w_df)[:, None] * fa_ax_f[None, :]
                    v_src_tpose_ref, _ = _dqs_skin(sv_pre, sn_raw, j_new, w_new, delta_mats)

        j52 = np.argsort(-W52, axis=1)[:, :4]
        w52 = np.take_along_axis(W52, j52, axis=1)
        w52 /= np.maximum(w52.sum(axis=1, keepdims=True), 1e-12)
        j52[w52 <= 1e-7] = 0

        min_y = min(min_y, float(v_tpose[:, 1].min()))
        deformed_prims.append(dict(
            v=v_tpose, n=n_tpose, v_ref=v_unscaled, n_ref=n_orig, uv=uv,
            j=j52.astype(np.uint8), w=w52.astype(np.float32), idx=idx,
            mat=p['material'], col=col,
            W52=W52 if pi == 0 else None,
            v_src_tpose=v_src_tpose_ref if pi == 0 else None,
        ))

    # Cross-primitive normal weld across shared skin seams (body / areola / ears)
    skin_prims = [dp for dp in deformed_prims if dp['mat'] == 0]
    if len(skin_prims) > 1:
        vr_all = np.concatenate([dp['v_ref'] for dp in skin_prims], axis=0)
        nr_all = np.concatenate([dp['n_ref'] for dp in skin_prims], axis=0)
        n_all = np.concatenate([dp['n'] for dp in skin_prims], axis=0)
        key = np.round(np.c_[vr_all, nr_all], 4)
        _, ii = np.unique(key, axis=0, return_inverse=True)
        nn = np.zeros((int(ii.max()) + 1, 3), dtype=float)
        np.add.at(nn, ii, n_all)
        nn /= np.maximum(np.linalg.norm(nn, axis=1, keepdims=True), 1e-12)
        welded = nn[ii]
        off = 0
        for dp in skin_prims:
            l = len(dp['v'])
            dp['n'] = welded[off:off + l]
            off += l

    for dp in deformed_prims:
        dp['v'][:, 1] -= min_y

    # Build full 52-bone bind skeleton (0..19 exact GAME_MIXAMO_20 + 20..49 fingers + 50..51 ToeBases)
    M_tpose_52 = np.zeros((NUM_BONES_52, 4, 4), dtype=float)
    M_tpose_52[:20] = M_tpose

    src_name_to_skin_idx = {
        src_raw.d['nodes'][nid].get('name', ''): sj_i
        for sj_i, nid in enumerate(src_skin_joints)
    }
    forearm_fit_by_hand = {}
    for _, s_f, fa_src_f, wr_src_f, fa_ax_f, len_s_f, len_d_f in forearm_fits:
        forearm_fit_by_hand[9 if s_f > 0 else 13] = (len_d_f - len_s_f) * fa_ax_f

    skin0 = deformed_prims[0]
    skin0_W52 = skin0['W52']
    skin0_v_tpose = skin0['v']
    skin0_v_src_tpose = skin0['v_src_tpose'] - np.array([0.0, min_y, 0.0]) if skin0['v_src_tpose'] is not None else None

    extra_node_specs = []  # (name, par_idx, local_tr, local_rot)
    for eb_off, (b_name, par_idx, src_list, base20_idx, _) in enumerate(EXTRA_BONES):
        idx_52 = 20 + eb_off
        # Compute world bind position of this extra joint from relaxed source joint + A->T delta + sculpt residual
        pts = []
        for s_nm in src_list:
            if s_nm not in sn:
                continue
            p_raw = S(s_nm)
            sj_i = src_name_to_skin_idx.get(s_nm, -1)
            if sj_i >= 0:
                D_rel = src_finger_deltas[sj_i]
                p_rel = D_rel[:3, :3] @ p_raw + D_rel[:3, 3]
            else:
                p_rel = p_raw
            if base20_idx in forearm_fit_by_hand:
                p_rel = p_rel + forearm_fit_by_hand[base20_idx]
            D_at = delta_mats[base20_idx]
            p_tpose = D_at[:3, :3] @ p_rel + D_at[:3, 3]
            p_tpose[1] -= min_y
            pts.append(p_tpose)
        # For ToeBase, use only proximal toe1-1..toe5-1 (ball of foot) for anatomical pivot
        if 'ToeBase' in b_name:
            prox_pts = []
            side_ch = 'L' if 'Left' in b_name else 'R'
            for t_i in range(1, 6):
                tn = f'toe{t_i}-1.{side_ch}'
                if tn in sn:
                    D_at = delta_mats[base20_idx]
                    pt = D_at[:3, :3] @ S(tn) + D_at[:3, 3]
                    pt[1] -= min_y
                    prox_pts.append(pt)
            if prox_pts:
                pts = prox_pts
        world_pos = np.mean(pts, axis=0) if pts else M_tpose_52[par_idx, :3, 3].copy()

        # Track any body sculpt (e.g., female petite hands/feet -16%) via weighted vertex displacement residual
        if skin0_W52 is not None and skin0_v_src_tpose is not None:
            wb = skin0_W52[:, idx_52]
            wsum = float(wb.sum())
            if wsum > 1e-4:
                resid = ((skin0_v_tpose - skin0_v_src_tpose) * wb[:, None]).sum(axis=0) / wsum
                world_pos = world_pos + resid

        # Orientation: fingers inherit Hand axes (identity); ToeBase points +Y forward (+Z world)
        if 'ToeBase' in b_name:
            loc_rot = [-0.70710678, 0.0, 0.0, 0.70710678]
        else:
            loc_rot = [0.0, 0.0, 0.0, 1.0]

        par_M = M_tpose_52[par_idx]
        par_inv = np.linalg.inv(par_M)
        loc_tr = (par_inv[:3, :3] @ world_pos + par_inv[:3, 3]).tolist()

        loc_M = np.eye(4, dtype=float)
        loc_M[:3, :3] = _quat_to_mat(loc_rot)
        loc_M[:3, 3] = loc_tr
        M_tpose_52[idx_52] = par_M @ loc_M
        extra_node_specs.append((b_name, par_idx, loc_tr, loc_rot))

    all_bones_52 = list(GAME_MIXAMO_20) + extra_node_specs
    nodes = []
    children_map = {i: [] for i in range(NUM_BONES_52)}
    for i, (_, par, _, _) in enumerate(all_bones_52):
        if par >= 0:
            children_map[par].append(i)
    for i, (name, _, tr, rot) in enumerate(all_bones_52):
        nd = {'name': name, 'translation': [float(x) for x in tr]}
        if rot != [0.0, 0.0, 0.0, 1.0]:
            nd['rotation'] = [float(x) for x in rot]
        if children_map[i]:
            nd['children'] = children_map[i]
        nodes.append(nd)
    mesh_node_idx = NUM_BONES_52
    arm_node_idx = NUM_BONES_52 + 1
    nodes.append({'name': 'MaleBody_Game52_Mesh', 'mesh': 0, 'skin': 0})
    nodes.append({'name': 'MaleBody_Armature', 'children': [mesh_node_idx, 0]})

    ibm = np.linalg.inv(M_tpose_52).transpose(0, 2, 1).reshape(-1, 16).astype('<f4')
    data = bytearray()
    buffer_views = []
    accessors = []

    def add(arr, typ, ct, target=None, bounds=False, normalized=False):
        a = np.ascontiguousarray(arr, dtype=CT[ct])
        data.extend(b'\0' * ((-len(data)) % 4))
        off = len(data)
        data.extend(a.tobytes())
        bv = {'buffer': 0, 'byteOffset': off, 'byteLength': a.nbytes}
        if target:
            bv['target'] = target
        buffer_views.append(bv)
        acc = {'bufferView': len(buffer_views) - 1, 'componentType': ct, 'count': len(a), 'type': typ}
        if bounds:
            acc.update(min=a.min(0).tolist(), max=a.max(0).tolist())
        if normalized:
            acc['normalized'] = True
        accessors.append(acc)
        return len(accessors) - 1

    ibm_acc = add(ibm, 'MAT4', 5126)
    prims_out = []
    total_v = total_t = 0
    for dp in deformed_prims:
        attrs = {
            'POSITION': add(dp['v'].astype(np.float32), 'VEC3', 5126, 34962, bounds=True),
            'NORMAL': add(dp['n'].astype(np.float32), 'VEC3', 5126, 34962),
            'TEXCOORD_0': add(dp['uv'], 'VEC2', 5126, 34962),
            'JOINTS_0': add(dp['j'], 'VEC4', 5121, 34962),
            'WEIGHTS_0': add(dp['w'], 'VEC4', 5126, 34962),
        }
        if dp['col'] is not None:
            c = dp['col']
            if c.shape[1] == 3:
                c = np.column_stack([c, np.full((len(c), 1), 255, dtype=np.uint8)])
            attrs['COLOR_0'] = add(c.astype(np.uint8), 'VEC4', 5121, 34962, normalized=True)
        prims_out.append({
            'attributes': attrs,
            'indices': add(dp['idx'], 'SCALAR', 5123, 34963),
            'material': dp['mat'],
            'mode': 4,
        })
        total_v += len(dp['v'])
        total_t += len(dp['idx']) // 3

    out_doc = {
        'asset': {
            'version': '2.0',
            'generator': 'Male Body V6 Game-Ready 52-Bone Mixamo T-Pose',
        },
        'scene': 0,
        'scenes': [{'name': 'Scene', 'nodes': [arm_node_idx]}],
        'nodes': nodes,
        'skins': [{'name': 'MaleBody_Armature', 'joints': list(range(NUM_BONES_52)), 'inverseBindMatrices': ibm_acc}],
        'materials': copy.deepcopy(g.d['materials']),
        'meshes': [{'name': 'MaleBody_Game52', 'primitives': prims_out}],
        'accessors': accessors,
        'bufferViews': buffer_views,
        'buffers': [{'byteLength': len(data)}],
    }

    js = json.dumps(out_doc, separators=(',', ':')).encode('utf-8')
    js += b' ' * ((-len(js)) % 4)
    data.extend(b'\0' * ((-len(data)) % 4))
    Path(dst_glb_path).write_bytes(
        struct.pack('<4sII', b'glTF', 2, 28 + len(js) + len(data))
        + struct.pack('<I4s', len(js), b'JSON') + js
        + struct.pack('<I4s', len(data), b'BIN\0') + data
    )

    chk = GLB(dst_glb_path)
    assert len(chk.d['skins'][0]['joints']) == NUM_BONES_52
    for idx_j, nid in enumerate(chk.d['skins'][0]['joints']):
        p_nid = chk.parents.get(nid, -1)
        if p_nid in chk.d['skins'][0]['joints']:
            assert chk.d['skins'][0]['joints'].index(p_nid) < idx_j
    chk_ib = chk.read(chk.d['skins'][0]['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)
    for idx_j, nid in enumerate(chk.d['skins'][0]['joints']):
        err = np.max(np.abs(chk_ib[idx_j] @ chk.matrix(nid) - np.eye(4)))
        assert err < 1e-5, f'IBM mismatch on joint {idx_j}: {err}'
    all_v = np.concatenate([chk.read(pr['attributes']['POSITION']) for pr in chk.d['meshes'][0]['primitives']])
    print(
        f'Game52 T-Pose GLB: {dst_glb_path} | joints={NUM_BONES_52} verts={total_v} tris={total_t} '
        f'| ground_shift={-min_y:+.4f}m | bounds={np.round(all_v.min(0), 4).tolist()}..{np.round(all_v.max(0), 4).tolist()}'
    )


def write_master_skeleton(path, output):
    g = GLB(path)
    skin = g.d['skins'][0]
    ids = skin['joints']
    index = {node: i for i, node in enumerate(ids)}
    ib = g.read(skin['inverseBindMatrices'])
    result = {
        'name': 'Master40_Anatomy', 'units': 'meter', 'up': '+Y',
        'forward': '+Z', 'matrix_order': 'column-major', 'joints': [],
    }
    for i, node in enumerate(ids):
        n = g.d['nodes'][node]
        result['joints'].append({
            'index': i, 'name': n['name'],
            'parent': index.get(g.parents.get(node), -1),
            'translation': n.get('translation', [0, 0, 0]),
            'rotation_xyzw': n.get('rotation', [0, 0, 0, 1]),
            'scale': n.get('scale', [1, 1, 1]),
            'inverse_bind_matrix_column_major': ib[i].tolist(),
        })
    Path(output).write_text(json.dumps(result, indent=2), encoding='utf-8')


# ---------- Embedded source decoder and CLI ----------
def embedded_inputs():
    """Reconstruct original human GLB and the minimal rig template in memory."""
    source = lzma.decompress(base64.b85decode(SOURCE_MESH_B85))
    if hashlib.sha256(source).hexdigest() != SOURCE_SHA256:
        raise ValueError("Embedded human mesh checksum mismatch")
    metadata = json.loads(REFERENCE_RIG_JSON)
    inverse_bind = np.asarray(REFERENCE_INVERSE_BIND, dtype='<f4').tobytes()
    js = json.dumps(metadata, separators=(',', ':')).encode('utf-8')
    js += b' ' * (-len(js) % 4)
    binary = inverse_bind + b'\0' * (-len(inverse_bind) % 4)
    reference = (
        struct.pack('<4sII', b'glTF', 2, 28 + len(js) + len(binary))
        + struct.pack('<I4s', len(js), b'JSON') + js
        + struct.pack('<I4s', len(binary), b'BIN\0') + binary
    )
    return source, reference


def main():
    root = Path(__file__).resolve().parent
    repo_root = root.parent.parent
    parser = argparse.ArgumentParser(description='Build game-ready 20-bone Mixamo T-Pose Male Body V6 GLBs.')
    parser.add_argument('--out-dir', type=Path, default=root, help='Output directory for male_body.glb & male_body_nude.glb')
    parser.add_argument('--game-out', type=Path, default=None, help='Optional extra copy destination for male_body.glb (with underwear)')
    parser.add_argument('--install', action='store_true', help='Install clothed-underwear male_body.glb to assets/characters/bodies/male_body.glb')
    parser.add_argument('--keep-intermediates', action='store_true', help='Keep intermediate 40-joint A-pose GLBs and master_skeleton_40.json in out-dir')
    parser.add_argument('--version', choices=['anatomy', 'v4', 'v5', 'v6', 'all'], default='v6')
    parser.add_argument('--preview', action='store_true', help='Render 4-view PNGs (requires vtk, Pillow)')
    args = parser.parse_args()

    if args.preview and args.version not in ('v5', 'v6', 'all'):
        parser.error('--preview requires --version v5, v6, or all')

    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    source, reference = embedded_inputs()

    def _run(work_dir: Path):
        build_anatomy(source, reference, work_dir)
        if args.keep_intermediates or args.version == 'anatomy':
            write_master_skeleton(work_dir / 'male_body_anatomy.glb', out_dir / 'master_skeleton_40.json')
        if args.version in ('v4', 'all'):
            build_athletic(work_dir)
        if args.version in ('v5', 'all'):
            build_refined(work_dir)
        if args.version in ('v6', 'all'):
            build_v6(work_dir)
            export_game_tpose_20(work_dir / 'male_body_v6.glb', out_dir / 'male_body_nude.glb', source)
            export_game_tpose_20(work_dir / 'male_body_v6_shorts.glb', out_dir / 'male_body.glb', source)
        elif args.version == 'v5':
            export_game_tpose_20(work_dir / 'male_body_refined.glb', out_dir / 'male_body_nude.glb', source)
            export_game_tpose_20(work_dir / 'male_body_refined_shorts.glb', out_dir / 'male_body.glb', source)

        if args.install and args.version in ('v5', 'v6', 'all'):
            dst = repo_root / 'assets/characters/bodies/male_body.glb'
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes((out_dir / 'male_body.glb').read_bytes())
            print('Installed game-ready GLB (with underwear) ->', dst)

        if args.game_out is not None and args.version in ('v5', 'v6', 'all'):
            args.game_out.parent.mkdir(parents=True, exist_ok=True)
            args.game_out.write_bytes((out_dir / 'male_body.glb').read_bytes())
            print('Copied game-ready GLB (with underwear) ->', args.game_out)

        if args.preview:
            if args.version in ('v6', 'all'):
                render_preview(out_dir, 'male_body.glb', 'male_body_game20_preview.png')
                if args.keep_intermediates:
                    render_preview(work_dir, 'male_body_v6_shorts.glb', 'male_body_v6_preview.png')
            else:
                render_preview(out_dir, 'male_body.glb', 'male_body_refined_preview.png')

    if args.keep_intermediates or args.version == 'anatomy':
        _run(out_dir)
    else:
        with tempfile.TemporaryDirectory(prefix='male_body_v6_') as tmp:
            _run(Path(tmp))

    print('Finished:', out_dir)


if __name__ == '__main__':
    main()
