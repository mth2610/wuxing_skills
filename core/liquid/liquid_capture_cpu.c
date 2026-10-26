#include "core/liquid/liquid_capture_cpu.h"
#include "core/resource_manager.h"
#include "raymath.h"
#include "rlgl.h"
#include <stddef.h>
#include <stdint.h>

#define LIQUID_CAPTURE_CPU_VERTEX_CAPACITY (LIQUID_CAPTURE_CPU_MAX_INSTANCES*6)
/* rlvk's canonical mesh streams have fixed strides. Keep planar attributes in
 * one persistent buffer; an interleaved stride is not a supported backend path. */
typedef struct LiquidCaptureCPUVertices {
    Vector3 position[LIQUID_CAPTURE_CPU_VERTEX_CAPACITY];
    Vector2 ndc[LIQUID_CAPTURE_CPU_VERTEX_CAPACITY];
    Vector3 center[LIQUID_CAPTURE_CPU_VERTEX_CAPACITY];
    unsigned char material[LIQUID_CAPTURE_CPU_VERTEX_CAPACITY][4];
    Vector4 radii[LIQUID_CAPTURE_CPU_VERTEX_CAPACITY];
} LiquidCaptureCPUVertices;

static LiquidCaptureCPUVertices s_vertices;
static unsigned int s_vao, s_vbo;
static Shader s_shader;
static bool s_ready;
static int s_preparedCount, s_vertexCount;
static Matrix s_projection, s_inverseProjection, s_viewToWorld;
static int s_orthographic;
static Texture2D s_frontDepth;

void LiquidCaptureCPU_SetFrontDepth(Texture2D frontDepth) { s_frontDepth = frontDepth; }

static struct {
    int projection, inverseProjection, viewToWorld, orthographic, backDepth, frontDepth, matchMaterial;
} s_uniform;

static void LiquidCaptureCPU_Attribute(unsigned int location, int components, size_t offset)
{
    rlSetVertexAttribute(location, components, RL_FLOAT, false, 0, (int)offset);
    rlEnableVertexAttribute(location);
}

bool LiquidCaptureCPU_Init(void)
{
    if (s_ready) return true;
    s_shader = ResourceManager_LoadShader("core/liquid/shaders/liquid_capture_ellipsoid.vs",
                                          "core/liquid/shaders/liquid_capture_ellipsoid.fs");
    if (!s_shader.id || s_shader.id == rlGetShaderIdDefault()) return false;
    s_uniform.projection = GetShaderLocation(s_shader, "u_projection");
    s_uniform.inverseProjection = GetShaderLocation(s_shader, "u_inverseProjection");
    s_uniform.viewToWorld = GetShaderLocation(s_shader, "u_viewToWorld");
    s_uniform.orthographic = GetShaderLocation(s_shader, "u_orthographic");
    s_uniform.backDepth = GetShaderLocation(s_shader, "u_backDepth");
    s_uniform.frontDepth = GetShaderLocation(s_shader, "u_frontDepthTex");
    s_uniform.matchMaterial = GetShaderLocation(s_shader, "u_matchFrontMaterial");
    if (s_uniform.projection < 0 || s_uniform.inverseProjection < 0 ||
        s_uniform.viewToWorld < 0 || s_uniform.orthographic < 0 || s_uniform.backDepth < 0)
        return false;
    s_vao = rlLoadVertexArray();
    if (!s_vao) return false;
    rlEnableVertexArray(s_vao);
    s_vbo = rlLoadVertexBuffer(NULL, sizeof(s_vertices), true);
    if (!s_vbo) {
        rlDisableVertexArray();
        rlUnloadVertexArray(s_vao);
        s_vao = 0;
        return false;
    }
    rlEnableVertexBuffer(s_vbo);
    LiquidCaptureCPU_Attribute(0, 3, offsetof(LiquidCaptureCPUVertices, position));
    LiquidCaptureCPU_Attribute(1, 2, offsetof(LiquidCaptureCPUVertices, ndc));
    LiquidCaptureCPU_Attribute(2, 3, offsetof(LiquidCaptureCPUVertices, center));
    rlSetVertexAttribute(3, 4, RL_UNSIGNED_BYTE, true, 0, (int)offsetof(LiquidCaptureCPUVertices, material));
    rlEnableVertexAttribute(3);
    LiquidCaptureCPU_Attribute(4, 4, offsetof(LiquidCaptureCPUVertices, radii));
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
    s_shader = (Shader){0};
    s_ready = false;
    s_preparedCount = s_vertexCount = 0;
}

int LiquidCaptureCPU_Prepare(const LiquidCaptureCPUInstance *instances, int count,
                             Matrix view, Matrix projection)
{
    /* Quad corners: 0=(minX,minY), 1=(maxX,minY), 2=(maxX,maxY), 3=(minX,maxY).
     * Unproject the 4 unique corners once per instance and index {0,1,2, 0,2,3}. */
    static const int triCorner[6] = {0, 1, 2, 0, 2, 3};
    s_preparedCount = s_vertexCount = 0;
    if (!s_ready || !instances || count <= 0) return 0;
    if (count > LIQUID_CAPTURE_CPU_MAX_INSTANCES) count = LIQUID_CAPTURE_CPU_MAX_INSTANCES;
    s_projection = projection;
    s_inverseProjection = MatrixInvert(projection);
    s_viewToWorld = MatrixInvert(view);
    s_orthographic = fabsf(projection.m15 - 1.0f) < 1e-6f;

    for (int i = 0; i < count; ++i) {
        const LiquidCaptureCPUInstance *instance = &instances[i];
        if (instance->radii.x <= 1e-5f || instance->radii.y <= 1e-5f || instance->radii.z <= 1e-5f)
            continue;
        Vector4 bounds;
        if (!LiquidCaptureCPU_ProjectBounds(instance->position, instance->radii,
                                            view, projection, &bounds))
            continue;

        Vector2 cornerNDC[4] = {
            {bounds.x, bounds.y},
            {bounds.z, bounds.y},
            {bounds.z, bounds.w},
            {bounds.x, bounds.w}
        };
        Vector3 cornerView[4];
        bool valid = true;
        for (int c = 0; c < 4; ++c) {
            if (!LiquidCaptureCPU_UnprojectNDC(s_inverseProjection,
                                               (Vector3){cornerNDC[c].x, cornerNDC[c].y, 0.0f},
                                               &cornerView[c])) {
                valid = false;
                break;
            }
        }
        if (!valid) continue;

        Vector4 center = LiquidCaptureCPU_Transform4(view,
            (Vector4){instance->position.x, instance->position.y, instance->position.z, 1.0f});
        Vector3 centerView = {center.x, center.y, center.z};
        Vector4 radii4 = {instance->radii.x, instance->radii.y, instance->radii.z, 0.0f};
        unsigned char matId = (unsigned char)instance->material;

        int base = s_vertexCount;
        for (int vertex = 0; vertex < 6; ++vertex) {
            int c = triCorner[vertex];
            int v = base + vertex;
            s_vertices.position[v] = cornerView[c];
            s_vertices.ndc[v] = cornerNDC[c];
            s_vertices.center[v] = centerView;
            s_vertices.material[v][0] = matId;
            s_vertices.material[v][1] = 0;
            s_vertices.material[v][2] = 0;
            s_vertices.material[v][3] = 255;
            s_vertices.radii[v] = radii4;
        }
        s_vertexCount = base + 6;
        s_preparedCount++;
    }

    if (s_vertexCount > 0) {
        /* Avoid uploading the entire max-capacity planar struct when only a small
         * fraction of instances are active; update active planar sub-ranges instead. */
        if (s_vertexCount * 2 < LIQUID_CAPTURE_CPU_VERTEX_CAPACITY) {
            int n = s_vertexCount;
            rlUpdateVertexBuffer(s_vbo, s_vertices.position, n * (int)sizeof(Vector3),
                                 (int)offsetof(LiquidCaptureCPUVertices, position));
            rlUpdateVertexBuffer(s_vbo, s_vertices.ndc, n * (int)sizeof(Vector2),
                                 (int)offsetof(LiquidCaptureCPUVertices, ndc));
            rlUpdateVertexBuffer(s_vbo, s_vertices.center, n * (int)sizeof(Vector3),
                                 (int)offsetof(LiquidCaptureCPUVertices, center));
            rlUpdateVertexBuffer(s_vbo, s_vertices.material, n * 4,
                                 (int)offsetof(LiquidCaptureCPUVertices, material));
            rlUpdateVertexBuffer(s_vbo, s_vertices.radii, n * (int)sizeof(Vector4),
                                 (int)offsetof(LiquidCaptureCPUVertices, radii));
        } else {
            rlUpdateVertexBuffer(s_vbo, &s_vertices, sizeof(s_vertices), 0);
        }
    }
    return s_preparedCount;
}

void LiquidCaptureCPU_Draw(bool backDepth)
{
    if (!s_ready || s_vertexCount <= 0) return;
    rlDrawRenderBatchActive();
    BeginShaderMode(s_shader);
    SetShaderValueMatrix(s_shader, s_uniform.projection, s_projection);
    SetShaderValueMatrix(s_shader, s_uniform.inverseProjection, s_inverseProjection);
    SetShaderValueMatrix(s_shader, s_uniform.viewToWorld, s_viewToWorld);
    SetShaderValue(s_shader, s_uniform.orthographic, &s_orthographic, SHADER_UNIFORM_INT);
    int back = backDepth ? 1 : 0;
    SetShaderValue(s_shader, s_uniform.backDepth, &back, SHADER_UNIFORM_INT);
    float match = (backDepth && s_frontDepth.id) ? 1.0f : 0.0f;
    if (s_uniform.matchMaterial >= 0)
        SetShaderValue(s_shader, s_uniform.matchMaterial, &match, SHADER_UNIFORM_FLOAT);
    int slot = 0;
    if (s_uniform.frontDepth >= 0)
        SetShaderValue(s_shader, s_uniform.frontDepth, &slot, SHADER_UNIFORM_INT);
    rlActiveTextureSlot(0);
    if (match > 0.5f) rlEnableTexture(s_frontDepth.id);
    rlEnableShader(s_shader.id);
    rlEnableVertexArray(s_vao);
    rlDrawVertexArray(0, s_vertexCount);
    rlDisableVertexArray();
    rlDisableShader();
    if (match > 0.5f) rlDisableTexture();
    EndShaderMode();
}

int LiquidCaptureCPU_GetPreparedCount(void) { return s_preparedCount; }
