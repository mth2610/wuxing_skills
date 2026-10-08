/* Compile the canonical UI with deterministic input and rendering stubs.
 * Exercise hit testing, release capture, scrolling and real parameter edits. */
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "../../core/composition/common/vc_params.h"

typedef struct { float x, y; } Vector2;
typedef struct { float x, y, z; } Vector3;
typedef struct { float x, y, width, height; } Rectangle;
typedef struct { unsigned char r, g, b, a; } Color;
#define SKYBLUE ((Color){102,191,255,255})
#define RAYWHITE ((Color){245,245,245,255})
#define LIGHTGRAY ((Color){200,200,200,255})
#define GRAY ((Color){130,130,130,255})
#define BLACK ((Color){0,0,0,255})
#define MOUSE_BUTTON_LEFT 0
#define LOG_INFO 1
#define TEST_CAT_MESH 0
#define TEST_CAT_NEWFX 1
#define TEST_CAT_COUNT 2
#define NEWFX_CAT_COUNT 7
static int s_testWidth = 975, s_testHeight = 643;
static Vector2 s_testMouse;
static bool s_testDown, s_testReleased;
static float s_testWheel;
static int s_testCasts;
typedef enum { FOG_MODE_VOLUMETRIC, FOG_MODE_HEIGHT, FOG_MODE_OFF } FogRenderMode;
static FogRenderMode s_testFogMode = FOG_MODE_HEIGHT;
static FogRenderMode Fog_GetRenderMode(void) { return s_testFogMode; }
static void Fog_SetRenderMode(FogRenderMode mode) { s_testFogMode = mode; }
static bool s_hideAllUI, s_isPanelOpen, s_clickedOnUI, s_hideDebugOverlays = true;
static bool s_isPlayingMesh = true;
static int s_inspectorParamCount, s_inspectorSelectedParam, s_testCategory = TEST_CAT_NEWFX;
static int s_testIndex, s_newfxFilter = 6;
static Vector3 s_prefabStartPos;
static float s_meshTime;
static VFX_ParamDef s_inspectorParams[32];
static const char *s_meshNames[] = {"Mesh"}, *s_newFxNames[] = {"Test"};
static const int s_newFxCategories[] = {6};
static const char *s_guidedFixturePresetNames[] = {"Preset"};
static int s_guidedFixturePreset;
static int GetScreenWidth(void) { return s_testWidth; }
static int GetScreenHeight(void) { return s_testHeight; }
static Vector2 GetMousePosition(void) { return s_testMouse; }
static float GetMouseWheelMove(void) { return s_testWheel; }
static bool IsMouseButtonDown(int button) { (void)button; return s_testDown; }
static bool IsMouseButtonReleased(int button) { (void)button; return s_testReleased; }
static bool CheckCollisionPointRec(Vector2 p, Rectangle r)
{ return p.x >= r.x && p.x < r.x + r.width && p.y >= r.y && p.y < r.y + r.height; }
static int VFXTest_NewFxCount(void) { return 1; }
static bool VFXTest_IsNewFxNamed(const char *name) { (void)name; return false; }
static void VFXTest_StopFixtures(void) {}
static bool VFXTest_FireNewFx(int index, Vector3 pos) { (void)index; (void)pos; ++s_testCasts; return true; }
static void VFXTest_RefreshInspectorParams(bool force) { (void)force; }
#define VFX_TEST_MAX_INSPECTOR_PARAMS 32
static int VFXTest_GuidedParameterEdited(const VFX_ParamDef *param, float before,
                                       VFX_ParamDef *params, int count, int max)
{ (void)param; (void)before; (void)params; (void)max; return count; }
static const char *TextFormat(const char *format, ...)
{ static char buffers[4][512]; static int slot; char *buffer = buffers[slot++ % 4]; va_list args; va_start(args, format); vsnprintf(buffer, 512, format, args); va_end(args); return buffer; }
static Color ColorAlpha(Color c, float alpha) { c.a = (unsigned char)(alpha * 255); return c; }
#define VFX_MeshParticleVariant_Name(x) "Variant"
#define MotionRibbonFixturePresetName(x) "Variant"
#define VFX_SmokeStyle_Name(x) "Variant"
#define VFX_MeshSurfaceAuraVariant_Name(x) "Variant"
#define VFX_DecalVariant_Name(x) "Variant"
#define VFX_SurfaceParticleRingVariant_Name(x) "Variant"
#define VFX_ImpactDustVariant_Name(x) "Variant"
#define VFX_FlameStyle_Name(x) "Variant"
#define VFXTest_SurfaceImpactReceiverName(x) "Variant"
static void TraceLog(int level, const char *format, ...) { (void)level; (void)format; }
static void BeginScissorMode(int x, int y, int width, int height) { (void)x; (void)y; (void)width; (void)height; }
static void EndScissorMode(void) {}
static void DrawText(const char *text, int x, int y, int size, Color color) { (void)text; (void)x; (void)y; (void)size; (void)color; }
static void DrawRectangle(int x, int y, int width, int height, Color color) { (void)x; (void)y; (void)width; (void)height; (void)color; }
static void DrawRectangleRounded(Rectangle rect, float roundness, int segments, Color color) { (void)rect; (void)roundness; (void)segments; (void)color; }
static void DrawRectangleRoundedLines(Rectangle rect, float roundness, int segments, Color color) { (void)rect; (void)roundness; (void)segments; (void)color; }
#include "../../scripts/templates/vfx_test_ui.c.in"

static void Click(Rectangle rect)
{
    s_testMouse = (Vector2){rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f};
    s_testDown = true; s_testReleased = false;
    VFXTest_UIHandleInput((Vector3){0});
    assert(s_clickedOnUI && VFXTest_IsPointerOverUI());
    s_testDown = false; s_testReleased = true;
    VFXTest_UIHandleInput((Vector3){0});
    assert(s_clickedOnUI);
    s_testReleased = false;
}

int main(void)
{
    float value = 0.5f; int integer = 1, enumeration = 0; bool enabled = false;
    s_inspectorParamCount = 4;
    s_inspectorParams[0] = (VFX_ParamDef){.name="Float",.group="Body",.type=VFX_PARAM_FLOAT,.valPtr=&value,.minFloat=0,.maxFloat=1,.stepFloat=.25f};
    s_inspectorParams[1] = (VFX_ParamDef){.name="Int",.group="Body",.type=VFX_PARAM_INT,.valPtr=&integer,.minInt=0,.maxInt=2};
    s_inspectorParams[2] = (VFX_ParamDef){.name="Enum",.group="Body",.type=VFX_PARAM_ENUM,.valPtr=&enumeration,.minInt=0,.maxInt=2};
    s_inspectorParams[3] = (VFX_ParamDef){.name="Bool",.group="Body",.type=VFX_PARAM_BOOL,.valPtr=&enabled};
    const int sizes[][2] = {{975,643},{1280,720},{360,640}};
    for (int i = 0; i < 3; ++i) {
        s_testWidth = sizes[i][0]; s_testHeight = sizes[i][1];
        VFXTest_UILayout ui = VFXTest_UIGetLayout();
        if (s_testWidth >= 1100) {
            assert(ui.header.height == 40);
            assert(ui.cameraStatus.y == ui.header.y);
            for (int metric = 0; metric < 3; ++metric)
                assert(ui.metrics[metric].y == ui.header.y && ui.metrics[metric].height == ui.header.height);
            assert(ui.cameraStatus.x + ui.cameraStatus.width <= VFXTest_UIHeaderButton(ui, 0).x);
        }
        Rectangle fog = VFXTest_UIFogButton(ui);
        assert(fog.x >= 0 && fog.x + fog.width <= s_testWidth);
        assert(fog.x + fog.width <= (s_testWidth >= 1100 ? VFXTest_UIHeaderButton(ui, 0).x : VFXTest_UITiltButton(ui, -1).x));
        assert(ui.inspector.x >= 0 && ui.inspector.x + ui.inspector.width <= s_testWidth);
        assert(ui.inspector.y + ui.inspector.height <= s_testHeight);
        Rectangle row = VFXTest_UIInspectorRow(ui, 0);
        assert(VFXTest_UIParameterButton(row, -1).x + 30 <= row.x + row.width);
        assert(VFXTest_UIParameterButton(row, 1).x + 30 <= row.x + row.width);
    }
    s_testWidth = 975; s_testHeight = 643;
    VFXTest_UILayout ui = VFXTest_UIGetLayout();
    VFXTest_SetPerformanceTelemetry(63.0f, 15.87f, 19.25f);
    VFXTest_SetCameraTelemetry(31.0f, 1.6f, 12.5f);
    assert(s_vfxTelemetryTilt == 31.0f && s_vfxTelemetryZoom == 1.6f && s_vfxTelemetryDistance == 12.5f);
    Click(VFXTest_UITiltButton(ui, 1)); assert(VFXTest_ConsumeCameraTiltStep() == 1);
    assert(VFXTest_ConsumeCameraTiltStep() == 0);
    Click(VFXTest_UITiltButton(ui, -1)); assert(VFXTest_ConsumeCameraTiltStep() == -1);
    Click(VFXTest_UIFogButton(ui)); assert(Fog_GetRenderMode() == FOG_MODE_VOLUMETRIC);
    Click(VFXTest_UIFogButton(ui)); assert(Fog_GetRenderMode() == FOG_MODE_OFF);
    Click(VFXTest_UIFogButton(ui)); assert(Fog_GetRenderMode() == FOG_MODE_HEIGHT);
    Click(VFXTest_UIParameterButton(VFXTest_UIInspectorRow(ui, 0), 1)); assert(value == .75f);
    Click(VFXTest_UIParameterButton(VFXTest_UIInspectorRow(ui, 1), 1)); assert(integer == 2 && s_inspectorSelectedParam == 1);
    Click(VFXTest_UIParameterButton(VFXTest_UIInspectorRow(ui, 1), 1)); assert(integer == 2);
    enumeration = 2;
    Click(VFXTest_UIParameterButton(VFXTest_UIInspectorRow(ui, 2), 1)); assert(enumeration == 0);
    Click(VFXTest_UIParameterButton(VFXTest_UIInspectorRow(ui, 2), -1)); assert(enumeration == 2);
    Click(VFXTest_UIParameterButton(VFXTest_UIInspectorRow(ui, 3), 1)); assert(enabled);
    Click(VFXTest_UIParameterButton(VFXTest_UIInspectorRow(ui, 3), -1)); assert(!enabled);
    value = 1; VFXTest_UIEditParameter(0, 1); assert(value == 1);
    value = 0; VFXTest_UIEditParameter(0, -1); assert(value == 0);
    VFXTest_UIDraw();
    s_inspectorParamCount = 23;
    s_inspectorSelectedParam = 22; VFXTest_UIRevealSelectedParameter();
    ui = VFXTest_UIGetLayout();
    assert(s_vfxInspectorScroll <= 22 && s_vfxInspectorScroll + ui.inspectorRows > 22);
    s_testMouse = (Vector2){ui.inspector.x + 5,ui.inspector.y + 5};
    assert(VFXTest_IsPointerOverUI());
    s_testDown = true; VFXTest_UIHandleInput((Vector3){0});
    s_testMouse = (Vector2){100,400}; assert(VFXTest_IsPointerOverUI());
    s_testDown = false; s_testReleased = true; VFXTest_UIHandleInput((Vector3){0});
    assert(s_clickedOnUI); s_testReleased = false; assert(!VFXTest_IsPointerOverUI());
    s_hideAllUI = true; assert(!VFXTest_IsPointerOverUI()); s_hideAllUI = false;
    s_hideDebugOverlays = false; s_testMouse = (Vector2){100,172}; assert(VFXTest_IsPointerOverUI());
    s_hideDebugOverlays = true; assert(!VFXTest_IsPointerOverUI());
    assert(s_testCasts == 0);
    puts("VFX UI layout, release capture and float/int/enum/bool edits: PASS");
    return 0;
}
