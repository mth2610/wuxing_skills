#!/usr/bin/env python3
"""Drop out-of-bounds triangles from a character GLB and rewrite it compactly.

Character garments are authored in metres around the origin: nothing legitimate
can sit more than ~1 m from the body centre line. A stray vertex left behind by
a bad boolean/UV op still produces a triangle that spans the whole arena once
skinned, so this script removes every triangle that touches a vertex outside a
sane bounding box and rewrites the binary chunk with only the referenced data.

Usage:
    python3 scripts/repair_glb_bounds.py assets/.../foo.glb            # in place (+ .bak)
    python3 scripts/repair_glb_bounds.py foo.glb --dry-run            # report only
    python3 scripts/repair_glb_bounds.py foo.glb --max-radius 1.2     # tune bounds
"""

from __future__ import annotations

import argparse
import json
import shutil
import struct
import sys

GLB_MAGIC = 0x46546C67
JSON_CHUNK = 0x4E4F534A
BIN_CHUNK = 0x004E4942

COMPONENT = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2),
             5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
TYPE_COUNT = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
ARRAY_BUFFER, ELEMENT_ARRAY = 34962, 34963


def read_glb(path):
    data = open(path, "rb").read()
    magic, version, length = struct.unpack_from("<III", data, 0)
    if magic != GLB_MAGIC or version != 2 or length != len(data):
        raise ValueError(f"{path}: not a valid GLB 2.0 file")
    offset, gltf, binary = 12, None, b""
    while offset < length:
        chunk_len, chunk_type = struct.unpack_from("<II", data, offset)
        offset += 8
        chunk = data[offset:offset + chunk_len]
        offset += chunk_len
        if chunk_type == JSON_CHUNK:
            gltf = json.loads(chunk.decode("utf-8"))
        elif chunk_type == BIN_CHUNK:
            binary = chunk
    if gltf is None:
        raise ValueError(f"{path}: missing JSON chunk")
    return gltf, binary


def write_glb(path, gltf, binary):
    js = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    js += b" " * ((4 - len(js) % 4) % 4)
    binary += b"\x00" * ((4 - len(binary) % 4) % 4)
    total = 12 + 8 + len(js) + (8 + len(binary) if binary else 0)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", GLB_MAGIC, 2, total))
        f.write(struct.pack("<II", len(js), JSON_CHUNK))
        f.write(js)
        if binary:
            f.write(struct.pack("<II", len(binary), BIN_CHUNK))
            f.write(binary)


def accessor_layout(gltf, index):
    acc = gltf["accessors"][index]
    view = gltf["bufferViews"][acc["bufferView"]]
    fmt, size = COMPONENT[acc["componentType"]]
    n = TYPE_COUNT[acc["type"]]
    elem = size * n
    stride = view.get("byteStride") or elem
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    return fmt, n, elem, stride, start


def read_elements(gltf, binary, index):
    fmt, n, _, stride, start = accessor_layout(gltf, index)
    return [struct.unpack_from("<" + fmt * n, binary, start + i * stride)
            for i in range(gltf["accessors"][index]["count"])]


class BinWriter:
    def __init__(self):
        self.data = bytearray()
        self.views = []

    def add(self, payload, target=None):
        while len(self.data) % 4:
            self.data.append(0)
        view = {"buffer": 0, "byteOffset": len(self.data), "byteLength": len(payload)}
        if target is not None:
            view["target"] = target
        self.views.append(view)
        self.data.extend(payload)
        return len(self.views) - 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("glb")
    ap.add_argument("--max-radius", type=float, default=1.5,
                    help="drop triangles with any |x|/|z| beyond this, or y outside [-0.2, 2.2]")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    gltf, binary = read_glb(args.glb)
    max_r = args.max_radius
    min_y, max_y = -0.2, 2.2

    def sane(p):
        return abs(p[0]) <= max_r and abs(p[2]) <= max_r and min_y <= p[1] <= max_y

    writer = BinWriter()
    new_accessors = []
    new_meshes = []

    def add_accessor(payload, component_type, count, type_name, target, minmax=None):
        view = writer.add(payload, target=target)
        acc = {"bufferView": view, "componentType": component_type,
               "count": count, "type": type_name}
        if minmax is not None:
            acc["min"], acc["max"] = minmax
        new_accessors.append(acc)
        return len(new_accessors) - 1

    for mesh in gltf.get("meshes", []):
        prims = []
        for prim in mesh["primitives"]:
            attrs = prim["attributes"]
            positions = read_elements(gltf, binary, attrs["POSITION"])
            index_list = [v[0] for v in read_elements(gltf, binary, prim["indices"])]
            assert len(index_list) % 3 == 0

            kept, dropped = [], 0
            for t in range(0, len(index_list), 3):
                tri = index_list[t:t + 3]
                if all(sane(positions[i]) for i in tri):
                    kept.extend(tri)
                else:
                    dropped += 1

            if not kept:
                print(f"  mesh '{mesh.get('name', '?')}' material {prim.get('material')}: "
                      f"all triangles out of bounds, primitive dropped")
                continue

            new_attrs = {}
            used_vertices = sorted(set(kept))
            for name, acc_index in attrs.items():
                fmt, n, _, stride, start = accessor_layout(gltf, acc_index)
                acc = gltf["accessors"][acc_index]
                payload = bytearray()
                lo = [float("inf")] * n
                hi = [float("-inf")] * n
                for vi in used_vertices:
                    v = struct.unpack_from("<" + fmt * n, binary, start + vi * stride)
                    payload.extend(struct.pack("<" + fmt * n, *v))
                    for c in range(n):
                        lo[c] = min(lo[c], v[c])
                        hi[c] = max(hi[c], v[c])
                minmax = None
                if acc["componentType"] == 5126:
                    minmax = ([round(x, 6) for x in lo], [round(x, 6) for x in hi])
                new_attrs[name] = add_accessor(bytes(payload), acc["componentType"],
                                               len(used_vertices), acc["type"],
                                               ARRAY_BUFFER, minmax)

            remap = {old: new for new, old in enumerate(used_vertices)}
            kept = [remap[i] for i in kept]
            if len(used_vertices) < 65536:
                index_payload = struct.pack("<" + "H" * len(kept), *kept)
                index_type = 5123
            else:
                index_payload = struct.pack("<" + "I" * len(kept), *kept)
                index_type = 5125
            new_prim = {k: v for k, v in prim.items() if k not in ("attributes", "indices")}
            new_prim["attributes"] = new_attrs
            new_prim["indices"] = add_accessor(index_payload, index_type,
                                               len(kept), "SCALAR", ELEMENT_ARRAY)
            prims.append(new_prim)
            print(f"  mesh '{mesh.get('name', '?')}' material {prim.get('material')}: "
                  f"kept {len(kept) // 3} tris, dropped {dropped}")

        if prims:
            new_meshes.append({"name": mesh.get("name", "mesh"), "primitives": prims})

    # Everything outside the meshes (skin inverse bind matrices) is copied as-is.
    remap = {}
    for old_index, acc in enumerate(gltf.get("accessors", [])):
        if acc.get("bufferView") is None:
            continue
        is_mesh_attr = any(acc is gltf["accessors"][a]
                           for mesh in gltf.get("meshes", [])
                           for prim in mesh["primitives"]
                           for a in list(prim["attributes"].values()) +
                           ([prim["indices"]] if "indices" in prim else []))
        if is_mesh_attr:
            continue
        fmt, n, elem, stride, start = accessor_layout(gltf, old_index)
        payload = bytearray()
        for i in range(acc["count"]):
            payload.extend(binary[start + i * stride:start + i * stride + elem])
        copied = dict(acc)
        copied.pop("byteOffset", None)
        copied["bufferView"] = writer.add(bytes(payload))
        remap[old_index] = len(new_accessors)
        new_accessors.append(copied)

    for skin in gltf.get("skins", []):
        if "inverseBindMatrices" in skin:
            skin["inverseBindMatrices"] = remap[skin["inverseBindMatrices"]]
    for anim in gltf.get("animations", []):
        for sampler in anim["samplers"]:
            sampler["input"] = remap[sampler["input"]]
            sampler["output"] = remap[sampler["output"]]

    gltf["meshes"] = new_meshes
    gltf["accessors"] = new_accessors
    gltf["bufferViews"] = writer.views
    gltf["buffers"] = [{"byteLength": len(writer.data)}]

    if args.dry_run:
        print("dry run: no file written")
        return

    shutil.copy2(args.glb, args.glb + ".bak")
    write_glb(args.glb, gltf, bytes(writer.data))
    print(f"repaired {args.glb} ({len(writer.data)} B binary, backup at {args.glb}.bak)")


if __name__ == "__main__":
    sys.exit(main())
