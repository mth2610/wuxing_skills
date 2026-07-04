#ifndef STATUS_VFX_H
#define STATUS_VFX_H

#include "core/skill_helper.h"
#include <stdbool.h>

// Item 29: Status/aura VFX attached to agents.
// Looping particles + VFXLight that follow a moving agent via
// SkillManager_GetAgentPos(). Expiry is graceful (fade-out then free slot).
// Re-attaching the same element refreshes duration instead of stacking.
//
// Usage:
//   int h = StatusVFX_Attach(agentId, EFFECT_PRESET_FIRE_EXPLOSION, 5.0f);
//   // fires automatically; on cleanse:
//   StatusVFX_Detach(h);

#define MAX_STATUS_VFX 32

int  StatusVFX_Attach(int agentId, EffectPresetType element, float duration);
void StatusVFX_Detach(int handle);
void StatusVFX_Update(float dt);
void StatusVFX_Draw(void);  // no-op for emitter-driven slots, reserved for future mesh overlays
void StatusVFX_GetStats(int *active, int *max);

#endif // STATUS_VFX_H
