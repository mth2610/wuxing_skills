/* ===========================================================================
 * GPU VERTEX DISPLACEMENT BASE MESHES
 * =========================================================================*/

Mesh ProceduralMesh_CreateBaseGrid(float width, float length, int segmentsX, int segmentsZ) {
    if (segmentsX < 1) segmentsX = 1; if (segmentsZ < 1) segmentsZ = 1;

    int vertCountX = segmentsX + 1, vertCountZ = segmentsZ + 1;
    int vertCount = vertCountX * vertCountZ, triCount = segmentsX * segmentsZ * 2;

    Mesh mesh = { 0 };
    mesh.vertexCount = vertCount; mesh.triangleCount = triCount;
    mesh.vertices = (float *)MemAlloc(vertCount * 3 * sizeof(float));
    mesh.texcoords = (float *)MemAlloc(vertCount * 2 * sizeof(float));
    mesh.normals = (float *)MemAlloc(vertCount * 3 * sizeof(float));
    mesh.indices = (unsigned short *)MemAlloc(triCount * 3 * sizeof(unsigned short));

    float halfW = width * 0.5f, halfL = length * 0.5f;

    for (int j = 0; j < vertCountZ; j++) {
        float v = (float)j / segmentsZ, z = -halfL + v * length;
        for (int i = 0; i < vertCountX; i++) {
            float u = (float)i / segmentsX, x = -halfW + u * width;
            int idx = j * vertCountX + i;
            mesh.vertices[idx * 3 + 0] = x; mesh.vertices[idx * 3 + 1] = 0.0f; mesh.vertices[idx * 3 + 2] = z;
            mesh.texcoords[idx * 2 + 0] = u; mesh.texcoords[idx * 2 + 1] = v;
            mesh.normals[idx * 3 + 0] = 0.0f; mesh.normals[idx * 3 + 1] = 1.0f; mesh.normals[idx * 3 + 2] = 0.0f;
        }
    }

    int t = 0;
    for (int j = 0; j < segmentsZ; j++) {
        for (int i = 0; i < segmentsX; i++) {
            unsigned short a = (unsigned short)(j * vertCountX + i);
            mesh.indices[t++] = a; mesh.indices[t++] = a + vertCountX; mesh.indices[t++] = a + 1;
            mesh.indices[t++] = a + 1; mesh.indices[t++] = a + vertCountX; mesh.indices[t++] = a + vertCountX + 1;
        }
    }
    UploadMesh(&mesh, false);
    return mesh;
}

Mesh ProceduralMesh_CreateBaseCylinder(int radialSegs, int heightSegs) {
    if (radialSegs < 3) radialSegs = 3; if (heightSegs < 1) heightSegs = 1;

    int vertCountRadial = radialSegs + 1, vertCountHeight = heightSegs + 1;
    int vertCount = vertCountRadial * vertCountHeight, triCount = radialSegs * heightSegs * 2;

    Mesh mesh = { 0 };
    mesh.vertexCount = vertCount; mesh.triangleCount = triCount;
    mesh.vertices = (float *)MemAlloc(vertCount * 3 * sizeof(float));
    mesh.texcoords = (float *)MemAlloc(vertCount * 2 * sizeof(float));
    mesh.normals = (float *)MemAlloc(vertCount * 3 * sizeof(float));
    mesh.indices = (unsigned short *)MemAlloc(triCount * 3 * sizeof(unsigned short));

    for (int j = 0; j < vertCountHeight; j++) {
        float v = (float)j / heightSegs;
        for (int i = 0; i < vertCountRadial; i++) {
            float u = (float)i / radialSegs, phi = u * 2.0f * PI;
            float cx = cosf(phi), cz = sinf(phi);
            int idx = j * vertCountRadial + i;
            mesh.vertices[idx * 3 + 0] = cx; mesh.vertices[idx * 3 + 1] = v; mesh.vertices[idx * 3 + 2] = cz;
            mesh.texcoords[idx * 2 + 0] = u; mesh.texcoords[idx * 2 + 1] = v;
            mesh.normals[idx * 3 + 0] = cx; mesh.normals[idx * 3 + 1] = 0.0f; mesh.normals[idx * 3 + 2] = cz;
        }
    }

    int t = 0;
    for (int j = 0; j < heightSegs; j++) {
        for (int i = 0; i < radialSegs; i++) {
            unsigned short a = (unsigned short)(j * vertCountRadial + i);
            mesh.indices[t++] = a; mesh.indices[t++] = a + vertCountRadial; mesh.indices[t++] = a + 1;
            mesh.indices[t++] = a + 1; mesh.indices[t++] = a + vertCountRadial; mesh.indices[t++] = a + vertCountRadial + 1;
        }
    }
    UploadMesh(&mesh, false);
    return mesh;
}

MeshDisplacementParams ProceduralMesh_DefaultDisplacementParams(void) {
    MeshDisplacementParams p = { 0 };
    p.amplitude = 10.0f; p.frequency = 0.05f; p.speed = 1.0f;
    p.twistAmount = 0.0f; p.taperStart = 1.0f; p.taperEnd = 1.0f;
    return p;
}

void ProceduralMesh_SetDisplacementUniforms(Shader shader, const MeshDisplacementParams *params) {
    if (shader.id == 0 || shader.locs == NULL || params == NULL) return;
    int loc;
    if ((loc = GetShaderLocation(shader, "u_dispAmplitude")) >= 0) SetShaderValue(shader, loc, &params->amplitude, SHADER_UNIFORM_FLOAT);
    if ((loc = GetShaderLocation(shader, "u_dispFrequency")) >= 0) SetShaderValue(shader, loc, &params->frequency, SHADER_UNIFORM_FLOAT);
    if ((loc = GetShaderLocation(shader, "u_dispSpeed")) >= 0) SetShaderValue(shader, loc, &params->speed, SHADER_UNIFORM_FLOAT);
    if ((loc = GetShaderLocation(shader, "u_dispTwist")) >= 0) SetShaderValue(shader, loc, &params->twistAmount, SHADER_UNIFORM_FLOAT);
    if ((loc = GetShaderLocation(shader, "u_dispTaperStart")) >= 0) SetShaderValue(shader, loc, &params->taperStart, SHADER_UNIFORM_FLOAT);
    if ((loc = GetShaderLocation(shader, "u_dispTaperEnd")) >= 0) SetShaderValue(shader, loc, &params->taperEnd, SHADER_UNIFORM_FLOAT);
    if ((loc = GetShaderLocation(shader, "u_dispPathP0")) >= 0) SetShaderValue(shader, loc, &params->pathP0, SHADER_UNIFORM_VEC3);
    if ((loc = GetShaderLocation(shader, "u_dispPathP1")) >= 0) SetShaderValue(shader, loc, &params->pathP1, SHADER_UNIFORM_VEC3);
    if ((loc = GetShaderLocation(shader, "u_dispPathP2")) >= 0) SetShaderValue(shader, loc, &params->pathP2, SHADER_UNIFORM_VEC3);
    if ((loc = GetShaderLocation(shader, "u_dispPathP3")) >= 0) SetShaderValue(shader, loc, &params->pathP3, SHADER_UNIFORM_VEC3);
}

void ProceduralMesh_UnloadBase(Mesh *mesh) {
    if (mesh == NULL) return;
    UnloadMesh(*mesh);
    *mesh = (Mesh){ 0 };
}