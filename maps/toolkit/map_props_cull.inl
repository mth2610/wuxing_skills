// Shared CPU-side view-frustum culling for map props and water.
// #include'd once from map_props.c (before map_props_rocks.inl) — not a
// standalone translation unit.
//
// IMPORTANT: the view matrix must come from rlGetMatrixTransform(), not
// rlGetMatrixModelview(). Under rlvk the camera is baked into the software
// vertex transform, so rlGetMatrixModelview() is IDENTITY inside BeginMode3D
// and a frustum built from it culls against a camera at the origin — every
// object fails the test (see ENGINE_LANDMINES.md §28). The projection is
// returned in the layout uploaded to the GPU, so the two must be composed
// per row; MatrixMultiply() would mix conventions and produce garbage.

typedef struct { float p[6][4]; } MapPropViewFrustum; // inside when a*x+b*y+c*z+d >= 0

static bool MapProp_BuildViewFrustum(MapPropViewFrustum *out)
{
    Matrix T = rlGetMatrixTransform();  // camera view, raylib row-vector order
    Matrix P = rlGetMatrixProjection(); // projection, GPU layout (rows m[r], m[4+r], ...)
    // No camera transform (called outside BeginMode3D, or a camera at the origin
    // looking down -Z): the frustum would be meaningless, so draw everything.
    if (T.m0 == 1.0f && T.m5 == 1.0f && T.m10 == 1.0f && T.m15 == 1.0f &&
        T.m1 == 0.0f && T.m2 == 0.0f && T.m3 == 0.0f && T.m4 == 0.0f &&
        T.m6 == 0.0f && T.m7 == 0.0f && T.m8 == 0.0f && T.m9 == 0.0f &&
        T.m11 == 0.0f && T.m12 == 0.0f && T.m13 == 0.0f && T.m14 == 0.0f)
        return false;
    const float *p = &P.m0;
    // Rows of T that produce view-space x, y and z from a world position.
    const float Tx[4] = { T.m0, T.m4, T.m8,  T.m12 };
    const float Ty[4] = { T.m1, T.m5, T.m9,  T.m13 };
    const float Tz[4] = { T.m2, T.m6, T.m10, T.m14 };

    // clip component r of a world position, as a plane (a,b,c,d): row r of P
    // applied to the view-space position.
    float clipPlane[4][4];
    for (int r = 0; r < 4; r++)
    {
        const float rx = p[r], ry = p[r + 4], rz = p[r + 8], rw = p[r + 12];
        for (int k = 0; k < 4; k++)
            clipPlane[r][k] = rx * Tx[k] + ry * Ty[k] + rz * Tz[k];
        clipPlane[r][3] += rw;
    }

    for (int i = 0; i < 6; i++)
    {
        int axis = i >> 1;                                  // 0=x 1=y 2=z
        float sign = ((i & 1) == 0) ? 1.0f : -1.0f;         // w + / - clip
        float a = clipPlane[3][0] + sign * clipPlane[axis][0];
        float b = clipPlane[3][1] + sign * clipPlane[axis][1];
        float c = clipPlane[3][2] + sign * clipPlane[axis][2];
        float d = clipPlane[3][3] + sign * clipPlane[axis][3];
        float len = sqrtf(a * a + b * b + c * c);
        if (!(len > 1e-6f))
            return false; // unset/NaN matrices: caller draws everything
        float inv = 1.0f / len;
        out->p[i][0] = a * inv;
        out->p[i][1] = b * inv;
        out->p[i][2] = c * inv;
        out->p[i][3] = d * inv;
    }
    return true;
}

// Conservative sphere test: cull only when the whole sphere is outside a plane.
static inline bool MapProp_FrustumCullsSphere(const MapPropViewFrustum *f, Vector3 c, float radius)
{
    for (int i = 0; i < 6; i++)
    {
        float dist = f->p[i][0] * c.x + f->p[i][1] * c.y + f->p[i][2] * c.z + f->p[i][3];
        if (dist < -radius)
            return true;
    }
    return false;
}

// Bounding sphere of a mesh in model space: the AABB centre and the farthest
// vertex from it, so the sphere encloses every vertex by construction.
static void MapProp_MeshBounds(const Mesh *mesh, Vector3 *outCenter, float *outRadius)
{
    if (!mesh || !mesh->vertices || mesh->vertexCount < 1)
    {
        *outCenter = (Vector3){0};
        *outRadius = 0.0f;
        return;
    }
    Vector3 mn = { mesh->vertices[0], mesh->vertices[1], mesh->vertices[2] };
    Vector3 mx = mn;
    for (int i = 1; i < mesh->vertexCount; i++)
    {
        float x = mesh->vertices[i * 3 + 0];
        float y = mesh->vertices[i * 3 + 1];
        float z = mesh->vertices[i * 3 + 2];
        if (x < mn.x) mn.x = x; else if (x > mx.x) mx.x = x;
        if (y < mn.y) mn.y = y; else if (y > mx.y) mx.y = y;
        if (z < mn.z) mn.z = z; else if (z > mx.z) mx.z = z;
    }
    Vector3 center = { (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f };
    float rSq = 0.0f;
    for (int i = 0; i < mesh->vertexCount; i++)
    {
        float dx = mesh->vertices[i * 3 + 0] - center.x;
        float dy = mesh->vertices[i * 3 + 1] - center.y;
        float dz = mesh->vertices[i * 3 + 2] - center.z;
        float d = dx * dx + dy * dy + dz * dz;
        if (d > rSq) rSq = d;
    }
    *outCenter = center;
    *outRadius = sqrtf(rSq);
}

// Bounding sphere of a whole Model (all meshes), in model space.
static void MapProp_ModelBounds(const Model *model, Vector3 *outCenter, float *outRadius)
{
    Vector3 mn = {0}, mx = {0};
    bool have = false;
    *outRadius = 0.0f;
    *outCenter = (Vector3){0};
    if (!model) return;
    for (int m = 0; m < model->meshCount; m++)
    {
        Vector3 c; float r;
        MapProp_MeshBounds(&model->meshes[m], &c, &r);
        if (r <= 0.0f) continue;
        Vector3 lo = { c.x - r, c.y - r, c.z - r };
        Vector3 hi = { c.x + r, c.y + r, c.z + r };
        if (!have) { mn = lo; mx = hi; have = true; }
        else
        {
            if (lo.x < mn.x) mn.x = lo.x; if (hi.x > mx.x) mx.x = hi.x;
            if (lo.y < mn.y) mn.y = lo.y; if (hi.y > mx.y) mx.y = hi.y;
            if (lo.z < mn.z) mn.z = lo.z; if (hi.z > mx.z) mx.z = hi.z;
        }
    }
    if (!have) return;
    *outCenter = (Vector3){ (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f };
    float dx = mx.x - mn.x, dy = mx.y - mn.y, dz = mx.z - mn.z;
    *outRadius = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz);
}
