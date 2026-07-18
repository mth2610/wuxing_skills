#include "shader_preprocessor.h"
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INCLUDE_DEPTH 8
#define MAX_SHADER_SIZE (64 * 1024) // 64 KB — đủ cho shader GLSL phức tạp nhất

static char *ProcessIncludes(const char *filePath, int depth);

static char *ProcessIncludes(const char *filePath, int depth) {
  if (depth > MAX_INCLUDE_DEPTH) {
    TraceLog(LOG_WARNING, "SHADER: Vượt giới hạn include depth (%d) tại: %s",
             MAX_INCLUDE_DEPTH, filePath);
    return NULL;
  }

  char *src = LoadFileText(filePath);
  if (!src) {
    TraceLog(LOG_ERROR, "SHADER: Không đọc được file: %s", filePath);
    return NULL;
  }

  char *output = (char *)RL_MALLOC(MAX_SHADER_SIZE);
  if (!output) {
    UnloadFileText(src);
    return NULL;
  }
  output[0] = '\0';
  int outLen = 0;

  const char *cursor = src;

  while (*cursor) {
    // Tìm directive #include tiếp theo
    const char *includeKw = strstr(cursor, "#include");
    if (!includeKw) {
      // Không còn #include, copy phần còn lại vào output
      int rem = (int)strlen(cursor);
      if (outLen + rem < MAX_SHADER_SIZE - 1) {
        memcpy(output + outLen, cursor, rem);
        outLen += rem;
        output[outLen] = '\0';
      }
      break;
    }

    // Copy mọi thứ trước #include
    int before = (int)(includeKw - cursor);
    if (outLen + before < MAX_SHADER_SIZE - 1) {
      memcpy(output + outLen, cursor, before);
      outLen += before;
      output[outLen] = '\0';
    }

    // Parse tên file: #include "path/to/file.glsl"
    const char *q1 = strchr(includeKw, '"');
    const char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
    if (!q1 || !q2) {
      TraceLog(LOG_WARNING, "SHADER: Directive #include không hợp lệ trong: %s",
               filePath);
      cursor = includeKw + 8; // nhảy qua "#include", tiếp tục
      continue;
    }

    char includePath[512];
    int pathLen = (int)(q2 - q1 - 1);
    if (pathLen <= 0 || pathLen >= (int)sizeof(includePath)) {
      cursor = q2 + 1;
      continue;
    }
    memcpy(includePath, q1 + 1, pathLen);
    includePath[pathLen] = '\0';

    // Đệ quy xử lý file được include
    char *included = ProcessIncludes(includePath, depth + 1);
    if (included) {
      int incLen = (int)strlen(included);
      if (outLen + incLen < MAX_SHADER_SIZE - 1) {
        memcpy(output + outLen, included, incLen);
        outLen += incLen;
        output[outLen] = '\0';
      }
      RL_FREE(included);
    }

    // Nhảy qua toàn bộ dòng #include
    const char *lineEnd = strchr(q2 + 1, '\n');
    cursor = lineEnd ? lineEnd + 1 : q2 + 1;
  }

  UnloadFileText(src);
  return output;
}

#if defined(__ANDROID__) && !defined(GRAPHICS_API_VULKAN)
// Rewrites "#version 330" (desktop GL 3.3) to "#version 300 es" (GLES 3.0).
// Also handles 310 es → 300 es so include files that mention 310 don't break.
// Called only on Android/GLES after include expansion - NOT under rlvk (Vulkan backend),
// which needs the original desktop-dialect GLSL 330 unchanged (shaderc targets
// vulkan1.1/SPIR-V1.3 and rejects "#version 300 es" outright: "ES shaders for SPIR-V require
// version 310 or higher"). Without this guard, every #include-using shader (the ones that
// route through ShaderPreprocessor_Load instead of build-time convert_shaders_to_gles.py -
// see that script's has_include_directives()) silently failed to compile under Vulkan even
// after the build-time GLES conversion was fixed to skip Vulkan builds (RLVK_HANDOFF.md §7.18).
static void RewriteVersionForGLES(char *buf, int maxLen) {
    // Replace "#version 330" (12 chars) with "#version 300 es\nprecision highp float;\nprecision highp int;\n" (57 chars)
    char *p = strstr(buf, "#version 330");
    if (p) {
        const char *newHeader = "#version 300 es\nprecision highp float;\nprecision highp int;\n";
        int newLen = (int)strlen(newHeader);
        int pos = (int)(p - buf);
        int tail = (int)strlen(p + 12) + 1;
        if (pos + newLen + tail <= maxLen) {
            memmove(p + newLen, p + 12, tail);
            memcpy(p, newHeader, newLen);
        }
    }
    // Replace "#version 310 es" if accidentally included from a compute header
    p = strstr(buf, "#version 310 es");
    if (p && !strstr(buf, "std430")) memcpy(p, "#version 300 es", 15);
}
#endif

char *ShaderPreprocessor_Load(const char *filePath) {
    char *result = ProcessIncludes(filePath, 0);
#if defined(__ANDROID__) && !defined(GRAPHICS_API_VULKAN)
    if (result) RewriteVersionForGLES(result, MAX_SHADER_SIZE);
#endif
    return result;
}

// ── Thay đổi cần thiết trong resource_manager.c ──────────────────────────────
/*
   Tìm đoạn code hiện tại nạp shader (dạng):

     Shader shader = LoadShaderFromMemory(...) hoặc LoadShader(vsPath, fsPath);

   Thay bằng:

     #include "core/shader_preprocessor.h"   // thêm ở đầu file

     // Bên trong ResourceManager_LoadShader():
     char *vsCode = vsFilePath ? ShaderPreprocessor_Load(vsFilePath) : NULL;
     char *fsCode = fsFilePath ? ShaderPreprocessor_Load(fsFilePath) : NULL;

     Shader shader = LoadShaderFromMemory(vsCode, fsCode);

     if (vsCode) RL_FREE(vsCode);
     if (fsCode) RL_FREE(fsCode);

   Phần còn lại (cache lookup, cache store, return) giữ nguyên.
*/
