import os
import shutil
import numpy as np

src_dir = "build_cache/smoke_puff_dense"
dst_dir = "build_cache/smoke_puff_mature"
os.makedirs(dst_dir, exist_ok=True)

# Resample physical frames 16..64 (49 frames) into 64 frames
start_f = 16
end_f = 64
n_frames = 64

t_points = np.linspace(start_f, end_f, n_frames)

for idx, t in enumerate(t_points):
    i0 = int(np.floor(t))
    i1 = min(int(np.ceil(t)), end_f)
    alpha = t - i0

    z0 = np.load(os.path.join(src_dir, f"f{i0:03d}.npz"))
    d0 = z0["density"].astype(np.float32)

    if i0 == i1 or alpha < 1e-5:
        d_interp = d0
    else:
        z1 = np.load(os.path.join(src_dir, f"f{i1:03d}.npz"))
        d1 = z1["density"].astype(np.float32)
        d_interp = (1.0 - alpha) * d0 + alpha * d1

    zero_flame = np.zeros_like(d_interp, dtype=np.float16)
    out_path = os.path.join(dst_dir, f"f{idx + 1:03d}.npz")
    np.savez_compressed(
        out_path,
        density=d_interp.astype(np.float16),
        flame=zero_flame,
        temperature=zero_flame,
        res=np.array(d_interp.shape, dtype=np.int32)
    )

print(f"Resampled {start_f}..{end_f} into {n_frames} frames in {dst_dir}")
