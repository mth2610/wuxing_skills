// Initialization-only packing for static blade meshes. Compare every authored
// attribute byte; coincident positions with different normals/UVs stay distinct.
static bool Nature_IndexBladeMesh(Mesh *mesh)
{
    if (!mesh || mesh->indices || mesh->vertexCount < 6 ||
        mesh->vertexCount > 196605 || mesh->vertexCount != mesh->triangleCount * 3 ||
        !mesh->vertices || !mesh->normals || !mesh->texcoords ||
        !mesh->texcoords2 || !mesh->colors)
        return false;
    int count = mesh->vertexCount;
    unsigned int capacity = 1;
    while (capacity < (unsigned int)count * 2u) capacity <<= 1;
    unsigned int *table = MemAlloc(capacity * sizeof(unsigned int));
    unsigned int *originals = MemAlloc(65535u * sizeof(unsigned int));
    unsigned short *indices = MemAlloc((unsigned int)count * sizeof(unsigned short));
    if (!table || !originals || !indices) {
        MemFree(table); MemFree(originals); MemFree(indices);
        return false;
    }
    memset(table, 0, capacity * sizeof(unsigned int));
    unsigned char *streams[5] = {
        (unsigned char *)mesh->vertices, (unsigned char *)mesh->normals,
        (unsigned char *)mesh->texcoords, (unsigned char *)mesh->texcoords2, mesh->colors,
    };
    const unsigned int sizes[5] = {
        3u * sizeof(float), 3u * sizeof(float), 2u * sizeof(float), 2u * sizeof(float), 4u,
    };
    unsigned int unique = 0;
    for (int i = 0; i < count; i++) {
        unsigned int hash = 2166136261u;
        for (int stream = 0; stream < 5; stream++) {
            const unsigned char *bytes = streams[stream] + (unsigned int)i * sizes[stream];
            for (unsigned int byte = 0; byte < sizes[stream]; byte++)
                hash = (hash ^ bytes[byte]) * 16777619u;
        }
        unsigned int slot = hash & (capacity - 1u);
        while (table[slot]) {
            unsigned int original = table[slot] - 1u;
            bool equal = true;
            for (int stream = 0; stream < 5; stream++) {
                if (memcmp(streams[stream] + (unsigned int)i * sizes[stream],
                           streams[stream] + original * sizes[stream], sizes[stream]) != 0) {
                    equal = false;
                    break;
                }
            }
            if (equal) break;
            slot = (slot + 1u) & (capacity - 1u);
        }
        if (!table[slot]) {
            // Do not mutate the source before eligibility is established.
            // Oversized chunks retain their complete non-indexed geometry.
            if (unique == 65535u) {
                MemFree(table); MemFree(originals); MemFree(indices);
                return false;
            }
            table[slot] = (unsigned int)i + 1u;
            originals[unique] = (unsigned int)i;
            indices[i] = (unsigned short)unique++;
        } else {
            indices[i] = indices[table[slot] - 1u];
        }
    }
    MemFree(table);
    if (unique * 100u >= (unsigned int)count * 92u) {
        MemFree(originals); MemFree(indices);
        return false;
    }
    for (unsigned int i = 0; i < unique; i++) {
        if (i == originals[i]) continue;
        for (int stream = 0; stream < 5; stream++)
            memcpy(streams[stream] + i * sizes[stream],
                   streams[stream] + originals[i] * sizes[stream], sizes[stream]);
    }
    MemFree(originals);
    // Reallocation failure only leaves an unused CPU tail, never lost geometry.
    for (int stream = 0; stream < 5; stream++) {
        void *smaller = MemRealloc(streams[stream], unique * sizes[stream]);
        if (smaller) streams[stream] = smaller;
    }
    mesh->vertices = (float *)streams[0];
    mesh->normals = (float *)streams[1];
    mesh->texcoords = (float *)streams[2];
    mesh->texcoords2 = (float *)streams[3];
    mesh->colors = streams[4];
    mesh->indices = indices;
    mesh->vertexCount = (int)unique;
    return true;
}
