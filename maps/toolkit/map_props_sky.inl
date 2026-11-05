// An indexed enclosing cube supplies continuous world-space view rays.
// The vertex shader places it at far depth; opaque scenery rejects its pixels.
MapSkyDome MapProp_CreateSkyDome(void)
{
    MapSkyDome sky = {0};
    Mesh mesh = {0};
    static const float corners[24] = {
        -1.0f, -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f,
        -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f};
    static const unsigned short triangles[36] = {
        0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7,
        0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5,
        3, 7, 6, 3, 6, 2, 0, 1, 5, 0, 5, 4};
    mesh.vertexCount = 8;
    mesh.triangleCount = 12;
    mesh.vertices = MemAlloc(sizeof(corners));
    mesh.normals = MemAlloc(sizeof(corners));
    mesh.texcoords = MemAlloc(8 * 2 * sizeof(float));
    mesh.indices = MemAlloc(sizeof(triangles));
    if (!mesh.vertices || !mesh.normals || !mesh.texcoords || !mesh.indices)
    {
        if (mesh.vertices)
            MemFree(mesh.vertices);
        if (mesh.normals)
            MemFree(mesh.normals);
        if (mesh.texcoords)
            MemFree(mesh.texcoords);
        if (mesh.indices)
            MemFree(mesh.indices);
        return sky;
    }
    memcpy(mesh.vertices, corners, sizeof(corners));
    memcpy(mesh.normals, corners, sizeof(corners));
    memset(mesh.texcoords, 0, 8 * 2 * sizeof(float));
    memcpy(mesh.indices, triangles, sizeof(triangles));
    UploadMesh(&mesh, false);

    sky.model = LoadModelFromMesh(mesh);
    sky.shader = ResourceManager_LoadShader("maps/toolkit/shaders/sky_dome.vs",
                                            "maps/toolkit/shaders/sky_dome.fs");
    if (sky.shader.id != 0 && sky.shader.locs != NULL)
    {
        sky.shader.locs[SHADER_LOC_VERTEX_POSITION] =
            GetShaderLocationAttrib(sky.shader, "vertexPosition");
        sky.shader.locs[SHADER_LOC_MATRIX_MVP] =
            GetShaderLocation(sky.shader, "mvp");
        sky.shader.locs[SHADER_LOC_MATRIX_MODEL] =
            GetShaderLocation(sky.shader, "matModel");
    }
    sky.sunLoc = GetShaderLocation(sky.shader, "u_sunDirection");
    sky.skyLoc = GetShaderLocation(sky.shader, "u_skyAmbient");
    sky.hazeLoc = GetShaderLocation(sky.shader, "u_hazeColor");
    sky.colorLoc = GetShaderLocation(sky.shader, "u_sunColor");
    sky.ready = (sky.shader.id != 0 && sky.sunLoc >= 0 && sky.skyLoc >= 0 &&
                 sky.hazeLoc >= 0 && sky.colorLoc >= 0);
    if (sky.model.materialCount > 0)
        sky.model.materials[0].shader = sky.shader;
    return sky;
}

void MapProp_DrawSkyDome(const MapSkyDome *sky)
{
    if (!sky || !sky->ready)
        return;

    EnvFrameLighting lighting = Environment_GetFrameLighting();
    Vector3 sun = Vector3Negate(lighting.sunDirection);
    Color ambient = lighting.skyAmbient;
    Color haze = lighting.atmosphere.color;
    Color direct = lighting.sunColor;

    const float inv255 = 1.0f / 255.0f;
    const float sunScale = lighting.sunIntensity * inv255;
    float skyRGB[3] = {(float)ambient.r * inv255, (float)ambient.g * inv255, (float)ambient.b * inv255};
    float hazeRGB[3] = {(float)haze.r * inv255, (float)haze.g * inv255, (float)haze.b * inv255};
    float sunRGB[3] = {(float)direct.r * sunScale, (float)direct.g * sunScale, (float)direct.b * sunScale};

    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    BeginShaderMode(sky->shader);
    SetShaderValue(sky->shader, sky->sunLoc, &sun, SHADER_UNIFORM_VEC3);
    SetShaderValue(sky->shader, sky->skyLoc, skyRGB, SHADER_UNIFORM_VEC3);
    SetShaderValue(sky->shader, sky->hazeLoc, hazeRGB, SHADER_UNIFORM_VEC3);
    SetShaderValue(sky->shader, sky->colorLoc, sunRGB, SHADER_UNIFORM_VEC3);
    DrawModel(sky->model, camera.position, 50.0f, WHITE);
    rlDrawRenderBatchActive();
    EndShaderMode();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

void MapProp_UnloadSkyDome(MapSkyDome *sky)
{
    if (!sky || !sky->model.meshCount)
        return;
    UnloadModel(sky->model);
    *sky = (MapSkyDome){0};
}