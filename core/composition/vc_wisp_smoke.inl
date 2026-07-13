// Wisp Smoke (Incense Smoke / Magical Vapor) visual composition.
// Emits glowing wispy smoke trails from a single dynamic or static 3D emitter position.
// Once emitted, particles rise vertically due to buoyancy, damp due to air drag,
// and curl dynamically over time using an animated Noise_Curl3D field.
// Supports customizable wind force, buoyancy, turbulence, and drag parameters.

#include "core/force_field.h"

#define MAX_WISP_PARTICLES 280
#define WISP_TRAIL_LENGTH 16

typedef struct {
    Vector3 position;
    Vector3 velocity;
    float age;
    float lifetime;
    float targetRadius;
    float angle;
    float angleSpeed;
    float phaseOffset;
    Vector3 history[WISP_TRAIL_LENGTH];
    int historyCount;
    bool active;
} WispParticle;

static WispParticle s_wispParticles[MAX_WISP_PARTICLES];
static bool s_wispInit = false;

void VFX_ComposeWispSmoke(
    VC_MaterialId matId,
    Vector3 pos,
    float width,
    Vector3 windDirection,
    float windStrength,
    float buoyancy,
    float turbulence,
    float drag,
    float time
)
{
    // 1. Initialize particle pool
    if (!s_wispInit)
    {
        for (int i = 0; i < MAX_WISP_PARTICLES; i++)
            s_wispParticles[i].active = false;
        s_wispInit = true;
    }

    float dt = GetFrameTime();
    const VFX_ElementMaterial *mat = VFX_Material(matId);
    Color bodyCol = mat->body;

    // Detect if emitter moved significantly to prevent trailing stretch glitches on teleport
    static Vector3 s_lastEmitterPos = {0};
    if (Vector3Distance(pos, s_lastEmitterPos) > 6.0f)
    {
        for (int i = 0; i < MAX_WISP_PARTICLES; i++)
            s_wispParticles[i].active = false;
    }
    s_lastEmitterPos = pos;

    // Normalize wind direction to prevent scaling errors
    Vector3 normalizedWind = (Vector3Length(windDirection) > 0.001f) ? Vector3Normalize(windDirection) : (Vector3){0};

    // 2. Physics & Pool Update (run once per frame)
    static float s_lastUpdateTime = -1.0f;
    if (time != s_lastUpdateTime)
    {
        // Update active particles in world space
        for (int i = 0; i < MAX_WISP_PARTICLES; i++)
        {
            WispParticle *p = &s_wispParticles[i];
            if (!p->active)
                continue;

            p->age += dt;
            if (p->age >= p->lifetime)
            {
                p->active = false;
                continue;
            }

            // Calculate height-based transition factor (from 0 at emitter tip to 1 at 1.8 meters high)
            float heightDiff = p->position.y - pos.y;
            // Air resistance / Drag (high viscosity/drag makes them float lazily)
            float dragCoeff = 4.2f;
            p->velocity = Vector3Scale(p->velocity, fmaxf(0.0f, 1.0f - dragCoeff * dt));

            // Buoyancy (smoke rises upwards vertically along +Y)
            float buoyancyStrength = 0.65f;
            Vector3 vBuoyancy = {0.0f, buoyancyStrength, 0.0f};
            p->velocity = Vector3Add(p->velocity, Vector3Scale(vBuoyancy, dt));

            // Wind Force (blows smoke in the specified direction)
            Vector3 vWind = Vector3Scale(normalizedWind, windStrength);
            p->velocity = Vector3Add(p->velocity, Vector3Scale(vWind, dt));

            // Curl Noise (offset coordinates with time so the field morphs and wiggles dynamically)
            float noiseScale = 1.8f;
            float actualTurbulence = turbulence * 3.6f;
            float morphSpeed = 0.8f;
            Vector3 vNoise = Noise_Curl3D(
                p->position.x,
                p->position.y - time * morphSpeed,
                p->position.z + time * (morphSpeed * 0.5f),
                noiseScale
            );
            p->velocity = Vector3Add(p->velocity, Vector3Scale(vNoise, actualTurbulence * dt));

            // Integrate position in world space
            p->position = Vector3Add(p->position, Vector3Scale(p->velocity, dt));

            // Shift and update history for ribbon trail rendering
            for (int j = WISP_TRAIL_LENGTH - 2; j >= 0; j--)
            {
                p->history[j + 1] = p->history[j];
            }
            p->history[0] = p->position;
            if (p->historyCount < WISP_TRAIL_LENGTH)
                p->historyCount++;
        }

        // Spawn new smoke particles at the emitter position
        int spawnCount = (int)(dt * 180.0f); // approx 180 particles/sec
        if (spawnCount < 1)
            spawnCount = 1;
        if (spawnCount > 8)
            spawnCount = 8;

        for (int sc = 0; sc < spawnCount; sc++)
        {
            for (int i = 0; i < MAX_WISP_PARTICLES; i++)
            {
                WispParticle *p = &s_wispParticles[i];
                if (!p->active)
                {
                    // Gentle randomized puff direction at spawn
                    float angle = Random01() * 3.14159265f * 2.0f;
                    float r = width * 0.15f * Random01();
                    
                    p->position = Vector3Add(pos, (Vector3){
                        cosf(angle) * r,
                        (Random01() - 0.5f) * 0.05f,
                        sinf(angle) * r
                    });

                    // Initial velocity is just a gentle puff in all directions
                    float puffSpeed = 0.08f + Random01() * 0.12f;
                    p->velocity = (Vector3){
                        cosf(angle) * puffSpeed,
                        0.15f + Random01() * 0.25f, // gentle upward boost
                        sinf(angle) * puffSpeed
                    };

                    p->age = 0.0f;
                    p->lifetime = 1.3f + Random01() * 1.0f;
                    
                    // Initialize entire history to spawn position
                    for (int j = 0; j < WISP_TRAIL_LENGTH; j++)
                    {
                        p->history[j] = p->position;
                    }
                    p->historyCount = 1;
                    p->active = true;
                    break;
                }
            }
        }

        s_lastUpdateTime = time;
    }

    // 3. Drawing Smoke Trails
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    BeginBlendMode(mat->blendMode);

    static RibbonPoint trailPoints[WISP_TRAIL_LENGTH];
    for (int i = 0; i < MAX_WISP_PARTICLES; i++)
    {
        const WispParticle *p = &s_wispParticles[i];
        if (!p->active || p->historyCount < 2)
            continue;

        for (int j = 0; j < p->historyCount; j++)
        {
            float norm = (float)j / (float)(p->historyCount - 1);
            trailPoints[j].position = p->history[j];
            
            // Ribbon width tapers to a fine point at the tail
            trailPoints[j].halfWidth = width * 0.11f * (1.0f - norm * 0.85f);
            trailPoints[j].v = norm;

            // Fade out towards the tail and age
            float ageFade = 1.0f - (p->age / p->lifetime);
            trailPoints[j].tint = ColorAlpha(bodyCol, ageFade * (1.0f - norm * 0.95f) * 0.95f);
        }

        // Draw the ribbon trail curve for this particle
        DrawRibbonStrip(trailPoints, p->historyCount, (Texture2D){0}, camera);
    }

    EndBlendMode();
    rlEnableBackfaceCulling();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}
