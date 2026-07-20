// VFX_ComposeTornado — Lốc xoáy liên tục, element-agnostic
//
// Continuous: gọi một lần mỗi frame khi lốc còn active.
//
// Kiến trúc 4 layer, mỗi layer giải quyết một vấn đề kỹ thuật riêng:
//
//  Layer 1 — VortexFunnel mesh + AuraShellMaterial shader
//            Hình học phễu chóp thay đổi theo time (twist spin), shader
//            FBM tạo filament cuộn lên theo trục Y. Rẻ: 1 draw call,
//            toàn bộ animation trong shader.
//
//  Layer 2 — TRAIL_TYPE_FOLLOWER xoắn ốc
//            N trail tip di chuyển theo helix leo dọc phễu mỗi frame.
//            Trail ghi lại lịch sử → tạo "dải lốc" nhìn thấy được,
//            smoothSpline nội suy mượt. Rẻ hơn nhiều so với ribbon thủ
//            công — trail system lo geometry.
//
//  Layer 3 — GpuParticleSystem_Spawn (debris + váy bụi đáy)
//            Hàng trăm hạt debris nhỏ vortex xoay + bay lên, ForceField
//            VORTEX + UPDRAFT. GPU path tính vật lý song song; CPU/VBO
//            path fallback tự động — caller không cần quan tâm.
//
//  Layer 4 — CPU particle váy đất + decal + distort + light
//            Váy bụi thấp (< 0.3m) dày đặc che gốc phễu; ground decal
//            xoay; distort cột không khí; light nhấp nháy.
//
// Tham số:
//   matId  — nguyên tố (màu body/glow từ bảng material)
//   pos    — gốc lốc trên mặt đất
//   radius — bán kính đáy (m)
//   height — chiều cao phễu (m)
//   time   — elapsed time tích lũy (dùng cho spin + helix phase)

#include "compute/gpu_particle_system.h"

// ── Pool state: helix trail handles ─────────────────────────────────────────
// Mỗi lần gọi VFX_ComposeTornado từ một "instance" lốc cụ thể, nó tự
// manage pool helix qua static array. Vì composition là stateless về
// design, ta dùng một pool cố định cho toàn bộ lốc đang active đồng thời.
// MAX_TORNADO_HELIXES phải ≥ N_HELIX_STRANDS * MAX_CONCURRENT_TORNADOES.

#define N_HELIX_STRANDS       5    // số dải cuốn song song quanh phễu
#define MAX_TORNADO_INSTANCES 4    // số lốc đồng thời tối đa
#define MAX_TORNADO_HELIXES  (N_HELIX_STRANDS * MAX_TORNADO_INSTANCES)

typedef struct {
    bool  active;
    int   trailIds[N_HELIX_STRANDS];
    float spawnTime;   // GetTime() lúc instance này được assign
    Vector3 pos;       // pos khi assign, dùng để detect "lốc mới" từ pos khác
} TornadoHelixSlot;

static TornadoHelixSlot s_tornadoSlots[MAX_TORNADO_INSTANCES];
static bool s_tornadoSlotsInit = false;

static void TornadoSlots_EnsureInit(void)
{
    if (s_tornadoSlotsInit) return;
    for (int i = 0; i < MAX_TORNADO_INSTANCES; i++) {
        s_tornadoSlots[i].active = false;
        for (int k = 0; k < N_HELIX_STRANDS; k++)
            s_tornadoSlots[i].trailIds[k] = -1;
    }
    s_tornadoSlotsInit = true;
}

// Tìm slot đang track pos này (distance < 0.5m) hoặc claim slot mới
static int TornadoSlots_Acquire(Vector3 pos)
{
    TornadoSlots_EnsureInit();
    float threshold = 0.5f;
    // Tìm slot đang active gần pos
    for (int i = 0; i < MAX_TORNADO_INSTANCES; i++) {
        if (!s_tornadoSlots[i].active) continue;
        Vector3 d = Vector3Subtract(s_tornadoSlots[i].pos, pos);
        if (Vector3LengthSqr(d) < threshold * threshold) return i;
    }
    // Claim slot free
    for (int i = 0; i < MAX_TORNADO_INSTANCES; i++) {
        if (!s_tornadoSlots[i].active) {
            s_tornadoSlots[i].active = true;
            s_tornadoSlots[i].spawnTime = (float)GetTime();
            s_tornadoSlots[i].pos = pos;
            for (int k = 0; k < N_HELIX_STRANDS; k++)
                s_tornadoSlots[i].trailIds[k] = -1;
            return i;
        }
    }
    return -1; // pool đầy
}

static void TornadoSlots_Release(int slot)
{
    if (slot < 0 || slot >= MAX_TORNADO_INSTANCES) return;
    for (int k = 0; k < N_HELIX_STRANDS; k++) {
        if (s_tornadoSlots[slot].trailIds[k] >= 0) {
            KillTrail(s_tornadoSlots[slot].trailIds[k]);
            s_tornadoSlots[slot].trailIds[k] = -1;
        }
    }
    s_tornadoSlots[slot].active = false;
}

// ── Gradient / ForceField dùng chung ────────────────────────────────────────

static ColorGradient s_helixGrad = {0};   // helix trail: sáng ở đầu, mờ ở đuôi
static ColorGradient s_debrisGrad = {0};  // GPU debris: bụi/mảnh vụn
static ColorGradient s_dustSkirtGrad = {0}; // CPU váy đất thấp
static bool s_tornadoGradInit = false;

static ForceField s_tornadoVortexFld = {0}; // ForceField VORTEX+UPDRAFT cho GPU particle
static bool s_tornadoFldInit = false;

static void Tornado_InitShared(const VFX_ElementMaterial *mat, Vector3 pos,
                                float radius, float height)
{
    if (!s_tornadoGradInit) {
        // Helix trail: bán trong suốt, màu tint nguyên tố
        ColorGradient_AddStop(&s_helixGrad, 0.0f,  (Color){255, 255, 255, 200}); // head bright
        ColorGradient_AddStop(&s_helixGrad, 0.25f, (Color){200, 200, 200, 160});
        ColorGradient_AddStop(&s_helixGrad, 0.7f,  (Color){100, 100, 100,  70});
        ColorGradient_AddStop(&s_helixGrad, 1.0f,  (Color){ 50,  50,  50,   0});

        // Debris GPU: bụi nâu-xám
        ColorGradient_AddStop(&s_debrisGrad, 0.0f,  (Color){160, 145, 120, 180});
        ColorGradient_AddStop(&s_debrisGrad, 0.5f,  (Color){120, 108,  88, 120});
        ColorGradient_AddStop(&s_debrisGrad, 1.0f,  (Color){ 60,  54,  44,   0});

        // Váy đất: bụi thấp, đậm hơn debris
        ColorGradient_AddStop(&s_dustSkirtGrad, 0.0f,  (Color){90, 82, 70,   0});
        ColorGradient_AddStop(&s_dustSkirtGrad, 0.2f,  (Color){110, 98, 82, 160});
        ColorGradient_AddStop(&s_dustSkirtGrad, 0.75f, (Color){ 80, 72, 60,  80});
        ColorGradient_AddStop(&s_dustSkirtGrad, 1.0f,  (Color){ 40, 36, 30,   0});

        s_tornadoGradInit = true;
    }

    // ForceField cho GPU particle: rebuild mỗi frame theo pos mới
    // (origin của VORTEX phải theo pos thực tế)
    ForceField_Clear(&s_tornadoVortexFld);
    ForceField_AddLayer(&s_tornadoVortexFld, (ForceLayer){
        .type      = FORCE_VORTEX,
        .origin    = pos,
        .direction = (Vector3){0.0f, 1.0f, 0.0f},
        .strength  = 7.0f,
        .radius    = radius * 3.0f,
        .falloff   = 1.0f});
    ForceField_AddLayer(&s_tornadoVortexFld, (ForceLayer){
        .type      = FORCE_GRAVITY_DIR,
        .direction = (Vector3){0.0f, 1.0f, 0.0f},
        .strength  = 2.5f});           // updraft
    ForceField_AddLayer(&s_tornadoVortexFld, (ForceLayer){
        .type     = FORCE_VISCOSITY,
        .strength = 0.3f});
    s_tornadoFldInit = true;

    (void)mat; (void)height;
}

// ── Main composition ─────────────────────────────────────────────────────────

void VFX_ComposeTornado(VC_MaterialId matId, Vector3 pos, float radius,
                         float height, float time)
{
    TaijiFx_InitShared();
    const VFX_ElementMaterial *mat = VFX_Material(matId);

    // Override gradient màu theo nguyên tố mỗi frame (head color)
    // — chỉ cần đổi 2 stop đầu, tail vẫn là xám
    s_helixGrad.stops[0].color = VC_WithAlpha(mat->glow, 200);
    s_helixGrad.stops[1].color = VC_WithAlpha(mat->body, 160);
    // Ensure init dù grad chưa được init lần đầu
    if (!s_tornadoGradInit) Tornado_InitShared(mat, pos, radius, height);
    Tornado_InitShared(mat, pos, radius, height); // rebuild ForceField theo pos mới

    float dt = GetFrameTime();
    int slot = TornadoSlots_Acquire(pos);

    // ── LAYER 1: VortexFunnel mesh + AuraShellMaterial ───────────────────────
    // Phễu hẹp đáy, mở rộng lên đỉnh. twistAmount xoay theo time tạo spin.
    {
        static AuraShellMaterial s_tornadoShellMat;
        static bool s_shellLoaded = false;
        if (!s_shellLoaded) {
            AuraShellMaterialParams p = {
                .bodyColor    = WHITE,
                .glowColor    = WHITE,
                .opacity      = 0.28f,
                .fresnelPower = 1.8f,
                .rimStrength  = 0.9f,
                .scrollSpeed  = 2.5f,   // cuộn nhanh — gió lốc
                .noiseScale   = 3.5f,
                .heightScale  = 2.2f,
                .scanFreq     = 6.0f,
                .scanSpeed    = 3.0f,
                .scanStrength = 0.45f,
                .displaceAmp  = 0.0f,
                .topY         = 0.0f,
            };
            AuraShellMaterial_Load(&s_tornadoShellMat, &p);
            s_shellLoaded = true;
        }

        // Cập nhật màu và geometry bounds mỗi frame
        s_tornadoShellMat.params.bodyColor = VC_WithAlpha(mat->body, 210);
        s_tornadoShellMat.params.glowColor = VC_WithAlpha(mat->glow, 255);
        s_tornadoShellMat.params.displaceAmp = radius * 0.06f;
        s_tornadoShellMat.params.topY = pos.y + height;

        VortexFunnelConfig fCfg = ProceduralMesh_DefaultVortexFunnelConfig();
        fCfg.bottomRadius = radius * 0.18f;       // hẹp ở đáy — chóp lốc
        fCfg.topRadius    = radius * 1.0f;         // mở rộng ở đỉnh
        fCfg.height       = height;
        fCfg.twistAmount  = fmodf(time * 180.0f, 360.0f); // spin liên tục
        fCfg.ridgeCount   = 3;
        fCfg.ridgeAmount  = 0.12f;                // gờ xoắn nhẹ

        VortexFunnelMeshData funnelData;
        ProceduralMesh_BuildVortexFunnel(&funnelData, pos, &fCfg,
                                          16, 24, time);

        rlDrawRenderBatchActive();
        BeginBlendMode(BLEND_ADDITIVE);
        rlDisableDepthMask();
        rlDisableBackfaceCulling();

        AuraShellMaterial_Begin(s_tornadoShellMat);
        ProceduralMesh_DrawVortexFunnel(&funnelData, WHITE);
        AuraShellMaterial_End();

        rlEnableBackfaceCulling();
        rlEnableDepthMask();
        EndBlendMode();
    }

    // ── LAYER 2: Helix FOLLOWER trails ──────────────────────────────────────
    // N strand xoắn ốc leo dọc phễu. Mỗi strand có phase lệch đều.
    // Trail tip được tính từ VC_MotionHelix mỗi frame, push qua
    // UpdateFollowerPosition → trail record history → smoothSpline render.
    if (slot >= 0) {
        // Màu gradient đã override ở trên theo nguyên tố

        for (int k = 0; k < N_HELIX_STRANDS; k++) {
            float phase = ((float)k / (float)N_HELIX_STRANDS) * 2.0f * PI;

            // Spawn trail nếu chưa có hoặc đã chết
            if (s_tornadoSlots[slot].trailIds[k] < 0) {
                TrailConfig tcfg    = {0};
                tcfg.type           = TRAIL_TYPE_FOLLOWER;
                tcfg.pos            = pos;
                tcfg.thick          = 0.018f;
                tcfg.len            = 0.025f;
                tcfg.trailLength    = 28.0f;  // node count — vệt dài
                tcfg.life           = 999.0f; // sống đến khi bị KillTrail
                tcfg.gradient       = &s_helixGrad;
                tcfg.widthEnvelope  = TRAIL_WIDTH_ENVELOPE_TAPER_BOTH;
                tcfg.smoothSpline   = true;
                tcfg.priority       = VFX_PRIORITY_LOW;

                int tid = SpawnTrailEntity(tcfg);
                s_tornadoSlots[slot].trailIds[k] = tid;

                // Seed vị trí ban đầu đúng ngay lập tức
                if (tid >= 0) {
                    Vector3 initTip = VC_MotionHelix(pos, radius * 0.5f,
                                                      0.0f, 2.5f, time, phase);
                    UpdateFollowerPosition(tid, initTip);
                }
            }

            // Cập nhật tip mỗi frame theo helix leo dọc phễu
            int tid = s_tornadoSlots[slot].trailIds[k];
            if (tid >= 0) {
                // riseSpeed = 0 → tip xoắn ốc tại cùng height rồi leo dần
                // theo time. Radius helix thu nhỏ tỷ lệ với height để
                // khớp hình phễu (đáy hẹp, đỉnh rộng).
                float helixPhase = time + phase;
                float heightT    = fmodf(time * 0.4f + phase * 0.15f, 1.0f);
                float tipY       = pos.y + heightT * height;
                // Radius tại height: linear lerp từ đáy hẹp lên đỉnh rộng
                float tipR       = radius * (0.18f + 0.82f * heightT);

                Vector3 tip = {
                    pos.x + cosf(helixPhase * 2.0f * PI / 1.0f) * tipR,
                    tipY,
                    pos.z + sinf(helixPhase * 2.0f * PI / 1.0f) * tipR
                };
                UpdateFollowerPosition(tid, tip);
            }
        }
    }

    // ── LAYER 3: GPU particle — debris + bụi xoáy ───────────────────────────
    // Spawn rate tỷ lệ với dt để framerate-independent.
    // ForceField VORTEX+UPDRAFT đã được init trong Tornado_InitShared.
    {
        // Debris: mảnh vụn bay cao trong lòng phễu
        int debrisPerFrame = (int)(60.0f * dt) + (Random01() < (60.0f * dt - (int)(60.0f * dt)) ? 1 : 0);
        for (int i = 0; i < debrisPerFrame; i++) {
            float a  = Random01() * 2.0f * PI;
            float rr = radius * (0.1f + Random01() * 0.8f);
            float vy = 1.5f + Random01() * 3.0f;
            // tangent velocity theo vortex
            float vTan = 2.0f + Random01() * 3.0f;

            GpuParticleSystem_Spawn((GpuParticleConfig){
                .position   = (Vector3){
                    pos.x + cosf(a) * rr,
                    pos.y + Random01() * height * 0.3f,
                    pos.z + sinf(a) * rr},
                .velocity   = (Vector3){
                    -sinf(a) * vTan,
                    vy,
                    cosf(a) * vTan},
                .colorStart = ColorGradient_Sample(&s_debrisGrad, 0.0f),
                .colorEnd   = ColorGradient_Sample(&s_debrisGrad, 1.0f),
                .radius     = 0.012f + Random01() * 0.018f,
                .lifetime   = 0.8f + Random01() * 1.2f,
                .drag       = 0.05f,
                .forceField = &s_tornadoVortexFld,
                .axisOrigin = pos,
                .axisDir    = (Vector3){0.0f, 1.0f, 0.0f}});
        }

        // Rim debris: mảnh vụn nhỏ văng ra từ vành ngoài
        if (Random01() < 25.0f * dt) {
            float a   = Random01() * 2.0f * PI;
            float rimR = radius * (0.85f + Random01() * 0.3f);
            GpuParticleSystem_Spawn((GpuParticleConfig){
                .position   = (Vector3){
                    pos.x + cosf(a) * rimR,
                    pos.y + Random01() * height * 0.6f,
                    pos.z + sinf(a) * rimR},
                .velocity   = (Vector3){
                    cosf(a) * (1.5f + Random01() * 2.0f),
                    Random01() * 1.0f,
                    sinf(a) * (1.5f + Random01() * 2.0f)},
                .colorStart = ColorGradient_Sample(&s_debrisGrad, 0.2f),
                .colorEnd   = ColorGradient_Sample(&s_debrisGrad, 1.0f),
                .radius     = 0.008f + Random01() * 0.010f,
                .lifetime   = 0.5f + Random01() * 0.5f,
                .drag       = 0.15f});
        }
    }

    // ── LAYER 4: CPU váy bụi đáy + decal + distort + light ──────────────────
    {
        // Váy bụi thấp: hạt lớn, mờ, gần mặt đất
        static ForceField s_skirtFld = {0};
        if (s_skirtFld.layerCount == 0) {
            ForceField_AddLayer(&s_skirtFld, (ForceLayer){
                .type = FORCE_NOISE_CURL, .strength = 1.2f,
                .noiseScale = 2.0f, .noiseSpeed = 0.8f});
            ForceField_AddLayer(&s_skirtFld, (ForceLayer){
                .type = FORCE_VISCOSITY, .strength = 3.5f});
        }

        int skirtCount = (int)(30.0f * dt) + (Random01() < (30.0f * dt - (int)(30.0f * dt)) ? 1 : 0);
        for (int i = 0; i < skirtCount; i++) {
            float a  = Random01() * 2.0f * PI;
            float rr = radius * (0.4f + Random01() * 0.8f);
            SpawnParticle((ParticleConfig){
                .position = (Vector3){
                    pos.x + cosf(a) * rr,
                    pos.y + Random01() * radius * 0.25f,
                    pos.z + sinf(a) * rr},
                .velocity = (Vector3){
                    -sinf(a) * (0.6f + Random01() * 0.8f),
                    0.05f + Random01() * 0.1f,
                    cosf(a) * (0.6f + Random01() * 0.8f)},
                .radius   = 0.06f + Random01() * 0.08f,
                .lifetime = 0.6f + Random01() * 0.5f,
                .gradient = &s_dustSkirtGrad,
                .forceField = &s_skirtFld});
        }

        // Ground decal xoay: vòng gió xoáy trên mặt đất
        if (GetRandomValue(0, 100) < 8) {
            Texture2D windTex = ResourceManager_LoadTexture(
                "assets/textures/decals/decal_wind_groove.png");
            DecalSystem_AddEx(pos, time * 90.0f, 15.0f,
                              radius * 0.7f, radius * 1.6f,
                              windTex, 0.5f,
                              ColorAlpha(mat->body, 100),
                              BLEND_ADDITIVE, 0.01f);
        }

        // Distort cột không khí — rung không gian trên trục lốc
        if (GetRandomValue(0, 100) < 5) {
            ScreenDistort_Add(
                Vector3Add(pos, (Vector3){0, height * 0.45f, 0}),
                radius * 1.1f, 0.11f, 0.6f, 1.8f);
        }

        // Ambient light nhấp nháy theo breathe
        if (GetRandomValue(0, 100) < 12) {
            float breathe = VC_Breathe(time, 2.2f, 0.35f);
            VFXLight_Spawn(
                Vector3Add(pos, (Vector3){0, height * 0.25f, 0}),
                mat->soft, radius * 2.0f * breathe, 0.2f, VFX_PRIORITY_LOW);
        }
    }
}

// Giải phóng helix trail khi lốc kết thúc — gọi thủ công khi dừng lốc
void VFX_ReleaseTornado(Vector3 pos)
{
    TornadoSlots_EnsureInit();
    float threshold = 0.5f;
    for (int i = 0; i < MAX_TORNADO_INSTANCES; i++) {
        if (!s_tornadoSlots[i].active) continue;
        Vector3 d = Vector3Subtract(s_tornadoSlots[i].pos, pos);
        if (Vector3LengthSqr(d) < threshold * threshold) {
            TornadoSlots_Release(i);
            return;
        }
    }
}
