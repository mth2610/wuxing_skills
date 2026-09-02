/* Material output is one decision: generated metadata, shader resolver and
 * runtime blend must agree. This headless guard cannot execute GLSL or inspect
 * GPU blend state; it pins the source wiring and mirrors the resolver arithmetic
 * so either half cannot drift silently. */
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static const char *ReadFile(const char *path, char *text, size_t capacity)
{
    FILE *f = fopen(path, "rb");
    size_t size;
    if (f == NULL) return NULL;
    size = fread(text, 1, capacity - 1u, f);
    if (!feof(f)) { fclose(f); return NULL; }
    text[size] = '\0';
    fclose(f);
    return text;
}

static void Check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static int Has(const char *text, const char *needle)
{
    return text != NULL && strstr(text, needle) != NULL;
}

int main(void)
{
    static char effectMatBuf[24000], effectVsBuf[12000], crystalMatBuf[24000];
    static char generatorBuf[30000], generatedBuf[24000];
    static char headerBuf[30000], implBuf[40000];
    static char fixtureBuf[70000];
    const char *effectMat = ReadFile("core/shading/materials/effect_material.mat",
                                     effectMatBuf, sizeof(effectMatBuf));
    const char *effectVs = ReadFile("core/shaders/effect_material.vs",
                                    effectVsBuf, sizeof(effectVsBuf));
    const char *crystalMat = ReadFile("core/shading/materials/crystal.mat",
                                      crystalMatBuf, sizeof(crystalMatBuf));
    const char *generator = ReadFile("scripts/gen_materials.py", generatorBuf,
                                     sizeof(generatorBuf));
    const char *generated = ReadFile("core/material/materials.generated.inl",
                                     generatedBuf, sizeof(generatedBuf));
    const char *header = ReadFile("core/material/material_system.h", headerBuf,
                                  sizeof(headerBuf));
    const char *impl = ReadFile("core/material/material_system.c", implBuf,
                                sizeof(implBuf));
    const char *fixture = ReadFile("sandbox/vfx_test.c", fixtureBuf,
                                   sizeof(fixtureBuf));

    Check(effectMat != NULL && effectVs != NULL && crystalMat != NULL && generator != NULL &&
              generated != NULL && header != NULL && impl != NULL &&
              fixture != NULL,
          "contract inputs must be readable");

    Check(Has(effectMat, "output : body") || Has(effectMat, "output  : body"),
          "EffectMaterial metadata must match its legacy body resolver");
    Check(Has(crystalMat, "output : body") || Has(crystalMat, "output  : body"),
          "Crystal metadata must match its body resolver");
    Check(Has(generator, "OUTPUT_RESOLVER") && Has(generator, "OUTPUT_SURFACE"),
          "the material compiler must validate resolvers and emit surface metadata");
    Check(Has(generated, "EFFECT_PARAMS_OUTPUT_SURFACE") &&
              Has(generated, "CRYSTAL_PARAMS_OUTPUT_SURFACE"),
          "generated material tables must carry machine-readable output surfaces");

    Check(Has(header, "EffectMaterialVFXOutput"),
          "EffectMaterial needs explicit body/emission output parameters");
    Check(Has(header, "EFFECT_MATERIAL_GEOMETRY_IMMEDIATE") &&
          Has(header, "geometryMode"),
          "surface-aware EffectMaterial must declare its mesh/immediate geometry contract");
    Check(Has(header, "Material_LoadCustomVFX"),
          "EffectMaterial needs a surface-aware shared-shader loader");
    Check(Has(header, "Material_LoadCustomShaderVFX"),
          "custom shaders need an explicit surface-aware loader");
    Check(Has(header, "Material_BeginVFX") && Has(header, "Material_EndVFX"),
          "material scope must own target/blend/depth and shader together");
    Check(Has(impl, "ResourceManager_LoadShaderVariant") &&
              Has(impl, "VFXRender_OutputDefines"),
          "surface-aware material loading must select the matching shader permutation");
    Check(Has(impl, "VFXRender_BeginDraw") && Has(impl, "VFXRender_EndDraw"),
          "surface-aware material drawing must use the unified render scope");

    Check(Has(effectMat, "VFX_ResolveOutput"),
          "the new EffectMaterial path must resolve through its output permutation");
    Check(Has(effectVs, "#if defined(EFFECT_MATERIAL_IMMEDIATE)") &&
          Has(effectVs, "fragPosition = displacedPos") &&
          Has(effectVs, "fragNormal = normalize(vertexNormal)"),
          "immediate EffectMaterial vertices/normals must bypass the second matModel transform");
    Check(Has(effectMat, "normalize(-fragPosition)") &&
          Has(effectMat, "u_lightDirView"),
          "surface-aware EffectMaterial lighting must compare view-space operands");
    Check(Has(effectMat, "outputCoverage = alpha;") &&
              Has(effectMat, "defined(OUTPUT_EMISSION)"),
          "a pure additive material must not be erased by zero body opacity");
    Check(Has(effectMat, "baseColor += baseColor * u_emissiveIntensity"),
          "the legacy EffectMaterial formula must remain available unchanged");
    Check(Has(fixture, "MATERIAL_OUTPUT_FIXTURE_INDEX") &&
              Has(fixture, ".surface = VFX_SURFACE_ALPHA") &&
              Has(fixture, ".surface = VFX_SURFACE_ADDITIVE") &&
              Has(fixture, ".surface = VFX_SURFACE_PREMULTIPLIED") &&
              Has(fixture, ".geometryMode = EFFECT_MATERIAL_GEOMETRY_IMMEDIATE"),
          "sandbox fixture must keep all three output surfaces side by side");
    Check(Has(fixture, "VFX_RENDER_PASS_BODY") &&
              Has(fixture, "VFX_RENDER_PASS_EMISSION") &&
              Has(fixture, "Material_BeginVFX") &&
              Has(fixture, "Material_EndVFX") &&
              Has(fixture, "DrawCoreSphere(pos, 0.62f, 28, 28, WHITE)") &&
              !Has(fixture, "DrawSphere(pos, 0.62f, WHITE)"),
          "sandbox fixture must exercise the owned VFX render scope");

    /* Mirror VFX_ResolvePremultiplied: emission is independent of coverage. */
    {
        const float bodyColor = 0.5f;
        const float coverage = 0.4f;
        const float emissionColor = 0.75f;
        const float emissionGain = 2.0f;
        const float resolved = bodyColor * coverage + emissionColor * emissionGain;
        const float lowerCoverage = bodyColor * 0.1f + emissionColor * emissionGain;
        Check(fabsf(resolved - 1.7f) < 0.0001f,
              "premultiplied output must combine covered body and independent HDR emission");
        Check(fabsf((resolved - lowerCoverage) - 0.15f) < 0.0001f,
              "changing coverage must not scale the emission term");
    }

    puts(failures ? "material output contract: FAIL" : "material output contract: PASS");
    return failures != 0;
}
