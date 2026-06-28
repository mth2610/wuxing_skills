#include "sandbox/skill_debugger.h"
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#define MKDIR(path) mkdir(path, 0777)
#endif

bool g_debugHideDecals = false;
bool g_debugHideMeshes = false;
bool g_debugHideTrails = false;
bool g_debugHideParticles = false;

bool g_isDebuggerCapturing = false;
static int s_captureStage = 0;

void SkillDebugger_Init(void) {
    // No longer creating screenshots directory, saving to root.
}

void SkillDebugger_CheckInput(void) {
    if (IsKeyPressed(KEY_N) && !g_isDebuggerCapturing) {
        g_isDebuggerCapturing = true;
        s_captureStage = 1;
    }
}

void SkillDebugger_PreRender(void) {
    if (g_isDebuggerCapturing) {
        g_debugHideDecals = (s_captureStage != 1);
        g_debugHideMeshes = (s_captureStage != 2);
        g_debugHideTrails = (s_captureStage != 3);
        g_debugHideParticles = (s_captureStage != 4);
    } else {
        g_debugHideDecals = false;
        g_debugHideMeshes = false;
        g_debugHideTrails = false;
        g_debugHideParticles = false;
    }
}

void SkillDebugger_PostRender(int activeSkillIndex, Vector3 castPos, Vector3 targetPos) {
    if (g_isDebuggerCapturing) {
        const char *stageName = "";
        switch (s_captureStage) {
            case 1: stageName = "Decals"; break;
            case 2: stageName = "BaseMesh"; break;
            case 3: stageName = "Trails"; break;
            case 4: stageName = "Particles"; break;
            default: stageName = "Unknown"; break;
        }

        // Draw a small indicator on screen just so user knows it's capturing (won't affect crop because we draw it AFTER LoadImageFromScreen if we wanted to, but actually we just don't draw anything on screen)

        // Capture screen to Image
        Image img = LoadImageFromScreen();
        Color *pixels = LoadImageColors(img);
        
        int minX = img.width, minY = img.height;
        int maxX = 0, maxY = 0;
        
        for (int y = 0; y < img.height; y++) {
            for (int x = 0; x < img.width; x++) {
                Color c = pixels[y * img.width + x];
                // Check if pixel is not strictly black (with a small threshold for bloom)
                if (c.r > 5 || c.g > 5 || c.b > 5) {
                    if (x < minX) minX = x;
                    if (x > maxX) maxX = x;
                    if (y < minY) minY = y;
                    if (y > maxY) maxY = y;
                }
            }
        }
        
        if (maxX >= minX && maxY >= minY) {
            // Add some padding
            int pad = 20;
            minX -= pad; minY -= pad;
            maxX += pad; maxY += pad;
            if (minX < 0) minX = 0;
            if (minY < 0) minY = 0;
            if (maxX >= img.width) maxX = img.width - 1;
            if (maxY >= img.height) maxY = img.height - 1;
            
            Rectangle cropRect = { (float)minX, (float)minY, (float)(maxX - minX + 1), (float)(maxY - minY + 1) };
            ImageCrop(&img, cropRect);
        } else {
            // Empty layer, crop to a 1x1 black pixel
            Rectangle cropRect = { 0, 0, 1, 1 };
            ImageCrop(&img, cropRect);
        }

        // Put metadata into filename instead of drawing onto the image
        const char *fileName = TextFormat("skill_%d_time_%.2f_cast_%.0f_%.0f_%.0f_tgt_%.0f_%.0f_%.0f_layer_%d_%s.png", 
                                          activeSkillIndex, GetTime(), 
                                          castPos.x, castPos.y, castPos.z,
                                          targetPos.x, targetPos.y, targetPos.z,
                                          s_captureStage, stageName);
        ExportImage(img, fileName);
        TraceLog(LOG_INFO, "SkillDebugger: Saved cropped %s", fileName);

        UnloadImageColors(pixels);
        UnloadImage(img);

        s_captureStage++;
        if (s_captureStage > 4) {
            // Finished capturing all layers
            g_isDebuggerCapturing = false;
            s_captureStage = 0;
            
            // Restore visibility flags immediately so the next frame is normal
            g_debugHideDecals = false;
            g_debugHideMeshes = false;
            g_debugHideTrails = false;
            g_debugHideParticles = false;
        }
    }
}
