#define ARCH_MAX_SMOKE_COLUMNS  6
#define SMOKE_COLUMN_FADE 0.35f

typedef struct {
    bool active;
    bool dying;      
    Vector3 pos;
    float halfWidth, height;
    int   planeCount;
    float duration, elapsed;  
    float dyingElapsed;
} Arch_SmokeColumn;

static Arch_SmokeColumn s_archSmokeColumns[ARCH_MAX_SMOKE_COLUMNS];

int VFX_SpawnSmokeColumn(Vector3 pos, float duration)
{
    for (int i = 0; i < ARCH_MAX_SMOKE_COLUMNS; i++) {
        if (!s_archSmokeColumns[i].active) {
            s_archSmokeColumns[i].active     = true;
            s_archSmokeColumns[i].dying      = false;
            s_archSmokeColumns[i].pos        = pos;
            s_archSmokeColumns[i].halfWidth  = 0.5f;  
            s_archSmokeColumns[i].height     = 3.5f;
            s_archSmokeColumns[i].planeCount = 1;
            s_archSmokeColumns[i].duration   = duration;
            s_archSmokeColumns[i].elapsed    = 0.0f;
            s_archSmokeColumns[i].dyingElapsed = 0.0f;
            return i;
        }
    }
    return -1;
}

void VFX_KillSmokeColumn(int handle)
{
    if (handle < 0 || handle >= ARCH_MAX_SMOKE_COLUMNS || !s_archSmokeColumns[handle].active)
        return;
    s_archSmokeColumns[handle].dying = true;
}

static void VC_SmokeColumn_Update(float dt)
{
    for (int i = 0; i < ARCH_MAX_SMOKE_COLUMNS; i++) {
        if (!s_archSmokeColumns[i].active) continue;
        s_archSmokeColumns[i].elapsed += dt;
        if (!s_archSmokeColumns[i].dying) {
            if (s_archSmokeColumns[i].duration > 0.0f &&
                s_archSmokeColumns[i].elapsed >= s_archSmokeColumns[i].duration)
                s_archSmokeColumns[i].dying = true;
        } else {
            s_archSmokeColumns[i].dyingElapsed += dt;
            if (s_archSmokeColumns[i].dyingElapsed >= SMOKE_COLUMN_FADE)
                s_archSmokeColumns[i].active = false;
        }
    }
}

static void VC_SmokeColumn_Draw3D(Camera3D cam)
{
    (void)cam;
    VFX_BeginSmokeColumnBatch();
    for (int i = 0; i < ARCH_MAX_SMOKE_COLUMNS; i++) {
        if (!s_archSmokeColumns[i].active) continue;
        float progress = s_archSmokeColumns[i].dying
            ? Clamp(1.0f - s_archSmokeColumns[i].dyingElapsed / SMOKE_COLUMN_FADE, 0.0f, 1.0f)
            : Clamp(s_archSmokeColumns[i].elapsed / SMOKE_COLUMN_FADE, 0.0f, 1.0f);
        VFX_ComposeSmokeColumnFX(s_archSmokeColumns[i].pos, s_archSmokeColumns[i].halfWidth,
                                 s_archSmokeColumns[i].height, progress,
                                 s_archSmokeColumns[i].planeCount);
    }
    VFX_EndSmokeColumnBatch();
}
