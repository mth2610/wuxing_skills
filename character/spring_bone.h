// character/spring_bone.h
// Lightweight Bone-Driven Secondary Motion / Spring Bone Physics for Wuxia Garments
// (Tà áo, tay áo thụng, dải lụa thắt lưng, áo choàng, tua kiếm).
// Uses Verlet numerical integration with distance & angular cone constraints,
// spherical collision prevention against body/thighs, and environmental wind response.

#ifndef SPRING_BONE_H
#define SPRING_BONE_H

#include "raylib.h"
#include <stdbool.h>

#define SPRING_BONE_MAX 16
#define SPRING_COLLIDER_MAX 8

typedef enum {
    SPRING_COLLIDER_SPHERE = 0,
    SPRING_COLLIDER_CAPSULE
} SpringColliderType;

typedef struct {
    SpringColliderType type;
    char boneName[32];      // Anchor bone in skeleton (e.g. "mixamorig:Hips", "mixamorig:LeftUpLeg")
    Vector3 localOffset;    // Center offset relative to anchor bone
    float radius;           // Collision sphere radius in meters
    Vector3 worldPos;       // Computed world position
} SpringCollider;

typedef struct {
    char name[32];          // Bone name (e.g. "cloth_skirt_f_l", "cloth_sleeve_l_01", "cloth_ribbon_01")
    char parentBone[32];    // Parent anchor bone (e.g. "mixamorig:Hips", "mixamorig:LeftForeArm", "mixamorig:Spine2")
    
    Vector3 localRestDir;   // Rest direction vector from parent head in parent/local space
    float length;           // Segment length in meters
    
    // Physics particle state in world space (Verlet integration)
    Vector3 headWorldPos;   // Anchor head position
    Vector3 currentPos;     // Tip position
    Vector3 prevPos;        // Previous frame tip position
    Vector3 restPosWorld;   // Dynamic rest position
    
    // Physical tunables
    float stiffness;        // Restoring spring factor towards rest pose (0.05 - 0.40)
    float damping;          // Velocity retention factor (0.80 - 0.95)
    float gravityFactor;    // Gravity scale (1.0 = 9.81 m/s^2)
    float windInfluence;    // Response multiplier to world wind (0.0 - 1.0)
    float maxAngleDeg;      // Angular cone limit in degrees from rest vector
    float boneRadius;       // Margin for collision resolution against colliders
    
    bool initialized;
} SpringBone;

typedef struct {
    SpringBone bones[SPRING_BONE_MAX];
    int boneCount;
    
    SpringCollider colliders[SPRING_COLLIDER_MAX];
    int colliderCount;
    
    Vector3 prevCharPos;
    float prevCharYaw;
    bool initialized;
} SpringBoneSystem;

// Forward declaration
struct CharacterAnimState;

// Initializes an empty spring bone system
void SpringBone_Init(SpringBoneSystem *sys);

// Populates default wuxia garment spring bones (Skirt tà áo 4 cung, Bell sleeves 2 bên, Ribbons dải lụa)
// and bodily colliders (thighs & pelvis)
void SpringBone_SetupWuxiaDefaults(SpringBoneSystem *sys);

// Registers a single spring bone
int SpringBone_Add(SpringBoneSystem *sys, const char *name, const char *parentBone,
                   Vector3 localRestDir, float length, float stiffness, float damping, float gravityFactor);

// Registers a bodily collision sphere
int SpringBone_AddCollider(SpringBoneSystem *sys, const char *boneName, Vector3 offset, float radius);

// Simulates one time step (Verlet integration + constraints + collision resolution)
void SpringBone_Update(SpringBoneSystem *sys, const struct CharacterAnimState *animState,
                       Vector3 charPos, float charYaw, float charScale, float dt, Vector3 windVelocity);

// Queries the resulting world-space head and tip positions of a spring bone
bool SpringBone_GetBoneWorldPos(const SpringBoneSystem *sys, const char *boneName, Vector3 *outHead, Vector3 *outTip);

// Generates an orientation matrix for socket attachments driven by a spring bone
bool SpringBone_GetBoneMatrix(const SpringBoneSystem *sys, const char *boneName, Matrix *outMatrix);

// Resets physics state to rest pose (e.g. after teleport or respawn)
void SpringBone_Reset(SpringBoneSystem *sys);

// Debug visualization: draws spring bones, rest lines, and collision spheres
void SpringBone_DrawDebug(const SpringBoneSystem *sys);

#endif // SPRING_BONE_H
