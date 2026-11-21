// Shared 3D Procedural Debris Mesh & Frame Helpers for GranularSim (body + shadow pass)

vec3 rotateAxisAngle(vec3 v, vec3 axis, float angle)
{
    float c = cos(angle), s = sin(angle);
    return v * c + cross(axis, v) * s + axis * dot(axis, v) * (1.0 - c);
}

mat3 eulerFrame(float yaw, float pitch, float roll)
{
    float cy = cos(yaw), sy = sin(yaw);
    float cp = cos(pitch), sp = sin(pitch);
    float cr = cos(roll), sr = sin(roll);
    vec3 lFwd   = vec3(cy * cp, sp, sy * cp);
    vec3 lUp    = vec3(-sy * sr - cy * sp * cr, cp * cr, cy * sr - sy * sp * cr);
    vec3 lRight = normalize(cross(lUp, lFwd));
    return mat3(lFwd, lRight, lUp);
}

// Per-seed jittered octahedron vertex for 3D Angular Clods / Gravel
vec3 clodVertex(int idx, float seed)
{
    const vec3 base[6] = vec3[6](
        vec3( 1.0,  0.0,  0.0),
        vec3(-1.0,  0.0,  0.0),
        vec3( 0.0,  1.0,  0.0),
        vec3( 0.0, -1.0,  0.0),
        vec3( 0.0,  0.0,  1.0),
        vec3( 0.0,  0.0, -1.0)
    );
    vec3 p = base[idx];
    float j0 = fract(seed * (13.7 + float(idx) * 7.3)) - 0.5;
    float j1 = fract(seed * (29.1 + float(idx) * 11.7)) - 0.5;
    p += vec3(j0 * 0.38, j1 * 0.34, (j0 - j1) * 0.30);
    p *= vec3(1.12, mix(0.55, 0.85, fract(seed * 41.3)), mix(0.65, 0.95, fract(seed * 59.9)));
    return p;
}

// 3D Curved & Folded Botanical Leaf / Petal Vertex (8 triangles = 24 verts)
vec3 leafVertex(int localV, float seed, bool isPetal, out vec2 uvOut)
{
    const int triVerts[24] = int[24](
        0, 2, 1,   0, 3, 2,
        1, 2, 5,   1, 5, 4,
        2, 3, 6,   2, 6, 5,
        4, 5, 7,   5, 6, 7
    );
    int vi = triVerts[localV];
    float t = (vi == 0) ? 0.0 : (vi <= 3 ? 0.35 : (vi <= 6 ? 0.72 : 1.0));
    float across = (vi == 1 || vi == 4) ? -1.0 : ((vi == 3 || vi == 6) ? 1.0 : 0.0);

    float w;
    if (isPetal) {
        w = sin(pow(clamp(t, 0.001, 1.0), 0.65) * 3.1415926) * 0.78;
    } else {
        w = sin(pow(clamp(t, 0.001, 1.0), 0.78) * 3.1415926) * (0.62 - 0.14 * t);
    }
    float curl = (fract(seed * 31.7) - 0.35) * 0.28 * t * t;
    float fold = 0.14 * abs(across) * w;
    float sideBend = (fract(seed * 19.3) - 0.5) * 0.18 * sin(t * 3.1415926);

    uvOut = vec2(t * 2.0 - 1.0, across);
    return vec3((t - 0.45) * 1.95, across * w + sideBend, curl + fold);
}

// 3D Triangular Bark Twig + Side Knot (8 triangles = 24 verts)
vec3 twigVertex(int localV, float seed, out vec2 uvOut)
{
    float len = 1.45;
    float rBase = 0.18, rTip = 0.11;
    float bend = (fract(seed * 17.3) - 0.5) * 0.22;
    const vec2 tri[3] = vec2[3](vec2(0.0, 1.0), vec2(-0.866, -0.5), vec2(0.866, -0.5));

    int triIdx = localV / 3;
    int corner = localV % 3;
    if (triIdx < 6) {
        int face = triIdx / 2;
        int nextFace = (face + 1) % 3;
        bool second = (triIdx % 2) == 1;
        int fIdx = second ? (corner == 0 ? face : (corner == 1 ? nextFace : face))
                          : (corner == 0 ? face : nextFace);
        int endFlag = second ? (corner == 0 ? 0 : 1) : (corner == 2 ? 1 : 0);
        float x = endFlag == 0 ? -len : len;
        float rad = endFlag == 0 ? rBase : rTip;
        vec2 yz = tri[fIdx] * rad;
        uvOut = vec2(float(endFlag) * 2.0 - 1.0, float(fIdx) * 0.5 - 0.5);
        return vec3(x, yz.x + bend * (endFlag == 1 ? 0.5 : 0.0), yz.y);
    } else {
        float sx = (fract(seed * 53.7) - 0.5) * 0.65;
        vec3 s0 = vec3(sx - 0.08, 0.06, -0.06);
        vec3 s1 = vec3(sx + 0.08, 0.06,  0.06);
        vec3 s2 = vec3(sx + 0.42, 0.45,  0.00);
        vec3 s3 = vec3(sx,       -0.06,  0.08);
        if (triIdx == 6) {
            uvOut = vec2(0.0, 0.5);
            return corner == 0 ? s0 : (corner == 1 ? s1 : s2);
        } else {
            uvOut = vec2(0.2, -0.5);
            return corner == 0 ? s1 : (corner == 1 ? s3 : s2);
        }
    }
}

// 3D Hexagonal Bipyramid Crystal / Ice Shard (12 triangles = 36 verts)
vec3 crystalVertex(int localV, float seed, out vec2 uvOut)
{
    int triIdx = localV / 3;
    int corner = localV % 3;
    bool topHalf = triIdx < 6;
    int side = triIdx % 6;
    int nextSide = (side + 1) % 6;

    float a0 = float(side) * 1.04719755;
    float a1 = float(nextSide) * 1.04719755;
    float r0 = 0.46 * (0.85 + 0.30 * fract(seed * (11.0 + float(side) * 3.7)));
    float r1 = 0.46 * (0.85 + 0.30 * fract(seed * (11.0 + float(nextSide) * 3.7)));

    vec3 apex = vec3(0.0, topHalf ? 1.38 : -1.20, 0.0);
    vec3 v0 = vec3(cos(a0) * r0, topHalf ? 0.12 : -0.12, sin(a0) * r0);
    vec3 v1 = vec3(cos(a1) * r1, topHalf ? 0.12 : -0.12, sin(a1) * r1);

    if (corner == 0) { uvOut = vec2(0.5, 1.0); return apex; }
    if (corner == 1) { uvOut = vec2(0.0, 0.0); return topHalf ? v0 : v1; }
    uvOut = vec2(1.0, 0.0);
    return topHalf ? v1 : v0;
}

// 3D 4-Plane Diamond-Bevel Metal Shard (8 triangles = 24 verts)
vec3 metalShardVertex(int localV, float seed, out vec2 uvOut)
{
    float asym = 0.85 + 0.30 * fract(seed * 23.7);
    const vec3 pts[6] = vec3[6](
        vec3( 1.38,  0.00,  0.00),
        vec3(-0.92,  0.00,  0.00),
        vec3( 0.10,  0.15,  0.00),
        vec3( 0.10, -0.15,  0.00),
        vec3(-0.10,  0.00, -0.44),
        vec3(-0.10,  0.00,  0.44)
    );
    const int tris[24] = int[24](
        0, 2, 5,   0, 4, 2,   0, 5, 3,   0, 3, 4,
        1, 5, 2,   1, 2, 4,   1, 3, 5,   1, 4, 3
    );
    vec3 p = pts[tris[localV]];
    p.z *= asym;
    uvOut = vec2(p.x, p.z * 2.0);
    return p;
}

// 3D Thin Flat Fractured Desert Shale / Slate Slab (8 triangles = 24 verts)
vec3 shaleVertex(int localV, float seed, out vec2 uvOut)
{
    const vec3 base[6] = vec3[6](
        vec3( 1.52,  0.00,  0.08),
        vec3(-1.42,  0.00, -0.10),
        vec3( 0.06,  0.24,  0.02),
        vec3(-0.04, -0.24, -0.02),
        vec3( 0.14,  0.02,  1.08),
        vec3(-0.18, -0.02, -1.04)
    );
    const int tris[24] = int[24](
        2, 4, 0,   2, 0, 5,   2, 5, 1,   2, 1, 4,
        3, 0, 4,   3, 5, 0,   3, 1, 5,   3, 4, 1
    );
    int idx = tris[localV];
    vec3 p = base[idx];
    float j0 = fract(seed * (17.3 + float(idx) * 9.1)) - 0.5;
    float j1 = fract(seed * (31.7 + float(idx) * 13.3)) - 0.5;
    p.x += j0 * 0.42;
    p.z += j1 * 0.36;
    p.y *= mix(0.75, 1.25, fract(seed * 47.1));
    uvOut = vec2(p.x * 0.65, p.z * 0.85);
    return p;
}
