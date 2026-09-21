#!/usr/bin/env python3
import taichi as ti
import numpy as np
from PIL import Image


# ============================================================
# CONFIG
# ============================================================

FRAME_SIZE = 256

GRID_X = 8
GRID_Y = 8
NUM_FRAMES = GRID_X * GRID_Y

SHEET_W = FRAME_SIZE * GRID_X
SHEET_H = FRAME_SIZE * GRID_Y

SUBSTEPS = 4

DT = 0.28
PRESSURE_ITERS = 40

DENSITY_DISSIPATION = 0.995
TEMPERATURE_DISSIPATION = 0.985
VELOCITY_DISSIPATION = 0.997

BUOYANCY = 0.75
SMOKE_WEIGHT = 0.04

VORTICITY = 1.8

NORMAL_STRENGTH = 8.0

# ti.gpu sẽ tự chọn CUDA / Vulkan / Metal / ...
ti.init(
    arch=ti.gpu,
    default_fp=ti.f32,
    random_seed=1234,
)


# ============================================================
# TAICHI FIELDS
#
# Taichi field layout ở đây là [x, y].
# Khi export sang PIL sẽ transpose + flip Y.
# ============================================================

N = FRAME_SIZE

velocity_a = ti.Vector.field(2, dtype=ti.f32, shape=(N, N))
velocity_b = ti.Vector.field(2, dtype=ti.f32, shape=(N, N))

density_a = ti.field(dtype=ti.f32, shape=(N, N))
density_b = ti.field(dtype=ti.f32, shape=(N, N))

temperature_a = ti.field(dtype=ti.f32, shape=(N, N))
temperature_b = ti.field(dtype=ti.f32, shape=(N, N))

pressure_a = ti.field(dtype=ti.f32, shape=(N, N))
pressure_b = ti.field(dtype=ti.f32, shape=(N, N))

divergence = ti.field(dtype=ti.f32, shape=(N, N))
curl = ti.field(dtype=ti.f32, shape=(N, N))


# ============================================================
# SAMPLING
# ============================================================

@ti.func
def clamp_x(x):
    return ti.max(0.0, ti.min(float(N - 1), x))


@ti.func
def sample_scalar(f: ti.template(), x: ti.f32, y: ti.f32):
    x = clamp_x(x)
    y = clamp_x(y)

    x0 = ti.cast(ti.floor(x), ti.i32)
    y0 = ti.cast(ti.floor(y), ti.i32)

    x1 = ti.min(x0 + 1, N - 1)
    y1 = ti.min(y0 + 1, N - 1)

    fx = x - float(x0)
    fy = y - float(y0)

    a = f[x0, y0]
    b = f[x1, y0]
    c = f[x0, y1]
    d = f[x1, y1]

    ab = a + (b - a) * fx
    cd = c + (d - c) * fx

    return ab + (cd - ab) * fy


@ti.func
def sample_vector(f: ti.template(), x: ti.f32, y: ti.f32):
    x = clamp_x(x)
    y = clamp_x(y)

    x0 = ti.cast(ti.floor(x), ti.i32)
    y0 = ti.cast(ti.floor(y), ti.i32)

    x1 = ti.min(x0 + 1, N - 1)
    y1 = ti.min(y0 + 1, N - 1)

    fx = x - float(x0)
    fy = y - float(y0)

    a = f[x0, y0]
    b = f[x1, y0]
    c = f[x0, y1]
    d = f[x1, y1]

    ab = a + (b - a) * fx
    cd = c + (d - c) * fx

    return ab + (cd - ab) * fy


@ti.func
def vel_at(f: ti.template(), x: ti.i32, y: ti.i32):
    xx = ti.max(0, ti.min(N - 1, x))
    yy = ti.max(0, ti.min(N - 1, y))

    return f[xx, yy]


@ti.func
def scalar_at(f: ti.template(), x: ti.i32, y: ti.i32):
    xx = ti.max(0, ti.min(N - 1, x))
    yy = ti.max(0, ti.min(N - 1, y))

    return f[xx, yy]


# ============================================================
# INITIALIZATION
# ============================================================

@ti.kernel
def clear_all():
    for i, j in density_a:
        velocity_a[i, j] = ti.Vector([0.0, 0.0])
        velocity_b[i, j] = ti.Vector([0.0, 0.0])

        density_a[i, j] = 0.0
        density_b[i, j] = 0.0

        temperature_a[i, j] = 0.0
        temperature_b[i, j] = 0.0

        pressure_a[i, j] = 0.0
        pressure_b[i, j] = 0.0

        divergence[i, j] = 0.0
        curl[i, j] = 0.0


# ============================================================
# SOURCE
#
# Source nằm hơi thấp hơn giữa frame giống texture reference.
# Chỉ hoạt động mạnh ở khoảng đầu animation để tạo một "puff"
# thay vì một cột khói phát liên tục.
# ============================================================

@ti.kernel
def inject_source(
    vel: ti.template(),
    den: ti.template(),
    temp: ti.template(),
    animation_t: ti.f32,
):
    for i, j in den:

        # Source khoảng:
        #
        # x ~ 58% width
        # y ~ 40% height
        #
        # Taichi y hướng lên.
        cx = float(N) * 0.58
        cy = float(N) * 0.40

        # Cho source dao động một chút để phá symmetry.
        cx += ti.sin(animation_t * 31.0) * 2.5
        cy += ti.sin(animation_t * 19.0) * 1.5

        dx = float(i) - cx
        dy = float(j) - cy

        r2 = dx * dx + dy * dy

        # Source tồn tại chủ yếu trong 30% đầu lifetime.
        source_life = ti.max(
            0.0,
            1.0 - animation_t / 0.30
        )

        # Ban đầu nhỏ, sau đó hơi nở.
        radius = 5.0 + animation_t * 18.0

        g = ti.exp(
            -r2 / (2.0 * radius * radius)
        )

        # Tạo irregularity.
        distortion = (
            0.78
            + 0.13 * ti.sin(float(i) * 0.18 + animation_t * 41.0)
            + 0.09 * ti.sin(float(j) * 0.23 - animation_t * 37.0)
        )

        g *= ti.max(0.0, distortion)
        g *= source_life

        if g > 0.00001:

            den[i, j] += g * 0.22

            temp[i, j] += g * 0.30

            dist = ti.sqrt(r2 + 1.0)

            nx = dx / dist
            ny = dy / dist

            # Explosion / expansion ban đầu.
            expansion = 1.6

            # Upward impulse.
            upward = 2.0

            # Một chút side swirl.
            swirl_x = -ny
            swirl_y = nx

            vel[i, j] += ti.Vector([
                nx * expansion
                + swirl_x * 0.55,

                ny * expansion
                + upward
                + swirl_y * 0.55
            ]) * g


# ============================================================
# ADVECTION
# ============================================================

@ti.kernel
def advect_velocity(
    src: ti.template(),
    dst: ti.template(),
):
    for i, j in src:

        v = src[i, j]

        old_x = float(i) - v.x * DT
        old_y = float(j) - v.y * DT

        dst[i, j] = (
            sample_vector(src, old_x, old_y)
            * VELOCITY_DISSIPATION
        )


@ti.kernel
def advect_scalar(
    src: ti.template(),
    dst: ti.template(),
    vel: ti.template(),
    dissipation: ti.f32,
):
    for i, j in src:

        v = vel[i, j]

        old_x = float(i) - v.x * DT
        old_y = float(j) - v.y * DT

        dst[i, j] = (
            sample_scalar(src, old_x, old_y)
            * dissipation
        )


# ============================================================
# BUOYANCY
# ============================================================

@ti.kernel
def apply_buoyancy(
    vel: ti.template(),
    den: ti.template(),
    temp: ti.template(),
):
    for i, j in den:

        lift = (
            BUOYANCY * temp[i, j]
            - SMOKE_WEIGHT * den[i, j]
        )

        vel[i, j].y += lift * DT


# ============================================================
# VORTICITY CONFINEMENT
#
# Phần này rất quan trọng nếu muốn khói có các mép cuộn/wispy
# giống flipbook reference.
# ============================================================

@ti.kernel
def compute_curl(vel: ti.template()):
    for i, j in curl:

        vl = vel_at(vel, i - 1, j)
        vr = vel_at(vel, i + 1, j)

        vb = vel_at(vel, i, j - 1)
        vt = vel_at(vel, i, j + 1)

        dv_dx = (vr.y - vl.y) * 0.5
        du_dy = (vt.x - vb.x) * 0.5

        curl[i, j] = dv_dx - du_dy


@ti.kernel
def apply_vorticity(vel: ti.template()):
    for i, j in vel:

        c_l = ti.abs(scalar_at(curl, i - 1, j))
        c_r = ti.abs(scalar_at(curl, i + 1, j))

        c_b = ti.abs(scalar_at(curl, i, j - 1))
        c_t = ti.abs(scalar_at(curl, i, j + 1))

        grad = ti.Vector([
            (c_r - c_l) * 0.5,
            (c_t - c_b) * 0.5
        ])

        length = grad.norm() + 1e-6
        n = grad / length

        omega = curl[i, j]

        # N x omega
        force = ti.Vector([
            n.y * omega,
            -n.x * omega
        ])

        vel[i, j] += (
            force
            * VORTICITY
            * DT
        )


# ============================================================
# PRESSURE PROJECTION
#
# Làm velocity field gần incompressible.
# ============================================================

@ti.kernel
def compute_divergence(vel: ti.template()):
    for i, j in divergence:

        vl = vel_at(vel, i - 1, j)
        vr = vel_at(vel, i + 1, j)

        vb = vel_at(vel, i, j - 1)
        vt = vel_at(vel, i, j + 1)

        divergence[i, j] = 0.5 * (
            vr.x - vl.x
            + vt.y - vb.y
        )


@ti.kernel
def clear_pressure(
    p: ti.template()
):
    for i, j in p:
        p[i, j] = 0.0


@ti.kernel
def pressure_jacobi(
    src: ti.template(),
    dst: ti.template(),
):
    for i, j in src:

        pl = scalar_at(src, i - 1, j)
        pr = scalar_at(src, i + 1, j)

        pb = scalar_at(src, i, j - 1)
        pt = scalar_at(src, i, j + 1)

        dst[i, j] = (
            pl + pr + pb + pt
            - divergence[i, j]
        ) * 0.25


@ti.kernel
def subtract_pressure_gradient(
    vel: ti.template(),
    pressure: ti.template(),
):
    for i, j in vel:

        pl = scalar_at(pressure, i - 1, j)
        pr = scalar_at(pressure, i + 1, j)

        pb = scalar_at(pressure, i, j - 1)
        pt = scalar_at(pressure, i, j + 1)

        grad = ti.Vector([
            (pr - pl) * 0.5,
            (pt - pb) * 0.5
        ])

        vel[i, j] -= grad


# ============================================================
# BOUNDARY
# ============================================================

@ti.kernel
def apply_velocity_boundary(
    vel: ti.template()
):
    for i, j in vel:

        if i <= 1 and vel[i, j].x < 0.0:
            vel[i, j].x = 0.0

        if i >= N - 2 and vel[i, j].x > 0.0:
            vel[i, j].x = 0.0

        if j <= 1 and vel[i, j].y < 0.0:
            vel[i, j].y = 0.0

        if j >= N - 2 and vel[i, j].y > 0.0:
            vel[i, j].y = 0.0


# ============================================================
# NUMPY POST PROCESSING
# ============================================================

def field_to_image(field):
    """
    Taichi:
        array[x, y]

    PIL / NumPy:
        array[y, x]

    Đồng thời đảo Y để +Y của simulation hướng lên.
    """

    a = field.to_numpy()

    return np.flipud(a.T)


def blur5(image):
    """
    Gaussian-ish separable blur:
        [1, 4, 6, 4, 1] / 16
    """

    p = np.pad(
        image,
        ((0, 0), (2, 2)),
        mode="edge"
    )

    horizontal = (
        p[:, 0:-4]
        + 4.0 * p[:, 1:-3]
        + 6.0 * p[:, 2:-2]
        + 4.0 * p[:, 3:-1]
        + p[:, 4:]
    ) / 16.0

    p = np.pad(
        horizontal,
        ((2, 2), (0, 0)),
        mode="edge"
    )

    vertical = (
        p[0:-4, :]
        + 4.0 * p[1:-3, :]
        + 6.0 * p[2:-2, :]
        + 4.0 * p[3:-1, :]
        + p[4:, :]
    ) / 16.0

    return vertical


def make_color_frame(density):
    """
    Tạo channel gần giống texture reference:

        B = 0

        alpha = density / opacity

        R tăng theo vùng đặc

        G gần 1.0 ở vùng mỏng,
        giảm nhẹ khi R tăng.

    Reference có đặc điểm:
        low density -> xanh
        high density -> vàng-xanh
    """

    # Beer-Lambert style opacity.
    alpha = 1.0 - np.exp(
        -density * 2.7
    )

    alpha = np.clip(
        alpha,
        0.0,
        1.0
    )

    # Cho edge wispy hơn.
    alpha = np.power(
        alpha,
        0.90
    )

    # Reference:
    # R tăng nonlinear theo opacity.
    red = np.power(
        alpha,
        1.55
    )

    green = (
        1.0
        - red * 0.12
    )

    blue = np.zeros_like(alpha)

    valid = alpha > (1.0 / 255.0)

    rgba = np.zeros(
        (FRAME_SIZE, FRAME_SIZE, 4),
        dtype=np.float32
    )

    rgba[..., 0] = red
    rgba[..., 1] = green
    rgba[..., 2] = blue
    rgba[..., 3] = alpha

    # Reference background thực sự:
    #
    # (0,0,0,0)
    rgba[~valid] = 0.0

    return np.uint8(
        np.clip(rgba * 255.0, 0, 255)
    )


def make_normal_frame(density):
    """
    Biến density thành height field rồi tính gradient.

    Reference normal map:
        neutral = 128,128,255
        B = 255 cố định
        A = 255

    nên ở đây ta cố tình KHÔNG encode Z đã normalized.
    Shader sẽ normalize sau.
    """

    height = (
        1.0
        - np.exp(-density * 2.7)
    )

    height = blur5(height)

    gx = np.zeros_like(height)
    gy = np.zeros_like(height)

    gx[:, 1:-1] = (
        height[:, 2:]
        - height[:, :-2]
    ) * 0.5

    gy[1:-1, :] = (
        height[2:, :]
        - height[:-2, :]
    ) * 0.5

    nx = np.clip(
        -gx * NORMAL_STRENGTH,
        -1.0,
        1.0
    )

    ny = np.clip(
        -gy * NORMAL_STRENGTH,
        -1.0,
        1.0
    )

    normal = np.empty(
        (FRAME_SIZE, FRAME_SIZE, 4),
        dtype=np.uint8
    )

    normal[..., 0] = np.uint8(
        np.clip(
            (nx * 0.5 + 0.5) * 255.0,
            0,
            255
        )
    )

    normal[..., 1] = np.uint8(
        np.clip(
            (ny * 0.5 + 0.5) * 255.0,
            0,
            255
        )
    )

    # Giống reference.
    normal[..., 2] = 255
    normal[..., 3] = 255

    return normal


# ============================================================
# SIMULATION STEP
# ============================================================

def simulate_substep(
    vel_src,
    vel_dst,
    den_src,
    den_dst,
    temp_src,
    temp_dst,
    p_src,
    p_dst,
    animation_t,
):
    # --------------------------------------------------------
    # Inject
    # --------------------------------------------------------

    inject_source(
        vel_src,
        den_src,
        temp_src,
        animation_t
    )

    # --------------------------------------------------------
    # Velocity advection
    # --------------------------------------------------------

    advect_velocity(
        vel_src,
        vel_dst
    )

    vel_src, vel_dst = vel_dst, vel_src

    # --------------------------------------------------------
    # Scalar advection
    # --------------------------------------------------------

    advect_scalar(
        den_src,
        den_dst,
        vel_src,
        DENSITY_DISSIPATION
    )

    den_src, den_dst = den_dst, den_src

    advect_scalar(
        temp_src,
        temp_dst,
        vel_src,
        TEMPERATURE_DISSIPATION
    )

    temp_src, temp_dst = temp_dst, temp_src

    # --------------------------------------------------------
    # Forces
    # --------------------------------------------------------

    apply_buoyancy(
        vel_src,
        den_src,
        temp_src
    )

    compute_curl(
        vel_src
    )

    apply_vorticity(
        vel_src
    )

    # --------------------------------------------------------
    # Projection
    # --------------------------------------------------------

    compute_divergence(
        vel_src
    )

    clear_pressure(
        p_src
    )

    clear_pressure(
        p_dst
    )

    for _ in range(PRESSURE_ITERS):

        pressure_jacobi(
            p_src,
            p_dst
        )

        p_src, p_dst = p_dst, p_src

    subtract_pressure_gradient(
        vel_src,
        p_src
    )

    apply_velocity_boundary(
        vel_src
    )

    return (
        vel_src,
        vel_dst,
        den_src,
        den_dst,
        temp_src,
        temp_dst,
        p_src,
        p_dst,
    )


# ============================================================
# GENERATE FLIPBOOK
# ============================================================

def main():

    clear_all()

    color_sheet = np.zeros(
        (SHEET_H, SHEET_W, 4),
        dtype=np.uint8
    )

    normal_sheet = np.zeros(
        (SHEET_H, SHEET_W, 4),
        dtype=np.uint8
    )

    normal_sheet[..., 0] = 128
    normal_sheet[..., 1] = 128
    normal_sheet[..., 2] = 255
    normal_sheet[..., 3] = 255

    vel_src = velocity_a
    vel_dst = velocity_b

    den_src = density_a
    den_dst = density_b

    temp_src = temperature_a
    temp_dst = temperature_b

    p_src = pressure_a
    p_dst = pressure_b

    total_steps = (
        NUM_FRAMES * SUBSTEPS
    )

    sim_step = 0

    for frame in range(NUM_FRAMES):

        for _ in range(SUBSTEPS):

            animation_t = (
                sim_step
                / float(total_steps - 1)
            )

            (
                vel_src,
                vel_dst,
                den_src,
                den_dst,
                temp_src,
                temp_dst,
                p_src,
                p_dst,
            ) = simulate_substep(
                vel_src,
                vel_dst,
                den_src,
                den_dst,
                temp_src,
                temp_dst,
                p_src,
                p_dst,
                animation_t,
            )

            sim_step += 1

        # GPU -> CPU
        ti.sync()

        density_image = field_to_image(
            den_src
        )

        color_frame = make_color_frame(
            density_image
        )

        normal_frame = make_normal_frame(
            density_image
        )

        row = frame // GRID_X
        col = frame % GRID_X

        x0 = col * FRAME_SIZE
        y0 = row * FRAME_SIZE

        color_sheet[
            y0:y0 + FRAME_SIZE,
            x0:x0 + FRAME_SIZE
        ] = color_frame

        normal_sheet[
            y0:y0 + FRAME_SIZE,
            x0:x0 + FRAME_SIZE
        ] = normal_frame

        print(
            f"frame {frame + 1:02d}/{NUM_FRAMES}"
        )

    Image.fromarray(
        color_sheet,
        mode="RGBA"
    ).save(
        "smoke_flipbook.png"
    )

    Image.fromarray(
        normal_sheet,
        mode="RGBA"
    ).save(
        "smoke_normal_flipbook.png"
    )

    print()
    print("Done")
    print(
        f"smoke_flipbook.png : "
        f"{SHEET_W}x{SHEET_H}"
    )

    print(
        f"smoke_normal_flipbook.png : "
        f"{SHEET_W}x{SHEET_H}"
    )


if __name__ == "__main__":
    main()