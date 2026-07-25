// skills/taiji/taiji_loi/taiji_loi_skill.c
#include "taiji_loi_skill.h"
#include "skills/taiji/taiji_phong/taiji_phong_skill.h"
#include "entities/entities.h"
#include "core/composition/visual_composer.h"
#include "core/composition/vfx_sequence.h"
#include <stddef.h>

// Meter-scaled. LOI_MANA_COST is deliberately brutal (2 casts drain a full
// default 100 pool → Entity_Update exits Thái Cực) — thiết kế §XVII "Vô Sát".
static const float LOI_RADIUS       = 3.0f;
static const float LOI_DAMAGE       = 35.0f;
static const float LOI_KNOCKBACK    = 5.0f;
static const float LOI_MANA_COST    = 45.0f;
static const float LOI_SKY_HEIGHT   = 14.0f;

static int s_skillIndex = -1;

// E3 beat adapters — a beat calls fn(pos, scale, ud), so each VFX_Compose* gets
// a small wrapper (the shape documented in vfx_sequence.h's COMPOSE row).
static void LoiBolt(Vector3 pos, float scale, void *ud)
{
    (void)ud;
    Vector3 sky = { pos.x, LOI_SKY_HEIGHT, pos.z };
    VFX_SpawnProcBeam(sky, pos, EFFECT_PRESET_LIGHTNING_IMPACT, 0.18f * scale, 0.35f);
}

static void LoiImpact(Vector3 pos, float scale, void *ud)
{
    (void)ud;
    VFX_ComposeImpact(pos, EFFECT_PRESET_LIGHTNING_IMPACT, 1.2f * scale);
    VFX_ComposeShockwaveRing(pos, LOI_RADIUS * scale, 0.4f, (Color){ 235, 225, 255, 255 });
}

void InitTaijiLoiSkill(int screenWidth, int screenHeight) {
    (void)screenWidth; (void)screenHeight;
    s_skillIndex = Skill_GetIndexByName("TAIJI_LOI");
    if (s_skillIndex >= 0) RegisterSkillManaCost(s_skillIndex, LOI_MANA_COST);
}

void CastTaijiLoiSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    (void)startPos; (void)params;
    if (!Entity_IsTaijiActive(agentId)) return; // Thái Cực exclusive

    // Giáng vào tâm Phong nếu đang mở — the gather→strike combo.
    Vector3 ground;
    if (!TaijiPhong_GetActiveCenter(&ground)) {
        ground = (Vector3){ target.x, 0.0f, target.z };
    }

    // Damage through the team-aware AoE path.
    const Agent *caster = Entity_GetAgent(agentId);
    AgentTeam team = caster ? caster->team : TEAM_NEUTRAL;
    Entity_ApplyAoEDamage(ground, LOI_RADIUS, LOI_DAMAGE, LOI_KNOCKBACK, team);

    // ── Đợt E / E3 — the strike runs on a beat track ────────────────────────
    // It used to fire the bolt, the impact and the shockwave on the SAME frame,
    // so a sky→ground strike had no travel and no envelope: everything simply
    // appeared at once. The bolt now leaves the sky first and the ground beats
    // land when it arrives, which is the whole difference between "a flash
    // happened" and "something struck here".
    //
    // DAMAGE IS NOT ON THE TRACK, deliberately. It stays above, at cast time —
    // moving it onto a beat would delay the hit by the bolt's travel and change
    // gameplay (and netcode) timing, which a VFX task has no business doing.
    // The visual envelope is short enough that the split is not readable.
    VFX_Sequence *seq = VFX_SeqBegin(ground, VC_MAT_LIGHTNING, 1.0f);
    if (seq != NULL)
    {
        const float STRIKE_T = 0.10f;   // bolt travel — when the ground beats land

        // t=0: the bolt leaves the sky, plus a faint ground glow telegraphing
        // where it will hit. The telegraph is what makes the strike readable
        // instead of arriving out of nowhere.
        VFX_SeqAt(seq, 0.0f, (VFX_Beat){ .kind = VFX_BEAT_COMPOSE, .cb = LoiBolt });
        VFX_SeqAt(seq, 0.0f, (VFX_Beat){ .kind = VFX_BEAT_LIGHT, .a = 1.2f, .b = STRIKE_T });

        // The strike: every beat on ONE frame. Simultaneity is the point.
        VFX_SeqAt(seq, STRIKE_T, (VFX_Beat){ .kind = VFX_BEAT_COMPOSE, .cb = LoiImpact });
        VFX_SeqAt(seq, STRIKE_T, (VFX_Beat){ .kind = VFX_BEAT_LIGHT,   .a = LOI_RADIUS, .b = 0.30f });
        VFX_SeqAt(seq, STRIKE_T, (VFX_Beat){ .kind = VFX_BEAT_SHAKE,   .a = 0.40f });
        VFX_SeqAt(seq, STRIKE_T, (VFX_Beat){ .kind = VFX_BEAT_RADIAL,  .a = 0.16f, .b = 0.45f });
        VFX_SeqAt(seq, STRIKE_T, (VFX_Beat){ .kind = VFX_BEAT_DISTORT, .a = 0.7f, .b = 0.4f, .c = 0.3f });

        // Tail: a dimming afterglow so the strike fades rather than cutting.
        VFX_SeqAt(seq, STRIKE_T + 0.30f, (VFX_Beat){ .kind = VFX_BEAT_LIGHT, .a = 1.4f, .b = 0.45f });
        VFX_SeqPlay(seq);
    }
}

void UpdateTaijiLoiSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)dt; (void)enemyPos; (void)enemyRadius;
    // Stateless — the composition layer animates the bolt/impact.
}

void DrawTaijiLoiSkill(void) {
    // Nothing owned here — VFX_Compose* draws via the composition pass.
}

void UnloadTaijiLoiSkill(void) {
}
