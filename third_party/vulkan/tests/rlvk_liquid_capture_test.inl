/* Real SSF shader/runtime contracts. This is not an image-quality acceptance
 * test: it checks float capture roots, flat material IDs and SSBO routing. */
#include "core/liquid/liquid_capture_cpu.h"
#include <float.h>

#define LIQUID_PROBE_SIZE 128
static char s_liquidProbeWhy[256];
static Shader s_liquidCPUOwnedShader;

static Shader liquidProbeShader(const char *vs, const char *fs)
{
    const char *root = getenv("RLVK_REPO_ROOT");
    Shader shader = {0};
    if (!root) return shader;
    char vsPath[1024], fsPath[1024];
    snprintf(vsPath,sizeof(vsPath),"%s/%s",root,vs);
    snprintf(fsPath,sizeof(fsPath),"%s/%s",root,fs);
    shader = LoadShader(vsPath,fsPath);
    return shader;
}

/* Test-only resource owner for the linked production CPU helper. The game
 * ResourceManager is deliberately absent from this standalone renderer test. */
Shader ResourceManager_LoadShader(const char *vs, const char *fs)
{
    s_liquidCPUOwnedShader = liquidProbeShader(vs,fs);
    return s_liquidCPUOwnedShader;
}

static RenderTexture2D liquidProbeTarget(void)
{
    RenderTexture2D target = fmtRT(LIQUID_PROBE_SIZE,LIQUID_PROBE_SIZE,
                                  RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32);
    if (!target.id || !target.texture.id) return target;
    rlEnableFramebuffer(target.id);
    target.depth.id = rlLoadTextureDepth(LIQUID_PROBE_SIZE,LIQUID_PROBE_SIZE,false);
    target.depth.width = target.depth.height = LIQUID_PROBE_SIZE;
    target.depth.mipmaps = 1;
    rlFramebufferAttach(target.id,target.depth.id,RL_ATTACHMENT_DEPTH,RL_ATTACHMENT_TEXTURE2D,0);
    bool complete = rlFramebufferComplete(target.id);
    rlDisableFramebuffer();
    if (!complete) {
        UnloadRenderTexture(target);
        target = (RenderTexture2D){0};
    }
    return target;
}

static void liquidProbeBegin(RenderTexture2D target, bool back)
{
    BeginTextureMode(target);
    ClearBackground(back ? BLANK : (Color){255,0,0,0});
    rlDrawRenderBatchActive();
    rlDisableColorBlend();
    rlDisableBackfaceCulling();
    rlEnableDepthTest();
    rlEnableDepthMask();
}

static void liquidProbeEnd(void)
{
    rlEnableColorBlend();
    rlEnableBackfaceCulling();
    rlDisableDepthTest();
    EndTextureMode();
}

static bool liquidProbeRead(RenderTexture2D target, int x, int y, float out[4])
{
    float *pixels = rlReadTexturePixels(target.texture.id,LIQUID_PROBE_SIZE,
        LIQUID_PROBE_SIZE,RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32);
    if (!pixels) return false;
    size_t index = ((size_t)y*LIQUID_PROBE_SIZE+(size_t)x)*4;
    memcpy(out,pixels+index,4*sizeof(float));
    RL_FREE(pixels);
    return true;
}

/* Find an interior texel in an asymmetric material patch, then sample the back
 * at precisely that texel. A flipped front sampler cannot pass this check. */
static bool liquidProbeMaterialPixel(RenderTexture2D target, float material,
                                     int *outX, int *outY, float out[4])
{
    float *pixels = rlReadTexturePixels(target.texture.id,LIQUID_PROBE_SIZE,
        LIQUID_PROBE_SIZE,RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32);
    if (!pixels) return false;
    float sumX = 0, sumY = 0;
    int count = 0;
    for (int y = 0; y < LIQUID_PROBE_SIZE; ++y) for (int x = 0; x < LIQUID_PROBE_SIZE; ++x) {
        const float *pixel = pixels+((size_t)y*LIQUID_PROBE_SIZE+(size_t)x)*4;
        if (pixel[1]>0.9f && fabsf(pixel[2]-material)<0.01f) {
            sumX += x; sumY += y; ++count;
        }
    }
    bool found = false;
    float bestDistance = FLT_MAX;
    if (count) for (int y = 0; y < LIQUID_PROBE_SIZE; ++y) for (int x = 0; x < LIQUID_PROBE_SIZE; ++x) {
        const float *pixel = pixels+((size_t)y*LIQUID_PROBE_SIZE+(size_t)x)*4;
        float dx = x-sumX/count, dy = y-sumY/count;
        float distance = dx*dx+dy*dy;
        if (pixel[1]>0.9f && fabsf(pixel[2]-material)<0.01f && distance<bestDistance) {
            bestDistance = distance; *outX = x; *outY = y;
            memcpy(out,pixel,4*sizeof(float)); found = true;
        }
    }
    RL_FREE(pixels);
    return found;
}

static float liquidProbeExpectedDepth(Vector3 center, Vector3 radii, Matrix view,
                                      Matrix projection, bool orthographic, bool back)
{
    float ndc = 1.0f/LIQUID_PROBE_SIZE; /* centre pixel centre, not the pixel corner */
    Matrix inverseProjection = MatrixInvert(projection), inverseView = MatrixInvert(view);
    Vector3 nearView, farView;
    LiquidCaptureCPU_UnprojectNDC(inverseProjection,(Vector3){ndc,ndc,-1},&nearView);
    LiquidCaptureCPU_UnprojectNDC(inverseProjection,(Vector3){ndc,ndc,1},&farView);
    Vector3 origin = orthographic ? nearView : (Vector3){0};
    Vector3 direction = Vector3Normalize(orthographic ? Vector3Subtract(farView,nearView) : nearView);
    Vector4 worldOrigin = LiquidCaptureCPU_Transform4(inverseView,(Vector4){origin.x,origin.y,origin.z,1});
    Vector4 worldDirection = LiquidCaptureCPU_Transform4(inverseView,(Vector4){direction.x,direction.y,direction.z,0});
    float nearRoot, farRoot;
    if (!LiquidCaptureCPU_RayEllipsoid((Vector3){worldOrigin.x,worldOrigin.y,worldOrigin.z},
        (Vector3){worldDirection.x,worldDirection.y,worldDirection.z},center,radii,&nearRoot,&farRoot))
        return -1.0f;
    Vector3 surfaceView = Vector3Add(origin,Vector3Scale(direction,back ? farRoot : nearRoot));
    Vector4 clip = LiquidCaptureCPU_Transform4(projection,
        (Vector4){surfaceView.x,surfaceView.y,surfaceView.z,1});
    return clip.z/clip.w*0.5f+0.5f;
}

static const char *sc_liquid_cpu_capture(void)
{
    RenderTexture2D front = liquidProbeTarget(), back = liquidProbeTarget();
    if (!front.id || !back.id) {
        if (front.id) UnloadRenderTexture(front);
        if (back.id) UnloadRenderTexture(back);
        return "liquid CPU float/depth targets unavailable";
    }
    const char *failure = NULL;
    if (!LiquidCaptureCPU_Init()) failure = "production CPU analytic shader/helper failed initialization";
    LiquidCaptureCPUInstance instances[3] = {
        {{0,0,-7},{0.5f,0.35f,0.5f},4},
        {{0,0,-5},{0.75f,0.25f,0.5f},2},
        {{1.2f,0.7f,-6},{0.3f,0.2f,0.3f},5}
    };
    for (int mode = 0; mode < 2 && !failure; ++mode) {
        Matrix view = MatrixIdentity();
        Matrix projection = mode == 0 ? MatrixOrtho(-2,2,-2,2,0.1,20) :
                                         MatrixPerspective(45*DEG2RAD,1,0.1,20);
        for (int masked = 0; masked < 2 && !failure; ++masked) {
            LiquidCaptureCPU_SetFrontDepth(masked ? front.texture : (Texture2D){0});
            for (int frame = 0; frame < 3; ++frame) {
                BeginDrawing(); ClearBackground(BLACK);
                if (LiquidCaptureCPU_Prepare(instances,3,view,projection) != 3 ||
                    LiquidCaptureCPU_GetPreparedCount() != 3) failure = "CPU instances were not prepared";
                liquidProbeBegin(front,false); LiquidCaptureCPU_Draw(false); liquidProbeEnd();
                liquidProbeBegin(back,true); LiquidCaptureCPU_Draw(true); liquidProbeEnd();
                DrawTexturePro(front.texture,(Rectangle){0,0,LIQUID_PROBE_SIZE,-LIQUID_PROBE_SIZE},
                    (Rectangle){0,0,W,H},(Vector2){0},0,WHITE);
                EndDrawing();
            }
            float nearPixel[4], farPixel[4], horizontal[4], vertical[4], empty[4], side[4], sideBack[4];
            int sideX = 0, sideY = 0;
            bool read = liquidProbeRead(front,64,64,nearPixel) && liquidProbeRead(back,64,64,farPixel) &&
                        liquidProbeRead(front,82,64,horizontal) && liquidProbeRead(front,64,82,vertical) &&
                        liquidProbeRead(front,8,8,empty);
            bool sideRead = read && liquidProbeMaterialPixel(front,5,&sideX,&sideY,side) &&
                            liquidProbeRead(back,sideX,sideY,sideBack);
            float expectedNear = liquidProbeExpectedDepth(instances[1].position,instances[1].radii,
                                                           view,projection,mode == 0,false);
            int backInstance = masked ? 1 : 0;
            float expectedFar = liquidProbeExpectedDepth(instances[backInstance].position,instances[backInstance].radii,
                                                          view,projection,mode == 0,true);
            if (!read) failure = "CPU float capture readback failed";
            else if (!sideRead) {
                snprintf(s_liquidProbeWhy,sizeof(s_liquidProbeWhy),
                    "CPU %s mask=%d material 5 absent: center depth=%.5f coverage=%.5f material=%.5f back=%.5f",
                    mode == 0 ? "ortho" : "perspective",masked,nearPixel[0],nearPixel[1],nearPixel[2],farPixel[0]);
                failure = s_liquidProbeWhy;
            }
            else if (fabsf(nearPixel[0]-expectedNear)>0.002f || fabsf(farPixel[0]-expectedFar)>0.002f ||
                     nearPixel[1]<0.9f || fabsf(nearPixel[2]-2.0f)>0.01f || farPixel[0]<=nearPixel[0] ||
                     sideBack[1]<0.9f || sideBack[0]<=side[0] || sideBack[0]>=0.999f || abs(sideY-64)<5) {
                snprintf(s_liquidProbeWhy,sizeof(s_liquidProbeWhy),
                    "CPU %s mask=%d roots/material wrong: front=%.5f expected=%.5f slot=%.2f back=%.5f expected=%.5f sideBack=%.5f",
                    mode == 0 ? "ortho" : "perspective",masked,nearPixel[0],expectedNear,nearPixel[2],farPixel[0],expectedFar,sideBack[0]);
                failure = s_liquidProbeWhy;
            } else if (horizontal[0]>=0.999f || vertical[0]<0.999f || empty[0]<0.999f)
                failure = "CPU ellipsoid axes or empty-space discard wrong";
        }
    }
    LiquidCaptureCPU_Unload();
    if (s_liquidCPUOwnedShader.id && s_liquidCPUOwnedShader.id != rlGetShaderIdDefault())
        UnloadShader(s_liquidCPUOwnedShader);
    s_liquidCPUOwnedShader = (Shader){0};
    UnloadRenderTexture(front); UnloadRenderTexture(back);
    return failure;
}

static const char *sc_liquid_indexed_capture(void)
{
    Shader shaders[2] = {
        liquidProbeShader("core/particles/shaders/gpu/liquid_surface_capture.vs",
                           "core/liquid/shaders/liquid_capture_particle.fs"),
        liquidProbeShader("core/particles/shaders/gpu/liquid_surface_capture.vs",
                           "core/liquid/shaders/liquid_capture_particle_back.fs")
    };
    for (int i = 0; i < 2; ++i) if (!shaders[i].id || shaders[i].id == rlGetShaderIdDefault()) {
        for (int j = 0; j < 2; ++j)
            if (shaders[j].id && shaders[j].id != rlGetShaderIdDefault()) UnloadShader(shaders[j]);
        return "indexed liquid front/back shader pair did not load";
    }
    struct Particle { float lane[9][4]; } particles[5] = {0};
    struct SurfaceIndex { uint32_t particleIndex; float material; } indices[4] = {
        {4,4},{3,2},{1,5},{2,1}
    };
    for (int i = 0; i < 5; ++i) {
        particles[i].lane[0][2] = -5; particles[i].lane[0][3] = 0.75f;
        particles[i].lane[4][0] = particles[i].lane[4][1] = particles[i].lane[4][3] = 1;
        particles[i].lane[6][0] = (float)(10+i); particles[i].lane[6][1] = 3;
    }
    particles[0].lane[0][2] = -3; /* excluded poison: accidental gl_InstanceID fetch wins depth */
    particles[1].lane[0][0] = 1.2f; particles[1].lane[0][1] = 0.7f;
    particles[1].lane[0][2] = -6; particles[1].lane[0][3] = 0.35f;
    particles[2].lane[0][0] = -1.2f; particles[2].lane[0][2] = -3; particles[2].lane[0][3] = 0.35f;
    particles[2].lane[4][0] = 0; /* indexed but dead: must not produce depth/material */
    particles[4].lane[0][2] = -7; particles[4].lane[0][3] = 0.5f;
    static const float quad[18] = {-1,-1,0,1,-1,0,1,1,0,-1,-1,0,1,1,0,-1,1,0};
    unsigned int vao = rlLoadVertexArray(); rlEnableVertexArray(vao);
    unsigned int vbo = rlLoadVertexBuffer(quad,sizeof(quad),false);
    rlSetVertexAttribute(0,3,RL_FLOAT,false,3*sizeof(float),0); rlEnableVertexAttribute(0);
    rlDisableVertexArray();
    unsigned int state = rlLoadShaderBuffer(sizeof(particles),NULL,RL_DYNAMIC_DRAW);
    unsigned int surface = rlLoadShaderBuffer(sizeof(indices),NULL,RL_DYNAMIC_DRAW);
    RenderTexture2D front = liquidProbeTarget(), back = liquidProbeTarget();
    const char *failure = (!front.id || !back.id || !state || !surface) ? "indexed capture resources unavailable" : NULL;
    Matrix view = MatrixIdentity(), projection = MatrixOrtho(-2,2,-2,2,0.1,20);
    for (int masked = 0; masked < 2 && !failure; ++masked) {
        for (int frame = 0; frame < 3 && !failure; ++frame) {
            BeginDrawing(); ClearBackground(BLACK);
            rlUpdateShaderBuffer(state,particles,sizeof(particles),0);
            rlUpdateShaderBuffer(surface,indices,sizeof(indices),0);
            for (int pass = 0; pass < 2; ++pass) {
                Shader shader = shaders[pass];
                liquidProbeBegin(pass ? back : front,pass != 0);
                BeginShaderMode(shader);
                SetShaderValueMatrix(shader,GetShaderLocation(shader,"u_view"),view);
                SetShaderValueMatrix(shader,GetShaderLocation(shader,"u_projection"),projection);
                float noFilter = -1.0f, poisonMaterial = 0.0f; int indexed = 1;
                SetShaderValue(shader,GetShaderLocation(shader,"u_filterEmitter"),&noFilter,SHADER_UNIFORM_FLOAT);
                SetShaderValue(shader,GetShaderLocation(shader,"u_filterRenderMode"),&noFilter,SHADER_UNIFORM_FLOAT);
                SetShaderValue(shader,GetShaderLocation(shader,"u_materialId"),&poisonMaterial,SHADER_UNIFORM_FLOAT);
                SetShaderValue(shader,GetShaderLocation(shader,"u_surfaceIndexed"),&indexed,SHADER_UNIFORM_INT);
                if (pass) {
                    float matchFront = (float)masked; int sampler = 0;
                    SetShaderValue(shader,GetShaderLocation(shader,"u_matchFrontMaterial"),&matchFront,SHADER_UNIFORM_FLOAT);
                    SetShaderValue(shader,GetShaderLocation(shader,"u_frontDepthTex"),&sampler,SHADER_UNIFORM_INT);
                    rlActiveTextureSlot(0);
                    if (masked) rlEnableTexture(front.texture.id);
                }
                rlBindShaderBuffer(state,0); rlBindShaderBuffer(surface,1);
                rlEnableShader(shader.id); rlEnableVertexArray(vao);
                rlDrawVertexArrayInstanced(0,6,4);
                rlDisableVertexArray(); rlDisableShader(); EndShaderMode();
                if (pass && masked) { rlActiveTextureSlot(0); rlDisableTexture(); }
                liquidProbeEnd();
            }
            DrawTexturePro(front.texture,(Rectangle){0,0,LIQUID_PROBE_SIZE,-LIQUID_PROBE_SIZE},
                (Rectangle){0,0,W,H},(Vector2){0},0,WHITE);
            EndDrawing();
        }
        float nearPixel[4], farPixel[4], side[4], sideBack[4], dead[4], empty[4];
        int sideX = 0, sideY = 0;
        if (!failure) {
            bool read = liquidProbeRead(front,64,64,nearPixel) && liquidProbeRead(back,64,64,farPixel) &&
                        liquidProbeRead(front,26,64,dead) &&
                        liquidProbeRead(front,8,8,empty);
            bool sideRead = read && liquidProbeMaterialPixel(front,5,&sideX,&sideY,side) &&
                            liquidProbeRead(back,sideX,sideY,sideBack);
            Vector4 nearClip = LiquidCaptureCPU_Transform4(projection,(Vector4){0,0,-4.25f,1});
            Vector4 farClip = LiquidCaptureCPU_Transform4(projection,(Vector4){0,0,masked ? -5.75f : -7.5f,1});
            Vector4 sideClip = LiquidCaptureCPU_Transform4(projection,(Vector4){1.2f,0.7f,-6,1});
            float expectedNear = nearClip.z/nearClip.w*0.5f+0.5f;
            float expectedFar = farClip.z/farClip.w*0.5f+0.5f;
            float expectedSideMean = sideClip.z/sideClip.w*0.5f+0.5f;
            if (!read) failure = "indexed float capture readback failed";
            else if (!sideRead) failure = "indexed isolated material 5 absent";
            else if (fabsf(nearPixel[0]-expectedNear)>0.002f || fabsf(farPixel[0]-expectedFar)>0.002f ||
                     fabsf(nearPixel[2]-2)>0.01f || fabsf(side[2]-5)>0.01f ||
                     nearPixel[1]<0.9f || side[0]>=0.999f || sideBack[1]<0.9f ||
                     sideBack[0]<=side[0] || sideBack[0]>=0.999f || abs(sideY-64)<5 ||
                     fabsf((side[0]+sideBack[0])*0.5f-expectedSideMean)>0.002f ||
                     dead[0]<0.999f || empty[0]<0.999f) {
                snprintf(s_liquidProbeWhy,sizeof(s_liquidProbeWhy),
                    "indexed mask=%d roots/IDs wrong: front=%.5f slot=%.2f back=%.5f sideSlot=%.2f sideBack=%.5f; SSBO bindings0..3",
                    masked,nearPixel[0],nearPixel[2],farPixel[0],side[2],sideBack[0]);
                failure = s_liquidProbeWhy;
            }
        }
    }
    if (front.id) UnloadRenderTexture(front);
    if (back.id) UnloadRenderTexture(back);
    rlUnloadShaderBuffer(state); rlUnloadShaderBuffer(surface);
    rlUnloadVertexBuffer(vbo); rlUnloadVertexArray(vao);
    UnloadShader(shaders[0]); UnloadShader(shaders[1]);
    return failure;
}
