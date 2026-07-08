// Simple deterministic hash to get pseudo-random float [0..1]
static float HashDeterministic(int seed, int subIndex)
{
    unsigned int n = (unsigned int)(seed * 73856093 ^ subIndex * 19349663);
    n = (n ^ 6179) * 31337;            // [cite: 2]
    return (float)(n % 1000) * 0.001f; // TỐI ƯU: Nhân 0.001f nhanh hơn chia 1000.0f
}

/* TỐI ƯU: sinh toàn bộ tam giác cục bộ (local space, quanh gốc (0,0,0), chưa
 * xoay/dịch) của 1 viên pha lê vào buffer phẳng — dùng chung bởi
 * ProceduralMesh_DrawCrystal (emit thẳng qua rlVertex) và
 * ProceduralMesh_BuildCrystalCluster (gộp vào 1 buffer cluster để chỉ tốn
 * đúng 1 draw call thay vì N). Trả về số tam giác đã ghi.
 * outPos/outNormal/outUV phải có sức chứa >= maxTris*3. */
static int ProceduralMesh__GenCrystalLocalTris(const CrystalDesc *desc, float progress,
                                               Vector3 *outPos, Vector3 *outNormal, Vector2 *outUV,
                                               int maxTris)
{
    if (desc == NULL || desc->sides < 3 || desc->segments < 2)
        return 0;

    int sides = desc->sides > 16 ? 16 : desc->sides;
    int segments = desc->segments > 16 ? 16 : desc->segments;

    float invSides = 1.0f / (float)sides;
    float invSegsMinus1 = 1.0f / (float)(segments - 1);

    int crystalSeed = (int)(desc->height * 1000.0f) ^ (int)(desc->radius * 1000.0f);

    float baseCos[16], baseSin[16], baseRadius[16];
    float angleStep = PI * invSides;

    for (int j = 0; j < sides; j++)
    {
        float angleOffset = (HashDeterministic(crystalSeed, j) - 0.5f) * (angleStep * 0.35f);
        float angle = (float)j * 2.0f * angleStep + angleOffset;

        float r_noise = 0.8f + 0.4f * HashDeterministic(crystalSeed + 1337, j);
        baseRadius[j] = desc->radius * r_noise;
        baseCos[j] = cosf(angle);
        baseSin[j] = sinf(angle);
    }

    Vector3 verts[256];

    for (int i = 0; i < segments; i++)
    {
        float t = (float)i * invSegsMinus1;
        float currentH = desc->height * t * progress;

        float taperFactor = 1.0f - (t * desc->taper);
        if (taperFactor < 0.0f)
            taperFactor = 0.0f;

        float twistAngle = t * desc->twist;
        float cosTwist = cosf(twistAngle);
        float sinTwist = sinf(twistAngle);

        float ridgeNoise = 1.0f + (HashDeterministic(crystalSeed, i * 19) - 0.5f) * desc->noise * 0.15f;
        float segmentScale = taperFactor * ridgeNoise;

        int rowOffset = i * sides;
        for (int j = 0; j < sides; j++)
        {
            float r = baseRadius[j] * segmentScale;
            float xRot = baseCos[j] * cosTwist - baseSin[j] * sinTwist;
            float zRot = baseSin[j] * cosTwist + baseCos[j] * sinTwist;

            verts[rowOffset + j] = (Vector3){xRot * r, currentH, zRot * r};
        }
    }

    float apexOffsetX = (HashDeterministic(crystalSeed, 991) - 0.5f) * desc->radius * 0.4f;
    float apexOffsetZ = (HashDeterministic(crystalSeed, 992) - 0.5f) * desc->radius * 0.4f;
    Vector3 apex = {
        apexOffsetX * (1.0f - desc->taper),
        desc->height * progress,
        apexOffsetZ * (1.0f - desc->taper)};

    int triCount = 0;

    // Thân
    for (int i = 0; i < segments - 1 && triCount + 2 <= maxTris; i++)
    {
        float t = (float)i * invSegsMinus1;
        float t_next = (float)(i + 1) * invSegsMinus1;
        int rowCur = i * sides;
        int rowNext = (i + 1) * sides;

        for (int j = 0; j < sides && triCount + 2 <= maxTris; j++)
        {
            int next_j = (j + 1) % sides;
            Vector3 v1 = verts[rowCur + j];
            Vector3 v2 = verts[rowCur + next_j];
            Vector3 v3 = verts[rowNext + next_j];
            Vector3 v4 = verts[rowNext + j];

            float u1 = (float)j * invSides;
            float u2 = (float)(j + 1) * invSides;

            Vector3 edge_v2_v1 = {v2.x - v1.x, v2.y - v1.y, v2.z - v1.z};
            Vector3 edge_v3_v1 = {v3.x - v1.x, v3.y - v1.y, v3.z - v1.z};
            Vector3 edge_v4_v1 = {v4.x - v1.x, v4.y - v1.y, v4.z - v1.z};

            Vector3 normal1 = Vector3Normalize(Vector3CrossProduct(edge_v2_v1, edge_v3_v1));

            int b = triCount * 3;
            outPos[b] = v1; outNormal[b] = normal1; outUV[b] = (Vector2){u1, t};
            outPos[b + 1] = v2; outNormal[b + 1] = normal1; outUV[b + 1] = (Vector2){u2, t};
            outPos[b + 2] = v3; outNormal[b + 2] = normal1; outUV[b + 2] = (Vector2){u2, t_next};
            triCount++;

            Vector3 normal2 = Vector3Normalize(Vector3CrossProduct(edge_v3_v1, edge_v4_v1));

            b = triCount * 3;
            outPos[b] = v1; outNormal[b] = normal2; outUV[b] = (Vector2){u1, t};
            outPos[b + 1] = v3; outNormal[b + 1] = normal2; outUV[b + 1] = (Vector2){u2, t_next};
            outPos[b + 2] = v4; outNormal[b + 2] = normal2; outUV[b + 2] = (Vector2){u1, t_next};
            triCount++;
        }
    }

    // Chóp đỉnh
    int topRow = (segments - 1) * sides;
    for (int j = 0; j < sides && triCount + 1 <= maxTris; j++)
    {
        int next_j = (j + 1) % sides;
        Vector3 v1 = verts[topRow + j];
        Vector3 v2 = verts[topRow + next_j];

        Vector3 edge1 = {v2.x - v1.x, v2.y - v1.y, v2.z - v1.z};
        Vector3 edge2 = {apex.x - v1.x, apex.y - v1.y, apex.z - v1.z};
        Vector3 normal = Vector3Normalize(Vector3CrossProduct(edge1, edge2));

        int b = triCount * 3;
        outPos[b] = v1; outNormal[b] = normal; outUV[b] = (Vector2){(float)j * invSides, 1.0f};
        outPos[b + 1] = v2; outNormal[b + 1] = normal; outUV[b + 1] = (Vector2){(float)(j + 1) * invSides, 1.0f};
        outPos[b + 2] = apex; outNormal[b + 2] = normal; outUV[b + 2] = (Vector2){0.5f, 1.05f};
        triCount++;
    }

    // Đáy
    Vector3 bottomNormal = {0.0f, -1.0f, 0.0f};
    for (int j = 1; j < sides - 1 && triCount + 1 <= maxTris; j++)
    {
        Vector3 v1 = verts[0];
        Vector3 v2 = verts[j + 1];
        Vector3 v3 = verts[j];

        int b = triCount * 3;
        outPos[b] = v1; outNormal[b] = bottomNormal; outUV[b] = (Vector2){0.5f, 0.5f};
        outPos[b + 1] = v2; outNormal[b + 1] = bottomNormal; outUV[b + 1] = (Vector2){0.5f, 0.5f};
        outPos[b + 2] = v3; outNormal[b + 2] = bottomNormal; outUV[b + 2] = (Vector2){0.5f, 0.5f};
        triCount++;
    }

    return triCount;
}

#define CRYSTAL_MAX_TRIS_SINGLE 512 // sides<=16,segments<=16 worst case: 480 thân + 16 chóp + 14 đáy = 510

void ProceduralMesh_DrawCrystal(Vector3 pos, const CrystalDesc *desc, float progress, Color color)
{
    // TỐI ƯU: buffer scratch static (không phải stack) — reuse cùng bộ nhớ
    // mỗi lần gọi thay vì cấp phát lại; engine single-threaded nên không cần
    // reentrancy.
    static Vector3 s_pos[CRYSTAL_MAX_TRIS_SINGLE * 3];
    static Vector3 s_normal[CRYSTAL_MAX_TRIS_SINGLE * 3];
    static Vector2 s_uv[CRYSTAL_MAX_TRIS_SINGLE * 3];

    int triCount = ProceduralMesh__GenCrystalLocalTris(desc, progress, s_pos, s_normal, s_uv, CRYSTAL_MAX_TRIS_SINGLE);
    if (triCount <= 0)
        return;

    int vertCount = triCount * 3;

    rlDrawRenderBatchActive();
    rlCheckRenderBatchLimit(vertCount);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(color.r, color.g, color.b, color.a);
    for (int v = 0; v < vertCount; v++)
    {
        rlNormal3f(s_normal[v].x, s_normal[v].y, s_normal[v].z);
        rlTexCoord2f(s_uv[v].x, s_uv[v].y);
        rlVertex3f(pos.x + s_pos[v].x, pos.y + s_pos[v].y, pos.z + s_pos[v].z);
    }
    rlEnd();
}

// LOD của viên con trong cluster — không cần chi tiết cao như crystal chính,
// giữ bộ nhớ tĩnh của CrystalClusterMeshData ở mức hợp lý.
#define CRYSTAL_CLUSTER_CHILD_MAX_SIDES 8
#define CRYSTAL_CLUSTER_CHILD_MAX_SEGMENTS 8
#define CRYSTAL_CLUSTER_CHILD_MAX_TRIS 128 // (8-1)*8*2 + 8 + (8-2) = 126, làm tròn lên

// Sinh 1 viên pha lê con (local space), xoay tiltDeg bằng CPU vector math
// (thay cho rlRotatef Y->Z->Y trên GL matrix stack) rồi dịch vào world space
// và append vào buffer cluster `out`.
static void ProceduralMesh__AppendCrystalChild(CrystalClusterMeshData *out, Vector3 crystalPos,
                                               float angle, float tiltDeg,
                                               const CrystalDesc *childDesc, float progress)
{
    static Vector3 s_localPos[CRYSTAL_CLUSTER_CHILD_MAX_TRIS * 3];
    static Vector3 s_localNormal[CRYSTAL_CLUSTER_CHILD_MAX_TRIS * 3];
    static Vector2 s_localUV[CRYSTAL_CLUSTER_CHILD_MAX_TRIS * 3];

    int childTris = ProceduralMesh__GenCrystalLocalTris(childDesc, progress, s_localPos, s_localNormal, s_localUV,
                                                         CRYSTAL_CLUSTER_CHILD_MAX_TRIS);

    int remainingTris = CRYSTAL_CLUSTER_MAX_TRIS - out->triCount;
    if (childTris > remainingTris)
        childTris = remainingTris;
    if (childTris <= 0)
        return;

    const Vector3 axisY = {0.0f, 1.0f, 0.0f};
    const Vector3 axisZ = {0.0f, 0.0f, 1.0f};
    float tiltRad = tiltDeg * DEG2RAD;

    int srcVertCount = childTris * 3;
    int base = out->triCount * 3;
    for (int v = 0; v < srcVertCount; v++)
    {
        // Khớp đúng thứ tự 3 lệnh rlRotatef cũ (Y(angle) * Z(tilt) * Y(-angle)
        // áp lên matrix stack): vertex local bị xoay -angle quanh Y trước,
        // rồi tilt quanh Z, rồi +angle quanh Y — normal chỉ xoay, không dịch.
        Vector3 p = Vector3RotateByAxisAngle(s_localPos[v], axisY, -angle);
        p = Vector3RotateByAxisAngle(p, axisZ, tiltRad);
        p = Vector3RotateByAxisAngle(p, axisY, angle);

        Vector3 n = Vector3RotateByAxisAngle(s_localNormal[v], axisY, -angle);
        n = Vector3RotateByAxisAngle(n, axisZ, tiltRad);
        n = Vector3RotateByAxisAngle(n, axisY, angle);

        out->pos[base + v] = Vector3Add(p, crystalPos);
        out->normal[base + v] = n;
        out->uv[base + v] = s_localUV[v];
    }
    out->triCount += childTris;
}

void ProceduralMesh_BuildCrystalCluster(CrystalClusterMeshData *out, Vector3 center, const CrystalDesc *desc,
                                        int count, int seed, float progress)
{
    if (out == NULL)
        return;
    out->triCount = 0;
    if (desc == NULL || count <= 0)
        return;
    if (count > CRYSTAL_CLUSTER_MAX_CRYSTALS)
        count = CRYSTAL_CLUSTER_MAX_CRYSTALS;

    for (int i = 0; i < count; i++)
    {
        float r_val = HashDeterministic(seed, i * 3);
        float a_val = HashDeterministic(seed, i * 3 + 1);
        float h_val = HashDeterministic(seed, i * 3 + 2);

        float dist = r_val * desc->radius * 0.9f;
        float angle = a_val * 2.0f * PI;

        // Thêm độ lún (âm) để cụm pha lê mọc ra tự nhiên, không bị bằng phẳng giả tạo ở dưới đáy
        float yOffset = -desc->height * 0.15f * HashDeterministic(seed, i * 3 + 4);

        Vector3 offset = {
            cosf(angle) * dist,
            yOffset,
            sinf(angle) * dist};

        Vector3 crystalPos = Vector3Add(center, offset);

        CrystalDesc childDesc = *desc;
        childDesc.height = desc->height * (0.4f + h_val * 0.8f); // Độ chênh lệch chiều cao đa dạng hơn
        childDesc.radius = desc->radius * (0.4f + r_val * 0.6f);
        childDesc.twist = desc->twist + (a_val - 0.5f) * 1.5f;
        if (childDesc.sides > CRYSTAL_CLUSTER_CHILD_MAX_SIDES)
            childDesc.sides = CRYSTAL_CLUSTER_CHILD_MAX_SIDES;
        if (childDesc.segments > CRYSTAL_CLUSTER_CHILD_MAX_SEGMENTS)
            childDesc.segments = CRYSTAL_CLUSTER_CHILD_MAX_SEGMENTS;

        // Tăng góc nghiêng lên 45 độ để cluster trông kịch tính và tỏa ra đẹp hơn
        float tiltDeg = r_val * 45.0f;

        ProceduralMesh__AppendCrystalChild(out, crystalPos, angle, tiltDeg, &childDesc, progress);
    }
}

void ProceduralMesh_DrawCrystalClusterMesh(const CrystalClusterMeshData *data, Color color)
{
    if (data == NULL || data->triCount <= 0)
        return;

    int vertCount = data->triCount * 3;

    rlDrawRenderBatchActive();
    rlCheckRenderBatchLimit(vertCount);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(color.r, color.g, color.b, color.a);
    for (int v = 0; v < vertCount; v++)
    {
        rlNormal3f(data->normal[v].x, data->normal[v].y, data->normal[v].z);
        rlTexCoord2f(data->uv[v].x, data->uv[v].y);
        rlVertex3f(data->pos[v].x, data->pos[v].y, data->pos[v].z);
    }
    rlEnd();
}

// TỐI ƯU: trước đây gọi rlPushMatrix + ProceduralMesh_DrawCrystal (rlBegin/
// rlEnd riêng) cho từng viên trong vòng lặp — N draw call cho N viên. Giờ
// build toàn bộ cụm vào 1 buffer rồi vẽ bằng đúng 1 rlBegin/rlEnd. Chữ ký
// giữ nguyên nên các call site hiện có (vc_metal.inl, vc_water.inl) không
// cần sửa gì.
void ProceduralMesh_DrawCrystalCluster(Vector3 center, const CrystalDesc *desc, int count, int seed, float progress, Color color)
{
    static CrystalClusterMeshData s_scratchCluster;
    ProceduralMesh_BuildCrystalCluster(&s_scratchCluster, center, desc, count, seed, progress);
    ProceduralMesh_DrawCrystalClusterMesh(&s_scratchCluster, color);
}

// TỐI ƯU: build đúng 1 viên pha lê "mẫu" (local space, tâm gốc, thẳng đứng —
// KHÔNG jitter vị trí/tilt/scale như 1 viên con trong cluster) để dùng lại
// nhiều lần qua DrawMesh với transform khác nhau mỗi viên. Dùng cho trường
// hợp cast dồn dập (VD nhiều nhân vật/nhiều lần bắn liên tiếp): build 1 lần
// duy nhất lúc khởi tạo (giống lifecycle shader/texture — không build lại,
// không unload), tránh gọi UploadMesh (tạo VBO mới, đồng bộ hoá GPU-driver
// tốn kém) mỗi lần cast như ProceduralMesh_BuildCrystalClusterMesh bên dưới.
// Biến thể hình dạng giữa các viên khi vẽ đến từ transform (dịch/xoay/scale)
// tính trên CPU, không phải hình học khác nhau — xem VFX_DrawIceCrystalBurst
// trong core/composition/vc_water.inl để có ví dụ đầy đủ.
Mesh ProceduralMesh_BuildCrystalTemplateMesh(const CrystalDesc *desc)
{
    Mesh mesh = {0};
    if (desc == NULL)
        return mesh;

    static Vector3 s_pos[CRYSTAL_MAX_TRIS_SINGLE * 3];
    static Vector3 s_normal[CRYSTAL_MAX_TRIS_SINGLE * 3];
    static Vector2 s_uv[CRYSTAL_MAX_TRIS_SINGLE * 3];

    int triCount = ProceduralMesh__GenCrystalLocalTris(desc, 1.0f, s_pos, s_normal, s_uv, CRYSTAL_MAX_TRIS_SINGLE);
    if (triCount <= 0)
        return mesh;

    int vertCount = triCount * 3;
    mesh.vertices = (float *)MemAlloc(vertCount * 3 * sizeof(float));
    mesh.normals = (float *)MemAlloc(vertCount * 3 * sizeof(float));
    mesh.texcoords = (float *)MemAlloc(vertCount * 2 * sizeof(float));

    for (int v = 0; v < vertCount; v++)
    {
        mesh.vertices[v * 3 + 0] = s_pos[v].x;
        mesh.vertices[v * 3 + 1] = s_pos[v].y;
        mesh.vertices[v * 3 + 2] = s_pos[v].z;
        mesh.normals[v * 3 + 0] = s_normal[v].x;
        mesh.normals[v * 3 + 1] = s_normal[v].y;
        mesh.normals[v * 3 + 2] = s_normal[v].z;
        mesh.texcoords[v * 2 + 0] = s_uv[v].x;
        mesh.texcoords[v * 2 + 1] = s_uv[v].y;
    }

    mesh.vertexCount = vertCount;
    mesh.triangleCount = triCount;
    UploadMesh(&mesh, false);
    return mesh;
}

// Số tam giác 1 viên pha lê sinh ra CHỈ phụ thuộc sides/segments (không phụ
// thuộc seed/height/radius) — dùng để tính chính xác kích thước MemAlloc cho
// Mesh GPU-resident bên dưới, khỏi phải build thử 2 lần.
static int ProceduralMesh__CrystalTriCountForDesc(const CrystalDesc *desc)
{
    if (desc == NULL || desc->sides < 3 || desc->segments < 2)
        return 0;
    int sides = desc->sides > 16 ? 16 : desc->sides;
    int segments = desc->segments > 16 ? 16 : desc->segments;
    return (segments - 1) * sides * 2 /* thân */ + sides /* chóp */ + (sides - 2) /* đáy */;
}

Mesh ProceduralMesh_BuildCrystalClusterMesh(const CrystalDesc *desc, int count, int seed)
{
    Mesh mesh = {0};
    if (desc == NULL || count <= 0)
        return mesh;
    if (count > 64)
        count = 64; // chặn trần hợp lý — tránh input bậy gây MemAlloc khổng lồ

    int trisPerChild = ProceduralMesh__CrystalTriCountForDesc(desc);
    if (trisPerChild <= 0)
        return mesh;

    int totalTris = trisPerChild * count;
    int vertCount = totalTris * 3;

    mesh.vertices = (float *)MemAlloc(vertCount * 3 * sizeof(float));
    mesh.normals = (float *)MemAlloc(vertCount * 3 * sizeof(float));
    mesh.texcoords = (float *)MemAlloc(vertCount * 2 * sizeof(float));

    // Scratch cho từng viên con (local space, progress=1.0 luôn — hiệu ứng
    // "mọc lên" do GPU vertex shader lo qua u_growProgress, không bake ở đây
    // nữa nên không cần rebuild lại khi progress animate).
    static Vector3 s_childPos[CRYSTAL_MAX_TRIS_SINGLE * 3];
    static Vector3 s_childNormal[CRYSTAL_MAX_TRIS_SINGLE * 3];
    static Vector2 s_childUV[CRYSTAL_MAX_TRIS_SINGLE * 3];

    const Vector3 axisY = {0.0f, 1.0f, 0.0f};
    const Vector3 axisZ = {0.0f, 0.0f, 1.0f};

    int writtenVerts = 0;
    for (int i = 0; i < count && writtenVerts < vertCount; i++)
    {
        // Layout giống hệt ProceduralMesh_BuildCrystalCluster (cùng seed thì
        // ra cùng bố cục) — chỉ khác: không LOD-cap sides/segments (mesh này
        // bake 1 lần rồi ở lại GPU, không có áp lực buffer tĩnh mỗi-frame).
        float r_val = HashDeterministic(seed, i * 3);
        float a_val = HashDeterministic(seed, i * 3 + 1);
        float h_val = HashDeterministic(seed, i * 3 + 2);

        float dist = r_val * desc->radius * 0.9f;
        float angle = a_val * 2.0f * PI;
        float yOffset = -desc->height * 0.15f * HashDeterministic(seed, i * 3 + 4);
        Vector3 crystalPos = {cosf(angle) * dist, yOffset, sinf(angle) * dist};

        CrystalDesc childDesc = *desc;
        childDesc.height = desc->height * (0.4f + h_val * 0.8f);
        childDesc.radius = desc->radius * (0.4f + r_val * 0.6f);
        childDesc.twist = desc->twist + (a_val - 0.5f) * 1.5f;

        float tiltDeg = r_val * 45.0f;
        float tiltRad = tiltDeg * DEG2RAD;

        int childTris = ProceduralMesh__GenCrystalLocalTris(&childDesc, 1.0f, s_childPos, s_childNormal, s_childUV, CRYSTAL_MAX_TRIS_SINGLE);
        int srcVertCount = childTris * 3;

        for (int v = 0; v < srcVertCount && writtenVerts < vertCount; v++)
        {
            // Cùng thứ tự xoay Y(-angle)->Z(tilt)->Y(angle) như bản CPU
            // immediate-mode, để hình dạng cluster giống hệt nhau.
            Vector3 p = Vector3RotateByAxisAngle(s_childPos[v], axisY, -angle);
            p = Vector3RotateByAxisAngle(p, axisZ, tiltRad);
            p = Vector3RotateByAxisAngle(p, axisY, angle);
            p = Vector3Add(p, crystalPos);

            Vector3 n = Vector3RotateByAxisAngle(s_childNormal[v], axisY, -angle);
            n = Vector3RotateByAxisAngle(n, axisZ, tiltRad);
            n = Vector3RotateByAxisAngle(n, axisY, angle);

            mesh.vertices[writtenVerts * 3 + 0] = p.x;
            mesh.vertices[writtenVerts * 3 + 1] = p.y;
            mesh.vertices[writtenVerts * 3 + 2] = p.z;
            mesh.normals[writtenVerts * 3 + 0] = n.x;
            mesh.normals[writtenVerts * 3 + 1] = n.y;
            mesh.normals[writtenVerts * 3 + 2] = n.z;
            mesh.texcoords[writtenVerts * 2 + 0] = s_childUV[v].x;
            mesh.texcoords[writtenVerts * 2 + 1] = s_childUV[v].y;
            writtenVerts++;
        }
    }

    mesh.vertexCount = writtenVerts;
    mesh.triangleCount = writtenVerts / 3;

    UploadMesh(&mesh, false); // static = không update lại mỗi frame; "mọc lên" do shader lo
    return mesh;
}

void ProceduralMesh_DrawBakedCrystalCluster(Mesh mesh, Material material, Matrix transform)
{
    if (mesh.vertexCount <= 0)
        return;
    DrawMesh(mesh, material, transform);
}

Material ProceduralMesh_GetPassthroughMaterial(Shader shader)
{
    static Material s_passthroughMaterial;
    static bool s_passthroughReady = false;
    if (!s_passthroughReady)
    {
        s_passthroughMaterial = LoadMaterialDefault();
        s_passthroughReady = true;
    }
    s_passthroughMaterial.shader = shader;
    return s_passthroughMaterial;
}