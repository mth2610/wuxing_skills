// Immutable indexed heightfield. Keep the same cell diagonals as GenMeshHeightmap.
// uint16 indices cap this path at 65535 nodes; larger grids use the legacy path.
static Mesh Ground_CreateIndexedHeightfield(Image image, Vector3 size, bool packed16, bool indexed)
{
    Mesh mesh = {0};
    if (!image.data || image.width < 2 || image.height < 2 ||
        (size_t)image.width * (size_t)image.height > 65535u) return mesh;
    int w = image.width, h = image.height;
    mesh.vertexCount = w * h;
    mesh.triangleCount = (w - 1) * (h - 1) * 2;
    mesh.vertices = MemAlloc((unsigned int)mesh.vertexCount * 3u * sizeof(float));
    mesh.normals = MemAlloc((unsigned int)mesh.vertexCount * 3u * sizeof(float));
    mesh.texcoords = MemAlloc((unsigned int)mesh.vertexCount * 2u * sizeof(float));
    mesh.indices = MemAlloc((unsigned int)mesh.triangleCount * 3u * sizeof(unsigned short));
    Color *pixels = LoadImageColors(image);
    if (!mesh.vertices || !mesh.normals || !mesh.texcoords || !mesh.indices || !pixels) {
        MemFree(mesh.vertices); MemFree(mesh.normals); MemFree(mesh.texcoords); MemFree(mesh.indices);
        if (pixels) UnloadImageColors(pixels);
        return (Mesh){0};
    }
    memset(mesh.normals, 0, (size_t)mesh.vertexCount * 3u * sizeof(float));
    for (int z = 0; z < h; z++) for (int x = 0; x < w; x++) {
        int i = z * w + x;
        float u = (float)x / (float)(w - 1), v = (float)z / (float)(h - 1);
        mesh.vertices[i * 3] = u * size.x;
        mesh.vertices[i * 3 + 1] = (packed16 ? (float)(pixels[i].r * 256 + pixels[i].g) / 65535.0f
            : (float)(pixels[i].r + pixels[i].g + pixels[i].b) / 765.0f) * size.y;
        mesh.vertices[i * 3 + 2] = v * size.z;
        mesh.texcoords[i * 2] = u; mesh.texcoords[i * 2 + 1] = v;
    }
    UnloadImageColors(pixels);
    int index = 0;
    for (int z = 0; z < h - 1; z++) for (int x = 0; x < w - 1; x++) {
        int a = z * w + x, b = a + w, c = a + 1, d = b + 1;
        int ids[6] = {a, b, c, c, b, d};
        for (int k = 0; k < 6; k++) mesh.indices[index++] = (unsigned short)ids[k];
    }
    for (int t = 0; t < mesh.triangleCount; t++) {
        unsigned short *ids = &mesh.indices[t * 3];
        Vector3 a = {mesh.vertices[ids[0]*3], mesh.vertices[ids[0]*3+1], mesh.vertices[ids[0]*3+2]};
        Vector3 b = {mesh.vertices[ids[1]*3], mesh.vertices[ids[1]*3+1], mesh.vertices[ids[1]*3+2]};
        Vector3 c = {mesh.vertices[ids[2]*3], mesh.vertices[ids[2]*3+1], mesh.vertices[ids[2]*3+2]};
        Vector3 n = Vector3CrossProduct(Vector3Subtract(b,a), Vector3Subtract(c,a));
        for (int k = 0; k < 3; k++) {
            mesh.normals[ids[k]*3] += n.x;
            mesh.normals[ids[k]*3+1] += n.y;
            mesh.normals[ids[k]*3+2] += n.z;
        }
    }
    for (int i = 0; i < mesh.vertexCount; i++) {
        Vector3 n = Vector3Normalize((Vector3){mesh.normals[i*3],mesh.normals[i*3+1],mesh.normals[i*3+2]});
        mesh.normals[i*3]=n.x; mesh.normals[i*3+1]=n.y; mesh.normals[i*3+2]=n.z;
    }
    if (!indexed) {
        Mesh expanded = {0};
        expanded.vertexCount = mesh.triangleCount * 3;
        expanded.triangleCount = mesh.triangleCount;
        expanded.vertices = MemAlloc((unsigned int)expanded.vertexCount * 3u * sizeof(float));
        expanded.normals = MemAlloc((unsigned int)expanded.vertexCount * 3u * sizeof(float));
        expanded.texcoords = MemAlloc((unsigned int)expanded.vertexCount * 2u * sizeof(float));
        if (expanded.vertices && expanded.normals && expanded.texcoords) {
            for (int i = 0; i < expanded.vertexCount; i++) {
                int j = mesh.indices[i];
                memcpy(&expanded.vertices[i*3], &mesh.vertices[j*3], 3u*sizeof(float));
                memcpy(&expanded.normals[i*3], &mesh.normals[j*3], 3u*sizeof(float));
                memcpy(&expanded.texcoords[i*2], &mesh.texcoords[j*2], 2u*sizeof(float));
            }
            MemFree(mesh.vertices); MemFree(mesh.normals); MemFree(mesh.texcoords); MemFree(mesh.indices);
            mesh = expanded;
        } else {
            MemFree(expanded.vertices); MemFree(expanded.normals); MemFree(expanded.texcoords);
        }
    }
    UploadMesh(&mesh, false);
    return mesh;
}
