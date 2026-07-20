// taiji.inl — Master include for taiji element VFX sub-modules
// Included once by visual_composer.c

static ColorGradient s_windGrad = {0};
static ColorGradient s_windDustGrad = {0};
static ColorGradient s_yinGrad = {0};
static ColorGradient s_yangGrad = {0};
static bool s_taijiFxInit = false;

static void TaijiFx_InitShared(void)
{
    if (s_taijiFxInit)
        return;
    ColorGradient_AddStop(&s_windGrad, 0.0f, (Color){225, 245, 250, 0});
    ColorGradient_AddStop(&s_windGrad, 0.2f, (Color){210, 240, 245, 140});
    ColorGradient_AddStop(&s_windGrad, 1.0f, (Color){170, 210, 225, 0});

    ColorGradient_AddStop(&s_windDustGrad, 0.0f, (Color){120, 115, 105, 0});
    ColorGradient_AddStop(&s_windDustGrad, 0.3f, (Color){105, 100, 92, 110});
    ColorGradient_AddStop(&s_windDustGrad, 1.0f, (Color){60, 58, 55, 0});

    ColorGradient_AddStop(&s_yinGrad, 0.0f, (Color){90, 40, 140, 200});
    ColorGradient_AddStop(&s_yinGrad, 1.0f, (Color){25, 10, 45, 0});

    ColorGradient_AddStop(&s_yangGrad, 0.0f, (Color){255, 255, 250, 230});
    ColorGradient_AddStop(&s_yangGrad, 1.0f, (Color){150, 130, 200, 0});
    s_taijiFxInit = true;
}

static Vector3 TaijiSphereDir(int index, int epoch)
{
    unsigned int rng = (unsigned int)(index * 668265263 + epoch * 374761393) + 1013904223u;
    rng ^= rng >> 13;
    rng *= 1274126177u;
    rng ^= rng >> 16;
    float u = (float)(rng & 0xFFFF) / 65535.0f;
    rng = rng * 1664525u + 1013904223u;
    float v = (float)(rng >> 8 & 0xFFFF) / 65535.0f;
    float yaw = u * 2.0f * PI;
    float t = 2.0f * v - 1.0f;
    float rxy = sqrtf(fmaxf(1.0f - t * t, 0.0f));
    return (Vector3){rxy * cosf(yaw), t, rxy * sinf(yaw)};
}

#include "gust_slash.inl"
#include "cyclone.inl"
#include "vc_spirit_wisp_test.inl"
#include "taiji_arc_strike.inl"
#include "tornado.inl"
