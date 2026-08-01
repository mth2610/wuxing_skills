#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "skills/taiji/galaxy_spiral_skill/galaxy_spiral_skill.h"
#include "core/particles/gpu/particle_gpu_legacy.h"
#include "core/force_field.h"

// --- tunables (meter-scaled: 1 unit = 1 m) ---------------------------------
#define GALAXY_PARTICLES 1000 // spawned per cast (ring buffer holds MAX_GPU_PARTICLES = 8192)
#define GALAXY_ARMS 2
#define GALAXY_RADIUS 6.0f // disk radius
#define GALAXY_LIFT 1.6f   // float the disk above the target so it reads in the air

// Swirl + binding field. Both layers are AXIS-type: their axis is the DYNAMIC axis passed per
// particle (config.axisOrigin/axisDir), so the field itself is center-agnostic and built once.
static ForceField s_field;

static float frand(void) { return (float)rand() / (float)RAND_MAX; }

// disk colour by normalised radius: bright white-cyan core -> Water cyan -> Taiji purple arms.
// Derived from ELEMENT_COLOR_* macros (no raw hardcoded colours).
static Color galaxyColor(float t)
{
    Color water = ELEMENT_COLOR_WATER; // cyan-blue core tint
    Color taiji = ELEMENT_COLOR_TAIJI; // purple arms
    Color c;
    if (t < 0.35f)
    {
        float k = t / 0.35f; // 0 = white core, 1 = water
        c.r = (unsigned char)(255 - k * (255 - water.r));
        c.g = (unsigned char)(255 - k * (255 - water.g));
        c.b = (unsigned char)(255 - k * (255 - water.b));
    }
    else
    {
        float k = (t - 0.35f) / 0.65f; // water -> taiji
        c.r = (unsigned char)(water.r + k * (taiji.r - water.r));
        c.g = (unsigned char)(water.g + k * (taiji.g - water.g));
        c.b = (unsigned char)(water.b + k * (taiji.b - water.b));
    }
    c.a = 255;
    return c;
}

void InitGalaxySpiralSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    ForceField_Clear(&s_field);
    // Differential rotation: tangential swirl that is stronger near the axis (falloff = 1.0 -> the
    // acceleration decays with distance) so the core spins faster than the rim.
    ForceField_AddLayer(&s_field, (ForceLayer){
                                      .type = FORCE_VORTEX_AXIS,
                                      .strength = 2.4f,
                                      .radius = GALAXY_RADIUS * 1.25f,
                                      .falloff = 1.0f,
                                  });
    // Gentle pull toward the axis so the arms stay a bound disk instead of flying apart.
    ForceField_AddLayer(&s_field, (ForceLayer){
                                      .type = FORCE_RADIAL_AXIS,
                                      .strength = 0.7f,
                                      .radius = GALAXY_RADIUS * 1.25f,
                                      .falloff = 0.0f,
                                  });
}

void CastGalaxySpiralSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    (void)agentId;
    (void)startPos;
    float sizeScale = (params.sizeScale > 0.0f) ? params.sizeScale : 1.0f;
    float R = GALAXY_RADIUS * sizeScale;

    Vector3 center = target;
    center.y += GALAXY_LIFT * sizeScale;

    for (int i = 0; i < GALAXY_PARTICLES; i++)
    {
        int arm = i % GALAXY_ARMS;

        // area-uniform radius (sqrt), biased slightly denser toward the core
        float u = frand();
        float r = R * sqrtf(u);
        if (r < 0.2f * sizeScale)
            r = 0.2f * sizeScale;

        // logarithmic-ish spiral arm angle + perpendicular scatter (wider toward the rim)
        float windings = 2.3f;
        float base = (float)arm * (2.0f * PI / (float)GALAXY_ARMS);
        float theta = base + (r / R) * windings * 2.0f * PI;
        theta += (frand() - 0.5f) * (0.35f + 0.9f * (r / R));

        float x = center.x + r * cosf(theta);
        float z = center.z + r * sinf(theta);
        // thin disk, thicker toward the core (a bulge)
        float y = center.y + (frand() - 0.5f) * 0.5f * sizeScale * (0.25f + 0.75f * (1.0f - r / R));

        // tangential (orbital) velocity, inner faster -> differential rotation
        Vector3 rad = {x - center.x, 0.0f, z - center.z};
        float rl = sqrtf(rad.x * rad.x + rad.z * rad.z);
        if (rl < 0.0001f)
            rl = 0.0001f;
        Vector3 tang = {-rad.z / rl, 0.0f, rad.x / rl}; // 90° CCW in XZ
        float speed = 2.4f * (0.35f + 0.65f * (1.0f - r / R)) * sizeScale;

        float tR = r / R; // 0 core .. 1 rim
        Color cs = galaxyColor(tR);
        Color ce = cs;
        ce.a = 0; // fade to transparent as the particle ages out

        GpuParticleSystem_Spawn((GpuParticleConfig){
            .position = (Vector3){x, y, z},
            .velocity = (Vector3){tang.x * speed, 0.0f, tang.z * speed},
            .colorStart = cs,
            .colorEnd = ce,
            .radius = (0.03f + 0.035f * (1.0f - tR)) * sizeScale, // core grains a touch bigger
            .lifetime = 10.0f + 5.0f * u,                         // stagger deaths so the disk breathes
            .drag = 0.0f,
            .forceField = &s_field,
            .axisOrigin = center,
            .axisDir = (Vector3){0.0f, 1.0f, 0.0f},
        });
    }
}

void UpdateGalaxySpiralSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)dt;
    (void)enemyPos;
    (void)enemyRadius;
    // Particles are simulated + drawn by the central GpuParticleSystem (main.c). Nothing per-frame.
}

void DrawGalaxySpiralSkill(void)
{
    // Nothing: the galaxy is rendered by the central GpuParticleSystem_Draw in main.c.
}

void UnloadGalaxySpiralSkill(void)
{
    // No owned resources.
}
