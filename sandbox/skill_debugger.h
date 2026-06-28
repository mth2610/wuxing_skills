#ifndef SKILL_DEBUGGER_H
#define SKILL_DEBUGGER_H

#include "raylib.h"
#include <stdbool.h>

// Visibility flags controlled by the debugger
extern bool g_debugHideDecals;
extern bool g_debugHideMeshes;
extern bool g_debugHideTrails;
extern bool g_debugHideParticles;

// Indicates if the debugger is currently active and freezing time
extern bool g_isDebuggerCapturing;

// Must be called once to ensure screenshots directory exists
void SkillDebugger_Init(void);

// Checks for F12 input to start capture
void SkillDebugger_CheckInput(void);

// Sets visibility flags for the current frame based on capture stage
void SkillDebugger_PreRender(void);

// Draws overlay metadata, saves the screenshot, and advances the stage
void SkillDebugger_PostRender(int activeSkillIndex, Vector3 castPos, Vector3 targetPos);

#endif // SKILL_DEBUGGER_H
