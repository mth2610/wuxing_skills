import re

with open("scratch/tune_fire_cell.py", "r") as f:
    code = f.read()

# Replace the normal output block
target = """        an_len = accum_norm.norm()
        n_unit = accum_norm / an_len if an_len > 1e-4 else ti.Vector([0.0, 0.0, 1.0])
        # BC5 format: R = 0.5 + 0.5*Nx, G = 0.5 + 0.5*Ny, B = 0, A = 1.0
        frame_norm[px, py] = ti.Vector([
            0.5 + 0.5 * n_unit.x,
            0.5 + 0.5 * n_unit.y,
            0.0,
            1.0
        ])"""

replacement = """        an_len = accum_norm.norm()
        n_unit = accum_norm / an_len if an_len > 1e-4 else ti.Vector([0.0, 0.0, 1.0])
        edge_blend = smoothstep_f(0.005, 0.30, coverage)
        n_smooth = n_unit * edge_blend + ti.Vector([0.0, 0.0, 1.0]) * (1.0 - edge_blend)
        ns_len = n_smooth.norm()
        n_final = n_smooth / ns_len if ns_len > 1e-4 else ti.Vector([0.0, 0.0, 1.0])
        frame_norm[px, py] = ti.Vector([
            0.5 + 0.5 * n_final.x,
            0.5 + 0.5 * n_final.y,
            0.0,
            1.0
        ])"""

assert target in code, "Target not found in code"
code = code.replace(target, replacement)
with open("scratch/tune_fire_cell.py", "w") as f:
    f.write(code)

