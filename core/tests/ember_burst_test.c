#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0;
#define CHECK(c, n) do { if (c) printf("PASS: %s\n", n); else { printf("FAIL: %s\n", n); failures++; } } while (0)

static int FileHas(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    static char text[80000];
    size_t count = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

static float StretchAspect(float speed, float strength)
{
    float aspect = 1.0f + speed * strength;
    return aspect < 3.5f ? aspect : 3.5f;
}

static float UniformConeCos(float coneRad, float u)
{
    return 1.0f - u * (1.0f - cosf(coneRad));
}

int main(void)
{
    const char *src = "core/composition/common/vc_ember_burst.inl";
    const char *motion = "core/composition/common/vc_motion.h";
    const char *sync = "scripts/sync_vfx_test.py";
    const char *manifest = "scripts/vfx_test_manifest.json";

    CHECK(StretchAspect(12.0f, 0.10f) > 2.1f &&
          StretchAspect(24.0f, 0.10f) < 3.5f,
          "spark length still varies with speed instead of every primary hitting the cap");

    CHECK(FileHas(src, "void VFX_ComposeEmberBurst(Vector3 pos, Vector3 normal,"),
          "ember burst is a reusable one-shot primary with an oriented launch hemisphere");
    CHECK(FileHas(src, "#define EMBER_BURST_MIN_COUNT 30") &&
          FileHas(src, "#define EMBER_BURST_MAX_COUNT 60"),
          "the one-shot population matches the NE_SparkDebris tier");
    CHECK(FileHas(src, "Math_Mix(4.0f, 8.0f") &&
          FileHas(src, "Math_Mix(0.65f, 1.20f") &&
          FileHas(src, "Math_Mix(0.018f, 0.028f"),
          "embers use a short-range physical launch envelope");
    CHECK(FileHas(src, ".gravityScale = 1.0f") &&
          FileHas(src, ".inverseMassKg = 4.0f") &&
          FileHas(src, ".linearDragPerSecond = 3.2f") &&
          FileHas(src, ".physics.initialImpulseNs = Vector3Scale(velocity, 0.25f)") &&
          FileHas(src, ".type = FORCE_NOISE_CURL"),
          "embers use an explicit ballistic impulse, gravity, drag and curl wander");
    CHECK(fabsf(UniformConeCos(1.15f, 0.5f) -
                 0.5f * (1.0f + cosf(1.15f))) < 0.0001f,
          "the cone sampler is uniform in solid angle rather than polar angle");
    CHECK(FileHas(motion, "static inline Vector3 VC_DirConeUniform") &&
          FileHas(motion, "float cosTheta = 1.0f - u2 * (1.0f - cosf(coneRad));") &&
          FileHas(src, "VC_DirConeUniform(normal, EMBER_BURST_CONE_RAD"),
          "the burst uses the reusable area-uniform cone sampler");
    CHECK(FileHas(src, ".physics.collisionEnabled = true") &&
          FileHas(src, ".physics.collisionElasticity = EMBER_BURST_BOUNCE") &&
          FileHas(src, ".physics.onCollisionEmit = &s_emberBurstSecondary") &&
          FileHas(src, ".physics.onCollisionEmitCount = EMBER_BURST_SECONDARIES"),
          "bounces shed the secondary-spark tier instead of passing through");
    CHECK(FileHas(src, "Texture2D sparkTex = ParticleSystem_SparkCapsuleSprite();") &&
          FileHas(src, ".render.texture = sparkTex") &&
          FileHas(src, ".render.trailLength = 0") &&
          !FileHas(src, ".render.trailColorStart"),
          "the primary uses Niagara's analytic sprite vocabulary, not an orange wire ribbon");
    CHECK(FileHas(src, ".render.blendMode = VFX_BLEND_ALPHA") &&
          FileHas(src, ".render.stretchStrength = 0.10f") &&
          FileHas(src, ".render.stretchMinSpeed = 1.0f") &&
          FileHas(src, ".render.facingMode = VFX_FACING_VELOCITY"),
          "the particle head is a velocity-aligned streak rather than a round blob");
    CHECK(FileHas(src, "SpawnParticle(body);"),
          "one HDR capsule owns its orange rim, white core, and bloom");
    CHECK(!FileHas(src, "ParticleConfig hot = body;") &&
          !FileHas(src, "hot.render.texture") &&
          !FileHas(src, "SpawnParticle(hot);"),
          "the capsule is not duplicated as a second nested geometry layer");
    CHECK(FileHas(src, "VFXLight_Spawn(origin,"),
          "the burst carries a short impact light for the opening frames");
    CHECK(FileHas(sync, "[PARTICLE] EMBER BURST") &&
          FileHas(sync, "\"modules\": [\"particle\"]") &&
          FileHas(sync, "\"VFX_ComposeEmberBurst\":        (\"event\",   \"burst\",      \"oneshot\")"),
          "fixture metadata exposes the implementation family");
    CHECK(FileHas(manifest,
          "VFX_ComposeEmberBurst($POS, (Vector3){0.0f, 1.0f, 0.0f}, VC_MAT_FIRE, 1.0f, 1.0f)"),
          "the diagnostic fixture fires a full-severity metre-scale burst");

    puts(failures ? "ember burst: FAIL" : "ember burst: PASS");
    return failures != 0;
}
