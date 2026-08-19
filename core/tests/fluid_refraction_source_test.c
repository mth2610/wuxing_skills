// core headless test — the SSF refraction tap must not sample the target it draws into.
//
// The water's transparency is produced by ONE sample: `u_sceneTex` in
// fluid_surface.fs. If that texture is also the colour attachment currently bound,
// the read is undefined in GL and a read/write hazard in Vulkan — the tap stops
// returning the background and the water collapses to its own opaque terms
// (in-scatter + specular), which reads as cyan plastic with a silver rim.
//
// That is what happened when the split VFX layers were retired (b03b7b6,
// 2026-08-10): `SceneTargets_BeginVFXBody()` stopped binding a separate
// `vfxBodyTex` and began binding `renderTex` — the exact texture
// `SceneTargets_GetSceneTexture()` hands out.
//
// This test encodes the RULE, not the workaround: it first detects whether the
// body pass and the scene getter still name the same target, and only then demands
// that the fluid composite sample a private copy. Restore separate layers and the
// requirement lapses on its own.
//
// What it cannot validate: that the copy is pixel-correct, correctly oriented, or
// taken at the right moment. Only the sandbox fixture (NEW FX tab, WATER ORB) shows
// that — a wrong flip would look like refraction sampling the mirrored screen.
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { bad++; printf("FAIL: %s\n", #expr); } } while (0)

static char *ReadFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *text = (char *)malloc((size_t)n + 1);
    if (!text) { fclose(f); return NULL; }
    size_t got = fread(text, 1, (size_t)n, f);
    text[got] = '\0'; fclose(f); return text;
}

// Body of a function, from its signature to the first line starting with '}'.
static char *FunctionBody(const char *src, const char *signature)
{
    const char *start = strstr(src, signature);
    if (!start) return NULL;
    const char *end = strstr(start, "\n}");
    if (!end) return NULL;
    size_t len = (size_t)(end - start);
    char *body = (char *)malloc(len + 1);
    if (!body) return NULL;
    memcpy(body, start, len); body[len] = '\0';
    return body;
}

int main(void)
{
    int bad = 0;

    char *distort = ReadFile("core/scene_targets.c");
    char *fluid = ReadFile("core/fluid/fluid_surface.c");
    if (!distort || !fluid) {
        printf("FAIL: cannot read core/scene_targets.c or core/fluid/fluid_surface.c\n");
        free(distort); free(fluid);
        return 1;
    }

    // Does the body pass bind the same target the scene getter exposes?
    char *bodyPass = FunctionBody(distort, "void SceneTargets_BeginVFXBody(void)");
    CHECK(bodyPass != NULL);
    bool bodyBindsSceneTarget = bodyPass && strstr(bodyPass, "rlEnableFramebuffer(renderTex.id)") != NULL;
    bool getterIsSceneTarget = strstr(distort, "SceneTargets_GetSceneTexture(void) { return renderTex.texture; }") != NULL;
    free(bodyPass);

    printf("      body pass binds renderTex: %d | scene getter returns renderTex.texture: %d\n",
           (int)bodyBindsSceneTarget, (int)getterIsSceneTarget);

    if (bodyBindsSceneTarget && getterIsSceneTarget)
    {
        // The hazard condition holds, so the fluid composite owes us a private copy.
        char *composite = FunctionBody(fluid, "void FluidSurface_Composite(void)");
        CHECK(composite != NULL);
        if (composite)
        {
            // It must bind the copy...
            CHECK(strstr(composite, "s_sceneCopy.texture") != NULL);
            // ...and must not hand the live target straight to the shader. (The
            // fallback `s_sceneCopy.id ? ... : SceneTargets_GetSceneTexture()` is
            // the only tolerated mention, so require the copy to be chosen first.)
            const char *liveUse = strstr(composite, "SceneTargets_GetSceneTexture()");
            const char *copyUse = strstr(composite, "s_sceneCopy.id?");
            CHECK(liveUse == NULL || (copyUse != NULL && copyUse < liveUse));
            free(composite);
        }
        // The copy has to be taken while the scene is only a source — Capture runs
        // before main.c's SceneTargets_BeginVFXBody(), the composite does not.
        char *capture = FunctionBody(fluid, "void FluidSurface_Capture(Camera3D camera)");
        CHECK(capture != NULL);
        if (capture)
        {
            CHECK(strstr(capture, "BeginTextureMode(s_sceneCopy)") != NULL);
            CHECK(strstr(capture, "SceneTargets_GetSceneTexture()") != NULL);
            free(capture);
        }
        // And the ordering the copy depends on must still be the one in main.c.
        char *main_c = ReadFile("main.c");
        CHECK(main_c != NULL);
        // Scope the ordering to the composite step: the body pass is also entered by
        // the ordinary VFX layer pass earlier in the file.
        char *step = main_c ? FunctionBody(main_c, "static void CompositeScreenSpaceVFX(Camera3D camera)") : NULL;
        CHECK(step != NULL);
        if (step)
        {
            const char *cap = strstr(step, "FluidSurface_Capture(camera)");
            const char *begin = strstr(step, "VFXRender_BeginPass(VFX_RENDER_PASS_BODY)");
            const char *comp = strstr(step, "FluidSurface_Composite()");
            CHECK(cap && begin && comp);
            CHECK(cap && begin && cap < begin);      // snapshot before the target is bound
            CHECK(begin && comp && begin < comp);    // composite inside the body pass
            free(step);
        }
        free(main_c);
    }
    else
    {
        printf("      separate body layer restored — the private copy is no longer required\n");
    }

    free(distort); free(fluid);
    printf("%s: fluid_refraction_source_test (%d failures)\n", bad ? "FAIL" : "PASS", bad);
    return bad ? 1 : 0;
}
