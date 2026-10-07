"""Compile the production Guided inspector keyboard blocks with deterministic input."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class GuidedInspectorInputTest(unittest.TestCase):
    def test_speed_edits_preserve_flow_ratios_and_zero_speed_memory(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / "sandbox/vfx_test.c").read_text()
        begin = source.index("static int VFXTest_GuidedParameterEdited(")
        end = source.index("\nstatic void VFXTest_InitGuidedConfig", begin)
        hook = source[begin:end]
        begin = source.index("static void VFXTest_SetGuidedPreset(")
        end = source.index("/* UI edits couple authored speeds", begin)
        preset = source[begin:end]
        begin = source.index("static void VFXTest_UIEditParameter(")
        end = source.index("\nstatic bool VFXTest_UIHandleInput", begin)
        edit = source[begin:end]
        fixture = r'''
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include "core/composition/common/vc_params.h"
#define VFX_TEST_MAX_INSPECTOR_PARAMS 4
typedef struct {
    float speed, swirlSpeed, turbulenceSpeed, duration, guideRadius, maxForceNewtons, drag, formationRadius;
    int count;
} VFX_GuidedParticleConfig;
static VFX_GuidedParticleConfig s_liveGuidedParticleConfig;
static bool s_liveGuidedParticleConfigInit;
static int s_guidedFixturePreset;
static VFX_GuidedParticleConfig VFX_GuidedParticle_DefaultConfig(void) {
    return (VFX_GuidedParticleConfig){.speed=12,.swirlSpeed=14.4f,.turbulenceSpeed=9.6f,.duration=4};
}
static float s_guidedSwirlRatio = 1.2f, s_guidedTurbulenceRatio = .8f;
static float s_guidedRatioReferenceSpeed = 12;
static VFX_ParamDef s_inspectorParams[4];
static int s_inspectorParamCount = 3, s_inspectorSelectedParam = 2, refreshes;
static int VFX_GuidedParticle_GetParams(VFX_GuidedParticleConfig *cfg, VFX_ParamDef *out, int max) {
    assert(max == 4); ++refreshes;
    out[0]=(VFX_ParamDef){.type=VFX_PARAM_FLOAT,.valPtr=&cfg->speed,.minFloat=0,.maxFloat=60,.stepFloat=12};
    out[1]=(VFX_ParamDef){.type=VFX_PARAM_FLOAT,.valPtr=&cfg->swirlSpeed,.minFloat=-120,.maxFloat=120,.stepFloat=12};
    out[2]=(VFX_ParamDef){.type=VFX_PARAM_FLOAT,.valPtr=&cfg->turbulenceSpeed,.minFloat=0,.maxFloat=120,.stepFloat=12};
    return 3;
}
''' + preset + hook + edit + r'''
static void Near(float actual,float expected) { assert(fabsf(actual-expected)<.0001f); }
int main(void) {
    VFXTest_SetGuidedPreset(0);
    assert(s_liveGuidedParticleConfigInit && s_guidedFixturePreset==0);
    Near(s_liveGuidedParticleConfig.duration,4);
    VFX_GuidedParticle_GetParams(&s_liveGuidedParticleConfig,s_inspectorParams,4);
    VFXTest_UIEditParameter(0,1);
    Near(s_liveGuidedParticleConfig.speed,24); Near(s_liveGuidedParticleConfig.swirlSpeed,28.8f);
    Near(s_liveGuidedParticleConfig.turbulenceSpeed,19.2f);
    assert(refreshes==2 && s_inspectorSelectedParam==2 && s_inspectorParamCount==3);
    VFXTest_UIEditParameter(0,-1); VFXTest_UIEditParameter(0,-1);
    Near(s_liveGuidedParticleConfig.speed,0); Near(s_liveGuidedParticleConfig.swirlSpeed,0);
    Near(s_liveGuidedParticleConfig.turbulenceSpeed,0);
    VFXTest_UIEditParameter(0,-1); /* Clamping at zero must not erase ratios. */
    VFXTest_UIEditParameter(0,1);
    Near(s_liveGuidedParticleConfig.swirlSpeed,14.4f); Near(s_liveGuidedParticleConfig.turbulenceSpeed,9.6f);
    VFXTest_UIEditParameter(0,-1);
    VFXTest_UIEditParameter(1,-1); /* Author stationary negative swirl using remembered 12 m/s. */
    VFXTest_UIEditParameter(2,1);
    VFXTest_UIEditParameter(0,1);
    Near(s_liveGuidedParticleConfig.swirlSpeed,-12); Near(s_liveGuidedParticleConfig.turbulenceSpeed,12);
    VFXTest_UIEditParameter(1,1); VFXTest_UIEditParameter(2,-1);
    VFXTest_UIEditParameter(0,1);
    Near(s_liveGuidedParticleConfig.swirlSpeed,0); Near(s_liveGuidedParticleConfig.turbulenceSpeed,0);
    float unrelated=2;
    s_inspectorParams[3]=(VFX_ParamDef){.type=VFX_PARAM_FLOAT,.valPtr=&unrelated,.minFloat=0,.maxFloat=10,.stepFloat=1};
    int priorRefreshes=refreshes;
    VFXTest_UIEditParameter(3,1);
    Near(unrelated,3); assert(refreshes==priorRefreshes);
    Near(s_liveGuidedParticleConfig.speed,24);
    VFXTest_SetGuidedPreset(0);
    Near(s_liveGuidedParticleConfig.speed,12); Near(s_liveGuidedParticleConfig.swirlSpeed,14.4f);
    Near(s_liveGuidedParticleConfig.turbulenceSpeed,9.6f);
    VFX_GuidedParticle_GetParams(&s_liveGuidedParticleConfig,s_inspectorParams,4);
    VFXTest_UIEditParameter(0,1);
    Near(s_liveGuidedParticleConfig.swirlSpeed,28.8f); Near(s_liveGuidedParticleConfig.turbulenceSpeed,19.2f);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="guided-inspector-speed-") as directory:
            code = Path(directory) / "test.c"
            binary = Path(directory) / "test"
            code.write_text(fixture)
            subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-I", str(root), str(code), "-lm", "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_edit_keys_preserve_preset_and_selection(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / "sandbox/vfx_test.c").read_text()
        begin = source.index("// @gen:newfx_guided_input begin")
        end = source.index('        if (VFXTest_IsNewFxNamed("MESH PARTICLE EMITTER"))', begin)
        production_input = source[begin:end]
        fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#define KEY_PERIOD 1
#define KEY_COMMA 2
#define KEY_SLASH 3
#define KEY_LEFT_SHIFT 4
#define KEY_RIGHT_SHIFT 5
#define KEY_CAPS_LOCK 6
#define KEY_TAB 7
#define LOG_INFO 1
#define VFXTEST_GUIDED_PRESET_COUNT 6
static int pressed, held, value, edits, resets;
static int s_guidedFixturePreset, s_inspectorSelectedParam = 1;
static int s_inspectorParamCount = 2;
static const char *s_guidedFixturePresetNames[] = {"A","B","C","D","E","F"};
static struct { const char *name; } s_inspectorParams[] = {{"A"},{"B"}};
static bool IsKeyPressed(int key) { return pressed == key; }
static bool IsKeyDown(int key) { return held == key; }
static bool VFXTest_IsNewFxNamed(const char *name) { (void)name; return true; }
static void VFXTest_SetGuidedPreset(int preset) { s_guidedFixturePreset = preset; ++resets; }
static void VFXTest_RefreshInspectorParams(bool force) { (void)force; s_inspectorSelectedParam = 0; }
static void VFXTest_UIRevealSelectedParameter(void) {}
static void VFXTest_UIEditParameter(int index, int direction) { assert(index == s_inspectorSelectedParam); value += direction; ++edits; }
#define VFX_Param_FormatValue(param,buf,size) ((void)(param), (void)(buf), (void)(size))
static void TraceLog(int level, const char *format, ...) { (void)level; (void)format; }
static void Input(void) {
''' + production_input + r'''
}
int main(void) {
    pressed = KEY_PERIOD; Input();
    assert(value == 1 && edits == 1 && resets == 0 && s_inspectorSelectedParam == 1);
    pressed = KEY_COMMA; Input();
    assert(value == 0 && edits == 2 && resets == 0 && s_inspectorSelectedParam == 1);
    held = KEY_LEFT_SHIFT; pressed = KEY_PERIOD; Input();
    assert(s_guidedFixturePreset == 1 && resets == 1 && edits == 2);
    held = KEY_RIGHT_SHIFT; pressed = KEY_COMMA; Input();
    assert(s_guidedFixturePreset == 0 && resets == 2 && edits == 2);
    pressed = KEY_COMMA; Input();
    assert(s_guidedFixturePreset == 5 && resets == 3 && edits == 2);
    held = 0; pressed = 0; s_inspectorSelectedParam = 1; Input();
    assert(s_inspectorSelectedParam == 1 && resets == 3);
    pressed = KEY_SLASH; Input(); assert(value == 1 && edits == 3);
    held = KEY_LEFT_SHIFT; Input(); assert(value == 0 && edits == 4 && resets == 3);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="guided-inspector-") as directory:
            code = Path(directory) / "test.c"
            binary = Path(directory) / "test"
            code.write_text(fixture)
            subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", str(code), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
