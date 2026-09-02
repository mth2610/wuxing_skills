/* Minimal raylib stand-in for the TIER-2 HEADLESS core tests.
 *
 * The headless tier links nothing from the game — no raylib, no GL, no window
 * (see scripts/run_core_tests.sh). But a few core headers that are otherwise
 * pure arithmetic (`core/vfx_contrast.h`) still `#include "raylib.h"` for one
 * or two plain-data types. Without this stub such a test does not fail, it
 * fails to BUILD, and a build failure in a 47-suite run is easy to read as
 * "that suite is fine" — which is exactly how vfx_contrast_test was reported
 * PASSing while it had never compiled in the harness at all.
 *
 * Rules for this file:
 *   - PLAIN DATA ONLY. Never declare a raylib *function* here. A test that
 *     needs raylib behaviour does not belong in the headless tier; transliterate
 *     the arithmetic instead (see core/tests/uv_deform_test.c).
 *   - Keep the layouts byte-identical to real raylib, so a struct assertion
 *     means the same thing here as in the game.
 *   - It is reached only because the real raylib is absent from the include
 *     path; the game build never sees it.
 */
#ifndef RAYLIB_H
#define RAYLIB_H

typedef struct Color {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} Color;

typedef struct Vector2 { float x, y; } Vector2;
typedef struct Vector3 { float x, y, z; } Vector3;
typedef struct Vector4 { float x, y, z, w; } Vector4;
typedef struct Rectangle { float x, y, width, height; } Rectangle;
typedef struct Matrix {
    float m0, m4, m8, m12;
    float m1, m5, m9, m13;
    float m2, m6, m10, m14;
    float m3, m7, m11, m15;
} Matrix;

typedef struct Texture {
    unsigned int id;
    int width;
    int height;
    int mipmaps;
    int format;
} Texture;
typedef Texture Texture2D;

typedef struct Shader {
    unsigned int id;
    int *locs;
} Shader;

#endif // RAYLIB_H
