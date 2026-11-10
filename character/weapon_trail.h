#ifndef CHARACTER_WEAPON_TRAIL_H
#define CHARACTER_WEAPON_TRAIL_H

#include "raylib.h"
#include <stdbool.h>

#define WEAPON_TRAIL_MAX_POINTS 48

typedef struct {
    Vector3 base;
    Vector3 tip;
    float   age;
    float   alpha;
} WeaponTrailPoint;

typedef struct {
    WeaponTrailPoint points[WEAPON_TRAIL_MAX_POINTS];
    int              count;
    Vector3          prevBase;
    Vector3          prevTip;
    bool             hasPrev;
    float            minSpeed;       // Speed in m/s to trigger trail (e.g. 2.0f)
    float            maxAge;         // Trail duration in seconds (e.g. 0.22f)
    Color            glowColor;      // Element/weapon glow aura tint
    Color            coreColor;      // Hot razor cutting edge tint
    bool             enabled;
} WeaponTrail;

// Initializes default weapon trail settings
void WeaponTrail_Init(WeaponTrail *trail);

// Resets/clears active trail points
void WeaponTrail_Reset(WeaponTrail *trail);

// Sets custom element colors (aura glow & razor core)
void WeaponTrail_SetColors(WeaponTrail *trail, Color glowColor, Color coreColor);

// Updates and samples new blade ribbon points if moving or attacking
void WeaponTrail_Sample(WeaponTrail *trail, Matrix weaponWorldMat, Vector3 localBase, Vector3 localTip, float dt, bool forceEmit);

// Fades remaining points when weapon is idle or unequipped
void WeaponTrail_Fade(WeaponTrail *trail, float dt);

// Renders the blade ribbon using sweep slash emission shader & texture
void WeaponTrail_Draw(const WeaponTrail *trail);

#endif // CHARACTER_WEAPON_TRAIL_H
