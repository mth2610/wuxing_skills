with open("scratch/tune_fire_cell.py", "r") as f:
    code = f.read()

# Replace fld[ix, iy, iz] with fld[iz, iy, ix]
old_block = """    c000 = fld[ix, iy, iz]
    c100 = fld[ix + 1, iy, iz]
    c010 = fld[ix, iy + 1, iz]
    c110 = fld[ix + 1, iy + 1, iz]
    c001 = fld[ix, iy, iz + 1]
    c101 = fld[ix + 1, iy, iz + 1]
    c011 = fld[ix, iy + 1, iz + 1]
    c111 = fld[ix + 1, iy + 1, iz + 1]"""

new_block = """    c000 = fld[iz, iy, ix]
    c100 = fld[iz, iy, ix + 1]
    c010 = fld[iz, iy + 1, ix]
    c110 = fld[iz, iy + 1, ix + 1]
    c001 = fld[iz + 1, iy, ix]
    c101 = fld[iz + 1, iy, ix + 1]
    c011 = fld[iz + 1, iy + 1, ix]
    c111 = fld[iz + 1, iy + 1, ix + 1]"""

assert old_block in code, "old_block not found"
code = code.replace(old_block, new_block)

with open("scratch/tune_fire_cell.py", "w") as f:
    f.write(code)

