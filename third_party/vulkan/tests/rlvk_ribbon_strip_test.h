static unsigned char *ribbonStripReadFile(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return NULL; }
    unsigned char *data = malloc((size_t)length + 1);
    if (!data) { fclose(file); return NULL; }
    if (fread(data, 1, (size_t)length, file) != (size_t)length)
    { free(data); fclose(file); return NULL; }
    fclose(file);
    data[length] = 0;
    *size = (size_t)length;
    return data;
}

// Expand quoted relative includes in the actual shader, before shaderc sees it.
static char *ribbonStripReadShader(const char *root, const char *path, int depth)
{
    size_t size;
    if (depth > 8) return NULL;
    char *source = (char *)ribbonStripReadFile(path, &size);
    if (!source) return NULL;
    char *directive;
    while ((directive = strstr(source, "#include \"")) != NULL)
    {
        char *name = directive + strlen("#include \"");
        char *end = strchr(name, '"');
        if (!end) { free(source); return NULL; }
        char includePath[2048];
        const char *slash = strrchr(path, '/');
        size_t prefix = slash ? (size_t)(slash - path + 1) : 0;
        size_t nameSize = (size_t)(end - name);
        if (prefix + nameSize >= sizeof(includePath)) { free(source); return NULL; }
        memcpy(includePath, path, prefix);
        memcpy(includePath + prefix, name, nameSize);
        includePath[prefix + nameSize] = 0;
        char *included = ribbonStripReadShader(root, includePath, depth + 1);
        if (!included)
        {
            int written = snprintf(includePath, sizeof(includePath), "%s/%.*s", root, (int)nameSize, name);
            if (written > 0 && (size_t)written < sizeof(includePath))
                included = ribbonStripReadShader(root, includePath, depth + 1);
        }
        if (!included) { free(source); return NULL; }
        size_t before = (size_t)(directive - source), includedSize = strlen(included);
        size_t after = strlen(end + 1);
        char *expanded = malloc(before + includedSize + after + 2);
        if (!expanded) { free(included); free(source); return NULL; }
        memcpy(expanded, source, before);
        memcpy(expanded + before, included, includedSize);
        expanded[before + includedSize] = '\n';
        memcpy(expanded + before + includedSize + 1, end + 1, after + 1);
        free(included);
        free(source);
        source = expanded;
    }
    return source;
}

/* Compile and rasterize the actual production ribbon graphics shaders.
 * Optional RLVK_RIBBON_FIXTURE imports actual0.bin from compute parity, so the
 * same GPU-produced node buffer is also exercised through graphics readback. */
static const char *ribbonGpuStripTest(bool worldFbo)
{
    const char *root = getenv("RLVK_REPO_ROOT");
    if (!root) return "RLVK_REPO_ROOT missing";
    if(worldFbo) {
        /* This scenario mirrors the production MVP wiring. Keep that boundary
         * explicit so a production regression cannot hide behind the mirror. */
        char productionPath[2048];
        snprintf(productionPath,sizeof(productionPath),"%s/core/trails/trail_ribbon_gpu.c",root);
        char *production=LoadFileText(productionPath);
        if(!production) return "production ribbon MVP source missing";
        size_t write=0;
        for(size_t read=0;production[read];read++) {
            char c=production[read];
            if(c!=' ' && c!='\n' && c!='\r' && c!='\t') production[write++]=c;
        }
        production[write]=0;
        bool wired=strstr(production,"MatrixMultiply(GetCameraMatrix(camera),rlGetMatrixProjection())")!=NULL;
        UnloadFileText(production);
        if(!wired) return "production ribbon world-space MVP differs from validated camera contract";
    }
    char vsPath[2048], fsPath[2048];
    snprintf(vsPath, sizeof(vsPath), "%s/core/trails/shaders/trail_ribbon_gpu.vs", root);
    snprintf(fsPath, sizeof(fsPath), "%s/core/trails/shaders/trail_ribbon_gpu.fs", root);
    char *vs=ribbonStripReadShader(root,vsPath,0);
    char *fs=ribbonStripReadShader(root,fsPath,0);
    if(!vs || !fs) {free(vs);free(fs);return "production ribbon graphics includes failed";}
    Shader shader = LoadShaderFromMemory(vs, fs);
    free(vs);free(fs);
    if (!shader.id || shader.id == rlGetShaderIdDefault()) return "production ribbon graphics shader failed";
    float nodes[60][12] = {{0}};
    int count = 16;
    for (int i = 0; i < count; i++) {
        nodes[i][0] = -1 + 2.f * i / (count - 1);
        nodes[i][1] = .1f * sinf(i * .4f);
    }
    if(worldFbo) for(int i=0;i<count;i++) {
        nodes[i][0]=5.825f-1.088f*i/(count-1);
        nodes[i][1]=1.522f-.402f*i/(count-1);
        nodes[i][2]=4.4f+.043f*i/(count-1);
    }
    const char *fixture = worldFbo?NULL:getenv("RLVK_RIBBON_FIXTURE");
    if (fixture) {
        char path[2048]; int size = 0;
        snprintf(path, sizeof(path), "%s/ssbo1.bin", fixture);
        unsigned char *params = LoadFileData(path, &size);
        if (!params || size < 64) { if (params) UnloadFileData(params); UnloadShader(shader); return "ribbon fixture params missing"; }
        memcpy(&count, params, sizeof(count)); UnloadFileData(params);
        if (count < 2 || count > 60) { UnloadShader(shader); return "ribbon fixture count invalid"; }
        snprintf(path, sizeof(path), "%s/actual0.bin", fixture);
        unsigned char *computed = LoadFileData(path, &size);
        if (!computed || size < (int)sizeof(nodes)) { if (computed) UnloadFileData(computed); UnloadShader(shader); return "GPU-computed ribbon nodes missing"; }
        memcpy(nodes, computed, sizeof(nodes)); UnloadFileData(computed);
    }
    Vector3 minimum = {nodes[0][0],nodes[0][1],nodes[0][2]}, maximum = minimum;
    for (int i = 0; i < count; i++) {
        minimum.x=fminf(minimum.x,nodes[i][0]); maximum.x=fmaxf(maximum.x,nodes[i][0]);
        minimum.y=fminf(minimum.y,nodes[i][1]); maximum.y=fmaxf(maximum.y,nodes[i][1]);
        minimum.z=fminf(minimum.z,nodes[i][2]); maximum.z=fmaxf(maximum.z,nodes[i][2]);
    }
    Vector3 center=Vector3Scale(Vector3Add(minimum,maximum),.5f);
    float span=fmaxf(.5f,Vector3Distance(minimum,maximum));
    Camera3D camera={.position={center.x,center.y,center.z+span*2.5f},
        .target=center,.up={0,1,0},.fovy=45,.projection=CAMERA_PERSPECTIVE};
    if(worldFbo) camera=(Camera3D){.position={6,6.83f,19.867f},
        .target={6,1.2f,4.4f},.up={0,1,0},.fovy=45,.projection=CAMERA_PERSPECTIVE};
    RenderTexture2D target={0};
    if(worldFbo) target=LoadRenderTexture(W,H);
    const float quad[]={-1,-1,0,1,-1,0,1,1,0,-1,-1,0,1,1,0,-1,1,0};
    unsigned int vao=rlLoadVertexArray();rlEnableVertexArray(vao);
    unsigned int vbo=rlLoadVertexBuffer(quad,sizeof(quad),false);
    rlSetVertexAttribute(0,3,RL_FLOAT,false,0,0);rlEnableVertexAttribute(0);rlDisableVertexArray();
    unsigned int ssbo=rlLoadShaderBuffer(sizeof(nodes),NULL,RL_DYNAMIC_DRAW);
    int slot=0, mvpLoc=GetShaderLocation(shader,"mvp");
    float width=span*.08f, color[]={.2f,.8f,1,1};
    Vector3 right={1,0,0};
    for (int f=0;f<3;f++) {
        BeginDrawing();ClearBackground(BLACK);rlvkBeginFrameCommands();
        if(worldFbo) {BeginTextureMode(target);ClearBackground(BLACK);}
        rlUpdateShaderBuffer(ssbo,nodes,sizeof(nodes),0);
        BeginMode3D(camera);
        if(worldFbo) {
            /* Match the engine camera wrapper: view lives in transform while
             * rlGetMatrixModelview remains identity. GPU nodes are world-space. */
            rlMatrixMode(RL_MODELVIEW);rlLoadIdentity();rlPushMatrix();
            Matrix view=GetCameraMatrix(camera);
            rlMultMatrixf(MatrixToFloat(view));
        }
        rlDrawRenderBatchActive();BeginBlendMode(BLEND_ALPHA);BeginShaderMode(shader);
        Matrix mvp=MatrixMultiply(GetCameraMatrix(camera),rlGetMatrixProjection());
        rlSetUniformMatrix(mvpLoc,mvp);
        rlSetUniform(GetShaderLocation(shader,"u_camera"),&camera.position,RL_SHADER_UNIFORM_VEC3,1);
        rlSetUniform(GetShaderLocation(shader,"u_right"),&right,RL_SHADER_UNIFORM_VEC3,1);
        rlSetUniform(GetShaderLocation(shader,"u_slot"),&slot,RL_SHADER_UNIFORM_INT,1);
        rlSetUniform(GetShaderLocation(shader,"u_count"),&count,RL_SHADER_UNIFORM_INT,1);
        rlSetUniform(GetShaderLocation(shader,"u_width"),&width,RL_SHADER_UNIFORM_FLOAT,1);
        rlSetUniform(GetShaderLocation(shader,"u_color"),color,RL_SHADER_UNIFORM_VEC4,1);
        rlBindShaderBuffer(ssbo,0);rlActiveTextureSlot(0);rlEnableTexture(rlGetTextureIdDefault());
        rlDisableBackfaceCulling();rlDisableDepthMask();rlEnableShader(shader.id);rlEnableVertexArray(vao);
        rlDrawVertexArrayInstanced(0,6,count-1);
        rlDisableVertexArray();rlEnableDepthMask();rlEnableBackfaceCulling();rlDisableTexture();rlDisableShader();EndShaderMode();EndBlendMode();
        if(worldFbo) rlPopMatrix();
        EndMode3D();
        if(worldFbo) {
            EndTextureMode();
            DrawTexturePro(target.texture,(Rectangle){0,0,W,-H},(Rectangle){0,0,W,H},(Vector2){0},0,WHITE);
        }
        EndDrawing();
    }
    Image image=snap();int colored=0;
    for (int y=0;y<image.height;y++) for(int x=0;x<image.width;x++) {
        Color c=at(image,x,y);if(c.g>100 && c.b>150 && c.r<100) colored++;
    }
    ExportImage(image,worldFbo?"/tmp/wuxing-ribbon-gpu-world-strip.png":"/tmp/wuxing-ribbon-gpu-strip.png");UnloadImage(image);
    if(worldFbo) UnloadRenderTexture(target);
    rlUnloadShaderBuffer(ssbo);rlUnloadVertexBuffer(vbo);rlUnloadVertexArray(vao);UnloadShader(shader);
    if (colored<(worldFbo?10:100)) return "production ribbon strip absent or wrong color";
    printf("      production ribbon visible cyan pixels: %d\n",colored);
    return NULL;
}

static const char *sc_ribbon_gpu_strip(void) {return ribbonGpuStripTest(false);}
static const char *sc_ribbon_gpu_world_strip(void) {return ribbonGpuStripTest(true);}
