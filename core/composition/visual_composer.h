#ifndef VISUAL_COMPOSER_H
#define VISUAL_COMPOSER_H

#include "raylib.h"
#include "core/skill_helper.h" // for EffectPresetType
#include "core/impact_burst.h" // for ImpactBurstConfig

// 1. Spawning Smoke Puff
void VFX_ComposeSmokePuff(Vector3 pos, float size);

// 2. Spawning Smoke Trail
void VFX_ComposeSmokeTrail(Vector3 start, Vector3 end, float duration);

// 3. Spawning Ground Fissure
void VFX_ComposeFissure(Vector3 start, Vector3 end, float width);

// 4. Spawning Lightning Beam
void VFX_ComposeLightningBeam(Vector3 start, Vector3 end, float duration);

// 5. Spawning Impact Effect (elemental: water, fire, wood, earth, metal, taiji)
void VFX_ComposeImpact(Vector3 pos, EffectPresetType preset, float scale);

// 6. Spawning Cast Effect (casting/windup elements)
void VFX_ComposeCast(Vector3 pos, EffectPresetType preset, float scale);

// 7. Spawning Projectile Trail
int VFX_ComposeProjectileTrail(Vector3 start, Vector3 target, EffectPresetType preset, float scale, float speed);

// 8. Triggering full generic 4-step Impact Burst (from impact_burst.h)
void VFX_ComposeTriggerImpactBurst(Vector3 pos, float sizeScale, const ImpactBurstConfig *cfg);

#endif // VISUAL_COMPOSER_H
