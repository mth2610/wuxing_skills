#include "skills/taiji/core_test/core_test_skill.h"
#include "core/resource_manager.h"
#include "core/procedural_mesh_utils.h"
#include "raymath.h"

typedef struct {
    Vector3 pos, dir;
    float speed, life;
    bool active;
} TestProj;

static TestProj s_proj[16] = {0};
static Shader s_shd;

void InitCoreTestSkill(int w, int h) {
    (void)w; (void)h;
    s_shd = ResourceManager_LoadShader("skills/taiji/core_test/core_test.vs", "skills/taiji/core_test/core_test.fs");
    for (int i = 0; i < 16; i++) s_proj[i].active = false;
}

void CastCoreTestSkill(Vector3 start, Vector3 target, SkillParams p) {
    (void)p;
    for (int i = 0; i < 16; i++) {
        if (!s_proj[i].active) {
            Vector3 diff = Vector3Subtract(target, start);
            float dist = Vector3Length(diff);
            s_proj[i].pos = start;
            s_proj[i].dir = (dist < 0.01f) ? (Vector3){0, 0, 1} : Vector3Normalize(diff);
            s_proj[i].speed = 50.0f;
            s_proj[i].life = (dist < 0.01f) ? 0.1f : dist / 50.0f;
            s_proj[i].active = true;
            break;
        }
    }
}

void UpdateCoreTestSkill(float dt, Vector3 ePos, float eRad) {
    (void)ePos; (void)eRad;
    for (int i = 0; i < 16; i++) {
        if (!s_proj[i].active) continue;
        s_proj[i].life -= dt;
        s_proj[i].pos = Vector3Add(s_proj[i].pos, Vector3Scale(s_proj[i].dir, s_proj[i].speed * dt));
        if (s_proj[i].life <= 0.0f) {
            ApplyAoEDamage(s_proj[i].pos, 60.0f, 250.0f, 200.0f);
            s_proj[i].active = false;
        }
    }
}

void DrawCoreTestSkill(void) {
    SkillManager_BeginShader(s_shd);
    for (int i = 0; i < 16; i++) {
        if (!s_proj[i].active) continue;
        Vector3 p = s_proj[i].pos;
        Color c = GOLD;
        switch (i % 7) {
            case 0: DrawCoreSphere(p, 5.f, 16, 16, c); break;
            case 1: DrawCoreCylinder(p, (Vector3){p.x, p.y + 10.f, p.z}, 4.f, 4.f, 16, c); break;
            case 2: DrawCoreCone(p, 5.f, 10.f, 16, c); break;
            case 3: DrawCoreTorus(p, 3.f, 6.f, 16, 16, c); break;
            case 4: DrawCoreCube(p, 8.f, 8.f, 8.f, c); break;
            case 5: DrawCorePrism(p, (Vector3){p.x, p.y + 10.f, p.z}, 5.f, 6, c); break;
            case 6: DrawCorePlanePolygon(p, 6.f, 8, c); break;
        }
    }
    SkillManager_EndShader();
}

void UnloadCoreTestSkill(void) {
    for (int i = 0; i < 16; i++) s_proj[i].active = false;
}
