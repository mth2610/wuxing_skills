/*
 * shader_preprocessor.h / shader_preprocessor.c
 * ─────────────────────────────────────────────────────────────────────────────
 * WUXING — GLSL #include Preprocessor
 *
 * Tích hợp vào ResourceManager_LoadShader() để xử lý directive:
 *
 *   #include "shaders/common/vs_header.glsl"
 *
 * trước khi nạp shader lên GPU. Hỗ trợ include đệ quy (tối đa MAX_INCLUDE_DEPTH
 * cấp) và dùng RL_MALLOC/RL_FREE của Raylib nên không vi phạm memory rules.
 *
 * CÁCH DÙNG:
 *   Trong resource_manager.c, thay LoadShaderFromMemory(NULL, fsCode) bằng:
 *
 *   char *vsCode = (vsPath) ? ShaderPreprocessor_Load(vsPath) : NULL;
 *   char *fsCode = (fsPath) ? ShaderPreprocessor_Load(fsPath) : NULL;
 *   Shader shader = LoadShaderFromMemory(vsCode, fsCode);
 *   if (vsCode) RL_FREE(vsCode);
 *   if (fsCode) RL_FREE(fsCode);
 * ─────────────────────────────────────────────────────────────────────────────
 */

// ── shader_preprocessor.h ────────────────────────────────────────────────────
#ifndef SHADER_PREPROCESSOR_H
#define SHADER_PREPROCESSOR_H

#include "raylib.h"

// Đọc file shader tại filePath, xử lý đệ quy mọi directive #include "..."
// và trả về chuỗi GLSL hoàn chỉnh (caller dùng RL_FREE để giải phóng).
// Trả về NULL nếu file không tồn tại hoặc vượt giới hạn include.
char *ShaderPreprocessor_Load(const char *filePath);

#endif // SHADER_PREPROCESSOR_H