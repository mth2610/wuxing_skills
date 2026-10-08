"""Compile the production Guided inspector keyboard blocks with deterministic input."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class GuidedInspectorInputTest(unittest.TestCase):
    def test_owned_frames_release_and_expire_without_draw_updates(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / "sandbox/vfx_test.c").read_text()
        begin = source.index("#define VFXTEST_GUIDED_FRAME_COUNT")
        end = source.index("static void VFXTest_FireGuidedMotion", begin)
        helper = source[begin:end]
        fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <stddef.h>
typedef unsigned MotionFrameHandle;
typedef struct {float x,y,z;} Vector3;
typedef struct {float x,y,z;} Matrix;
typedef struct {float inverseMassKg,gravityScale;} MotionBodyProfile;
static int destroyed, updated;
static bool seen[17];
static void MotionFrame_Destroy(MotionFrameHandle h) {
    assert(h>0 && h<=16 && !seen[h]); seen[h]=true; ++destroyed;
}
static bool MotionFrame_Update(MotionFrameHandle h,Matrix m,float dt,bool jump) {
    assert(h>0 && h<=16 && !seen[h] && dt>0 && !jump);
    assert(isfinite(m.x) && isfinite(m.y) && m.y>=0.2f); ++updated; return true;
}
static Matrix MatrixTranslate(float x,float y,float z) {return (Matrix){x,y,z};}
static Vector3 Vector3Add(Vector3 a,Vector3 b) {return (Vector3){a.x+b.x,a.y+b.y,a.z+b.z};}
static Vector3 Vector3Scale(Vector3 a,float k) {return (Vector3){a.x*k,a.y*k,a.z*k};}
static Vector3 MotionBody_AdvanceVelocity(Vector3 v,const MotionBodyProfile *p,
    Vector3 a,Vector3 f,Vector3 air,float dt) {
    (void)a;(void)f;(void)air; v.y-=9.81f*p->gravityScale*dt; return v;
}
''' + helper + r'''
int main(void) {
    s_guidedFrames[0]=(VFXTest_GuidedFrame){.frame=1,.position={0,1,0},.velocity={1,4,0},.active=true,.release=true};
    s_guidedFrames[1]=(VFXTest_GuidedFrame){.frame=2,.position={0,1,0},.velocity={1,4,0},.active=true};
    VFXTest_UpdateGuidedFrames(0); VFXTest_UpdateGuidedFrames(NAN); assert(updated==0);
    for(int i=0;i<71;++i) VFXTest_UpdateGuidedFrames(1.0f/60);
    assert(s_guidedFrames[0].active && s_guidedFrames[1].active && destroyed==0);
    VFXTest_UpdateGuidedFrames(0.02f);
    assert(s_guidedFrames[0].active && s_guidedFrames[0].frame==0);
    assert(s_guidedFrames[1].active && destroyed==1);
    assert(s_guidedFrames[1].position.x>0.5f);
    for(int i=0;i<180;++i) VFXTest_UpdateGuidedFrames(1.0f/60);
    assert(!s_guidedFrames[0].active && !s_guidedFrames[1].active && destroyed==2);
    s_guidedFrames[3]=(VFXTest_GuidedFrame){.frame=4,.origin={1,2,3},.active=true,.kinematic=true};
    VFXTest_UpdateGuidedFrames(0.5f);
    float a=0.5f*1.35f;
    assert(fabsf(s_guidedFrames[3].position.x-(1+3*sinf(a)))<0.00001f);
    assert(fabsf(s_guidedFrames[3].position.y-(3.5f+0.45f*sinf(a*0.7f)))<0.00001f);
    assert(fabsf(s_guidedFrames[3].position.z-(3+2.1f*cosf(a*1.3f)))<0.00001f);
    VFXTest_UpdateGuidedFrames(3.51f); assert(!s_guidedFrames[3].active && destroyed==3);
    s_guidedFrames[2]=(VFXTest_GuidedFrame){.frame=3,.active=true};
    VFXTest_ClearGuidedFrames(); assert(destroyed==4);
    VFXTest_ClearGuidedFrames(); assert(destroyed==4);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="guided-frame-lifetime-") as directory:
            code = Path(directory) / "test.c"
            binary = Path(directory) / "test"
            code.write_text(fixture)
            subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", str(code), "-lm", "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
        draw = source[source.index("void VFXTest_Draw3D(void)"):]
        self.assertNotIn("VFXTest_UpdateGuidedFrames(", draw)
        self.assertIn("VFXTest_UpdateGuidedFrames(TimeFX_RawDelta());", source)

    def test_capture_repeat_schedule_is_bounded_and_frame_deterministic(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / "sandbox/vfx_test.c").read_text()
        begin = source.index("static int VFXTest_GuidedCaptureInt(")
        end = source.index("// @gen:newfx_guided_state end", begin)
        production = source[begin:end]
        fixture = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#define LOG_WARNING 1
#define LOG_INFO 2
#define TraceLog(...) ((void)0)
typedef struct { float x,y,z; } Vector3;
static bool selected=true, s_guidedCaptureActive;
static int s_guidedCaptureRemaining,s_guidedCaptureFrame,s_guidedCaptureInterval;
static Vector3 s_guidedCaptureSource={1,2,3},s_guidedCaptureTarget={4,5,6};
static int casts=1, frames[4];
static bool VFXTest_IsNewFxNamed(const char *name) { (void)name; return selected; }
static void VFXTest_FireGuidedMotion(Vector3 source,Vector3 target) {
    assert(s_guidedCaptureActive && source.x==1 && target.z==6);
    assert(casts>=1 && casts<=4); frames[casts-1]=s_guidedCaptureFrame; ++casts;
}
''' + production + r'''
int main(void) {
    unsetenv("WUXING_GUIDED_CASTS"); unsetenv("WUXING_GUIDED_CAST_INTERVAL_FRAMES");
    VFXTest_BeginGuidedCaptureRepeats();
    for(int i=0;i<30;i++) VFXTest_UpdateGuidedCaptureRepeats();
    assert(casts==1);
    setenv("WUXING_GUIDED_CASTS","5",1);
    VFXTest_BeginGuidedCaptureRepeats();
    for(int i=0;i<30;i++) VFXTest_UpdateGuidedCaptureRepeats();
    assert(casts==5 && frames[0]==6 && frames[1]==12 && frames[2]==18 && frames[3]==24);
    assert(!s_guidedCaptureActive);
    setenv("WUXING_GUIDED_CASTS","17",1);
    VFXTest_BeginGuidedCaptureRepeats(); assert(s_guidedCaptureRemaining==0);
    setenv("WUXING_GUIDED_CASTS","5garbage",1);
    VFXTest_BeginGuidedCaptureRepeats(); assert(s_guidedCaptureRemaining==0);
    setenv("WUXING_GUIDED_CASTS","5",1);
    setenv("WUXING_GUIDED_CAST_INTERVAL_FRAMES","0",1);
    VFXTest_BeginGuidedCaptureRepeats(); assert(s_guidedCaptureInterval==6);
    selected=false; VFXTest_UpdateGuidedCaptureRepeats(); assert(s_guidedCaptureRemaining==0);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="guided-capture-repeat-") as directory:
            code = Path(directory) / "test.c"
            binary = Path(directory) / "test"
            code.write_text(fixture)
            subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", str(code), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

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
    float speed, swirlSpeed, turbulenceSpeed, duration, guideRadius, maxForceNewtons, drag, formationRadius, emitDuration, gravityScale, massKg, trailLength, trailWidth;
    int count, output, trailCount, motionPattern, trailNodes, material, trailStyle, guidancePreset, trailMotion;
} VFX_GuidedMotionConfig;
enum { VFX_GUIDED_PARTICLES, VFX_GUIDED_TRAILS, VFX_GUIDED_BOTH };
enum { VFX_GUIDED_ROUTE, VFX_GUIDED_ORBIT, VFX_GUIDED_AIRFLOW };
enum { GUIDE_TIGHT = 3 };
enum { VFX_GUIDED_TRAIL_MOTION_FIELD, VFX_GUIDED_TRAIL_MOTION_SPLINE, VFX_GUIDED_TRAIL_MOTION_GUIDED };
typedef int VC_MaterialId;
enum { VC_MAT_WATER = 3, VC_MAT_LIGHTNING, VC_MAT_METAL, VC_MAT_FIRE };
enum { VFX_GUIDED_TRAIL_PLAIN, VFX_GUIDED_TRAIL_ENERGY_SILK, VFX_GUIDED_TRAIL_SMOKE_WISP, VFX_GUIDED_TRAIL_EMBER_FILAMENT, VFX_GUIDED_TRAIL_WATER_STREAM };
static VFX_GuidedMotionConfig s_liveGuidedMotionConfig;
static bool s_liveGuidedMotionConfigInit;
static int s_guidedFixturePreset;
static VFX_GuidedMotionConfig VFX_GuidedMotion_DefaultConfig(void) {
    return (VFX_GuidedMotionConfig){.speed=12,.swirlSpeed=14.4f,.turbulenceSpeed=9.6f,.duration=4,.output=VFX_GUIDED_BOTH};
}
static float s_guidedSwirlRatio = 1.2f, s_guidedTurbulenceRatio = .8f;
static float s_guidedRatioReferenceSpeed = 12;
static VFX_ParamDef s_inspectorParams[4];
static int s_inspectorParamCount = 3, s_inspectorSelectedParam = 2, refreshes;
static int VFX_GuidedMotion_GetParams(VFX_GuidedMotionConfig *cfg, VFX_ParamDef *out, int max) {
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
    assert(s_liveGuidedMotionConfigInit && s_guidedFixturePreset==0);
    Near(s_liveGuidedMotionConfig.duration,4);
    assert(s_liveGuidedMotionConfig.output==VFX_GUIDED_BOTH);
    VFXTest_SetGuidedPreset(6); assert(s_liveGuidedMotionConfig.output==VFX_GUIDED_PARTICLES);
    VFXTest_SetGuidedPreset(7); assert(s_liveGuidedMotionConfig.output==VFX_GUIDED_TRAILS);
    assert(s_liveGuidedMotionConfig.trailMotion==VFX_GUIDED_TRAIL_MOTION_FIELD);
    VFXTest_SetGuidedPreset(8); assert(s_liveGuidedMotionConfig.output==VFX_GUIDED_BOTH);
    VFXTest_SetGuidedPreset(9);
    assert(s_liveGuidedMotionConfig.output==VFX_GUIDED_BOTH);
    assert(s_liveGuidedMotionConfig.count==192 && s_liveGuidedMotionConfig.trailCount==8);
    Near(s_liveGuidedMotionConfig.emitDuration,1.5f);
    Near(s_liveGuidedMotionConfig.speed,12);
    VFXTest_SetGuidedPreset(10);
    assert(s_liveGuidedMotionConfig.output==VFX_GUIDED_TRAILS);
    assert(s_liveGuidedMotionConfig.count==192 && s_liveGuidedMotionConfig.trailCount==8);
    Near(s_liveGuidedMotionConfig.emitDuration,1.5f);
    Near(s_liveGuidedMotionConfig.speed,12);
    VFXTest_SetGuidedPreset(11);
    assert(s_liveGuidedMotionConfig.motionPattern==VFX_GUIDED_ORBIT);
    assert(s_liveGuidedMotionConfig.output==VFX_GUIDED_TRAILS);
    assert(s_liveGuidedMotionConfig.trailCount==8);
    Near(s_liveGuidedMotionConfig.emitDuration,1.5f);
    VFXTest_SetGuidedPreset(12);
    assert(s_liveGuidedMotionConfig.motionPattern==VFX_GUIDED_AIRFLOW);
    assert(s_liveGuidedMotionConfig.trailNodes==24);
    assert(s_liveGuidedMotionConfig.trailCount==1 && s_liveGuidedMotionConfig.material==VC_MAT_WATER);
    Near(s_liveGuidedMotionConfig.emitDuration,0);
    Near(s_liveGuidedMotionConfig.trailWidth,0.16f);
    Near(s_liveGuidedMotionConfig.gravityScale,0.05f);
    Near(s_liveGuidedMotionConfig.massKg,0.004f);
    VFXTest_SetGuidedPreset(13);
    assert(s_liveGuidedMotionConfig.motionPattern==VFX_GUIDED_AIRFLOW);
    assert(s_liveGuidedMotionConfig.trailCount==1 && s_liveGuidedMotionConfig.material==VC_MAT_WATER);
    Near(s_liveGuidedMotionConfig.emitDuration,0);
    Near(s_liveGuidedMotionConfig.trailWidth,0.16f);
    for (int preset=14; preset<=17; ++preset) {
        VFXTest_SetGuidedPreset(preset);
        assert(s_liveGuidedMotionConfig.trailStyle==preset-13);
        assert(s_liveGuidedMotionConfig.output==VFX_GUIDED_TRAILS);
        assert(s_liveGuidedMotionConfig.motionPattern==VFX_GUIDED_AIRFLOW);
        assert(s_liveGuidedMotionConfig.trailCount==1);
        Near(s_liveGuidedMotionConfig.emitDuration,0);
        Near(s_liveGuidedMotionConfig.trailWidth,0.18f);
        Near(s_liveGuidedMotionConfig.duration,4);
    }
    VFXTest_SetGuidedPreset(18);
    assert(s_liveGuidedMotionConfig.trailMotion==VFX_GUIDED_TRAIL_MOTION_SPLINE);
    assert(s_liveGuidedMotionConfig.output==VFX_GUIDED_TRAILS);
    assert(s_liveGuidedMotionConfig.trailCount==1 && s_liveGuidedMotionConfig.trailNodes==24);
    assert(s_liveGuidedMotionConfig.motionPattern==VFX_GUIDED_ROUTE);
    assert(s_liveGuidedMotionConfig.trailStyle==VFX_GUIDED_TRAIL_ENERGY_SILK);
    assert(s_liveGuidedMotionConfig.guidancePreset==GUIDE_TIGHT);
    Near(s_liveGuidedMotionConfig.trailLength,0.8f);
    Near(s_liveGuidedMotionConfig.speed,2.0f);
    Near(s_liveGuidedMotionConfig.swirlSpeed,0.0f);
    Near(s_liveGuidedMotionConfig.turbulenceSpeed,0.0f);
    Near(s_liveGuidedMotionConfig.gravityScale,0.0f);
    Near(s_liveGuidedMotionConfig.formationRadius,0.0f);
    VFXTest_SetGuidedPreset(19);
    assert(s_liveGuidedMotionConfig.trailMotion==VFX_GUIDED_TRAIL_MOTION_GUIDED);
    assert(s_liveGuidedMotionConfig.output==VFX_GUIDED_TRAILS);
    assert(s_liveGuidedMotionConfig.trailCount==1 && s_liveGuidedMotionConfig.trailNodes==24);
    assert(s_liveGuidedMotionConfig.motionPattern==VFX_GUIDED_ROUTE);
    assert(s_liveGuidedMotionConfig.trailStyle==VFX_GUIDED_TRAIL_ENERGY_SILK);
    Near(s_liveGuidedMotionConfig.trailLength,0.8f);
    Near(s_liveGuidedMotionConfig.speed,2.0f);
    Near(s_liveGuidedMotionConfig.swirlSpeed,1.2f);
    Near(s_liveGuidedMotionConfig.turbulenceSpeed,0.6f);
    Near(s_liveGuidedMotionConfig.guideRadius,0.45f);
    Near(s_liveGuidedMotionConfig.formationRadius,0.15f);
    VFXTest_SetGuidedPreset(3); assert(s_liveGuidedMotionConfig.count==0 && s_liveGuidedMotionConfig.trailCount==0);
    VFXTest_SetGuidedPreset(0);
    VFX_GuidedMotion_GetParams(&s_liveGuidedMotionConfig,s_inspectorParams,4);
    VFXTest_UIEditParameter(0,1);
    Near(s_liveGuidedMotionConfig.speed,24); Near(s_liveGuidedMotionConfig.swirlSpeed,28.8f);
    Near(s_liveGuidedMotionConfig.turbulenceSpeed,19.2f);
    assert(refreshes==2 && s_inspectorSelectedParam==2 && s_inspectorParamCount==3);
    VFXTest_UIEditParameter(0,-1); VFXTest_UIEditParameter(0,-1);
    Near(s_liveGuidedMotionConfig.speed,0); Near(s_liveGuidedMotionConfig.swirlSpeed,0);
    Near(s_liveGuidedMotionConfig.turbulenceSpeed,0);
    VFXTest_UIEditParameter(0,-1); /* Clamping at zero must not erase ratios. */
    VFXTest_UIEditParameter(0,1);
    Near(s_liveGuidedMotionConfig.swirlSpeed,14.4f); Near(s_liveGuidedMotionConfig.turbulenceSpeed,9.6f);
    VFXTest_UIEditParameter(0,-1);
    VFXTest_UIEditParameter(1,-1); /* Author stationary negative swirl using remembered 12 m/s. */
    VFXTest_UIEditParameter(2,1);
    VFXTest_UIEditParameter(0,1);
    Near(s_liveGuidedMotionConfig.swirlSpeed,-12); Near(s_liveGuidedMotionConfig.turbulenceSpeed,12);
    VFXTest_UIEditParameter(1,1); VFXTest_UIEditParameter(2,-1);
    VFXTest_UIEditParameter(0,1);
    Near(s_liveGuidedMotionConfig.swirlSpeed,0); Near(s_liveGuidedMotionConfig.turbulenceSpeed,0);
    float unrelated=2;
    s_inspectorParams[3]=(VFX_ParamDef){.type=VFX_PARAM_FLOAT,.valPtr=&unrelated,.minFloat=0,.maxFloat=10,.stepFloat=1};
    int priorRefreshes=refreshes;
    VFXTest_UIEditParameter(3,1);
    Near(unrelated,3); assert(refreshes==priorRefreshes);
    Near(s_liveGuidedMotionConfig.speed,24);
    VFXTest_SetGuidedPreset(0);
    Near(s_liveGuidedMotionConfig.speed,12); Near(s_liveGuidedMotionConfig.swirlSpeed,14.4f);
    Near(s_liveGuidedMotionConfig.turbulenceSpeed,9.6f);
    VFX_GuidedMotion_GetParams(&s_liveGuidedMotionConfig,s_inspectorParams,4);
    VFXTest_UIEditParameter(0,1);
    Near(s_liveGuidedMotionConfig.swirlSpeed,28.8f); Near(s_liveGuidedMotionConfig.turbulenceSpeed,19.2f);
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
#define VFXTEST_GUIDED_PRESET_COUNT 11
static int pressed, held, value, edits, resets;
static int s_guidedFixturePreset, s_inspectorSelectedParam = 1;
static int s_inspectorParamCount = 2;
static const char *s_guidedFixturePresetNames[] = {"A","B","C","D","E","F","G","H","I","J","K"};
static struct { const char *name; } s_inspectorParams[] = {{"A"},{"B"}};
static bool IsKeyPressed(int key) { return pressed == key; }
static bool IsKeyDown(int key) { return held == key; }
static bool VFXTest_IsNewFxNamed(const char *name) { (void)name; return true; }
static void VFXTest_SetGuidedPreset(int preset) { s_guidedFixturePreset = preset; ++resets; }
static void VFXTest_RefreshInspectorParams(bool force) { (void)force; s_inspectorSelectedParam = 0; }
static void VFXTest_UIRevealSelectedParameter(void) {}
static void VFXTest_UpdateGuidedCaptureRepeats(void) {}
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
    assert(s_guidedFixturePreset == 10 && resets == 3 && edits == 2);
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
