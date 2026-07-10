/* ===========================================================================
 * CORE GEOMETRY PRIMITIVES
 * =========================================================================*/

void DrawCoreSphere(Vector3 center, float radius, int rings, int slices, Color color)
{
    if (rings < 3)
        rings = 3;
    if (slices < 3)
        slices = 3;

    rlCheckRenderBatchLimit(rings * slices * 4);

    float cosPhi[slices + 1];
    float sinPhi[slices + 1];
    for (int j = 0; j <= slices; j++)
    {
        float phi = (float)j * (2.0f * PI) / slices;
        cosPhi[j] = cosf(phi);
        sinPhi[j] = sinf(phi);
    }

    rlBegin(RL_QUADS);
    rlColor4ub(color.r, color.g, color.b, color.a);

    float prevTheta = 0.0f;
    float prevSt = sinf(prevTheta), prevCt = cosf(prevTheta);

    for (int i = 0; i < rings; i++)
    {
        float theta2 = (float)(i + 1) * PI / rings;
        float st1 = prevSt, ct1 = prevCt;
        float st2 = sinf(theta2), ct2 = cosf(theta2);
        prevSt = st2;
        prevCt = ct2;

        float vt1 = (float)i / rings;
        float vt2 = (float)(i + 1) / rings;

        for (int j = 0; j < slices; j++)
        {
            float cp1 = cosPhi[j], sp1 = sinPhi[j];
            float cp2 = cosPhi[j + 1], sp2 = sinPhi[j + 1];

            float u1 = (float)j / slices;
            float u2 = (float)(j + 1) / slices;

            Vector3 n1 = {st1 * cp1, ct1, st1 * sp1};
            Vector3 n2 = {st1 * cp2, ct1, st1 * sp2};
            Vector3 n3 = {st2 * cp2, ct2, st2 * sp2};
            Vector3 n4 = {st2 * cp1, ct2, st2 * sp1};

            rlNormal3f(n1.x, n1.y, n1.z);
            rlTexCoord2f(u1, vt1);
            rlVertex3f(center.x + n1.x * radius, center.y + n1.y * radius, center.z + n1.z * radius);

            rlNormal3f(n2.x, n2.y, n2.z);
            rlTexCoord2f(u2, vt1);
            rlVertex3f(center.x + n2.x * radius, center.y + n2.y * radius, center.z + n2.z * radius);

            rlNormal3f(n3.x, n3.y, n3.z);
            rlTexCoord2f(u2, vt2);
            rlVertex3f(center.x + n3.x * radius, center.y + n3.y * radius, center.z + n3.z * radius);

            rlNormal3f(n4.x, n4.y, n4.z);
            rlTexCoord2f(u1, vt2);
            rlVertex3f(center.x + n4.x * radius, center.y + n4.y * radius, center.z + n4.z * radius);
        }
    }
    rlEnd();
}

// Camera-facing quad (billboard) — for shader-driven 2D-domain effects
// (e.g. energy smoke erosion-dissolve) where the shape should be defined
// entirely by the shader's own alpha/density, not by a 3D mesh silhouette
// (a sphere's silhouette is always a hard geometric circle in screen space
// no matter what surface noise does; a flat quad has no such constraint —
// the visible shape is purely whatever the fragment shader outputs).
// vertexNormal is set to face the camera (-forward) so shaders reading
// fragNormal (e.g. rim/fresnel terms) still get a sane value.
void DrawCoreBillboardQuad(Vector3 center, float halfSize, Camera3D cam, Color color)
{
    Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 right   = Vector3Normalize(Vector3CrossProduct(forward, cam.up));
    Vector3 up      = Vector3CrossProduct(right, forward);
    Vector3 n       = Vector3Negate(forward);

    Vector3 p00 = Vector3Add(center, Vector3Add(Vector3Scale(right, -halfSize), Vector3Scale(up, -halfSize)));
    Vector3 p10 = Vector3Add(center, Vector3Add(Vector3Scale(right,  halfSize), Vector3Scale(up, -halfSize)));
    Vector3 p11 = Vector3Add(center, Vector3Add(Vector3Scale(right,  halfSize), Vector3Scale(up,  halfSize)));
    Vector3 p01 = Vector3Add(center, Vector3Add(Vector3Scale(right, -halfSize), Vector3Scale(up,  halfSize)));

    rlBegin(RL_QUADS);
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(0.0f, 0.0f); rlVertex3f(p00.x, p00.y, p00.z);
    rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(1.0f, 0.0f); rlVertex3f(p10.x, p10.y, p10.z);
    rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(1.0f, 1.0f); rlVertex3f(p11.x, p11.y, p11.z);
    rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(0.0f, 1.0f); rlVertex3f(p01.x, p01.y, p01.z);
    rlEnd();
}

// Flat quad on a FIXED world-space plane (center + normal) — not camera-
// facing like DrawCoreBillboardQuad, not terrain-conforming like
// DrawCoreGroundPatch (that one's per-vertex heightFn is Y-up/absolute-Y
// specific, matching MapProp_SampleGroundHeight — doesn't generalize to an
// arbitrary normal). This is the "put a density-field quad on any surface"
// primitive: a sloped rock face, a wall, a ceiling, not just horizontal
// ground. right/fwd basis built via Gram-Schmidt against world-up (or
// world-right if normal is nearly vertical, avoiding a degenerate cross
// product when normal≈(0,±1,0)).
void DrawCoreOrientedQuad(Vector3 center, Vector3 normal, float halfSize, Color color)
{
    Vector3 n = Vector3Normalize(normal);
    Vector3 up = (fabsf(n.y) < 0.99f) ? (Vector3){0.0f, 1.0f, 0.0f} : (Vector3){1.0f, 0.0f, 0.0f};
    Vector3 right = Vector3Normalize(Vector3CrossProduct(up, n));
    Vector3 fwd   = Vector3CrossProduct(n, right);

    Vector3 p00 = Vector3Add(center, Vector3Add(Vector3Scale(right, -halfSize), Vector3Scale(fwd, -halfSize)));
    Vector3 p10 = Vector3Add(center, Vector3Add(Vector3Scale(right,  halfSize), Vector3Scale(fwd, -halfSize)));
    Vector3 p11 = Vector3Add(center, Vector3Add(Vector3Scale(right,  halfSize), Vector3Scale(fwd,  halfSize)));
    Vector3 p01 = Vector3Add(center, Vector3Add(Vector3Scale(right, -halfSize), Vector3Scale(fwd,  halfSize)));

    rlBegin(RL_QUADS);
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(0.0f, 0.0f); rlVertex3f(p00.x, p00.y, p00.z);
    rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(1.0f, 0.0f); rlVertex3f(p10.x, p10.y, p10.z);
    rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(1.0f, 1.0f); rlVertex3f(p11.x, p11.y, p11.z);
    rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(0.0f, 1.0f); rlVertex3f(p01.x, p01.y, p01.z);
    rlEnd();
}

// N vertical rectangles all sharing the vertical (Y) axis through `base`,
// evenly rotated around Y (classic "cross billboard" — same trick as
// crossed-plane tree/grass sprites), rising from `base` to `base + (0,height,0)`.
// Unlike DrawCoreBillboardQuad this is NOT camera-facing — the planes are
// fixed in world space, so 2-3 of them read as a volumetric column shape
// from any horizontal viewing angle without a true cylinder mesh. Vertical
// UV: v=0 at the base, v=1 at the top — pair with a shader that reads
// fragTexCoord as [-1,1] uv and offsets its origin toward v=0 (a "base
// source" point) for e.g. a rising-smoke density field.
void DrawCoreCrossQuads(Vector3 base, float halfWidth, float height, int planeCount, Color color)
{
    if (planeCount < 1) return;

    for (int i = 0; i < planeCount; i++)
    {
        float theta = (PI * i) / planeCount; // planes are full rectangles, so only need to span 180°
        Vector3 right = { cosf(theta), 0.0f, sinf(theta) };
        Vector3 n     = { -sinf(theta), 0.0f, cosf(theta) }; // arbitrary but consistent facing for lighting

        Vector3 p00 = Vector3Add(base, Vector3Scale(right, -halfWidth));
        Vector3 p10 = Vector3Add(base, Vector3Scale(right,  halfWidth));
        Vector3 p11 = Vector3Add(p10, (Vector3){0.0f, height, 0.0f});
        Vector3 p01 = Vector3Add(p00, (Vector3){0.0f, height, 0.0f});

        rlBegin(RL_QUADS);
        rlColor4ub(color.r, color.g, color.b, color.a);
        rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(0.0f, 0.0f); rlVertex3f(p00.x, p00.y, p00.z);
        rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(1.0f, 0.0f); rlVertex3f(p10.x, p10.y, p10.z);
        rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(1.0f, 1.0f); rlVertex3f(p11.x, p11.y, p11.z);
        rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(0.0f, 1.0f); rlVertex3f(p01.x, p01.y, p01.z);
        rlEnd();
    }
}

// Horizontal ground-hugging patch — a subdiv×subdiv grid on the XZ plane
// centered at `center`, with each vertex's Y independently queried through
// `heightFn` (NULL = flat, all vertices at center.y). Lets a shader-driven
// ground effect (dry-ice gas, fog patch) conform to sloped/heightmap terrain
// instead of floating above or clipping through it like a single flat quad
// would — see MAP_API.md's GetHeightmapHeight for the kind of sampler a map
// would plug in here (this file stays map-agnostic; it only calls whatever
// function pointer it's given). UV: standard [0,1] across the whole patch,
// pair with a shader that reads fragTexCoord as [-1,1] uv (same convention
// as DrawCoreBillboardQuad/DrawCoreCrossQuads).
//
// `yLift` pushes every vertex that many meters above the sampled height —
// REQUIRED to be > 0 whenever the patch sits on/near real ground geometry:
// a translucent quad exactly coplanar with an opaque ground mesh z-fights
// against it, which reads as a jagged, hard-edged clip through the smoke
// (not a shader bug — confirmed by core/decal_system.c:214 needing the same
// 0.02f lift for ground decals). ~0.02-0.05 is enough at this project's
// close VFX viewing distances; 0.0 only if the caller has already biased
// `heightFn` itself.
void DrawCoreGroundPatch(Vector3 center, float halfSize, int subdiv, float yLift,
                         GroundHeightSampleFn heightFn, void *userData, Color color)
{
    if (halfSize <= 0.0f) return;
    if (subdiv < 1) subdiv = 1;

    rlBegin(RL_TRIANGLES);
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlNormal3f(0.0f, 1.0f, 0.0f);

    for (int iz = 0; iz < subdiv; iz++)
    {
        for (int ix = 0; ix < subdiv; ix++)
        {
            float u0 = (float)ix / subdiv, u1 = (float)(ix + 1) / subdiv;
            float v0 = (float)iz / subdiv, v1 = (float)(iz + 1) / subdiv;

            float x0 = center.x + (u0 * 2.0f - 1.0f) * halfSize;
            float x1 = center.x + (u1 * 2.0f - 1.0f) * halfSize;
            float z0 = center.z + (v0 * 2.0f - 1.0f) * halfSize;
            float z1 = center.z + (v1 * 2.0f - 1.0f) * halfSize;

            float y00 = (heightFn ? heightFn(x0, z0, userData) : center.y) + yLift;
            float y10 = (heightFn ? heightFn(x1, z0, userData) : center.y) + yLift;
            float y11 = (heightFn ? heightFn(x1, z1, userData) : center.y) + yLift;
            float y01 = (heightFn ? heightFn(x0, z1, userData) : center.y) + yLift;

            rlTexCoord2f(u0, v0); rlVertex3f(x0, y00, z0);
            rlTexCoord2f(u1, v0); rlVertex3f(x1, y10, z0);
            rlTexCoord2f(u1, v1); rlVertex3f(x1, y11, z1);

            rlTexCoord2f(u0, v0); rlVertex3f(x0, y00, z0);
            rlTexCoord2f(u1, v1); rlVertex3f(x1, y11, z1);
            rlTexCoord2f(u0, v1); rlVertex3f(x0, y01, z1);
        }
    }
    rlEnd();
}

void DrawCoreCylinder(Vector3 bottom, Vector3 top, float radiusBottom, float radiusTop, int slices, Color color)
{
    if (slices < 3)
        slices = 3;

    Vector3 axis = Vector3Subtract(top, bottom);
    float height = Vector3Length(axis);
    if (height < 0.0001f)
        return;

    Vector3 dir = Vector3Normalize(axis);
    Vector3 u;
    if (fabsf(dir.x) > 0.9f)
    {
        u = (Vector3){0.0f, 1.0f, 0.0f};
    }
    else
    {
        u = (Vector3){1.0f, 0.0f, 0.0f};
    }
    Vector3 v = Vector3Normalize(Vector3CrossProduct(dir, u));
    u = Vector3Normalize(Vector3CrossProduct(v, dir));

    float slopeLen = sqrtf((radiusBottom - radiusTop) * (radiusBottom - radiusTop) + height * height);
    float nRadial = (slopeLen > 0.0001f) ? (height / slopeLen) : 1.0f;
    float nAxial = (slopeLen > 0.0001f) ? ((radiusBottom - radiusTop) / slopeLen) : 0.0f;

    float cosPhi[slices + 1];
    float sinPhi[slices + 1];
    for (int j = 0; j <= slices; j++)
    {
        float phi = (float)j * (2.0f * PI) / slices;
        cosPhi[j] = cosf(phi);
        sinPhi[j] = sinf(phi);
    }

    rlCheckRenderBatchLimit(slices * 4);
    rlBegin(RL_QUADS);
    rlColor4ub(color.r, color.g, color.b, color.a);
    for (int j = 0; j < slices; j++)
    {
        float c1 = cosPhi[j], s1 = sinPhi[j];
        float c2 = cosPhi[j + 1], s2 = sinPhi[j + 1];

        Vector3 ringDir1 = Vector3Add(Vector3Scale(u, c1), Vector3Scale(v, s1));
        Vector3 ringDir2 = Vector3Add(Vector3Scale(u, c2), Vector3Scale(v, s2));

        Vector3 p1 = Vector3Add(bottom, Vector3Scale(ringDir1, radiusBottom));
        Vector3 p2 = Vector3Add(bottom, Vector3Scale(ringDir2, radiusBottom));
        Vector3 p3 = Vector3Add(top, Vector3Scale(ringDir2, radiusTop));
        Vector3 p4 = Vector3Add(top, Vector3Scale(ringDir1, radiusTop));

        Vector3 n1 = Vector3Add(Vector3Scale(ringDir1, nRadial), Vector3Scale(dir, nAxial));
        Vector3 n2 = Vector3Add(Vector3Scale(ringDir2, nRadial), Vector3Scale(dir, nAxial));

        rlNormal3f(n1.x, n1.y, n1.z);
        rlTexCoord2f((float)j / slices, 0.0f);
        rlVertex3f(p1.x, p1.y, p1.z);
        rlNormal3f(n2.x, n2.y, n2.z);
        rlTexCoord2f((float)(j + 1) / slices, 0.0f);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlNormal3f(n2.x, n2.y, n2.z);
        rlTexCoord2f((float)(j + 1) / slices, 1.0f);
        rlVertex3f(p3.x, p3.y, p3.z);
        rlNormal3f(n1.x, n1.y, n1.z);
        rlTexCoord2f((float)j / slices, 1.0f);
        rlVertex3f(p4.x, p4.y, p4.z);
    }
    rlEnd();

    if (radiusBottom > 0.0f)
    {
        rlCheckRenderBatchLimit(slices * 3);
        rlBegin(RL_TRIANGLES);
        rlColor4ub(color.r, color.g, color.b, color.a);
        rlNormal3f(-dir.x, -dir.y, -dir.z);
        for (int j = 0; j < slices; j++)
        {
            float c1 = cosPhi[j], s1 = sinPhi[j];
            float c2 = cosPhi[j + 1], s2 = sinPhi[j + 1];
            Vector3 ringDir1 = Vector3Add(Vector3Scale(u, c1), Vector3Scale(v, s1));
            Vector3 ringDir2 = Vector3Add(Vector3Scale(u, c2), Vector3Scale(v, s2));
            Vector3 p1 = Vector3Add(bottom, Vector3Scale(ringDir1, radiusBottom));
            Vector3 p2 = Vector3Add(bottom, Vector3Scale(ringDir2, radiusBottom));
            rlVertex3f(bottom.x, bottom.y, bottom.z);
            rlVertex3f(p2.x, p2.y, p2.z);
            rlVertex3f(p1.x, p1.y, p1.z);
        }
        rlEnd();
    }

    if (radiusTop > 0.0f)
    {
        rlCheckRenderBatchLimit(slices * 3);
        rlBegin(RL_TRIANGLES);
        rlColor4ub(color.r, color.g, color.b, color.a);
        rlNormal3f(dir.x, dir.y, dir.z);
        for (int j = 0; j < slices; j++)
        {
            float c1 = cosPhi[j], s1 = sinPhi[j];
            float c2 = cosPhi[j + 1], s2 = sinPhi[j + 1];
            Vector3 ringDir1 = Vector3Add(Vector3Scale(u, c1), Vector3Scale(v, s1));
            Vector3 ringDir2 = Vector3Add(Vector3Scale(u, c2), Vector3Scale(v, s2));
            Vector3 p1 = Vector3Add(top, Vector3Scale(ringDir1, radiusTop));
            Vector3 p2 = Vector3Add(top, Vector3Scale(ringDir2, radiusTop));
            rlVertex3f(top.x, top.y, top.z);
            rlVertex3f(p1.x, p1.y, p1.z);
            rlVertex3f(p2.x, p2.y, p2.z);
        }
        rlEnd();
    }
}

void DrawCoreCone(Vector3 bottom, float radius, float height, int slices, Color color)
{
    if (slices < 3)
        slices = 3;
    Vector3 top = {bottom.x, bottom.y + height, bottom.z};

    float len = sqrtf(radius * radius + height * height);
    float ny = (len > 0.0001f) ? (radius / len) : 0.0f;
    float nxz = (len > 0.0001f) ? (height / len) : 1.0f;

    float cosPhi[slices + 1];
    float sinPhi[slices + 1];
    for (int j = 0; j <= slices; j++)
    {
        float phi = (float)j * (2.0f * PI) / slices;
        cosPhi[j] = cosf(phi);
        sinPhi[j] = sinf(phi);
    }

    if (radius > 0.0f)
    {
        rlCheckRenderBatchLimit(slices * 3);
        rlBegin(RL_TRIANGLES);
        rlColor4ub(color.r, color.g, color.b, color.a);
        rlNormal3f(0.0f, -1.0f, 0.0f);
        for (int j = 0; j < slices; j++)
        {
            float c1 = cosPhi[j], s1 = sinPhi[j];
            float c2 = cosPhi[j + 1], s2 = sinPhi[j + 1];
            Vector3 p1 = {bottom.x + c1 * radius, bottom.y, bottom.z + s1 * radius};
            Vector3 p2 = {bottom.x + c2 * radius, bottom.y, bottom.z + s2 * radius};
            rlVertex3f(bottom.x, bottom.y, bottom.z);
            rlVertex3f(p2.x, p2.y, p2.z);
            rlVertex3f(p1.x, p1.y, p1.z);
        }
        rlEnd();
    }

    rlCheckRenderBatchLimit(slices * 3);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(color.r, color.g, color.b, color.a);
    for (int j = 0; j < slices; j++)
    {
        float phi1 = (float)j * (2.0f * PI) / slices;
        float phi2 = (j + 1 == slices) ? 0.0f : (float)(j + 1) * (2.0f * PI) / slices;
        float c1 = cosPhi[j], s1 = sinPhi[j];
        float c2 = cosPhi[j + 1], s2 = sinPhi[j + 1];
        Vector3 p1 = {bottom.x + c1 * radius, bottom.y, bottom.z + s1 * radius};
        Vector3 p2 = {bottom.x + c2 * radius, bottom.y, bottom.z + s2 * radius};

        float avgCos = cosf((phi1 + phi2) * 0.5f);
        float avgSin = sinf((phi1 + phi2) * 0.5f);
        Vector3 n = {avgCos * nxz, ny, avgSin * nxz};

        rlNormal3f(n.x, n.y, n.z);
        rlVertex3f(top.x, top.y, top.z);
        rlVertex3f(p1.x, p1.y, p1.z);
        rlVertex3f(p2.x, p2.y, p2.z);
    }
    rlEnd();
}

void DrawCorePlaneRect(Vector3 center, Vector2 size, Color color)
{
    float hw = size.x * 0.5f;
    float hl = size.y * 0.5f;

    rlCheckRenderBatchLimit(4);
    rlBegin(RL_QUADS);
    rlNormal3f(0.0f, 1.0f, 0.0f);
    rlColor4ub(color.r, color.g, color.b, color.a);

    // Đã thêm rlTexCoord2f cho cả 4 góc (0.0 -> 1.0)
    rlTexCoord2f(0.0f, 0.0f);
    rlVertex3f(center.x - hw, center.y, center.z - hl);

    rlTexCoord2f(0.0f, 1.0f);
    rlVertex3f(center.x - hw, center.y, center.z + hl);

    rlTexCoord2f(1.0f, 1.0f);
    rlVertex3f(center.x + hw, center.y, center.z + hl);

    rlTexCoord2f(1.0f, 0.0f);
    rlVertex3f(center.x + hw, center.y, center.z - hl);

    rlEnd();
}

void DrawCorePlanePolygon(Vector3 center, float radius, int sides, Color color)
{
    if (sides < 3)
        sides = 3;

    rlCheckRenderBatchLimit(sides * 3);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlNormal3f(0.0f, 1.0f, 0.0f);

    float prevCos = 1.0f, prevSin = 0.0f;
    for (int i = 0; i < sides; i++)
    {
        float phi2 = (i + 1 == sides) ? 0.0f : (float)(i + 1) * (2.0f * PI) / sides;
        float c1 = prevCos, s1 = prevSin;
        float c2 = cosf(phi2), s2 = sinf(phi2);
        prevCos = c2;
        prevSin = s2;

        Vector3 p1 = {center.x + c1 * radius, center.y, center.z + s1 * radius};
        Vector3 p2 = {center.x + c2 * radius, center.y, center.z + s2 * radius};

        rlVertex3f(center.x, center.y, center.z);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlVertex3f(p1.x, p1.y, p1.z);
    }
    rlEnd();
}

void DrawCoreCube(Vector3 position, float width, float height, float length, Color color)
{
    float x = position.x, y = position.y, z = position.z;
    float hw = width * 0.5f, hh = height * 0.5f, hl = length * 0.5f;

    rlCheckRenderBatchLimit(24);
    rlBegin(RL_QUADS);
    rlColor4ub(color.r, color.g, color.b, color.a);

    rlNormal3f(0.0f, 0.0f, 1.0f);
    rlVertex3f(x - hw, y - hh, z + hl);
    rlVertex3f(x + hw, y - hh, z + hl);
    rlVertex3f(x + hw, y + hh, z + hl);
    rlVertex3f(x - hw, y + hh, z + hl);

    rlNormal3f(0.0f, 0.0f, -1.0f);
    rlVertex3f(x - hw, y - hh, z - hl);
    rlVertex3f(x - hw, y + hh, z - hl);
    rlVertex3f(x + hw, y + hh, z - hl);
    rlVertex3f(x + hw, y - hh, z - hl);

    rlNormal3f(0.0f, 1.0f, 0.0f);
    rlVertex3f(x - hw, y + hh, z - hl);
    rlVertex3f(x - hw, y + hh, z + hl);
    rlVertex3f(x + hw, y + hh, z + hl);
    rlVertex3f(x + hw, y + hh, z - hl);

    rlNormal3f(0.0f, -1.0f, 0.0f);
    rlVertex3f(x - hw, y - hh, z - hl);
    rlVertex3f(x + hw, y - hh, z - hl);
    rlVertex3f(x + hw, y - hh, z + hl);
    rlVertex3f(x - hw, y - hh, z + hl);

    rlNormal3f(1.0f, 0.0f, 0.0f);
    rlVertex3f(x + hw, y - hh, z - hl);
    rlVertex3f(x + hw, y + hh, z - hl);
    rlVertex3f(x + hw, y + hh, z + hl);
    rlVertex3f(x + hw, y - hh, z + hl);

    rlNormal3f(-1.0f, 0.0f, 0.0f);
    rlVertex3f(x - hw, y - hh, z - hl);
    rlVertex3f(x - hw, y - hh, z + hl);
    rlVertex3f(x - hw, y + hh, z + hl);
    rlVertex3f(x - hw, y + hh, z - hl);
    rlEnd();
}

void DrawCoreTorus(Vector3 center, float innerRadius, float outerRadius, int sides, int rings, Color color)
{
    if (sides < 3)
        sides = 3;
    if (rings < 3)
        rings = 3;

    float r = (outerRadius - innerRadius) * 0.5f;
    float R = innerRadius + r;

    rlCheckRenderBatchLimit(rings * sides * 4);

    float cosTheta[rings + 1];
    float sinTheta[rings + 1];
    for (int i = 0; i <= rings; i++)
    {
        float theta = (float)i * (2.0f * PI) / rings;
        cosTheta[i] = cosf(theta);
        sinTheta[i] = sinf(theta);
    }

    float cosPhi[sides + 1];
    float sinPhi[sides + 1];
    for (int j = 0; j <= sides; j++)
    {
        float phi = (float)j * (2.0f * PI) / sides;
        cosPhi[j] = cosf(phi);
        sinPhi[j] = sinf(phi);
    }

    rlBegin(RL_QUADS);
    rlColor4ub(color.r, color.g, color.b, color.a);
    for (int i = 0; i < rings; i++)
    {
        float cosTheta1 = cosTheta[i], sinTheta1 = sinTheta[i];
        float cosTheta2 = cosTheta[i + 1], sinTheta2 = sinTheta[i + 1];

        for (int j = 0; j < sides; j++)
        {
            float cosPhi1 = cosPhi[j], sinPhi1 = sinPhi[j];
            float cosPhi2 = cosPhi[j + 1], sinPhi2 = sinPhi[j + 1];

            Vector3 p1 = {center.x + (R + r * cosPhi1) * cosTheta1, center.y + r * sinPhi1, center.z + (R + r * cosPhi1) * sinTheta1};
            Vector3 p2 = {center.x + (R + r * cosPhi2) * cosTheta1, center.y + r * sinPhi2, center.z + (R + r * cosPhi2) * sinTheta1};
            Vector3 p3 = {center.x + (R + r * cosPhi2) * cosTheta2, center.y + r * sinPhi2, center.z + (R + r * cosPhi2) * sinTheta2};
            Vector3 p4 = {center.x + (R + r * cosPhi1) * cosTheta2, center.y + r * sinPhi1, center.z + (R + r * cosPhi1) * sinTheta2};

            Vector3 n1 = {cosPhi1 * cosTheta1, sinPhi1, cosPhi1 * sinTheta1};
            Vector3 n2 = {cosPhi2 * cosTheta1, sinPhi2, cosPhi2 * sinTheta1};
            Vector3 n3 = {cosPhi2 * cosTheta2, sinPhi2, cosPhi2 * sinTheta2};
            Vector3 n4 = {cosPhi1 * cosTheta2, sinPhi1, cosPhi1 * sinTheta2};

            rlNormal3f(n1.x, n1.y, n1.z);
            rlVertex3f(p1.x, p1.y, p1.z);
            rlNormal3f(n4.x, n4.y, n4.z);
            rlVertex3f(p4.x, p4.y, p4.z);
            rlNormal3f(n3.x, n3.y, n3.z);
            rlVertex3f(p3.x, p3.y, p3.z);
            rlNormal3f(n2.x, n2.y, n2.z);
            rlVertex3f(p2.x, p2.y, p2.z);
        }
    }
    rlEnd();
}

void DrawCorePrism(Vector3 bottom, Vector3 top, float radius, int sides, Color color)
{
    DrawCoreCylinder(bottom, top, radius, radius, sides, color);
}