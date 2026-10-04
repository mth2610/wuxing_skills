#include "core/liquid/liquid_capture_cpu.h"
#include "core/resource_manager.h"
#include "raymath.h"
#include "rlgl.h"
#include <stddef.h>
#include <stdint.h>

typedef struct LiquidCaptureCPUVertex {
    Vector3 viewPosition;
    Vector2 ndc;
    Vector3 centerView;
    Vector3 radii;
    float material;
} LiquidCaptureCPUVertex;

static LiquidCaptureCPUVertex s_vertices[LIQUID_CAPTURE_CPU_MAX_INSTANCES*6];
static unsigned int s_vao, s_vbo;
static Shader s_shader;
static bool s_ready;
static int s_preparedCount, s_vertexCount;
static Matrix s_projection, s_inverseProjection, s_viewToWorld;
static int s_orthographic;
static Texture2D s_frontDepth;
void LiquidCaptureCPU_SetFrontDepth(Texture2D frontDepth) { s_frontDepth=frontDepth; }
static struct {
    int projection, inverseProjection, viewToWorld, orthographic, backDepth, frontDepth, matchMaterial;
} s_uniform;

static void LiquidCaptureCPU_Attribute(unsigned int location, int components, size_t offset)
{
    rlSetVertexAttribute(location, components, RL_FLOAT, false,
                         sizeof(LiquidCaptureCPUVertex), (int)offset);
    rlEnableVertexAttribute(location);
}

bool LiquidCaptureCPU_Init(void)
{
    if (s_ready) return true;
    s_shader = ResourceManager_LoadShader("core/liquid/shaders/liquid_capture_ellipsoid.vs",
                                           "core/liquid/shaders/liquid_capture_ellipsoid.fs");
    /* ResourceManager may return raylib's default shader after a compile error.
     * Rendering with it would produce plausible quads instead of liquid depth. */
    if (!s_shader.id || s_shader.id == rlGetShaderIdDefault()) return false;
    s_uniform.projection = GetShaderLocation(s_shader,"u_projection");
    s_uniform.inverseProjection = GetShaderLocation(s_shader,"u_inverseProjection");
    s_uniform.viewToWorld = GetShaderLocation(s_shader,"u_viewToWorld");
    s_uniform.orthographic = GetShaderLocation(s_shader,"u_orthographic");
    s_uniform.backDepth = GetShaderLocation(s_shader,"u_backDepth");
    s_uniform.frontDepth = GetShaderLocation(s_shader,"u_frontDepthTex");
    s_uniform.matchMaterial = GetShaderLocation(s_shader,"u_matchFrontMaterial");
    if (s_uniform.projection < 0 || s_uniform.inverseProjection < 0 ||
        s_uniform.viewToWorld < 0 || s_uniform.orthographic < 0 || s_uniform.backDepth < 0)
        return false;
    s_vao = rlLoadVertexArray();
    if (!s_vao) return false;
    rlEnableVertexArray(s_vao);
    s_vbo = rlLoadVertexBuffer(NULL,sizeof(s_vertices),true);
    if (!s_vbo) {
        rlDisableVertexArray();
        rlUnloadVertexArray(s_vao); s_vao = 0;
        return false;
    }
    rlEnableVertexBuffer(s_vbo);
    LiquidCaptureCPU_Attribute(0,3,offsetof(LiquidCaptureCPUVertex,viewPosition));
    LiquidCaptureCPU_Attribute(1,2,offsetof(LiquidCaptureCPUVertex,ndc));
    LiquidCaptureCPU_Attribute(2,3,offsetof(LiquidCaptureCPUVertex,centerView));
    LiquidCaptureCPU_Attribute(3,3,offsetof(LiquidCaptureCPUVertex,radii));
    LiquidCaptureCPU_Attribute(4,1,offsetof(LiquidCaptureCPUVertex,material));
    rlDisableVertexBuffer();
    rlDisableVertexArray();
    s_ready = true;
    return true;
}

void LiquidCaptureCPU_Unload(void)
{
    if (s_vbo) rlUnloadVertexBuffer(s_vbo);
    if (s_vao) rlUnloadVertexArray(s_vao);
    s_vbo = s_vao = 0;
    s_shader = (Shader){0}; /* ResourceManager owns the shader. */
    s_ready = false;
    s_preparedCount = s_vertexCount = 0;
}

int LiquidCaptureCPU_Prepare(const LiquidCaptureCPUInstance *instances, int count,
                             Matrix view, Matrix projection)
{
    static const int cornerX[6] = {0,1,1,0,1,0};
    static const int cornerY[6] = {0,0,1,0,1,1};
    s_preparedCount = s_vertexCount = 0;
    if (!s_ready || !instances || count <= 0) return 0;
    if (count > LIQUID_CAPTURE_CPU_MAX_INSTANCES) count = LIQUID_CAPTURE_CPU_MAX_INSTANCES;
    s_projection = projection;
    s_inverseProjection = MatrixInvert(projection);
    s_viewToWorld = MatrixInvert(view);
    s_orthographic = fabsf(projection.m15-1.0f) < 1e-6f;
    for (int i = 0; i < count; ++i) {
        const LiquidCaptureCPUInstance *instance = &instances[i];
        Vector4 bounds;
        if (!LiquidCaptureCPU_ProjectBounds(instance->position,instance->radii,
                                             view,projection,&bounds)) continue;
        Vector4 center = LiquidCaptureCPU_Transform4(view,
            (Vector4){instance->position.x,instance->position.y,instance->position.z,1});
        Vector3 centerView = {center.x,center.y,center.z};
        int firstVertex = s_vertexCount;
        for (int vertex = 0; vertex < 6; ++vertex) {
            Vector2 ndc = {cornerX[vertex] ? bounds.z : bounds.x,
                           cornerY[vertex] ? bounds.w : bounds.y};
            /* Raster geometry sits at NDC depth zero. This remains inside the
             * clip volume even if the actual ellipsoid centre is outside the
             * near plane; the fragment stage clips its selected analytic root. */
            Vector3 viewPosition;
            if (!LiquidCaptureCPU_UnprojectNDC(s_inverseProjection,
                                                (Vector3){ndc.x,ndc.y,0},&viewPosition)) {
                s_vertexCount = firstVertex;
                break;
            }
            s_vertices[s_vertexCount++] = (LiquidCaptureCPUVertex){viewPosition,ndc,
                centerView,instance->radii,(float)instance->material};
        }
        if (s_vertexCount == firstVertex+6) s_preparedCount++;
    }
    if (s_vertexCount > 0) rlUpdateVertexBuffer(s_vbo,s_vertices,
        s_vertexCount*(int)sizeof(LiquidCaptureCPUVertex),0);
    return s_preparedCount;
}

void LiquidCaptureCPU_Draw(bool backDepth)
{
    if (!s_ready || s_vertexCount <= 0) return;
    rlDrawRenderBatchActive();
    BeginShaderMode(s_shader);
    SetShaderValueMatrix(s_shader,s_uniform.projection,s_projection);
    SetShaderValueMatrix(s_shader,s_uniform.inverseProjection,s_inverseProjection);
    SetShaderValueMatrix(s_shader,s_uniform.viewToWorld,s_viewToWorld);
    SetShaderValue(s_shader,s_uniform.orthographic,&s_orthographic,SHADER_UNIFORM_INT);
    int back = backDepth ? 1 : 0;
    SetShaderValue(s_shader,s_uniform.backDepth,&back,SHADER_UNIFORM_INT);
    float match=backDepth && s_frontDepth.id?1.0f:0.0f;
    SetShaderValue(s_shader,s_uniform.matchMaterial,&match,SHADER_UNIFORM_FLOAT);
    int slot=0;
    SetShaderValue(s_shader,s_uniform.frontDepth,&slot,SHADER_UNIFORM_INT);
    rlActiveTextureSlot(0);
    if(match>0.5f) rlEnableTexture(s_frontDepth.id);
    rlEnableShader(s_shader.id);
    rlEnableVertexArray(s_vao);
    rlDrawVertexArray(0,s_vertexCount);
    rlDisableVertexArray();
    rlDisableShader();
    if(match>0.5f) rlDisableTexture();
    EndShaderMode();
}

int LiquidCaptureCPU_GetPreparedCount(void) { return s_preparedCount; }
