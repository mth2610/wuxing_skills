#define ARCH_CHAIN_MAX_QUEUE    32

typedef struct {
    Vector3 from, to;
    float delay, elapsed, scale;
    bool active;
} Arch_ChainEntry;

static Arch_ChainEntry  s_archChain[ARCH_CHAIN_MAX_QUEUE];

void VFX_ChainLightning(const Vector3 *points, int count,
                        float scale, float hopDelay)
{
    if (!points || count < 2) return;
    for (int i = 0; i + 1 < count; i++) {
        for (int j = 0; j < ARCH_CHAIN_MAX_QUEUE; j++) {
            if (!s_archChain[j].active) {
                s_archChain[j].active  = true;
                s_archChain[j].from    = points[i];
                s_archChain[j].to      = points[i + 1];
                s_archChain[j].delay   = hopDelay * i;
                s_archChain[j].elapsed = 0.0f;
                s_archChain[j].scale   = scale;
                break;
            }
        }
    }
}

static void VC_ChainLightning_Update(float dt)
{
    for (int i = 0; i < ARCH_CHAIN_MAX_QUEUE; i++) {
        if (!s_archChain[i].active) continue;
        s_archChain[i].elapsed += dt;
        if (s_archChain[i].elapsed >= s_archChain[i].delay) {
            SpawnLightningTrail(s_archChain[i].from, s_archChain[i].to,
                                s_archChain[i].scale, 8.0f);
            s_archChain[i].active = false;
        }
    }
}

static void VC_ChainLightning_Draw3D(Camera3D cam)
{
    (void)cam;
}
