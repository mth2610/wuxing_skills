#ifndef VFX_PROC_RAY_H
#define VFX_PROC_RAY_H

#include "raylib.h"

// Procedural Ray VFX — managed free-end wave ray.
// One end is fixed (origin), the other whips freely via a traveling wave.
// Supports multiple visual flavors through ProcRayConfig.
//
// Usage pattern:
//   int id = SpawnProcRay(ProcRay_LightningConfig(), scale);   // Init / Cast
//   ProcRay_Update(id, origin, dir, length, scale, dt);        // Update each frame
//   ProcRay_Draw(id, cam);                                      // Draw each frame
//   ProcRay_Kill(id);                                           // on expire / impact
//
// Pool: 32 simultaneous rays.

typedef struct {
    Color colorCore;       // inner ribbon color
    Color colorGlow;       // outer glow pass color
    float glowWidthMult;   // glow width = thickness * glowWidthMult (try 1.4–2.0)
    float waveSpeed;       // rad/s phase advance (try 3–6)
    float amplitudeRatio;  // max displacement = length * amplitudeRatio (try 0.25–0.45)
    float jitterStrength;  // random per-frame crackle — 0=smooth, 1=full electric jitter
    float thickness;       // ribbon half-width in world units (try 1–3 for thin bolts)
    float envelopePow;     // displacement envelope = t^envelopePow (t=0 at origin, 1 at free end)
                           // lower = faster bloom (0.7=lightning diverges early, 1.5=slow whip)
    bool  sharpKinks;      // true=linear interp between waypoints (jagged, lightning-style)
                           // false=Catmull-Rom (smooth curves, energy/wind-style)
    float taperTip;        // width multiplier at the far end (t=1). 0.1=needle tip,
                           // 1.0=uniform. <=0 is treated as 1.0 (backward compat)
    int   branchCount;     // bolts only: forks off the main channel (0–4). Rays ignore it.
    float branchScale;     // branch width/alpha relative to main channel (try 0.4–0.6)
    float flowScrollSpeed; // EnergyFlow only — flow-texture scroll speed (arc-length
                           // units/sec along the channel). Ignored by Ray/Bolt. 0 = no scroll.
} ProcRayConfig;

// Named presets — call these to get a ready-to-use config, tweak fields if needed.
ProcRayConfig ProcRay_LightningConfig(void);     // violet/white, high jitter, fast wave — free-end rays
ProcRayConfig ProcRay_BoltLightningConfig(void); // same colors, low amplitude — for fixed sky→ground bolts
ProcRayConfig ProcRay_EnergyConfig(void);        // cyan/gold, smooth, medium wave — used by VFX_SpawnProcBeam
ProcRayConfig ProcRay_EnergyFlowConfig(void);    // cyan/gold, smooth, scrolling flow texture — for EnergyFlow
ProcRayConfig ProcRay_WindConfig(void);          // white/teal, very low amplitude, slow wave

// ── Free-end ray (one fixed origin, one whipping free end) ─────────────────
int  SpawnProcRay(ProcRayConfig config, float scale);
void ProcRay_SetPhase(int id, float phase);  // offset concurrent rays so they differ
void ProcRay_SetBrightness(int id, float b); // alpha multiplier, 1=normal. >1 saturates toward flash
void ProcRay_Update(int id, Vector3 origin, Vector3 dir, float length, float scale, float dt);
void ProcRay_Draw(int id, Camera3D cam);
void ProcRay_Kill(int id);

// ── Fixed bolt (both endpoints clamped, jagged middle flickers) ────────────
// Use for sky→ground lightning, A→B energy beams — anywhere both ends are pinned.
// Supports child branches (config.branchCount) regenerated on each flicker.
// Pool: 32 simultaneous bolts.
int  SpawnProcBolt(ProcRayConfig config, float scale);
void ProcBolt_SetBrightness(int id, float b); // alpha multiplier — drive strike flash→afterglow
void ProcBolt_Update(int id, Vector3 from, Vector3 to, float scale, float dt);
void ProcBolt_Draw(int id, Camera3D cam);
void ProcBolt_Kill(int id);

// ── Energy Flow (fixed A→B smooth channel, continuously scrolling flow
// texture instead of jagged flicker/whip — mana stream, power conduit,
// channel VFX). Unlike ProcBolt the shape does NOT re-randomize every
// frame — only the flow texture scrolls (config.flowScrollSpeed) — so a
// channel between two moving points tracks smoothly instead of flickering.
// Pool: 16 simultaneous flows.
int  SpawnEnergyFlow(ProcRayConfig config, float scale);
void EnergyFlow_SetBrightness(int id, float b); // alpha multiplier, 1=normal
void EnergyFlow_Update(int id, Vector3 from, Vector3 to, float scale, float dt);
void EnergyFlow_Draw(int id, Camera3D cam);
void EnergyFlow_Kill(int id);

// ── Lightning Trail System (Hợp nhất từ skill_helper) ─────────────────────
int SpawnLightningTrail(Vector3 start, Vector3 target, float scale, float speed);
int SpawnLightningFollowerTrail(Vector3 startPos, float scale, float life);
void Lightning_UpdateFollowerTip(int id, Vector3 tipPos, float scale);

void RegenerateLightningWaypoints(Vector3 *waypoints9, Vector3 from, Vector3 to, float scale);
void RegenerateLightningRay(Vector3 *waypoints9, Vector3 origin, Vector3 direction,
                             float length, float phase, float amplitude, float scale);
void DrawLightningBolt(const Vector3 *waypoints9, float thickness, Camera3D cam);
// Same two-pass ribbon draw with caller-chosen colors (glow = outer wide pass,
// core = inner bright pass). DrawLightningBolt == Ex with the violet/white legacy palette.
void DrawLightningBoltEx(const Vector3 *waypoints9, float thickness, Camera3D cam,
                         Color colorGlow, Color colorCore);

#endif // VFX_PROC_RAY_H
