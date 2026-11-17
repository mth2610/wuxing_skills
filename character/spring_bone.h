// character/spring_bone.h
// Lightweight Bone-Driven Secondary Motion / Spring Bone Physics for Wuxia Garments
// (Tà áo, tay áo thụng, dải lụa thắt lưng, áo choàng, tua kiếm).
//
// Verlet integration in world space with distance + angular cone constraints,
// spherical collision against body/thighs, and environmental wind response.
//
// Chain model: garment bones are DISCOVERED from the loaded model's bind pose
// (`cloth_*` naming, 1..N segments per chain), so a new garment dropped into
// assets/characters/equipment/ needs no code or table entry — its rest
// directions, segment lengths and anchors are read from the rig itself.
// `SpringBone_SetupWuxiaDefaults()` remains as a hand-tuned fallback for models
// whose rig cannot be inspected.

#ifndef SPRING_BONE_H
#define SPRING_BONE_H

#include "raylib.h"
#include <stdbool.h>

struct Model;
struct CharacterAnimState;

#define SPRING_BONE_MAX 48
#define SPRING_COLLIDER_MAX 12

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
    char name[32];          // Bone name (e.g. "cloth_skirt_f_l", "cloth_sleeve_l_01")
    char parentBone[32];    // Chain anchor skeleton bone (non-cloth), same for every segment

    // Chain topology, resolved once at registration.
    int parentSpring;       // >= 0: head follows that segment's tip (2nd/3rd segment)
    int boneIndex;          // Skeleton bone index this segment drives (-1 for hand-authored bones)
    Vector3 localHeadOffset;// Head offset in the anchor bone's local space (chain root only)
    Vector3 localRestDir;   // Rest direction in the anchor bone's local space
    Vector3 bindHead;       // Bind-pose head in model space (skinning override)
    Vector3 bindDir;        // Bind-pose direction in model space (skinning override)
    float length;           // Segment length in meters

    // Physics particle state in world space (Verlet integration)
    Vector3 headWorldPos;   // Anchor head position
    Vector3 currentPos;     // Tip position
    Vector3 prevPos;        // Previous frame tip position
    Vector3 restPosWorld;   // Dynamic rest position
    Vector3 restDirWorld;   // Dynamic rest direction (animated parent, no sway)

    // Resolved pose exported to the skinning pass (model space, this frame)
    Vector3 modelHeadPos;   // Simulated head in model space
    Vector3 modelDir;       // Simulated direction in model space (unit)

    // Physical tunables
    float stiffness;        // Restoring spring factor towards rest pose (0.05 - 0.40)
    float damping;          // Velocity retention factor (0.80 - 0.95)
    float gravityFactor;    // Gravity scale (1.0 = 9.81 m/s^2)
    float windInfluence;    // Response multiplier to world wind (0.0 - 1.0)
    float maxAngleDeg;      // Angular cone limit in degrees from rest vector
    float boneRadius;       // Margin for collision resolution against colliders

    bool initialized;
} SpringBone;

typedef struct SpringBoneSystem {
    SpringBone bones[SPRING_BONE_MAX];
    int boneCount;

    SpringCollider colliders[SPRING_COLLIDER_MAX];
    int colliderCount;

    Vector3 prevCharPos;
    float prevCharYaw;
    unsigned int stamp;     // Bumped every update; part of the CPU-skin cache key
    bool initialized;
} SpringBoneSystem;

// Optional spatial wind query, sampled at each cloth segment's head in world
// space. Lets the physics follow the environment's macro wind + local gust
// fields without this module depending on them.
typedef Vector3 (*SpringWindFn)(Vector3 worldPos, void *userData);

// Initializes an empty spring bone system
void SpringBone_Init(SpringBoneSystem *sys);

// Populates default wuxia garment spring bones (Skirt tà áo 4 cung, Bell sleeves 2 bên, Ribbons dải lụa)
// and bodily colliders (thighs & pelvis) — fallback when the model rig is unavailable.
void SpringBone_SetupWuxiaDefaults(SpringBoneSystem *sys);

// Rebuilds the whole system from a loaded garment/body model: every bone whose
// name starts with "cloth" becomes a segment, anchored to its skeleton parent
// and chained to the previous segment when the rig nests them. Segment rest
// directions and lengths are measured from the bind pose and the skinned mesh,
// so no per-asset table is needed. Existing bones registered by earlier calls
// are kept (the instance may wear torso + legs + back at once), and the default
// collider set is installed once. Returns the number of segments added.
int SpringBone_RegisterModelCloth(SpringBoneSystem *sys, const struct Model *model);

// Registers a single spring bone (chain root, anchored to a skeleton bone)
int SpringBone_Add(SpringBoneSystem *sys, const char *name, const char *parentBone,
                   Vector3 localRestDir, float length, float stiffness, float damping, float gravityFactor);

// Registers the default pelvis + thigh colliders once (no-op if any exist)
void SpringBone_AddDefaultColliders(SpringBoneSystem *sys);

// Registers a bodily collision sphere
int SpringBone_AddCollider(SpringBoneSystem *sys, const char *boneName, Vector3 offset, float radius);

// Simulates one time step (Verlet integration + constraints + collision resolution)
void SpringBone_Update(SpringBoneSystem *sys, const struct CharacterAnimState *animState,
                       Vector3 charPos, float charYaw, float charScale, float dt, Vector3 windVelocity);

// Same, with wind sampled per segment in world space (macro wind, gusts, slipstream).
void SpringBone_UpdateWithWind(SpringBoneSystem *sys, const struct CharacterAnimState *animState,
                               Vector3 charPos, float charYaw, float charScale, float dt,
                               SpringWindFn windFn, void *userData);

// Queries the resulting world-space head and tip positions of a spring bone
bool SpringBone_GetBoneWorldPos(const SpringBoneSystem *sys, const char *boneName, Vector3 *outHead, Vector3 *outTip);

// Queries the simulated pose a spring bone exports to the skinning pass, in the
// character model's own space (i.e. before position/yaw/scale are applied).
// `outHead` receives the segment head, `outDir` the unit direction. Returns
// false when the segment has not been simulated yet.
bool SpringBone_GetBoneModelPose(const SpringBoneSystem *sys, const char *boneName, Vector3 *outHead, Vector3 *outDir);

// Queries the bind-pose frame a segment was registered with (model space), used
// to build the bone override matrix the renderer applies to that bone.
bool SpringBone_GetBindPose(const SpringBoneSystem *sys, const char *boneName, Vector3 *outHead, Vector3 *outDir);

// Generates an orientation matrix for socket attachments driven by a spring bone
bool SpringBone_GetBoneMatrix(const SpringBoneSystem *sys, const char *boneName, Matrix *outMatrix);

// Resets physics state to rest pose (e.g. after teleport or respawn)
void SpringBone_Reset(SpringBoneSystem *sys);

// Debug visualization: draws spring bones, rest lines, and collision spheres
void SpringBone_DrawDebug(const SpringBoneSystem *sys);

#endif // SPRING_BONE_H
