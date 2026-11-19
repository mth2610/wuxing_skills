// open_world/ow_weather.c
#include "open_world/ow_weather.h"
#include "open_world/ow_loot.h"
#include "open_world/ow_world.h"
#include "character/player_profile.h"
#include "entities/entities.h"
#include "environment/atmosphere.h"
#include "environment/environment_system.h"
#include "environment/wind/wind_system.h"
#include "maps/map_manager.h"
#include "post_process/post_fx.h"
#include "skills/skill_manager.h"
#include "ui/ui.h"
#include "ui/ui_theme.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OW_MAX_WEATHER_DROPS 10

typedef enum {
    WEATHER_DROP_SPIRIT_ORB = 0,
    WEATHER_DROP_METEOR_NODE
} OWWeatherDropKind;

typedef struct {
    bool              active;
    OWWeatherDropKind kind;
    Vector3           pos;
    float             timer;
    float             phase;
} OWWeatherDrop;

static OWWeatherProfileDef s_profiles[OW_MAX_WEATHER_PROFILES];
static int                 s_profileCount   = 0;
static int                 s_currentIdx     = 0;
static float               s_weatherTimer   = 60.0f;
static float               s_vortexTimer    = 0.0f;
static float               s_dropSpawnTimer = 0.0f;
static float               s_clock          = 0.0f;
static int                 s_realmId        = 3;
static Vector3             s_safeCenter     = { 0 };
static float               s_safeRadius     = 20.0f;
static Vector3             s_fieldAnchors[3];
static OWWeatherDrop       s_drops[OW_MAX_WEATHER_DROPS];

static float DistXZ(Vector3 a, Vector3 b) {
    float dx = a.x - b.x, dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}

static void ClearDrops(void) {
    memset(s_drops, 0, sizeof(s_drops));
}

static EnvFogConfig s_baseFog = { 0 };
static bool         s_hasBaseFog = false;

static void ApplyWeatherEnvironment(const OWWeatherProfileDef *p) {
    if (!p) return;
    Atmosphere_SetMode(p->atmoMode);

    if (!s_hasBaseFog) {
        s_baseFog = Environment_GetFogConfig();
        s_hasBaseFog = true;
    }

    EnvFogConfig fog = s_baseFog;
    if (p->type == OW_WEATHER_STORM) {
        // Sandstorm / element storm: dense golden-ochre dust veil + strong heat shimmer
        fog.enabled = true;
        fog.color = (Color){ 216, 162, 102, 255 };
        fog.start = fminf(s_baseFog.start, 24.0f);
        fog.end   = fminf(s_baseFog.end,   145.0f);
        fog.density = fmaxf(s_baseFog.density, 0.011f);
        PostFX_SetHeatHaze(0.0010f, 0.50f, 0.92f);
    } else if (p->type == OW_WEATHER_SPIRIT_PHENOM) {
        // Mirage / Spirit tide: shimmering horizon haze + warm amber lift
        fog.color = (Color){ 238, 198, 148, 255 };
        fog.start = fminf(s_baseFog.start, 40.0f);
        fog.end   = fminf(s_baseFog.end,   195.0f);
        PostFX_SetHeatHaze(0.0016f, 0.48f, 0.92f);
    } else if (p->type == OW_WEATHER_CATACLYSM) {
        // Meteor / solar cataclysm: fiery horizon tint + mid shimmer
        fog.color = (Color){ 228, 138, 92, 255 };
        fog.start = fminf(s_baseFog.start, 35.0f);
        fog.end   = fminf(s_baseFog.end,   180.0f);
        PostFX_SetHeatHaze(0.0012f, 0.50f, 0.92f);
    } else {
        PostFX_SetHeatHaze(0.0f, 0.0f, 0.0f);
    }
    Environment_SetFogConfig(fog);

    WindMacroConfig cfg = Wind_GetMacro();
    float len = sqrtf(p->windDirection.x * p->windDirection.x + p->windDirection.y * p->windDirection.y);
    if (len > 0.001f) {
        cfg.baseDirection = (Vector3){
            (p->windDirection.x / len) * p->windSpeedMps,
            0.0f,
            (p->windDirection.y / len) * p->windSpeedMps
        };
    }
    cfg.gustAmplitude = p->windTurbulence;
    Wind_SetMacro(&cfg);

    if (p->revealHiddenNodes) {
        OWLoot_TriggerSandstormReveal();
    }
}

static void SpawnWeatherDrop(OWWeatherDropKind kind, Vector3 nearPos) {
    for (int i = 0; i < OW_MAX_WEATHER_DROPS; i++) {
        if (s_drops[i].active) continue;
        float ang = ((float)(rand() % 360)) * DEG2RAD;
        float r   = 7.0f + ((float)(rand() % 120)) * 0.1f;
        float x   = nearPos.x + cosf(ang) * r;
        float z   = nearPos.z + sinf(ang) * r;
        Vector3 sp = { x, MapManager_GetGroundHeightAt(x, z), z };
        if (DistXZ(sp, s_safeCenter) < s_safeRadius + 2.0f) {
            sp = s_fieldAnchors[i % 3];
            sp.x += cosf(ang) * 6.0f;
            sp.z += sinf(ang) * 6.0f;
            sp.y = MapManager_GetGroundHeightAt(sp.x, sp.z);
        }
        s_drops[i] = (OWWeatherDrop){
            .active = true,
            .kind   = kind,
            .pos    = sp,
            .timer  = 35.0f,
            .phase  = (float)i * 0.9f
        };
        if (kind == WEATHER_DROP_METEOR_NODE) {
            Wind_SpawnRadialBlast(sp, 6.0f, 9.0f, 0.9f);
        }
        return;
    }
}

void OWWeather_Init(const OWRealmDef *realm) {
    OWWeather_Clear();
    if (!realm) return;

    s_realmId    = realm->realmId;
    s_safeCenter = realm->safeCenter;
    s_safeRadius = realm->safeRadius;
    s_fieldAnchors[0] = realm->dunesCenter;
    s_fieldAnchors[1] = realm->canyonCenter;
    s_fieldAnchors[2] = realm->badlandsCenter;

    s_profileCount = realm->weatherProfileCount;
    if (s_profileCount > OW_MAX_WEATHER_PROFILES) s_profileCount = OW_MAX_WEATHER_PROFILES;

    if (s_profileCount > 0) {
        for (int i = 0; i < s_profileCount; i++) {
            s_profiles[i] = realm->weatherProfiles[i];
        }
    } else {
        // Fallback default clear profile if a realm omits weatherProfiles
        s_profileCount = 1;
        s_profiles[0] = (OWWeatherProfileDef){
            .type = OW_WEATHER_CLEAR,
            .name = "THOI TIET ON DINH",
            .startBanner = "",
            .defaultDurationSec = 60.0f,
            .atmoMode = ATMO_MODE_MOONLIGHT_DUST,
            .windDirection = { 0.8f, 0.5f },
            .windSpeedMps = 3.0f,
            .windTurbulence = 0.25f,
            .accentColor = (Color){ 210, 225, 245, 255 },
            .moveSpeedBonus = 0.0f,
            .xpMultiplier = 1.0f
        };
    }

    s_currentIdx   = 0;
    s_weatherTimer = (s_profiles[0].defaultDurationSec > 0.0f) ? s_profiles[0].defaultDurationSec : 60.0f;
    ApplyWeatherEnvironment(&s_profiles[0]);
}

void OWWeather_Clear(void) {
    if (s_hasBaseFog) {
        Environment_SetFogConfig(s_baseFog);
        s_hasBaseFog = false;
    }
    PostFX_SetHeatHaze(0.0f, 0.0f, 0.0f);
    memset(s_profiles, 0, sizeof(s_profiles));
    s_profileCount   = 0;
    s_currentIdx     = 0;
    s_weatherTimer   = 60.0f;
    s_vortexTimer    = 0.0f;
    s_dropSpawnTimer = 0.0f;
    s_clock          = 0.0f;
    ClearDrops();
}

void OWWeather_SetWeather(int profileIndex, float customDuration) {
    if (s_profileCount <= 0) return;
    if (profileIndex < 0) profileIndex = 0;
    profileIndex %= s_profileCount;

    s_currentIdx = profileIndex;
    const OWWeatherProfileDef *p = &s_profiles[s_currentIdx];
    s_weatherTimer = (customDuration > 0.0f) ? customDuration :
                     ((p->defaultDurationSec > 0.0f) ? p->defaultDurationSec : 55.0f);
    s_vortexTimer = 1.5f;
    s_dropSpawnTimer = 1.0f;
    ClearDrops();
    ApplyWeatherEnvironment(p);
    if (p->startBanner && p->startBanner[0] != '\0') {
        OW_SetBanner(p->startBanner, 4.0f);
    }
}

int OWWeather_GetCurrentIndex(void) { return s_currentIdx; }
int OWWeather_GetProfileCount(void) { return s_profileCount; }
float OWWeather_GetRemainingTime(void) { return s_weatherTimer; }

const OWWeatherProfileDef *OWWeather_GetCurrentProfile(void) {
    if (s_profileCount <= 0) return NULL;
    return &s_profiles[s_currentIdx];
}

const OWWeatherProfileDef *OWWeather_GetProfile(int index) {
    if (index < 0 || index >= s_profileCount) return NULL;
    return &s_profiles[index];
}

float OWWeather_GetMoveSpeedMult(void) {
    const OWWeatherProfileDef *p = OWWeather_GetCurrentProfile();
    return p ? (1.0f + fmaxf(0.0f, p->moveSpeedBonus)) : 1.0f;
}

float OWWeather_GetXpMult(void) {
    const OWWeatherProfileDef *p = OWWeather_GetCurrentProfile();
    return (p && p->xpMultiplier > 0.0f) ? p->xpMultiplier : 1.0f;
}

void OWWeather_Update(PlayerEntity *player, float dt) {
    if (s_profileCount <= 0) return;
    s_clock += dt;

    s_weatherTimer -= dt;
    if (s_weatherTimer <= 0.0f) {
        int nextIdx = (s_currentIdx + 1) % s_profileCount;
        OWWeather_SetWeather(nextIdx, 0.0f);
    }

    const OWWeatherProfileDef *cur = &s_profiles[s_currentIdx];
    const Agent *pa = player ? Entity_GetAgent(player->agentId) : NULL;
    Vector3 anchorPos = pa ? pa->position : s_fieldAnchors[0];

    if (cur->vortexSpawnInterval > 0.0f) {
        s_vortexTimer -= dt;
        if (s_vortexTimer <= 0.0f) {
            s_vortexTimer = cur->vortexSpawnInterval;
            float ang = ((float)(rand() % 360)) * DEG2RAD;
            Vector3 vp = {
                anchorPos.x + cosf(ang) * 11.0f,
                anchorPos.y,
                anchorPos.z + sinf(ang) * 11.0f
            };
            vp.y = MapManager_GetGroundHeightAt(vp.x, vp.z);
            Wind_SpawnVortex(vp, (Vector3){ 0.0f, 1.0f, 0.0f }, 7.0f, 10.0f, 2.5f, 4.5f);
        }
    }

    if (cur->spawnSpiritOrbs || cur->spawnMeteorNodes) {
        s_dropSpawnTimer -= dt;
        if (s_dropSpawnTimer <= 0.0f) {
            s_dropSpawnTimer = 5.5f;
            if (cur->spawnSpiritOrbs) SpawnWeatherDrop(WEATHER_DROP_SPIRIT_ORB, anchorPos);
            if (cur->spawnMeteorNodes) SpawnWeatherDrop(WEATHER_DROP_METEOR_NODE, anchorPos);
        }
    }

    for (int i = 0; i < OW_MAX_WEATHER_DROPS; i++) {
        OWWeatherDrop *d = &s_drops[i];
        if (!d->active) continue;
        d->phase += dt * 3.0f;
        d->timer -= dt;
        if (d->timer <= 0.0f) {
            d->active = false;
            continue;
        }
        if (pa && DistXZ(pa->position, d->pos) <= 2.1f) {
            d->active = false;
            if (d->kind == WEATHER_DROP_SPIRIT_ORB) {
                Entity_RestoreMana(player->agentId, 45.0f);
                Entity_AddModifier(player->agentId, 1.25f, 6.0f);
                if (pa->equippedSkills[0] >= 0) {
                    PlayerProfile_AddSkillXp(pa->equippedSkills[0], 45u);
                    PlayerProfile_SaveGlobal();
                }
                AddFloatingText(d->pos, "LINH CHAU AO ANH! +45 MP +45 XP", (Color){ 120, 245, 255, 255 }, 16.0f, 1.6f);
            } else {
                OWLoot_SpawnOrb(d->pos, OW_LOOT_STONE, 2);
                OWLoot_SpawnOrb(d->pos, OW_LOOT_GOLD, 95);
                AddFloatingText(d->pos, "THIEN THACH HUYEN KHOANG!", ORANGE, 16.0f, 1.6f);
            }
        }
    }
}

void OWWeather_Draw3D(const PlayerEntity *player, Camera3D cam) {
    (void)player;
    (void)cam;
    for (int i = 0; i < OW_MAX_WEATHER_DROPS; i++) {
        const OWWeatherDrop *d = &s_drops[i];
        if (!d->active) continue;
        if (d->kind == WEATHER_DROP_SPIRIT_ORB) {
            float bob = 0.85f + 0.22f * sinf(d->phase);
            Vector3 p = { d->pos.x, d->pos.y + bob, d->pos.z };
            DrawSphere(p, 0.34f, (Color){ 110, 235, 255, 235 });
            DrawCircle3D(p, 0.65f, (Vector3){ 0, 1, 0 }, s_clock * 110.0f, (Color){ 180, 255, 240, 200 });
        } else {
            DrawCylinder(d->pos, 0.55f, 0.25f, 1.05f, 7, (Color){ 195, 95, 35, 255 });
            DrawCircle3D((Vector3){ d->pos.x, d->pos.y + 0.14f, d->pos.z }, 1.25f,
                         (Vector3){ 1, 0, 0 }, 90.0f, (Color){ 255, 160, 60, 220 });
        }
    }
}

void OWWeather_DrawHUD(void) {
    const OWWeatherProfileDef *cur = OWWeather_GetCurrentProfile();
    if (!cur) return;

    int sw = GetScreenWidth();
    bool touch = UI_IsTouchControlsEnabled();
    float w = touch ? 236.0f : 262.0f;
    float x = (float)sw * 0.5f - w * 0.5f;
    float y = touch ? 108.0f : 76.0f;
    Rectangle r = { x, y, w, 24.0f };
    WuxiaUI_DrawPanel(r, cur->accentColor, false);
    const char *txt = TextFormat("%s (%.0fs)", cur->name, s_weatherTimer);
    WuxiaUI_DrawTextCentered(txt, r, 11.5f, cur->accentColor);
}
