/*
 * core/shading/shader_preprocessor.h / .c
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

// Như trên, nhưng chèn `defines` ngay SAU dòng #version — nền của permutation
// (một shader nguồn, N biến thể compile-time). `defines` là chuỗi GLSL thô,
// ví dụ "#define INSTANCED 1\n"; NULL hoặc "" cho ra kết quả y hệt
// ShaderPreprocessor_Load.
//
// Chèn SAU rewrite GLES (không phải trước) là bắt buộc: trên Android
// "#version 330" bị thay bằng một header 3 dòng, nên vị trí chèn tính theo
// dòng #version CUỐI CÙNG, không theo offset ban đầu.
//
// Vì sao là compile-time chứ không phải `if` runtime: attribute chỉ tồn tại ở
// một biến thể (`in mat4 instanceTransform`) — đọc nó khi KHÔNG vẽ bằng
// DrawMeshInstanced là undefined behaviour tuỳ driver. #ifdef loại hẳn khai
// báo khỏi biến thể không instancing; một nhánh `if` thì không.
char *ShaderPreprocessor_LoadWithDefines(const char *filePath, const char *defines);

#endif // SHADER_PREPROCESSOR_H