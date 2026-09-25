import numpy as np
from PIL import Image
import taichi as ti
import scratch.tune_fire_cell as tfc

# Load frame 48 and 64
for f_idx in [16, 48, 64]:
    p = f"build_cache/fire_tongue_01/f{f_idx:03d}.npz"
    z = np.load(p)
    tfc.flame.from_numpy(np.ascontiguousarray(z["flame"], np.float32))

    zoom = 1.05
    tfc.raymarch_cauliflower(zoom, 3.2, 5.0, 1.0, float(f_idx) * 0.08)

    c_arr = tfc.frame_color.to_numpy().transpose(1, 0, 2)
    c_u8 = (np.clip(c_arr, 0.0, 1.0) * 255.0).astype(np.uint8)

    # Check top/bottom/left/right border alpha
    top_a = c_u8[0, :, 3].max()
    bot_a = c_u8[-1, :, 3].max()
    left_a = c_u8[:, 0, 3].max()
    right_a = c_u8[:, -1, 3].max()
    print(f"Frame {f_idx:03d} at zoom {zoom:.2f}: border max alpha: top={top_a}, bot={bot_a}, left={left_a}, right={right_a}")
    Image.fromarray(c_u8, "RGBA").save(f"scratch/zoom_test_f{f_idx:03d}.png")
