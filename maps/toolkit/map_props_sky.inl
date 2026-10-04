// An indexed enclosing cube supplies continuous world-space view rays.
// The vertex shader places it at far depth; opaque scenery rejects its pixels.
MapSkyDome MapProp_CreateSkyDome(void)
{
    MapSkyDome sky = {0};
    Mesh mesh = {0};
    static const float corners[24] = {
        -1,-1,-1, 1,-1,-1, 1,1,-1, -1,1,-1,
        -1,-1,1, 1,-1,1, 1,1,1, -1,1,1
    };
    static const unsigned short triangles[36] = {
        0,2,1, 0,3,2, 4,5,6, 4,6,7,
        0,4,7, 0,7,3, 1,2,6, 1,6,5,
        3,7,6, 3,6,2, 0,1,5, 0,5,4
    };
    mesh.vertexCount = 8;
    mesh.triangleCount = 12;
    mesh.vertices = MemAlloc(sizeof(corners));
    mesh.normals = MemAlloc(sizeof(corners));
    mesh.texcoords = MemAlloc(8 * 2 * sizeof(float));
    mesh.indices = MemAlloc(sizeof(triangles));
    if (!mesh.vertices || !mesh.normals || !mesh.texcoords || !mesh.indices) {
        if (mesh.vertices) MemFree(mesh.vertices);
        if (mesh.normals) MemFree(mesh.normals);
        if (mesh.texcoords) MemFree(mesh.texcoords);
        if (mesh.indices) MemFree(mesh.indices);
        return sky;
    }
    memcpy(mesh.vertices, corners, sizeof(corners));
    memcpy(mesh.normals, corners, sizeof(corners));
    memset(mesh.texcoords, 0, 8 * 2 * sizeof(float));
    memcpy(mesh.indices, triangles, sizeof(triangles));
    UploadMesh(&mesh, false);
    sky.model = LoadModelFromMesh(mesh);
    sky.shader = LoadShader("maps/toolkit/shaders/sky_dome.vs", "maps/toolkit/shaders/sky_dome.fs");
    sky.sunLoc = GetShaderLocation(sky.shader, "u_sunDirection");
    sky.skyLoc = GetShaderLocation(sky.shader, "u_skyAmbient");
    sky.hazeLoc = GetShaderLocation(sky.shader, "u_hazeColor");
    sky.colorLoc = GetShaderLocation(sky.shader, "u_sunColor");
    sky.ready = sky.sunLoc >= 0 && sky.skyLoc >= 0 && sky.hazeLoc >= 0 && sky.colorLoc >= 0;
    sky.model.materials[0].shader = sky.shader;
    return sky;
}

void MapProp_DrawSkyDome(const MapSkyDome *sky)
{
    if (!sky || !sky->ready) return;
    EnvFrameLighting lighting = Environment_GetFrameLighting();
    Vector3 sun = Vector3Negate(lighting.sunDirection);
    Color ambient = lighting.skyAmbient, haze = lighting.atmosphere.color, direct = lighting.sunColor;
    float skyRGB[3] = {ambient.r/255.0f, ambient.g/255.0f, ambient.b/255.0f};
    float hazeRGB[3] = {haze.r/255.0f, haze.g/255.0f, haze.b/255.0f};
    float sunRGB[3] = {direct.r/255.0f, direct.g/255.0f, direct.b/255.0f};
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
    if (!sky || !sky->model.meshCount) return;
    UnloadModel(sky->model);
    if (sky->shader.id && sky->shader.id != rlGetShaderIdDefault()) UnloadShader(sky->shader);
    *sky = (MapSkyDome){0};
}
