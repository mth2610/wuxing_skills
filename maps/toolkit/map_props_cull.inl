// Shared CPU-side view-frustum culling for map props and water.
// #include'd once from map_props.c (before map_props_rocks.inl) — not a
// standalone translation unit.
//
// IMPORTANT: the view matrix must come from rlGetMatrixTransform(), not
// rlGetMatrixModelview(). Under rlvk the camera is baked into the software
// vertex transform, so rlGetMatrixModelview() is IDENTITY inside BeginMode3D
// and a frustum built from it culls against a camera at the origin — every
// object fails the test (see ENGINE_LANDMINES.md §28).

typedef struct
{
    float p[6][4];
} MapPropViewFrustum; // inside when a*x+b*y+c*z+d >= 0

static Matrix s_cachedFrustumT = {0};
static Matrix s_cachedFrustumP = {0};
static MapPropViewFrustum s_cachedFrustum = {0};
static bool s_cachedFrustumValid = false;
static bool s_cachedFrustumResult = false;

static bool MapProp_BuildViewFrustum(MapPropViewFrustum *out)
{
    if (!out)
        return false;

    Matrix T = rlGetMatrixTransform();  // camera view, raylib row-vector order
    Matrix P = rlGetMatrixProjection(); // projection, GPU layout (rows m[r], m[4+r], ...)

    if (s_cachedFrustumValid &&
        memcmp(&T, &s_cachedFrustumT, sizeof(Matrix)) == 0 &&
        memcmp(&P, &s_cachedFrustumP, sizeof(Matrix)) == 0)
    {
        if (s_cachedFrustumResult)
            *out = s_cachedFrustum;
        return s_cachedFrustumResult;
    }

    s_cachedFrustumT = T;
    s_cachedFrustumP = P;
    s_cachedFrustumValid = true;

    // No camera transform (called outside BeginMode3D, or a camera at the origin
    // looking down -Z): the frustum would be meaningless, so draw everything.
    if (T.m0 == 1.0f && T.m5 == 1.0f && T.m10 == 1.0f && T.m15 == 1.0f &&
        T.m12 == 0.0f && T.m13 == 0.0f && T.m14 == 0.0f &&
        T.m1 == 0.0f && T.m2 == 0.0f && T.m3 == 0.0f && T.m4 == 0.0f &&
        T.m6 == 0.0f && T.m7 == 0.0f && T.m8 == 0.0f && T.m9 == 0.0f &&
        T.m11 == 0.0f)
    {
        s_cachedFrustumResult = false;
        return false;
    }

    const float *p = &P.m0;
    const float Tx[4] = {T.m0, T.m4, T.m8, T.m12};
    const float Ty[4] = {T.m1, T.m5, T.m9, T.m13};
    const float Tz[4] = {T.m2, T.m6, T.m10, T.m14};

    float clipPlane[4][4];
    for (int r = 0; r < 4; r++)
    {
        const float rx = p[r], ry = p[r + 4], rz = p[r + 8], rw = p[r + 12];
        clipPlane[r][0] = rx * Tx[0] + ry * Ty[0] + rz * Tz[0];
        clipPlane[r][1] = rx * Tx[1] + ry * Ty[1] + rz * Tz[1];
        clipPlane[r][2] = rx * Tx[2] + ry * Ty[2] + rz * Tz[2];
        clipPlane[r][3] = rx * Tx[3] + ry * Ty[3] + rz * Tz[3] + rw;
    }

    for (int i = 0; i < 6; i++)
    {
        int axis = i >> 1;                          // 0=x 1=y 2=z
        float sign = ((i & 1) == 0) ? 1.0f : -1.0f; // w + / - clip
        float a = clipPlane[3][0] + sign * clipPlane[axis][0];
        float b = clipPlane[3][1] + sign * clipPlane[axis][1];
        float c = clipPlane[3][2] + sign * clipPlane[axis][2];
        float d = clipPlane[3][3] + sign * clipPlane[axis][3];
        float lenSq = a * a + b * b + c * c;
        if (!(lenSq > 1e-12f))
        {
            s_cachedFrustumResult = false;
            return false; // unset/NaN matrices: caller draws everything
        }
        float inv = 1.0f / sqrtf(lenSq);
        out->p[i][0] = a * inv;
        out->p[i][1] = b * inv;
        out->p[i][2] = c * inv;
        out->p[i][3] = d * inv;
    }

    s_cachedFrustum = *out;
    s_cachedFrustumResult = true;
    return true;
}

// Conservative sphere test: cull only when the whole sphere is outside a plane.
// Checks Left, Right, Near first as they reject the vast majority of off-screen isometric props.
static inline bool MapProp_FrustumCullsSphere(const MapPropViewFrustum *f, Vector3 c, float radius)
{
    const float negR = -radius;
    if (f->p[0][0] * c.x + f->p[0][1] * c.y + f->p[0][2] * c.z + f->p[0][3] < negR)
        return true;
    if (f->p[1][0] * c.x + f->p[1][1] * c.y + f->p[1][2] * c.z + f->p[1][3] < negR)
        return true;
    if (f->p[4][0] * c.x + f->p[4][1] * c.y + f->p[4][2] * c.z + f->p[4][3] < negR)
        return true;
    if (f->p[2][0] * c.x + f->p[2][1] * c.y + f->p[2][2] * c.z + f->p[2][3] < negR)
        return true;
    if (f->p[3][0] * c.x + f->p[3][1] * c.y + f->p[3][2] * c.z + f->p[3][3] < negR)
        return true;
    if (f->p[5][0] * c.x + f->p[5][1] * c.y + f->p[5][2] * c.z + f->p[5][3] < negR)
        return true;
    return false;
}

// Bounding sphere of a mesh in model space: the AABB centre and the farthest
// vertex from it, so the sphere encloses every vertex by construction.
static void MapProp_MeshBounds(const Mesh *mesh, Vector3 *outCenter, float *outRadius)
{
    if (!outCenter || !outRadius)
        return;
    if (!mesh || !mesh->vertices || mesh->vertexCount < 1)
    {
        *outCenter = (Vector3){0};
        *outRadius = 0.0f;
        return;
    }
    const float *restrict v = mesh->vertices;
    const int count = mesh->vertexCount;
    float minX = v[0], maxX = v[0];
    float minY = v[1], maxY = v[1];
    float minZ = v[2], maxZ = v[2];
    v += 3;
    for (int i = 1; i < count; i++, v += 3)
    {
        float x = v[0];
        float y = v[1];
        float z = v[2];
        if (x < minX)
            minX = x;
        else if (x > maxX)
            maxX = x;
        if (y < minY)
            minY = y;
        else if (y > maxY)
            maxY = y;
        if (z < minZ)
            minZ = z;
        else if (z > maxZ)
            maxZ = z;
    }
    float cx = (minX + maxX) * 0.5f;
    float cy = (minY + maxY) * 0.5f;
    float cz = (minZ + maxZ) * 0.5f;
    float rSq = 0.0f;
    v = mesh->vertices;
    for (int i = 0; i < count; i++, v += 3)
    {
        float dx = v[0] - cx;
        float dy = v[1] - cy;
        float dz = v[2] - cz;
        float d = dx * dx + dy * dy + dz * dz;
        if (d > rSq)
            rSq = d;
    }
    *outCenter = (Vector3){cx, cy, cz};
    *outRadius = sqrtf(rSq);
}

// Tight conservative bounding sphere of a whole Model (all meshes) in model space.
// Computes the exact vertex AABB center and farthest vertex distance across all meshes,
// avoiding the ~1.73x AABB-diagonal over-inflation that causes off-screen mobile draws.
static void MapProp_ModelBounds(const Model *model, Vector3 *outCenter, float *outRadius)
{
    if (!outCenter || !outRadius)
        return;
    *outRadius = 0.0f;
    *outCenter = (Vector3){0};
    if (!model || model->meshCount < 1)
        return;

    float minX = 1e30f, maxX = -1e30f;
    float minY = 1e30f, maxY = -1e30f;
    float minZ = 1e30f, maxZ = -1e30f;
    bool have = false;

    for (int m = 0; m < model->meshCount; m++)
    {
        const Mesh *mesh = &model->meshes[m];
        if (!mesh->vertices || mesh->vertexCount < 1)
            continue;
        have = true;
        const float *restrict v = mesh->vertices;
        const int count = mesh->vertexCount;
        for (int i = 0; i < count; i++, v += 3)
        {
            float x = v[0], y = v[1], z = v[2];
            if (x < minX)
                minX = x;
            if (x > maxX)
                maxX = x;
            if (y < minY)
                minY = y;
            if (y > maxY)
                maxY = y;
            if (z < minZ)
                minZ = z;
            if (z > maxZ)
                maxZ = z;
        }
    }
    if (!have)
        return;

    float cx = (minX + maxX) * 0.5f;
    float cy = (minY + maxY) * 0.5f;
    float cz = (minZ + maxZ) * 0.5f;
    float rSq = 0.0f;

    for (int m = 0; m < model->meshCount; m++)
    {
        const Mesh *mesh = &model->meshes[m];
        if (!mesh->vertices || mesh->vertexCount < 1)
            continue;
        const float *restrict v = mesh->vertices;
        const int count = mesh->vertexCount;
        for (int i = 0; i < count; i++, v += 3)
        {
            float dx = v[0] - cx;
            float dy = v[1] - cy;
            float dz = v[2] - cz;
            float d = dx * dx + dy * dy + dz * dz;
            if (d > rSq)
                rSq = d;
        }
    }

    *outCenter = (Vector3){cx, cy, cz};
    *outRadius = sqrtf(rSq);
}