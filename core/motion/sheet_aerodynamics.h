#ifndef CORE_MOTION_SHEET_AERODYNAMICS_H
#define CORE_MOTION_SHEET_AERODYNAMICS_H

#include "raylib.h"
#include <stdbool.h>

// ============================================================================
// CORE/MOTION/SHEET_AERODYNAMICS.H
//
// Reusable 6-DOF thin-sheet / lamina aerodynamics and coupled spring-damper
// deformation solver (zero heap allocation, pure scalar math, 1 unit = 1 m).
//
// Designed for thin planar bodies: Daoist talismans, falling leaves, flower
// petals, cloth scraps, burning ash flakes, joss paper, and banners.
//
// Coordinate convention of the sheet's local frame:
//   +X (R) : width axis  [-halfWidth  .. +halfWidth ]
//   +Y (U) : length axis [-halfHeight .. +halfHeight] (+Y = leading head)
//   +Z (N) : face normal (front face +Z, back face -Z)
// ============================================================================

typedef struct SheetAeroParams {
    // Physical half-extents of the sheet (meters)
    float halfWidth;
    float halfHeight;

    // Anisotropic linear + quadratic drag in local (R, U, N) axes
    float dragNormal;       // High face-normal drag
    float dragNormalQuad;
    float dragLength;       // Low knife-edge drag along spine (+Y)
    float dragLengthQuad;
    float dragWidth;        // Medium edge drag across width (+X)
    float dragWidthQuad;

    // Thin-plate aerodynamic lift & glide projection
    float liftCoeff;        // Peaks around 25-40 deg AoA, stalls smoothly
    float glideBoost;       // Forward projection along tilted plane

    // Shifting center-of-pressure aerodynamic torque coefficients
    float torquePitchCoeff; // Around R axis: -vU * vN (pitch-up stall)
    float torqueRollCoeff;  // Around U axis: +vR * vN (leaf-fall rocking)
    float torqueYawCoeff;   // Around N axis: +vR * vU
    float gustTorqueAmp;    // Ambient organic flutter torque amplitude
    float rotDamping;       // Base angular damping (1/s)
    float rotDampingSpeed;  // Airspeed-coupled angular damping
    float maxAngularSpeed;  // Max angular velocity clamp (rad/s)

    // Gravity & sheet compliance
    float gravity;          // Downward acceleration (m/s^2, e.g. 3.15 for paper, 0.72 in vortex)
    float softness;         // 0.15 (stiff card) .. 1.0 (silky thin paper)

    // Floor contact, sliding, flat alignment, and wind peel-up
    bool  enableFloor;
    float floorY;           // Ground height under sheet (meters)
    float floorFriction;    // Exponential horizontal sliding decay (1/s)
    float floorAlignK;      // Torque gain aligning sheet normal to +/-Y
    float peelWindThreshold;// Horizontal wind speed (m/s) that peels sheet off ground
    float peelLiftGain;     // Upward impulse gain during peel-up

    // Optional soft spring tether & upright alignment (e.g. close-up hero mode)
    bool    tetherEnabled;
    Vector3 tetherPos;
    float   tetherStiffnessXZ;
    float   tetherStiffnessY;
    float   tetherDamping;
    float   uprightRestore; // Restoring torque toward U->+Y, N->+Z (0 = disabled)
} SheetAeroParams;

typedef struct SheetAeroState {
    // 6-DOF rigid body state (quat is (x, y, z, w), layout-identical to Raylib Quaternion)
    Vector3 pos;
    Vector3 vel;
    Vector4 quat;
    Vector3 angVel;
    float   scale;

    // Coupled 4-mode spring-damper deformation (passed to GPU or CPU deformer)
    float bend;             // Longitudinal arch camber
    float bendV;
    float sBend;            // Stall S-curve camber
    float sBendV;
    float twist;            // Helical torsional twist along spine
    float twistV;
    float cup;              // Transverse cross-width cupping
    float cupV;

    // Airspeed-driven traveling trailing-edge flutter
    float flutterAmp;
    float flutterPhase;

    // Per-instance traits & contact state
    float naturalCurl;      // Resting parchment curvature bias
    float seed;             // Deterministic phase seed
    float restTime;         // Continuous seconds resting on floor
    bool  onFloor;
} SheetAeroState;

// Default material presets
SheetAeroParams SheetAero_DefaultPaper(void);
SheetAeroParams SheetAero_DefaultLeaf(void);

// Initialize a sheet state cleanly (normalizes quaternion, zeroes velocities)
void SheetAero_InitState(SheetAeroState *state, Vector3 pos, Vector4 quat,
                         float scale, float naturalCurl, float seed);

// Extract local orthonormal basis vectors (R = +X, U = +Y, N = +Z) from quaternion
void SheetAero_ExtractBasis(Vector4 q, Vector3 *outR, Vector3 *outU, Vector3 *outN);

// Advance 6-DOF aerodynamics, ground contact, and coupled spring-damper deformation
void SheetAero_Step(SheetAeroState *state, const SheetAeroParams *params,
                    Vector3 windVel, float gustMag, float dt, float time);

// Build knife-edge launch orientation basis (+Y = aimDir, +X = banked width, +Z = normal)
void SheetAero_ComputeLaunchBasis(Vector3 aimDir, float bankRad,
                                  Vector4 *outQuat, Vector3 *outSide,
                                  Vector3 *outLead, Vector3 *outNorm);

// CPU mirror of Sheet_DeformPoint() in core/shaders/common/sheet_deform.glsl.
// Evaluates displaced local position and finite-difference local normal.
void SheetAero_DeformPoint(Vector3 localPos, float halfW, float halfH,
                           float bend, float sBend, float twist, float cup,
                           float flutterAmp, float flutterPhase,
                           Vector3 *outPos, Vector3 *outNormal);

#endif // CORE_MOTION_SHEET_AERODYNAMICS_H
