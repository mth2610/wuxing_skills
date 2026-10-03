#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Compile the actual production boundary with backend stubs. GPU barriers,
   snapshot content and copy counts belong to depth_twin_demand, not this test. */
int main(void)
{
    FILE *source = fopen("core/scene_targets.c", "rb");
    if (!source) return 1;
    char text[64000];
    size_t n = fread(text, 1, sizeof(text)-1, source);
    fclose(source); text[n] = '\0';
    char *function = strstr(text, "bool SceneTargets_PrepareRawDepth(void)");
    char *body = function ? strchr(function, '{') : NULL;
    if (!body) { puts("FAIL: missing production depth preparation boundary"); return 1; }
    int braces = 1;
    char *end = body+1;
    while (*end && braces) { if (*end == '{') braces++; if (*end == '}') braces--; end++; }
    if (braces) return 1;

    char path[160], binary[160], command[512];
    snprintf(path, sizeof(path), "/tmp/wuxing_scene_depth_prepare_%ld.c", (long)getpid());
    snprintf(binary, sizeof(binary), "/tmp/wuxing_scene_depth_prepare_%ld", (long)getpid());
    FILE *probe = fopen(path, "wb");
    if (!probe) return 1;
    fputs("#include <stdbool.h>\n#include <assert.h>\n"
          "static bool s_depthTextureActive = true;\n"
          "static bool s_depthSampleOnDemand = true;\n"
          "static struct { unsigned int id; } renderTex = {17};\n"
          "static int calls; static bool refreshResult = true;\n"
          "bool rlvkRefreshFramebufferDepthTexture(unsigned int id) { assert(id==17); calls++; return refreshResult; }\n", probe);
    fwrite(function, 1, (size_t)(end-function), probe);
    fputs("\nint main(void) {\n"
          "s_depthTextureActive=false; assert(!SceneTargets_PrepareRawDepth()); assert(calls==0);\n"
          "s_depthTextureActive=true; assert(SceneTargets_PrepareRawDepth());\n"
          "#if defined(GRAPHICS_API_VULKAN)\n"
          "assert(calls==1); refreshResult=false; assert(!SceneTargets_PrepareRawDepth()); assert(calls==2);\n"
          "s_depthSampleOnDemand=false; assert(SceneTargets_PrepareRawDepth()); assert(calls==2);\n"
          "#else\nassert(calls==0);\n#endif\nreturn 0; }\n", probe);
    fclose(probe);
    int failures = 0;
    for (int vulkan = 0; vulkan <= 1; vulkan++) {
        snprintf(command, sizeof(command), "cc -std=c99 %s %s -o %s && %s",
                 vulkan ? "-DGRAPHICS_API_VULKAN" : "", path, binary, binary);
        if (system(command) != 0) failures++;
    }
    unlink(path); unlink(binary);
    if (failures) return 1;
    puts("PASS: production depth preparation routes requests, failures and native fallback correctly");
    return 0;
}
