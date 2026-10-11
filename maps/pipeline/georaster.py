"""Strict uncompressed, north-up, single-band GeoTIFF reader (NumPy only).

Unsupported TIFF encodings/CRSs fail explicitly; convert them with GDAL first.
Raster samples are metres above datum; horizontal metrics require projected metres.
"""
import struct
import numpy as np


def read_dem(path):
    with open(path, 'rb') as f:
        header = f.read(8)
        if header[:2] not in (b'II', b'MM'):
            raise ValueError('Not a TIFF file')
        endian = '<' if header[:2] == b'II' else '>'
        magic, offset = struct.unpack(endian + 'HI', header[2:])
        if magic != 42:
            raise ValueError('Only classic TIFF supported; convert BigTIFF with GDAL')
        f.seek(offset)
        count = struct.unpack(endian + 'H', f.read(2))[0]
        entries = [struct.unpack(endian + 'HHI4s', f.read(12)) for _ in range(count)]
        tags = {}
        types = {2: ('c', 1), 3: ('H', 2), 4: ('I', 4), 12: ('d', 8)}
        for tag, typ, n, raw in entries:
            if typ not in types:
                continue
            fmt, size = types[typ]
            if n * size > 4:
                f.seek(struct.unpack(endian + 'I', raw)[0])
                raw = f.read(n * size)
            else:
                raw = raw[:n * size]
            tags[tag] = raw.rstrip(b'\0').decode('ascii') if typ == 2 else struct.unpack(endian + str(n) + fmt, raw)
        def scalar(tag, default):
            return tags.get(tag, (default,))[0]
        if scalar(259, 1) != 1 or scalar(317, 1) != 1:
            raise ValueError('Compressed/predicted TIFF unsupported; convert using GDAL COMPRESS=NONE PREDICTOR=1')
        if scalar(277, 1) != 1 or scalar(274, 1) != 1 or 34264 in tags:
            raise ValueError('DEM must be north-up with one band and no model transformation')
        bits, sample = scalar(258, 0), scalar(339, 1)
        kind = {1: 'u', 2: 'i', 3: 'f'}.get(sample)
        if kind is None or bits not in (8, 16, 32, 64) or (sample == 3 and bits < 32):
            raise ValueError('Unsupported sample encoding')
        dtype = np.dtype(endian + kind + str(bits // 8))
        w, h = scalar(256, 0), scalar(257, 0)
        if min(w, h) < 2:
            raise ValueError('DEM must have at least two rows and columns')
        dem = np.empty((h, w), np.float64)
        if 324 in tags:
            bw, bh = scalar(322, 0), scalar(323, 0)
            offsets, sizes = tags[324], tags[325]
        else:
            bw, bh = w, scalar(278, h)
            offsets, sizes = tags[273], tags[279]
        if bw < 1 or bh < 1:
            raise ValueError('Invalid TIFF block dimensions')
        across = (w + bw - 1) // bw
        expected = across * ((h + bh - 1) // bh)
        if len(offsets) != expected or len(sizes) != expected:
            raise ValueError('Incomplete TIFF tile/strip directory')
        for i, (off, size) in enumerate(zip(offsets, sizes)):
            x, y = (i % across) * bw, (i // across) * bh
            f.seek(off)
            rows = bh if 324 in tags else min(bh, h - y)
            block = np.frombuffer(f.read(size), dtype=dtype).reshape(rows, bw)
            dem[y:min(y+bh, h), x:min(x+bw, w)] = block[:min(bh,h-y), :min(bw,w-x)]
        scale, tie = tags.get(33550), tags.get(33922)
        if scale is None or tie is None or min(scale[:2]) <= 0:
            raise ValueError('GeoTIFF pixel scale and tie point are required')
        keys = tags.get(34735, ())
        keys = {keys[i]: keys[i+3] for i in range(4, len(keys), 4) if keys[i+1] == 0}
        if keys.get(1024) != 1 or keys.get(3076) != 9001 or keys.get(4099, 9001) != 9001:
            raise ValueError('Reproject DEM to a projected CRS in metres before baking')
        nodata = float(tags[42113]) if 42113 in tags else None
        valid = np.isfinite(dem)
        if nodata is not None:
            valid &= dem != nodata
        meta = dict(crs_epsg=keys.get(3072), pixel_size_m=list(scale[:2]),
                    origin=[tie[3]-tie[0]*scale[0], tie[4]+tie[1]*scale[1]],
                    pixel_is_area=keys.get(1025, 1) == 1, nodata=nodata,
                    shape=[h, w], valid_area_m2=float(valid.sum()*scale[0]*scale[1]))
        return dem, valid, meta
