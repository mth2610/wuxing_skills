#include "particle_system.h"
#include "raymath.h"
#include "utils_math.h"

typedef struct {
  bool active;
  Vector3 position;
  Vector3 velocity;
  Vector3 force;
  float drag;
  float turbulence;
  float radius;
  float lifetime;
  float maxLifetime;
  Color colorStart;
  Color colorEnd;
  Color currentColor;
  float angle;
  unsigned int physicsFlags;
} Particle;

static Particle globalParticles[MAX_GLOBAL_PARTICLES];
static int lastUsedIndex = 0;

void InitParticleSystem(void) {
  for (int i = 0; i < MAX_GLOBAL_PARTICLES; i++) {
    globalParticles[i].active = false;
  }
  lastUsedIndex = 0;
}

void SpawnParticle(ParticleConfig config) {
  for (int i = 0; i < MAX_GLOBAL_PARTICLES; i++) {
    int index = (lastUsedIndex + i) % MAX_GLOBAL_PARTICLES;
    if (!globalParticles[index].active) {
      globalParticles[index].active = true;
      globalParticles[index].position = config.position;
      globalParticles[index].velocity = config.velocity;
      globalParticles[index].radius = config.radius;
      globalParticles[index].lifetime = config.lifetime;
      globalParticles[index].maxLifetime = config.lifetime;
      globalParticles[index].colorStart = config.colorStart;
      globalParticles[index].colorEnd = config.colorEnd;
      globalParticles[index].currentColor = config.colorStart;
      globalParticles[index].angle = Random01() * PI * 2.0f;

      // CẬP NHẬT TỪ REVIEW: Nhận trực tiếp cờ vật lý từ cấu hình
      globalParticles[index].physicsFlags = config.physicsFlags;
      globalParticles[index].drag = config.drag;
      globalParticles[index].force = config.force;
      globalParticles[index].turbulence = config.turbulence;

      lastUsedIndex = (index + 1) % MAX_GLOBAL_PARTICLES;
      break;
    }
  }
}

void UpdateParticles(float dt) {
  float time = GetTime();
  for (int i = 0; i < MAX_GLOBAL_PARTICLES; i++) {
    if (!globalParticles[i].active)
      continue;

    globalParticles[i].lifetime -= dt;
    if (globalParticles[i].lifetime <= 0.0f) {
      globalParticles[i].active = false;
      continue;
    }

    float lifeRatio =
        globalParticles[i].lifetime / globalParticles[i].maxLifetime;

    globalParticles[i].currentColor.r =
        (unsigned char)Math_Mix(globalParticles[i].colorEnd.r,
                                globalParticles[i].colorStart.r, lifeRatio);
    globalParticles[i].currentColor.g =
        (unsigned char)Math_Mix(globalParticles[i].colorEnd.g,
                                globalParticles[i].colorStart.g, lifeRatio);
    globalParticles[i].currentColor.b =
        (unsigned char)Math_Mix(globalParticles[i].colorEnd.b,
                                globalParticles[i].colorStart.b, lifeRatio);
    globalParticles[i].currentColor.a = (unsigned char)Math_Mix(
        globalParticles[i].colorEnd.a, globalParticles[i].colorStart.a,
        lifeRatio * lifeRatio);

    unsigned int flags = globalParticles[i].physicsFlags;

    if (flags & P_PHYSICS_DRAG) {
      float dragFactor = 1.0f - globalParticles[i].drag * dt;
      globalParticles[i].velocity.x *= dragFactor;
      globalParticles[i].velocity.y *= dragFactor;
      globalParticles[i].velocity.z *= dragFactor;
    }

    if (flags & P_PHYSICS_FORCE) {
      globalParticles[i].velocity.x += globalParticles[i].force.x * dt;
      globalParticles[i].velocity.y += globalParticles[i].force.y * dt;
      globalParticles[i].velocity.z += globalParticles[i].force.z * dt;
    }

    if (flags & P_PHYSICS_TURBULENCE) {
      float t = time * 18.0f + globalParticles[i].angle;
      float turbStrength = globalParticles[i].turbulence * lifeRatio;
      float s = sinf(t);
      float c = cosf(t * 0.8f);

      globalParticles[i].velocity.x += s * turbStrength * dt;
      globalParticles[i].velocity.y += c * turbStrength * dt;
      globalParticles[i].velocity.z += (s * c) * turbStrength * dt;
    }

    globalParticles[i].position.x += globalParticles[i].velocity.x * dt;
    globalParticles[i].position.y += globalParticles[i].velocity.y * dt;
    globalParticles[i].position.z += globalParticles[i].velocity.z * dt;
  }
}

void DrawParticles(Camera3D camera, Texture2D texture) {
  for (int i = 0; i < MAX_GLOBAL_PARTICLES; i++) {
    if (!globalParticles[i].active)
      continue;
    DrawBillboard(camera, texture, globalParticles[i].position,
                  globalParticles[i].radius, globalParticles[i].currentColor);
  }
}

bool IsParticleSystemActive(void) {
  for (int i = 0; i < MAX_GLOBAL_PARTICLES; i++) {
    if (globalParticles[i].active)
      return true;
  }
  return false;
}