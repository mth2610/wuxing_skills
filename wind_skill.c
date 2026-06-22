#include "wind_skill.h"
#include "skill_manager.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stddef.h>

#define MAX_WIND_PARTICLES 2000
#define MAX_EMITTERS 5

typedef struct {
    bool active;
    Vector3 position;
    float radius;
    float maxLifetime;
    float lifetime;
    float pullStrength;
    float sizeScale;
    float spawnAccumulator;
} WindEmitter;

typedef struct {
    bool active;
    Vector3 position;
    Vector3 velocity;
    float radius;
    float lifetime;
    float maxLifetime;
    float angle;
    float speed;
    float distFromCenter;
    float heightOffset;
    WindEmitter* parent;
} WindParticle;

static WindParticle particlePool[MAX_WIND_PARTICLES];
static WindEmitter emitters[MAX_EMITTERS];
static int lastUsedParticle = 0;

static RenderTexture2D canvasTexture;
static Shader windShader;
static int timeLoc;

extern Camera3D camera;

static float Random01(void) {
    return (float)GetRandomValue(0, 10000) / 10000.0f;
}

static void SpawnParticle(Vector3 pos, float dist, float speed, float angle, float height, float radius, float life, WindEmitter* parent) {
    for (int i = 0; i < MAX_WIND_PARTICLES; i++) {
        int index = (lastUsedParticle + i) % MAX_WIND_PARTICLES;
        if (!particlePool[index].active) {
            particlePool[index].active = true;
            particlePool[index].position = pos;
            particlePool[index].distFromCenter = dist;
            particlePool[index].speed = speed;
            particlePool[index].angle = angle;
            particlePool[index].heightOffset = height;
            particlePool[index].radius = radius;
            particlePool[index].maxLifetime = life;
            particlePool[index].lifetime = life;
            particlePool[index].parent = parent;
            lastUsedParticle = (index + 1) % MAX_WIND_PARTICLES;
            break;
        }
    }
}

void InitWindSkill(int screenWidth, int screenHeight) {
    canvasTexture = LoadRenderTexture(screenWidth, screenHeight);
    windShader = LoadShader(0, "wind.fs");
    timeLoc = GetShaderLocation(windShader, "u_time");

    for (int i = 0; i < MAX_WIND_PARTICLES; i++)
        particlePool[i].active = false;
    for (int i = 0; i < MAX_EMITTERS; i++)
        emitters[i].active = false;
}

void CastWindSkill(Vector3 startPos, Vector3 target, float sizeScale) {
    for (int i = 0; i < MAX_EMITTERS; i++) {
        if (!emitters[i].active) {
            emitters[i].active = true;
            emitters[i].position = target;
            emitters[i].position.y = 0.1f; // Flat on the floor
            emitters[i].radius = 240.0f * sizeScale;
            emitters[i].maxLifetime = 4.0f;
            emitters[i].lifetime = 4.0f;
            emitters[i].pullStrength = 300.0f;
            emitters[i].sizeScale = sizeScale;
            emitters[i].spawnAccumulator = 0.0f;
            break;
        }
    }
}

void UpdateWindSkill(float dt) {
    float time = GetTime();

    // 1. Update emitters
    for (int e = 0; e < MAX_EMITTERS; e++) {
        if (!emitters[e].active) continue;

        emitters[e].lifetime -= dt;
        if (emitters[e].lifetime <= 0.0f) {
            emitters[e].active = false;
            continue;
        }

        // Spawn swirling dust/wind particles
        emitters[e].spawnAccumulator += dt;
        float spawnRate = 0.008f; // Very fast spawning
        while (emitters[e].spawnAccumulator >= spawnRate) {
            float dist = (10.0f + Random01() * emitters[e].radius);
            float speed = 3.0f + Random01() * 4.0f;
            float angle = Random01() * 2.0f * PI;
            float height = Random01() * 150.0f * emitters[e].sizeScale;
            float rad = 4.0f + Random01() * 8.0f;
            float life = 0.8f + Random01() * 0.8f;
            
            SpawnParticle(emitters[e].position, dist, speed, angle, height, rad, life, &emitters[e]);
            emitters[e].spawnAccumulator -= spawnRate;
        }
    }

    // 2. Update particles
    for (int i = 0; i < MAX_WIND_PARTICLES; i++) {
        if (!particlePool[i].active) continue;

        particlePool[i].lifetime -= dt;
        if (particlePool[i].lifetime <= 0.0f || (particlePool[i].parent && !particlePool[i].parent->active)) {
            particlePool[i].active = false;
            continue;
        }

        // Spiral inward and upward
        particlePool[i].angle += particlePool[i].speed * dt;
        particlePool[i].distFromCenter -= (particlePool[i].distFromCenter * 0.7f) * dt; // Spiral inward
        if (particlePool[i].distFromCenter < 2.0f) particlePool[i].distFromCenter = 2.0f;
        
        particlePool[i].heightOffset += (25.0f * dt); // Rise slowly

        Vector3 center = particlePool[i].parent ? particlePool[i].parent->position : (Vector3){0};
        particlePool[i].position.x = center.x + cosf(particlePool[i].angle) * particlePool[i].distFromCenter;
        particlePool[i].position.y = center.y + particlePool[i].heightOffset;
        particlePool[i].position.z = center.z + sinf(particlePool[i].angle) * particlePool[i].distFromCenter;
    }
}

void DrawWindSkill(void) {
    bool active = false;
    for (int i = 0; i < MAX_EMITTERS; i++) {
        if (emitters[i].active) { active = true; break; }
    }
    if (!active) return;

    float time = GetTime();

    BeginTextureMode(canvasTexture);
        ClearBackground(BLANK);
        BeginBlendMode(BLEND_ALPHA);

            for (int i = 0; i < MAX_WIND_PARTICLES; i++) {
                if (!particlePool[i].active) continue;

                float lifeRatio = particlePool[i].lifetime / particlePool[i].maxLifetime;
                ProjectedPoint pt = ProjectPointCached(particlePool[i].position, camera);
                if (pt.behindCamera) continue;
                Vector2 screenPos = pt.screenPos;
                float depthFactor = pt.depthFactor;

                float r = particlePool[i].radius * lifeRatio * depthFactor;
                // Swirling particles drawn in pale blue/cyan/white gradients
                Color col = ColorAlpha(WHITE, lifeRatio * 0.4f);
                DrawCircleGradient((int)screenPos.x, (int)screenPos.y, r * 1.5f, col, BLANK);
                DrawCircleGradient((int)screenPos.x, (int)screenPos.y, r * 0.7f, ColorAlpha(SKYBLUE, lifeRatio * 0.6f), BLANK);
            }

        EndBlendMode();
    EndTextureMode();

    SetShaderValue(windShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(windShader);
        DrawTextureRec(canvasTexture.texture, (Rectangle){ 0, 0, (float)canvasTexture.texture.width, (float)-canvasTexture.texture.height }, (Vector2){ 0, 0 }, WHITE);
    EndShaderMode();
}

void UnloadWindSkill(void) {
    UnloadShader(windShader);
    UnloadRenderTexture(canvasTexture);
}

bool GetWindPullForce(Vector3* outPullCenter, float* outPullStrength) {
    for (int i = 0; i < MAX_EMITTERS; i++) {
        if (emitters[i].active) {
            *outPullCenter = emitters[i].position;
            *outPullStrength = emitters[i].pullStrength * emitters[i].sizeScale;
            return true;
        }
    }
    return false;
}
