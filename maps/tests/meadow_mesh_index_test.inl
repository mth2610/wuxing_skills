// Standalone geometry oracle (.inl avoids the game's recursive .c glob):
// cc -x c -std=c99 -Wall -Wextra -Werror maps/tests/meadow_mesh_index_test.inl -o /tmp/meadow_mesh_index_test
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Mesh {
    int vertexCount, triangleCount;
    float *vertices, *normals, *texcoords, *texcoords2;
    unsigned char *colors;
    unsigned short *indices;
} Mesh;
static int allocationCall, failAllocation;
static bool failShrink;
static void *MemAlloc(unsigned int bytes)
{
    return ++allocationCall == failAllocation ? NULL : malloc(bytes);
}
static void *MemRealloc(void *ptr, unsigned int bytes)
{
    return failShrink ? NULL : realloc(ptr, bytes);
}
static void MemFree(void *ptr) { free(ptr); }
#include "../toolkit/map_props_mesh_index.inl"

static unsigned char *stream(Mesh *m, int s)
{
    switch (s) {
        case 0: return (unsigned char *)m->vertices;
        case 1: return (unsigned char *)m->normals;
        case 2: return (unsigned char *)m->texcoords;
        case 3: return (unsigned char *)m->texcoords2;
        default: return m->colors;
    }
}
static const unsigned int sizes[5] = {12, 12, 8, 8, 4};
static Mesh makeMesh(int count, bool allUnique)
{
    Mesh m = {0};
    m.vertexCount = count; m.triangleCount = count / 3;
    m.vertices = calloc((size_t)count, 12);
    m.normals = calloc((size_t)count, 12);
    m.texcoords = calloc((size_t)count, 8);
    m.texcoords2 = calloc((size_t)count, 8);
    m.colors = calloc((size_t)count, 4);
    for (int i = 0; i < count; i++) {
        unsigned int id = allUnique ? (unsigned int)i : (unsigned int)i / 2;
        for (int s = 0; s < 5; s++) memcpy(stream(&m, s) + i*sizes[s], &id, 4);
    }
    return m;
}
static void destroy(Mesh *m)
{
    for (int s = 0; s < 5; s++) free(stream(m, s));
    free(m->indices);
}
static void check(bool expectPacked, bool seams, int count, bool allUnique)
{
    Mesh m = makeMesh(count, allUnique);
    // Identical positions but a distinct seam in each other attribute stream.
    if (seams) for (int s = 1; s < 5; s++) stream(&m, s)[(s*2+1)*sizes[s]+4-1] ^= 128;
    unsigned char *original[5];
    for (int s = 0; s < 5; s++) {
        original[s] = malloc(count*sizes[s]);
        memcpy(original[s], stream(&m, s), count*sizes[s]);
    }
    int triangles = m.triangleCount;
    allocationCall = 0;
    assert(Nature_IndexBladeMesh(&m) == expectPacked);
    assert(m.triangleCount == triangles);
    if (!expectPacked) assert(m.vertexCount == count && !m.indices);
    for (int i = 0; i < count; i++) {
        unsigned int v = m.indices ? m.indices[i] : (unsigned int)i;
        assert(v < (unsigned int)m.vertexCount);
        for (int s = 0; s < 5; s++)
            assert(!memcmp(original[s]+i*sizes[s], stream(&m,s)+v*sizes[s], sizes[s]));
    }
    for (int s = 0; s < 5; s++) free(original[s]);
    destroy(&m);
}
int main(void)
{
    check(true, true, 600, false);
    failShrink = true; check(true, true, 600, false); failShrink = false;
    for (failAllocation = 1; failAllocation <= 3; failAllocation++) check(false, false, 600, false);
    failAllocation = 0;
    check(false, false, 600, true);
    check(false, false, 65538, true);
    check(true, false, 131070, false);
    puts("PASS: triangle/attribute equivalence, seams, 16-bit boundary, allocation failure");
    return 0;
}
