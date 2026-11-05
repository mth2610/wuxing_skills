#ifndef GROUND_SHADOW_H
#define GROUND_SHADOW_H

// Real Shading P6 — opt-in shadow receiver for raw immediate-mode ground
// draws (rlBegin(RL_TRIANGLES)/rlVertex3f/rlColor4ub, e.g. default_arena.c's
// floor plate + zone discs, verdant_path.c's zone discs).

void GroundShadow_UpdateFrame(void); // once per frame (main.c, alongside SurfaceMaterial_UpdateFrame)
void GroundShadow_Begin(void);
void GroundShadow_End(void);

#endif // GROUND_SHADOW_H