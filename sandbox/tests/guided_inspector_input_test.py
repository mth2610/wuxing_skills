"""Compile the production Guided inspector keyboard blocks with deterministic input."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class GuidedInspectorInputTest(unittest.TestCase):
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
