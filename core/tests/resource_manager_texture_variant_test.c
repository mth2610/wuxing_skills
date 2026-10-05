/* Execute production cache/lifetime code with deterministic graphics-call
   substitutes. This checks ownership and metadata, not actual GPU mip contents. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"
typedef struct { int unused; } Sound;
typedef struct { Texture2D texture; } Font;
typedef struct { int meshCount; } Model;
typedef struct { int unused; } ModelAnimation;
typedef struct { void *data; int width, height, mipmaps, format; } Image;
enum { PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 = 7 };
enum { TEXTURE_FILTER_POINT, TEXTURE_FILTER_BILINEAR, TEXTURE_FILTER_TRILINEAR,
       TEXTURE_FILTER_ANISOTROPIC_4X, TEXTURE_FILTER_ANISOTROPIC_8X, TEXTURE_FILTER_ANISOTROPIC_16X };
enum { TEXTURE_WRAP_REPEAT, TEXTURE_WRAP_CLAMP, TEXTURE_WRAP_MIRROR_REPEAT, TEXTURE_WRAP_MIRROR_CLAMP };
enum { LOG_WARNING };
#define RL_FREE free
static int failures, loads, mipCalls, filterCalls, wrapCalls, failMipmaps;
static int imageLoads, imageUnloads, imageUploads, imageConversions, failConversion, failUpload;
static int unsupportedAnisotropy;
static unsigned int nextId = 1;
static int unloads[256], filters[256], wraps[256];
static int filterHistory[256][4], filterHistoryCount[256];
#define CHECK(c, label) do { if (!(c)) { puts("FAIL: " label); ++failures; } } while (0)
Texture2D LoadTexture(const char *path) {
    ++loads;
    if (!strcmp(path, "missing")) return (Texture2D){0};
    return (Texture2D){nextId++, 8, 4, 1, 0};
}
void UnloadTexture(Texture2D t) { ++unloads[t.id]; }
Image LoadImage(const char *path) {
    ++imageLoads;
    if (!strcmp(path, "missing")) return (Image){0};
    static char pixels;
    return (Image){&pixels, 8, 4, 4, 1};
}
void ImageFormat(Image *image, int format) {
    ++imageConversions;
    CHECK(image->mipmaps == 1, "pre-baked image uploads only its base level");
    if (!failConversion) image->format = format;
}
Texture2D LoadTextureFromImage(Image image) {
    ++imageUploads;
    CHECK(image.mipmaps == 1 && image.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
          "mip generation receives a single RGBA8 base level");
    if (failUpload) return (Texture2D){0};
    Texture2D texture = LoadTexture("image");
    texture.format = image.format;
    return texture;
}
void UnloadImage(Image image) { (void)image; ++imageUnloads; }
void GenTextureMipmaps(Texture2D *t) { ++mipCalls; if (!failMipmaps) t->mipmaps = 4; }
void SetTextureFilter(Texture2D t, int f) {
    ++filterCalls;
    if (filterHistoryCount[t.id] < 4) filterHistory[t.id][filterHistoryCount[t.id]++] = f;
    if (!unsupportedAnisotropy || f < TEXTURE_FILTER_ANISOTROPIC_4X) filters[t.id] = f;
}
void SetTextureWrap(Texture2D t, int w) { ++wrapCalls; wraps[t.id] = w; }
void TraceLog(int level, const char *text, ...) { (void)level; (void)text; }
char *ShaderPreprocessor_LoadWithDefines(const char *path, const char *defines) { (void)path; (void)defines; return NULL; }
Shader LoadShaderFromMemory(const char *vs, const char *fs) { (void)vs; (void)fs; return (Shader){0}; }
void UnloadShader(Shader s) { (void)s; }
Sound LoadSound(const char *path) { (void)path; return (Sound){0}; }
void UnloadSound(Sound s) { (void)s; }
Font GetFontDefault(void) { return (Font){0}; }
Font LoadFontEx(const char *p, int size, int *cp, int count) { (void)p; (void)size; (void)cp; (void)count; return (Font){0}; }
void UnloadFont(Font f) { (void)f; }
Model LoadModel(const char *path) { (void)path; return (Model){0}; }
void UnloadModel(Model m) { (void)m; }
ModelAnimation *LoadModelAnimations(const char *p, int *count) { (void)p; *count = 0; return NULL; }
void UnloadModelAnimations(ModelAnimation *a, int n) { (void)a; (void)n; }
#ifndef RESOURCE_MANAGER_IMPLEMENTATION
#define RESOURCE_MANAGER_IMPLEMENTATION "core/resource_manager.c"
#endif
#include RESOURCE_MANAGER_IMPLEMENTATION

int main(void) {
    ResourceManager_Init();
    Texture2D legacy = ResourceManager_LoadTexture("noise");
    CHECK(ResourceManager_LoadTexture("noise").id == legacy.id && loads == 1, "legacy path-only reuse unchanged");
    Texture2D variant = ResourceManager_LoadTextureVariant("noise", true, TEXTURE_FILTER_TRILINEAR, TEXTURE_WRAP_REPEAT);
    CHECK(variant.id != 0 && variant.id != legacy.id, "variant owns a distinct GPU texture");
    CHECK(variant.mipmaps == 4 && mipCalls == 1, "mipmap metadata is generated before caching");
    CHECK(imageLoads == 1 && imageConversions == 1 && imageUploads == 1 && imageUnloads == 1,
          "source image normalization is owned and released exactly once");
    CHECK(filters[variant.id] == TEXTURE_FILTER_TRILINEAR && wraps[variant.id] == TEXTURE_WRAP_REPEAT,
          "requested sampler options applied to variant");
    CHECK(ResourceManager_LoadTextureVariant("noise", true, TEXTURE_FILTER_TRILINEAR, TEXTURE_WRAP_REPEAT).id == variant.id && loads == 2,
          "identical variant reuses one allocation");
    CHECK(ResourceManager_LoadTextureVariant("noise", true, TEXTURE_FILTER_TRILINEAR, TEXTURE_WRAP_REPEAT).mipmaps == 4,
          "cached return retains generated mipmap count");
    CHECK(ResourceManager_LoadTexture("noise").id == legacy.id && ResourceManager_LoadTexture("noise").mipmaps == 1 &&
          filterCalls == 1 && wrapCalls == 1, "legacy texture metadata and sampler remain unchanged");
    Texture2D plain = ResourceManager_LoadTextureVariant("noise", false, TEXTURE_FILTER_POINT, TEXTURE_WRAP_REPEAT);
    Texture2D clamp = ResourceManager_LoadTextureVariant("noise", true, TEXTURE_FILTER_TRILINEAR, TEXTURE_WRAP_CLAMP);
    Texture2D linear = ResourceManager_LoadTextureVariant("noise", true, TEXTURE_FILTER_BILINEAR, TEXTURE_WRAP_REPEAT);
    Texture2D noMips = ResourceManager_LoadTextureVariant("noise", false, TEXTURE_FILTER_BILINEAR, TEXTURE_WRAP_REPEAT);
    CHECK(plain.id != legacy.id && clamp.id != variant.id && linear.id != variant.id && noMips.id != linear.id,
          "wrapper kind, wrap, filter and mip flag each separate cache keys");
    unsupportedAnisotropy = 1;
    const int anisotropicFilters[] = {TEXTURE_FILTER_ANISOTROPIC_4X, TEXTURE_FILTER_ANISOTROPIC_8X,
                                     TEXTURE_FILTER_ANISOTROPIC_16X};
    unsigned int anisotropicIds[3];
    for (int i = 0; i < 3; ++i) {
        Texture2D aniso = ResourceManager_LoadTextureVariant("noise", true, anisotropicFilters[i], TEXTURE_WRAP_REPEAT);
        anisotropicIds[i] = aniso.id;
        CHECK(aniso.id != 0 && aniso.mipmaps == 4, "anisotropic variants retain generated mipmap metadata");
        CHECK(filterHistoryCount[aniso.id] == 2 && filterHistory[aniso.id][0] == TEXTURE_FILTER_TRILINEAR &&
              filterHistory[aniso.id][1] == anisotropicFilters[i], "trilinear mip selection precedes optional anisotropy");
        CHECK(filters[aniso.id] == TEXTURE_FILTER_TRILINEAR, "unsupported anisotropy retains trilinear mip selection");
        CHECK(ResourceManager_LoadTextureVariant("noise", true, anisotropicFilters[i], TEXTURE_WRAP_REPEAT).id == aniso.id,
              "requested anisotropic filter remains the immutable cache key");
    }
    CHECK(anisotropicIds[0] != anisotropicIds[1] && anisotropicIds[1] != anisotropicIds[2] &&
          anisotropicIds[0] != variant.id, "anisotropic requests have independent entries from each other and trilinear");
    unsupportedAnisotropy = 0;
    int before = loads;
    CHECK(ResourceManager_LoadTextureVariant("missing", false, 0, 0).id == 0 &&
          ResourceManager_LoadTextureVariant("missing", false, 0, 0).id == 0 && loads == before+2,
          "failed loads are not cached and may retry");
    int beforeImages = imageLoads;
    CHECK(ResourceManager_LoadTextureVariant("missing", true, 2, 0).id == 0 &&
          ResourceManager_LoadTextureVariant("missing", true, 2, 0).id == 0 && imageLoads == beforeImages+2,
          "failed image loads are not cached");
    int beforeUnloads = imageUnloads;
    failConversion = 1;
    CHECK(ResourceManager_LoadTextureVariant("conversion_failure", true, 2, 0).id == 0 &&
          imageUnloads == beforeUnloads+1, "format conversion failure releases source without GPU allocation");
    failConversion = 0;
    failUpload = 1;
    beforeUnloads = imageUnloads;
    CHECK(ResourceManager_LoadTextureVariant("upload_failure", true, 2, 0).id == 0 &&
          imageUnloads == beforeUnloads+1, "GPU upload failure releases source without caching");
    failUpload = 0;
    failMipmaps = 1;
    unsigned int failedId = nextId;
    CHECK(ResourceManager_LoadTextureVariant("mip_failure", true, 2, 0).id == 0 && unloads[failedId] == 1,
          "mipmap failure immediately releases its owned ID");
    failMipmaps = 0;
    Texture2D retry = ResourceManager_LoadTextureVariant("mip_failure", true, 2, 0);
    CHECK(retry.id != 0 && retry.mipmaps == 4, "mipmap failure does not poison a cache slot");
    char longPath[129]; memset(longPath, 'x', 128); longPath[128] = 0;
    before = loads;
    CHECK(ResourceManager_LoadTextureVariant(NULL, false, 0, 0).id == 0 &&
          ResourceManager_LoadTextureVariant("", false, 0, 0).id == 0 &&
          ResourceManager_LoadTextureVariant(longPath, false, 0, 0).id == 0 &&
          ResourceManager_LoadTextureVariant("bad", false, 2, 0).id == 0 &&
          ResourceManager_LoadTextureVariant("bad", false, 99, 0).id == 0 &&
          ResourceManager_LoadTextureVariant("bad", false, TEXTURE_FILTER_ANISOTROPIC_4X, 0).id == 0 &&
          ResourceManager_LoadTextureVariant("bad", false, TEXTURE_FILTER_ANISOTROPIC_8X, 0).id == 0 &&
          ResourceManager_LoadTextureVariant("bad", false, TEXTURE_FILTER_ANISOTROPIC_16X, 0).id == 0 &&
          ResourceManager_LoadTextureVariant("bad", false, 0, -1).id == 0 && loads == before,
          "invalid requests allocate nothing");
    int occupied = 0;
    for (int i = 0; i < MAX_CACHED_TEXTURES; ++i) occupied += s_textures[i].active ? 1 : 0;
    for (int i = 0; i < MAX_CACHED_TEXTURES-occupied; ++i) {
        char path[32]; snprintf(path, sizeof(path), "capacity_%d", i);
        CHECK(ResourceManager_LoadTextureVariant(path, false, 0, 0).id != 0, "remaining shared slots fill");
    }
    before = loads;
    CHECK(ResourceManager_LoadTextureVariant("overflow", false, 0, 0).id == 0 && loads == before,
          "full variant cache never leaks an unmanaged allocation");
    CHECK(ResourceManager_LoadTextureVariant("noise", true, 2, 0).id == variant.id &&
          ResourceManager_LoadTexture("noise").id == legacy.id, "existing entries reuse even at capacity");
    ResourceManager_Unload();
    ResourceManager_Unload();
    for (unsigned int id = 1; id < nextId; ++id) CHECK(unloads[id] == 1, "every texture unloads exactly once");
    ResourceManager_Init();
    CHECK(ResourceManager_LoadTexture("noise").id != legacy.id, "fresh legacy entry survives prior variant metadata");
    before = loads;
    CHECK(ResourceManager_LoadTexture("missing").id == 0 && ResourceManager_LoadTexture("missing").id == 0 && loads == before+1,
          "legacy failed-load caching behavior remains unchanged");
    ResourceManager_Unload();
    if (failures) return 1;
    puts("PASS: production texture variant cache, ownership, metadata and failures");
    return 0;
}
