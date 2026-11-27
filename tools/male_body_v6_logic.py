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
import argparse, base64, copy, hashlib, json, lzma, struct
from pathlib import Path
import numpy as np


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
            parts.append(dict(v=v, out=v * 0.92 + np.array([0, 0, -0.025]), uv=uv, n=n, f=f, w=weights))

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
    """V6 Anatomical Martial-Athlete Sculpt:
    - Directional limb anatomy (deltoid/biceps/triceps, brachioradialis, quads/VMO/hamstrings, gastrocnemius/Achilles)
    - Trapezoidal pectoral form, clavicle shelf, deltopectoral groove, serratus & Apollo's belt
    - Trapezius, SCM neck cords, spinal furrow, erector spinae, V-taper lats & firm glutes
    - Eye-safe masculine jaw/cheek/brow refinement + baked micro-AO & warm skin shading
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
        front = smooth(0.016, 0.068, z)
        back = 1.0 - smooth(-0.068, -0.016, z)
        # Strict orbital protection so eyelids never deform into eyeballs
        eye_protect = 1.0 - ga(a, 0.0285, 0.016) * ga(y, 1.541, 0.016) * smooth(0.06, 0.10, z)

        # 1. Heroic V-Taper silhouette: broad upper chest/lats, trimmed waist flanks
        upper_broad = ga(y, 1.272, 0.072) * ga(a, 0.148, 0.065)
        lat_flare = ga(y, 1.185, 0.056) * ga(a, 0.135, 0.052) * (1.0 - 0.55 * front)
        waist_trim = ga(y, 1.055, 0.048) * ga(a, 0.122, 0.042) * smooth(0.05, 0.10, a)
        iliac_shelf = ga(y, 0.992, 0.036) * ga(a, 0.128, 0.036) * smooth(0.04, 0.09, a)
        out[:, 0] += sx * (
            (0.0145 * upper_broad + 0.0085 * lat_flare) * smooth(0.02, 0.065, a)
            - 0.0052 * waist_trim
            + 0.0026 * iliac_shelf
        )

        # 2. Trapezius slope & Sternocleidomastoid (SCM) neck cords
        trap_mask = (
            ga(a, 0.088, 0.048)
            * ga(y, 1.362 - 0.16 * a, 0.028)
            * smooth(0.028, 0.065, a)
            * (1.0 - smooth(0.038, 0.078, z))
        )
        out[:, 1] += 0.0105 * trap_mask
        out[:, 2] -= 0.0075 * trap_mask * (1.0 - smooth(-0.015, 0.032, z))

        scm_x = 0.014 + 0.34 * np.clip(y - 1.328, 0.0, 0.10)
        scm = (
            ga(a, scm_x, 0.011)
            * smooth(1.325, 1.345, y)
            * (1.0 - smooth(1.405, 1.435, y))
            * smooth(0.005, 0.045, z)
        )
        out[:, 2] += 0.0024 * scm
        out[:, 0] += sx * 0.0016 * scm

        # 3. Trapezoidal Pectoralis Major, Clavicle shelf & Deltopectoral groove
        pec_bot = 1.186 + 0.15 * np.maximum(a - 0.03, 0.0) + 1.6 * np.maximum(a - 0.12, 0.0) ** 2
        pec_top = 1.312 - 0.14 * a
        pec_y = smooth(pec_bot - 0.024, pec_bot + 0.026, y) * (1.0 - smooth(pec_top - 0.032, pec_top + 0.028, y))
        pec_x = smooth(0.010, 0.042, a) * (1.0 - smooth(0.150, 0.210, a))
        pec_belly = 0.72 + 0.42 * ga(y, 1.228, 0.035) * ga(a, 0.095, 0.055)
        pec = pec_x * pec_y * pec_belly

        pec_fold = ga(y, pec_bot - 0.004, 0.013) * smooth(0.022, 0.055, a) * (1.0 - smooth(0.145, 0.185, a))
        sternum = ga(x, 0.0, 0.016) * ga(y, 1.245, 0.052)
        clav_y = 1.326 - 0.11 * a
        clav_bone = ga(y, clav_y, 0.012) * smooth(0.018, 0.045, a) * (1.0 - smooth(0.145, 0.185, a))
        clav_hollow = ga(y, clav_y - 0.018, 0.014) * ga(a, 0.075, 0.042)
        deltopec = ga(a, 0.168 - 0.16 * (y - 1.25), 0.014) * ga(y, 1.275, 0.038)

        out[:, 2] += front * (
            0.031 * pec
            - 0.0042 * pec_fold
            - 0.0046 * sternum
            + 0.0042 * clav_bone
            - 0.0022 * clav_hollow
            - 0.0028 * deltopec
        )

        # 4. Abdominal Wall, Obliques, Serratus Anterior & Apollo's Belt (V-cut)
        ay = y - 0.08 * (a - 0.032)
        ab_rows = (
            0.0115 * ga(ay, 1.158, 0.017)
            + 0.0110 * ga(ay, 1.112, 0.017)
            + 0.0090 * ga(ay, 1.066, 0.018)
            + 0.0052 * ga(y, 1.012, 0.026)
        )
        ab_x = (ga(x, 0.037, 0.022) + ga(x, -0.037, 0.022)) * (1.0 - smooth(0.068, 0.106, a))
        linea_alba = ga(x, 0.0, 0.012) * smooth(1.035, 1.060, y) * (1.0 - smooth(1.165, 1.195, y))
        semilunaris = ga(a, 0.074, 0.013) * smooth(1.015, 1.045, y) * (1.0 - smooth(1.155, 1.185, y))
        oblique = 0.0075 * ga(a, 0.102, 0.028) * ga(y, 1.088, 0.062)
        belly = 0.0050 * ga(y, 1.085, 0.078) * ga(a, 0.042, 0.062)

        vcut_y = 0.945 + 1.05 * np.clip(a - 0.035, 0.0, 0.10)
        vcut_ridge = 0.0036 * ga(y, vcut_y + 0.014, 0.015) * smooth(0.035, 0.065, a) * (1.0 - smooth(0.115, 0.145, a))
        vcut_groove = 0.0025 * ga(y, vcut_y - 0.005, 0.012) * smooth(0.032, 0.058, a) * (1.0 - smooth(0.108, 0.138, a))

        serr_y = y + 0.35 * (a - 0.125)
        serr = (
            0.0028 * (ga(serr_y, 1.178, 0.010) + ga(serr_y, 1.152, 0.010) + ga(serr_y, 1.126, 0.010))
            * ga(a, 0.132, 0.022)
            * smooth(-0.02, 0.04, z)
        )
        out[:, 0] += sx * serr * 0.72
        out[:, 2] += front * (
            ab_x * ab_rows + oblique + belly + vcut_ridge - vcut_groove
            - 0.0022 * linea_alba - 0.0018 * semilunaris
        ) + serr * 0.62

        # Subtle source-registered areola & umbilicus refinement
        nipple = (
            0.0009 * ga(a, 0.091, 0.0075) * ga(y, 1.205, 0.0068)
            + 0.0010 * ga(a, 0.091, 0.0028) * ga(y, 1.205, 0.0028)
        )
        nav = ga(x, 0.0, 0.0072) * ga(y, 1.041, 0.0052)
        nav_hood = ga(x, 0.0, 0.0105) * ga(y, 1.048, 0.0045)
        out[:, 2] += front * (nipple - 0.0032 * nav + 0.0011 * nav_hood)

        # 5. Back Anatomy (Spine furrow, Erector spinae, Lats, Scapula) & Gluteus Maximus
        spine_furrow = 0.0055 * ga(x, 0.0, 0.016) * smooth(0.96, 1.02, y) * (1.0 - smooth(1.31, 1.37, y))
        erector = (
            0.0085 * (ga(x, 0.036, 0.022) + ga(x, -0.036, 0.022))
            * smooth(0.98, 1.04, y) * (1.0 - smooth(1.18, 1.25, y))
        )
        lats = 0.0175 * ga(y, 1.228, 0.075) * ga(a, 0.115, 0.055)
        scapula = 0.0110 * ga(y, 1.288, 0.042) * ga(a, 0.088, 0.048)
        out[:, 2] -= back * torso * (erector + lats + scapula - spine_furrow)

        glute_lobe = (
            (ga(x, 0.068, 0.046) + ga(x, -0.068, 0.046))
            * ga(y, 0.908, 0.052)
            * (1.0 - smooth(0.135, 0.185, a))
        )
        glute_cleft = ga(x, 0.0, 0.014) * ga(y, 0.895, 0.055)
        glute_fold = ga(y, 0.838 + 0.08 * a, 0.012) * ga(a, 0.065, 0.045)
        out[:, 2] -= back * (0.0140 * glute_lobe - 0.0035 * glute_cleft - 0.0025 * glute_fold)

        # 6. Anatomically Directional Limb Sculpting (Left & Right mirror-symmetric)
        for s in (-1.0, 1.0):
            # --- Upper Arm: Deltoid cap + Biceps peak + Triceps horseshoe ---
            A = np.array([s * 0.187, 1.315, -0.008])
            B = np.array([s * 0.344, 1.133, -0.009])
            ab, _, lat, fwd = _limb_basis(A, B, np.array([0.0, 1.0, 0.0]), np.array([0.0, 0.0, 1.0]))
            t = (v - A) @ ab / (ab @ ab)
            radial = v - A - t[:, None] * ab
            rlen = np.linalg.norm(radial, axis=1)
            rhat = radial / np.maximum(rlen[:, None], 1e-6)
            c_lat = rhat @ lat
            c_fwd = rhat @ fwd
            arm_mask = smooth(0.145, 0.220, a) * (x * s > 0) * (1.0 - smooth(0.080, 0.145, rlen))
            end_gate = smooth(-0.18, 0.06, t) * (1.0 - smooth(0.76, 1.02, t))
            deltoid = 0.22 * ga(t, 0.10, 0.19) * (0.55 + 0.45 * smooth(-0.35, 0.65, c_lat))
            biceps = 0.18 * ga(t, 0.54, 0.19) * smooth(-0.15, 0.80, c_fwd)
            triceps = 0.21 * ga(t, 0.46, 0.20) * smooth(-0.15, 0.80, -c_fwd) * (0.65 + 0.35 * smooth(-0.3, 0.6, c_lat))
            base_arm = 0.055 * ga(t, 0.42, 0.28)
            out += radial * (arm_mask * end_gate * (deltoid + biceps + triceps + base_arm))[:, None]

            # --- Forearm: Brachioradialis + Flexor belly, crisp wrist taper ---
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
            fa_gate = smooth(-0.08, 0.08, t) * (1.0 - smooth(0.60, 0.90, t))
            brachio = 0.125 * ga(t, 0.22, 0.19) * smooth(-0.20, 0.80, c_lat)
            flexor = 0.095 * ga(t, 0.28, 0.21) * smooth(-0.20, 0.80, -c_fwd)
            out += radial * (fa_mask * fa_gate * (brachio + flexor + 0.025 * ga(t, 0.25, 0.22)))[:, None]

            # --- Thigh: Vastus lateralis + Rectus femoris + VMO teardrop + Hamstring (no inner-thigh bloat) ---
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
            th_gate = smooth(-0.06, 0.10, t) * (1.0 - smooth(0.82, 1.02, t))
            # Prevent inner-upper thigh rubbing
            inner_guard = 1.0 - 0.85 * smooth(0.10, 0.75, -c_lat) * (1.0 - smooth(0.32, 0.56, t))
            vastus_lat = 0.135 * ga(t, 0.40, 0.24) * smooth(-0.15, 0.80, c_lat)
            rectus_fem = 0.115 * ga(t, 0.42, 0.25) * smooth(-0.10, 0.85, c_fwd)
            vmo_drop = 0.080 * ga(t, 0.74, 0.12) * smooth(0.0, 0.80, c_fwd) * smooth(-0.10, 0.75, -c_lat)
            hamstring = 0.085 * ga(t, 0.44, 0.24) * smooth(-0.10, 0.85, -c_fwd)
            out += radial * (th_mask * th_gate * inner_guard * (vastus_lat + rectus_fem + vmo_drop + hamstring))[:, None]

            # --- Lower Leg: Medial/Lateral Gastrocnemius + Lean Shin + Achilles Taper ---
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
            gastroc_med = 0.165 * ga(t, 0.34, 0.16) * smooth(0.05, 0.85, -c_fwd) * smooth(-0.30, 0.70, -c_lat)
            gastroc_lat = 0.140 * ga(t, 0.28, 0.15) * smooth(0.05, 0.85, -c_fwd) * smooth(-0.30, 0.70, c_lat)
            peroneals = 0.045 * ga(t, 0.42, 0.20) * smooth(0.20, 0.90, c_lat) * (1.0 - smooth(0.35, 0.85, c_fwd))
            achilles_trim = -0.038 * ga(t, 0.80, 0.11) * (1.0 - smooth(0.15, 0.75, c_fwd)) * (1.0 - smooth(0.55, 0.95, -c_fwd))
            out += radial * (cf_mask * cf_gate * (gastroc_med + gastroc_lat + peroneals + achilles_trim))[:, None]

        # 7. Eye-safe Masculine Facial Structure (jawline, zygomatic arch, chin, subtle brow)
        out[:, 0] += sx * 0.0038 * ga(a, 0.046, 0.014) * ga(y, 1.466, 0.022) * smooth(0.028, 0.075, z) * eye_protect
        out[:, 0] += sx * 0.0022 * ga(a, 0.048, 0.013) * ga(y, 1.518, 0.016) * smooth(0.045, 0.085, z) * eye_protect
        out[:, 2] += 0.0024 * ga(x, 0.0, 0.016) * ga(y, 1.448, 0.013) * smooth(0.075, 0.105, z)
        out[:, 2] += 0.0022 * ga(a, 0.026, 0.020) * ga(y, 1.560, 0.009) * smooth(0.078, 0.105, z) * eye_protect
        return out

    def colors(v):
        x, y, z = v.T
        a = np.abs(x)
        front = smooth(0.020, 0.072, z)
        back = 1.0 - smooth(-0.070, -0.018, z)
        col = np.ones((len(v), 3), dtype=float)

        # Areola, nipple tip & umbilicus (registered to source topology)
        areola = ga(a, 0.091, 0.0080) * ga(y, 1.205, 0.0070) * front
        tip = ga(a, 0.091, 0.0028) * ga(y, 1.205, 0.0028) * front
        navel = ga(x, 0.0, 0.0055) * ga(y, 1.041, 0.0042) * front
        col -= areola[:, None] * np.array([0.13, 0.28, 0.31])
        col -= tip[:, None] * np.array([0.045, 0.075, 0.075])
        col -= navel[:, None] * np.array([0.16, 0.20, 0.22])

        # Warm vascular skin variation (cheeks, nose, ears, knees, elbows)
        blush = (
            ga(a, 0.042, 0.018) * ga(y, 1.512, 0.021) * smooth(0.068, 0.100, z)
            + 0.42 * ga(a, 0.0, 0.015) * ga(y, 1.509, 0.020) * smooth(0.090, 0.120, z)
            + 0.45 * ga(a, 0.078, 0.014) * ga(y, 1.518, 0.022) * (1.0 - smooth(0.02, 0.06, z))
            + 0.35 * ga(a, 0.142, 0.028) * ga(y, 0.480, 0.032) * smooth(0.01, 0.05, z)
        )
        col -= blush[:, None] * np.array([0.006, 0.052, 0.064])

        # Layered masculine eyebrow (feathered head -> structured arch -> tapered tail) + subtle lash crease
        brow_y = 1.559 + 0.075 * (a - 0.028) - 4.8 * (a - 0.029) ** 2
        brow_thick = 0.0028 * (1.0 - 0.28 * smooth(0.032, 0.049, a))
        brow_core = np.exp(-0.5 * ((y - brow_y) / brow_thick) ** 2)
        brow_env = smooth(0.008, 0.017, a) * (1.0 - smooth(0.040, 0.053, a)) * smooth(0.074, 0.097, z)
        brow = brow_core * brow_env
        col -= brow[:, None] * np.array([0.64, 0.72, 0.76])

        lash_y = 1.547 - 1.5 * (a - 0.0285) ** 2
        lash = ga(y, lash_y, 0.0022) * smooth(0.014, 0.020, a) * (1.0 - smooth(0.036, 0.043, a)) * smooth(0.080, 0.098, z)
        col -= lash[:, None] * np.array([0.14, 0.18, 0.20])

        # Subtle baked anatomical micro-AO (warm shadow tint so skin never turns muddy gray)
        ao = np.zeros(len(v), dtype=float)
        # Under-chin / upper neck shadow
        ao += 0.11 * ga(a, 0.0, 0.048) * ga(y, 1.430, 0.022) * smooth(0.015, 0.065, z)
        # Clavicular hollow & sternal cleft
        ao += 0.06 * ga(y, 1.308 - 0.11 * a, 0.014) * ga(a, 0.075, 0.042) * front
        ao += 0.07 * ga(x, 0.0, 0.016) * ga(y, 1.242, 0.050) * front
        # Inferior pectoral shadow & linea alba
        pec_bot = 1.184 + 0.15 * np.maximum(a - 0.03, 0.0)
        ao += 0.08 * ga(y, pec_bot, 0.013) * smooth(0.022, 0.055, a) * (1.0 - smooth(0.145, 0.185, a)) * front
        ao += 0.05 * ga(x, 0.0, 0.013) * smooth(1.035, 1.060, y) * (1.0 - smooth(1.165, 1.195, y)) * front
        # Armpit / axilla cavity AO
        ao += 0.12 * ga(a, 0.168, 0.028) * ga(y, 1.245, 0.038) * (1.0 - smooth(0.05, 0.09, np.abs(z)))
        # Spinal furrow & gluteal fold AO
        ao += 0.08 * ga(x, 0.0, 0.017) * smooth(0.96, 1.02, y) * (1.0 - smooth(1.31, 1.37, y)) * back
        ao += 0.09 * ga(y, 0.838 + 0.08 * a, 0.013) * ga(a, 0.065, 0.046) * back

        ao = np.clip(ao, 0.0, 0.22)
        col -= ao[:, None] * np.array([0.82, 0.96, 1.06])
        return np.clip(col, 0.0, 1.0)

    _export_sculpted_pair(
        D, sculpt, colors,
        ('male_body_v6.glb', 'male_body_v6_shorts.glb'),
        'MaleBody_V6_Master40', 'Male Anatomy V6 directional anatomical sculpt',
        mat_overrides={
            0: {'baseColorFactor': [0.56, 0.39, 0.295, 1], 'roughnessFactor': 0.70},
            1: {'baseColorFactor': [0.56, 0.40, 0.34, 1], 'roughnessFactor': 0.42},
            2: {'baseColorFactor': [0.49, 0.30, 0.25, 1], 'roughnessFactor': 0.54},
            3: {'baseColorFactor': [0.60, 0.58, 0.54, 1], 'roughnessFactor': 0.22},
            5: {'baseColorFactor': [0.062, 0.030, 0.017, 1], 'roughnessFactor': 0.18},
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


# Exact 20-bone Mixamo T-Pose matching assets/characters/animations/humanoid_anims.glb
GAME_MIXAMO_20 = [
    ('mixamorig:Hips',          -1, [ 0.00,  0.95, 0.0], [ 0.0,  0.0,  0.0, 1.0]),
    ('mixamorig:Spine',          0, [ 0.00,  0.13, 0.0], [ 0.0,  0.0,  0.0, 1.0]),
    ('mixamorig:Spine1',         1, [ 0.00,  0.14, 0.0], [ 0.0,  0.0,  0.0, 1.0]),
    ('mixamorig:Spine2',         2, [ 0.00,  0.14, 0.0], [ 0.0,  0.0,  0.0, 1.0]),
    ('mixamorig:Neck',           3, [ 0.00,  0.16, 0.0], [ 0.0,  0.0,  0.0, 1.0]),
    ('mixamorig:Head',           4, [ 0.00,  0.14, 0.0], [ 0.0,  0.0,  0.0, 1.0]),
    ('mixamorig:LeftShoulder',   3, [ 0.08,  0.12, 0.0], [-0.5, -0.5, -0.5, 0.5]),
    ('mixamorig:LeftArm',        6, [ 0.00,  0.14, 0.0], [ 0.0,  0.0,  0.0, 1.0]),
    ('mixamorig:LeftForeArm',    7, [ 0.00,  0.26, 0.0], [ 0.0,  0.0,  0.0, 1.0]),
    ('mixamorig:LeftHand',       8, [ 0.00,  0.24, 0.0], [ 0.0,  0.0,  0.0, 1.0]),
    ('mixamorig:RightShoulder',  3, [-0.08,  0.12, 0.0], [-0.5,  0.5,  0.5, 0.5]),
    ('mixamorig:RightArm',      10, [ 0.00,  0.14, 0.0], [ 0.0,  0.0,  0.0, 1.0]),
    ('mixamorig:RightForeArm',  11, [ 0.00,  0.26, 0.0], [ 0.0,  0.0,  0.0, 1.0]),
    ('mixamorig:RightHand',     12, [ 0.00,  0.24, 0.0], [ 0.0,  0.0,  0.0, 1.0]),
    ('mixamorig:LeftUpLeg',      0, [ 0.10, -0.05, 0.0], [-1.0,  0.0,  0.0, 0.0]),
    ('mixamorig:LeftLeg',       14, [ 0.00,  0.42, 0.0], [ 0.0,  0.0,  0.0, 1.0]),
    ('mixamorig:LeftFoot',      15, [ 0.00,  0.40, 0.0], [ 0.0,  0.0,  0.0, 1.0]),
    ('mixamorig:RightUpLeg',     0, [-0.10, -0.05, 0.0], [-1.0,  0.0,  0.0, 0.0]),
    ('mixamorig:RightLeg',      17, [ 0.00,  0.42, 0.0], [ 0.0,  0.0,  0.0, 1.0]),
    ('mixamorig:RightFoot',     18, [ 0.00,  0.40, 0.0], [ 0.0,  0.0,  0.0, 1.0]),
]


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


def export_game_tpose_20(sculpted_glb_path, dst_glb_path, source_bytes):
    """Retarget sculpted body into exact 20-bone Mixamo T-Pose matching humanoid_anims.glb.

    V6 improvements:
    - Deduplicated, consolidated 20-joint top-4 skin weights (zero duplicate bone indices).
    - Localized volume-preserving Taubin relaxation on high-strain shoulder/axilla crease only.
    - Exact post-DQS area-weighted normal recomputation respecting hard splits & UV seams.
    - VEC4 UINT8 normalized COLOR_0 (RGBA) + closed-mesh backface culling.
    """
    src_raw = GLB(source_bytes)
    sn = {n['name']: i for i, n in enumerate(src_raw.d['nodes'])}
    S = lambda k: src_raw.matrix(sn[k])[:3, 3]

    M_tpose = np.zeros((20, 4, 4), dtype=float)
    for i, (_, par, tr, rot) in enumerate(GAME_MIXAMO_20):
        loc = np.eye(4)
        loc[:3, :3] = _quat_to_mat(rot)
        loc[:3, 3] = tr
        M_tpose[i] = M_tpose[par] @ loc if par >= 0 else loc

    M_anat = np.tile(np.eye(4), (20, 1, 1))
    torso_map = [
        (0, 'spine05'), (1, 'spine04'), (2, 'spine02'),
        (3, 'spine01'), (4, 'neck01'),  (5, 'head'),
    ]
    for idx, bname in torso_map:
        p = S(bname).copy()
        p[2] = 0.0
        M_anat[idx, :3, 3] = p

    # Left arm chain
    M_anat[6, :3, 3] = S('clavicle.L')
    M_anat[6, :3, :3] = _make_bone_frame(S('upperarm01.L') - S('clavicle.L'), np.array([0.0, 1.0, 0.0]), 'z')
    M_anat[7, :3, 3] = S('upperarm01.L')
    M_anat[7, :3, :3] = _make_bone_frame(S('lowerarm01.L') - S('upperarm01.L'), np.array([0.0, 1.0, 0.0]), 'z')
    M_anat[8, :3, 3] = S('lowerarm01.L')
    M_anat[8, :3, :3] = _make_bone_frame(S('wrist.L') - S('lowerarm01.L'), M_anat[7, :3, 2], 'z')
    M_anat[9, :3, 3] = S('wrist.L')
    M_anat[9, :3, :3] = _make_bone_frame(S('finger3-1.L') - S('wrist.L'), M_anat[8, :3, 2], 'z')

    # Right arm chain
    M_anat[10, :3, 3] = S('clavicle.R')
    M_anat[10, :3, :3] = _make_bone_frame(S('upperarm01.R') - S('clavicle.R'), np.array([0.0, 1.0, 0.0]), 'z')
    M_anat[11, :3, 3] = S('upperarm01.R')
    M_anat[11, :3, :3] = _make_bone_frame(S('lowerarm01.R') - S('upperarm01.R'), np.array([0.0, 1.0, 0.0]), 'z')
    M_anat[12, :3, 3] = S('lowerarm01.R')
    M_anat[12, :3, :3] = _make_bone_frame(S('wrist.R') - S('lowerarm01.R'), M_anat[11, :3, 2], 'z')
    M_anat[13, :3, 3] = S('wrist.R')
    M_anat[13, :3, :3] = _make_bone_frame(S('finger3-1.R') - S('wrist.R'), M_anat[12, :3, 2], 'z')

    # Leg chains
    for base, s in [(14, 'L'), (17, 'R')]:
        M_anat[base + 0, :3, 3] = S('upperleg01.' + s)
        M_anat[base + 0, :3, :3] = _make_bone_frame(S('lowerleg01.' + s) - S('upperleg01.' + s), np.array([0.0, 0.0, -1.0]), 'z')
        M_anat[base + 1, :3, 3] = S('lowerleg01.' + s)
        M_anat[base + 1, :3, :3] = _make_bone_frame(S('foot.' + s) - S('lowerleg01.' + s), np.array([0.0, 0.0, -1.0]), 'z')
        M_anat[base + 2, :3, 3] = S('foot.' + s)
        M_anat[base + 2, :3, :3] = np.diag([1.0, -1.0, -1.0])

    delta_mats = np.array([M_tpose[i] @ np.linalg.inv(M_anat[i]) for i in range(20)])

    g = GLB(sculpted_glb_path)
    old_joints = g.d['skins'][0]['joints']
    name_to_new = {name: i for i, (name, _, _, _) in enumerate(GAME_MIXAMO_20)}
    old_to_new = np.array([name_to_new.get(g.d['nodes'][j]['name'], 0) for j in old_joints], dtype=int)

    deformed_prims = []
    min_y = 1e9
    for p in g.d['meshes'][0]['primitives']:
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
        j_new = np.argsort(-W20, axis=1)[:, :4]
        w_new = np.take_along_axis(W20, j_new, axis=1)
        w_new /= np.maximum(w_new.sum(axis=1, keepdims=True), 1e-12)
        j_new[w_new <= 1e-7] = 0

        v_tpose, n_tpose = _dqs_skin(v_unscaled, n_orig, j_new, w_new, delta_mats)

        # Relax high-strain axilla/deltoid crease formed during 45 deg A-pose -> T-pose arm lift
        if p['material'] == 0:
            w_arm = W20[:, 7] + W20[:, 11]
            w_torso_shd = W20[:, 6] + W20[:, 10] + W20[:, 3]
            crease_mask = np.clip(4.0 * w_arm * w_torso_shd, 0.0, 1.0) * 0.65
            v_tpose = _taubin_smooth_masked(v_tpose, v_unscaled, faces, crease_mask, iters=3, lam=0.35, mu=-0.36)

        # Recompute exact normals on deformed T-pose triangles (skip rigid eyeballs)
        if p['material'] not in (3, 5, 6):
            n_tpose = _recompute_normals_split_safe(v_tpose, v_unscaled, n_orig, faces)

        min_y = min(min_y, float(v_tpose[:, 1].min()))
        deformed_prims.append(dict(
            v=v_tpose, n=n_tpose, uv=uv, j=j_new.astype(np.uint8),
            w=w_new.astype(np.float32), idx=idx, mat=p['material'], col=col,
        ))

    for dp in deformed_prims:
        dp['v'][:, 1] -= min_y

    nodes = []
    children_map = {i: [] for i in range(20)}
    for i, (_, par, _, _) in enumerate(GAME_MIXAMO_20):
        if par >= 0:
            children_map[par].append(i)
    for i, (name, _, tr, rot) in enumerate(GAME_MIXAMO_20):
        nd = {'name': name, 'translation': [float(x) for x in tr]}
        if rot != [0.0, 0.0, 0.0, 1.0]:
            nd['rotation'] = [float(x) for x in rot]
        if children_map[i]:
            nd['children'] = children_map[i]
        nodes.append(nd)
    nodes.append({'name': 'MaleBody_Game20_Mesh', 'mesh': 0, 'skin': 0})
    nodes.append({'name': 'MaleBody_Armature', 'children': [20, 0]})

    ibm = np.linalg.inv(M_tpose).transpose(0, 2, 1).reshape(-1, 16).astype('<f4')
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
            'generator': 'Male Body V6 Game-Ready 20-Bone Mixamo T-Pose',
        },
        'scene': 0,
        'scenes': [{'name': 'Scene', 'nodes': [21]}],
        'nodes': nodes,
        'skins': [{'name': 'MaleBody_Armature', 'joints': list(range(20)), 'inverseBindMatrices': ibm_acc}],
        'materials': copy.deepcopy(g.d['materials']),
        'meshes': [{'name': 'MaleBody_Game20', 'primitives': prims_out}],
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
    assert len(chk.d['skins'][0]['joints']) == 20
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
        f'Game20 T-Pose GLB: {dst_glb_path} | verts={total_v} tris={total_t} '
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out-dir', type=Path, default=root)
    parser.add_argument('--game-out', type=Path, default=None, help='Optional output path for game-ready 20-bone Mixamo T-Pose GLB (shorts version)')
    parser.add_argument('--version', choices=['anatomy', 'v4', 'v5', 'v6', 'all'], default='v6')
    parser.add_argument('--preview', action='store_true', help='Render 4-view PNGs for A-pose & T-pose (requires vtk, Pillow)')
    parser.add_argument('--print-code', action='store_true', help='Print editable source without embedded data')
    args = parser.parse_args()
    if args.print_code:
        marker = '# ' + '========== EMBEDDED DATA =========='
        print(Path(__file__).read_text(encoding='utf-8').split(marker, 1)[0], end='')
        return
    if args.preview and args.version not in ('v5', 'v6', 'all'):
        parser.error('--preview requires --version v5, v6, or all')
    D = args.out_dir.resolve()
    D.mkdir(parents=True, exist_ok=True)
    source, reference = embedded_inputs()
    build_anatomy(source, reference, D)
    write_master_skeleton(D / 'male_body_anatomy.glb', D / 'master_skeleton_40.json')
    if args.version in ('v4', 'all'):
        build_athletic(D)
    if args.version in ('v5', 'all'):
        build_refined(D)
    if args.version in ('v6', 'all'):
        build_v6(D)
        export_game_tpose_20(D / 'male_body_v6.glb', D / 'male_body_game20.glb', source)
        export_game_tpose_20(D / 'male_body_v6_shorts.glb', D / 'male_body_game20_shorts.glb', source)
    elif args.version == 'v5':
        export_game_tpose_20(D / 'male_body_refined.glb', D / 'male_body_game20.glb', source)
        export_game_tpose_20(D / 'male_body_refined_shorts.glb', D / 'male_body_game20_shorts.glb', source)

    if args.game_out is not None and args.version in ('v5', 'v6', 'all'):
        args.game_out.parent.mkdir(parents=True, exist_ok=True)
        args.game_out.write_bytes((D / 'male_body_game20_shorts.glb').read_bytes())
        print('Copied game-ready GLB ->', args.game_out)

    if args.preview:
        if args.version in ('v6', 'all'):
            render_preview(D, 'male_body_v6_shorts.glb', 'male_body_v6_preview.png')
            render_preview(D, 'male_body_game20_shorts.glb', 'male_body_game20_preview.png')
        else:
            render_preview(D, 'male_body_refined_shorts.glb', 'male_body_refined_preview.png')
    print('Finished:', D)