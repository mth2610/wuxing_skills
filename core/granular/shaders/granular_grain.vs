#version 430 core
struct GranularGrain {
    vec4 position_radius;
    vec4 velocity_life;
    vec4 meta_ao;
    vec4 aux_pad; // x=shearWork, y=rotPhase, z=angSpeed, w=reserved
};

layout(std430, binding = 0) readonly buffer GranularState { GranularGrain grains[]; };

in vec3 vertexPosition; // .xy = quad corner (0..5), .z = vertex index 0..35

uniform mat4 u_view;
uniform mat4 u_projection;
uniform float u_aspectRatio;
uniform float u_time;

out vec2 v_corner;
out vec3 v_worldNormal;
out vec3 v_worldPos;
out vec3 v_localPos;
out float v_life;
out float v_seed;
out float v_contactAO;
out float v_materialId;
out float v_speed;
out float v_flags;
out float v_tiltShade;

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
    // Squash into realistic chiseled rock/clod proportions
    p *= vec3(1.12, mix(0.55, 0.85, fract(seed * 41.3)), mix(0.65, 0.95, fract(seed * 59.9)));
    return p;
}

// 3D Curved & Folded Botanical Leaf / Petal Vertex (8 triangles = 24 verts)
// Returns local (along, across, camber) and sets uvOut = (t*2-1, across)
vec3 leafVertex(int localV, float seed, bool isPetal, out vec2 uvOut)
{
    // 8 triangles forming a 3D cambered & V-folded blade:
    // Ring 0 (base): (-1, 0)
    // Ring 1 (t=0.35): L(-0.3, -1), M(-0.3, 0), R(-0.3, +1)
    // Ring 2 (t=0.70): L(+0.4, -1), M(+0.4, 0), R(+0.4, +1)
    // Ring 3 (tip):  (+1, 0)
    const int triVerts[24] = int[24](
        0, 2, 1,   0, 3, 2,   // base -> ring 1
        1, 2, 5,   1, 5, 4,   // ring 1 -> ring 2 (left)
        2, 3, 6,   2, 6, 5,   // ring 1 -> ring 2 (right)
        4, 5, 7,   5, 6, 7    // ring 2 -> tip
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
    // 6 prism vertices (3 front, 3 back) + 3 stub vertices
    float len = 1.45;
    float rBase = 0.18, rTip = 0.11;
    float bend = (fract(seed * 17.3) - 0.5) * 0.22;
    const vec2 tri[3] = vec2[3](vec2(0.0, 1.0), vec2(-0.866, -0.5), vec2(0.866, -0.5));

    // Triangles: 0..5 = 3 side quads (6 tris), 6..7 = side fork stub (2 tris)
    int triIdx = localV / 3;
    int corner = localV % 3;
    if (triIdx < 6) {
        int face = triIdx / 2;
        int nextFace = (face + 1) % 3;
        bool second = (triIdx % 2) == 1;
        // (face,0), (nextFace,0), (nextFace,1) | (face,0), (nextFace,1), (face,1)
        int fIdx = second ? (corner == 0 ? face : (corner == 1 ? nextFace : face))
                          : (corner == 0 ? face : nextFace);
        int endFlag = second ? (corner == 0 ? 0 : 1) : (corner == 2 ? 1 : 0);
        float x = endFlag == 0 ? -len : len;
        float rad = endFlag == 0 ? rBase : rTip;
        vec2 yz = tri[fIdx] * rad;
        uvOut = vec2(float(endFlag) * 2.0 - 1.0, float(fIdx) * 0.5 - 0.5);
        return vec3(x, yz.x + bend * (endFlag == 1 ? 0.5 : 0.0), yz.y);
    } else {
        // Side forked knot stub
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
    // 6 vertices: Tip(+1.35, 0, 0), Base(-0.95, 0, 0), Top(0, +0.16, 0), Bot(0, -0.16, 0), L(0, 0, -0.44), R(0, 0, +0.44)
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

void main()
{
    GranularGrain g = grains[gl_InstanceID];
    v_life = g.velocity_life.w;
    v_seed = g.meta_ao.x;
    v_materialId = g.meta_ao.y;
    v_contactAO = g.meta_ao.z;
    v_flags = g.meta_ao.w;
    v_speed = length(g.velocity_life.xyz);
    v_tiltShade = 1.0;
    v_worldNormal = vec3(0.0, 1.0, 0.0);
    v_worldPos = g.position_radius.xyz;
    v_localPos = vec3(0.0);
    v_corner = vertexPosition.xy;

    if (v_life <= 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    int vid = int(vertexPosition.z + 0.5);
    int matId = int(v_materialId + 0.5);
    bool isClod       = (matId == 1 || matId == 2);
    bool isLamina     = (matId == 5 || matId == 6 || matId == 8);
    bool isTwig       = (matId == 7);
    bool isCrystal    = (matId == 9 || matId == 11);
    bool isMetal      = (matId == 12);
    bool isFineSand   = !(isClod || isLamina || isTwig || isCrystal || isMetal);

    // Cull unused vertices for morphologies with fewer triangles
    int maxVerts = isFineSand ? 6 : (isCrystal ? 36 : 24);
    if (vid >= maxVerts) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    if (isFineSand) {
        vec3 centerView = (u_view * vec4(g.position_radius.xyz, 1.0)).xyz;
        float r = g.position_radius.w * 1.48 * mix(0.80, 1.25, fract(v_seed * 17.31));
        vec3 velView = mat3(u_view) * g.velocity_life.xyz;
        float speedXY = length(velView.xy);
        float stretch = clamp(1.0 + speedXY * 0.24, 1.0, 2.2);
        float crossScale = inversesqrt(stretch) * mix(0.76, 1.0, u_aspectRatio);

        float rotAngle = v_seed * 6.2831853;
        vec2 axis = speedXY > 0.2 ? velView.xy / speedXY : vec2(cos(rotAngle), sin(rotAngle));
        vec2 perp = vec2(-axis.y, axis.x);
        vec2 offsetView = (axis * (vertexPosition.x * stretch) + perp * (vertexPosition.y * crossScale)) * r;

        // Subpixel AA floor so distant grains never shimmer/pop
        float distCam = max(-centerView.z, 1.0);
        float minViewR = distCam * 0.0014;
        if (length(offsetView) < minViewR) {
            offsetView = normalize(offsetView + vec2(1e-5)) * minViewR;
        }
        v_worldPos = g.position_radius.xyz;
        gl_Position = u_projection * vec4(centerView + vec3(offsetView, 0.0), 1.0);
        return;
    }

    // ── True 3D Procedural Meshes (Clods, Leaves, Twigs, Crystals, Metal) ──
    float rotPhase = g.aux_pad.y;
    bool resting = (v_flags > 0.5 && v_flags < 1.5);
    float scaleR = g.position_radius.w * (isLamina ? 2.10 : (isClod ? 1.38 : (isTwig ? 1.85 : 1.70)))
                 * mix(0.80, 1.28, fract(v_seed * 17.31));

    // Deterministic 3D rotation frame
    vec3 spinAxis = normalize(vec3(
        fract(v_seed * 13.1) - 0.5,
        fract(v_seed * 29.7) - 0.5,
        fract(v_seed * 47.3) - 0.5
    ) + vec3(0.01, 0.1, 0.01));

    int triBase = (vid / 3) * 3;
    vec3 pA, pB, pC;
    vec2 uvA = vec2(0.0), uvB = vec2(0.0), uvC = vec2(0.0);
    int cornerIdx = vid % 3;

    if (isLamina) {
        bool isPetal = (matId == 8);
        float yaw   = v_seed * 6.2831853 + (resting ? 0.0 : rotPhase * 0.7);
        float pitch = resting ? 0.10 * sin(v_seed * 41.0) : (rotPhase + v_seed * 6.283);
        float roll  = resting ? 0.08 * cos(v_seed * 29.0) : sin(rotPhase * 1.35 + v_seed * 19.0) * 0.85;
        mat3 frame = eulerFrame(yaw, pitch, roll);

        pA = frame * leafVertex(triBase + 0, v_seed, isPetal, uvA);
        pB = frame * leafVertex(triBase + 1, v_seed, isPetal, uvB);
        pC = frame * leafVertex(triBase + 2, v_seed, isPetal, uvC);

        vec3 viewUp = mat3(u_view) * frame[2];
        v_tiltShade = 0.68 + 0.32 * abs(viewUp.z);
    } else if (isClod) {
        const int octTris[24] = int[24](
            2, 4, 0,   2, 0, 5,   2, 5, 1,   2, 1, 4,
            3, 0, 4,   3, 5, 0,   3, 1, 5,   3, 4, 1
        );
        float ang = resting ? v_seed * 6.2831853 : (rotPhase + v_seed * 6.2831853);
        pA = rotateAxisAngle(clodVertex(octTris[triBase + 0], v_seed), spinAxis, ang);
        pB = rotateAxisAngle(clodVertex(octTris[triBase + 1], v_seed), spinAxis, ang);
        pC = rotateAxisAngle(clodVertex(octTris[triBase + 2], v_seed), spinAxis, ang);
    } else if (isTwig) {
        float ang = resting ? v_seed * 6.2831853 : (rotPhase + v_seed * 6.2831853);
        vec3 twigAxis = resting ? vec3(0.0, 1.0, 0.0) : spinAxis;
        pA = rotateAxisAngle(twigVertex(triBase + 0, v_seed, uvA), twigAxis, ang);
        pB = rotateAxisAngle(twigVertex(triBase + 1, v_seed, uvB), twigAxis, ang);
        pC = rotateAxisAngle(twigVertex(triBase + 2, v_seed, uvC), twigAxis, ang);
    } else if (isCrystal) {
        float ang = resting ? v_seed * 6.2831853 : (rotPhase * 1.15 + v_seed * 6.2831853);
        pA = rotateAxisAngle(crystalVertex(triBase + 0, v_seed, uvA), spinAxis, ang);
        pB = rotateAxisAngle(crystalVertex(triBase + 1, v_seed, uvB), spinAxis, ang);
        pC = rotateAxisAngle(crystalVertex(triBase + 2, v_seed, uvC), spinAxis, ang);
    } else {
        // Metal shard
        float ang = resting ? v_seed * 6.2831853 : (rotPhase * 1.45 + v_seed * 6.2831853);
        pA = rotateAxisAngle(metalShardVertex(triBase + 0, v_seed, uvA), spinAxis, ang);
        pB = rotateAxisAngle(metalShardVertex(triBase + 1, v_seed, uvB), spinAxis, ang);
        pC = rotateAxisAngle(metalShardVertex(triBase + 2, v_seed, uvC), spinAxis, ang);
    }

    vec3 localPt = (cornerIdx == 0) ? pA : ((cornerIdx == 1) ? pB : pC);
    v_corner     = (cornerIdx == 0) ? uvA : ((cornerIdx == 1) ? uvB : uvC);
    v_localPos   = localPt;

    vec3 rawN = cross(pB - pA, pC - pA);
    float nLen2 = dot(rawN, rawN);
    v_worldNormal = (nLen2 > 1e-8) ? (rawN * inversesqrt(nLen2)) : vec3(0.0, 1.0, 0.0);

    vec3 worldPt = g.position_radius.xyz + localPt * scaleR;
    if (resting && isLamina) worldPt.y += 0.012;
    v_worldPos = worldPt;

    vec3 pView = (u_view * vec4(worldPt, 1.0)).xyz;
    gl_Position = u_projection * vec4(pView, 1.0);
}
