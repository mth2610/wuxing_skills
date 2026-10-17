//----------------------------------------------------------------------------------
// rlvk.h implementation fragment: Matrix ops, vertex-level ops, OpenGL-style state functions
//
// Part of the rlvk single-header backend. NOT a standalone header - it is textually
// included by rlvk.h inside the ONE RLVK_IMPLEMENTATION translation unit. No include
// guard: order is fixed by the #include chain in rlvk.h. Do not #include directly.
//----------------------------------------------------------------------------------

//----------------------------------------------------------------------------------
// Module Functions Definition - Matrix operations
//----------------------------------------------------------------------------------

// Choose the current matrix to be transformed
void rlMatrixMode(int mode)
{
    switch (mode)
    {
    case RL_PROJECTION:
        RLVK.State.currentMatrix = &RLVK.State.projection;
        break;
    case RL_MODELVIEW:
        RLVK.State.currentMatrix = RLVK.State.transformRequired ? &RLVK.State.transform : &RLVK.State.modelview;
        break;
    // RL_TEXTURE intentionally unimplemented (no rlgl consumer; the texture matrix stays identity)
    default:
        break;
    }
    RLVK.State.currentMatrixMode = mode;
}

// Push the current matrix into RLVK.State.stack
void rlPushMatrix(void)
{
    if (RLVK.State.stackCounter >= RL_MAX_MATRIX_STACK_SIZE)
    {
        TRACELOG(RL_LOG_ERROR, "RLVK: Matrix stack overflow (RL_MAX_MATRIX_STACK_SIZE)");
        return;
    }

    // Only the MODELVIEW stack redirects to the software 'transform' matrix; PROJECTION pushes
    // must keep operating on the projection matrix (so BeginMode3D's rlFrustum lands there).
    if (RLVK.State.currentMatrixMode == RL_MODELVIEW)
    {
        if (RLVK.State.mvStackDepth == 0)
            RLVK.State.transform = rlvkMatrixIdentity();
        RLVK.State.transformRequired = true;
        RLVK.State.currentMatrix = &RLVK.State.transform;
        RLVK.State.mvStackDepth++;
    }

    RLVK.State.stack[RLVK.State.stackCounter] = *RLVK.State.currentMatrix;
    RLVK.State.stackCounter++;
}

// Pop latest inserted matrix from RLVK.State.stack
void rlPopMatrix(void)
{
    if (RLVK.State.stackCounter > 0)
    {
        RLVK.State.stackCounter--;
        Matrix mat = RLVK.State.stack[RLVK.State.stackCounter];
        *RLVK.State.currentMatrix = mat;

        // Reset the software-transform redirect when the last MODELVIEW push is popped. The shared
        // stackCounter is WRONG here: BeginMode3D leaves a PROJECTION push outstanding, so a
        // balanced MODELVIEW push/pop (e.g. DrawCube) would never see stackCounter==0 and would leak
        // transformRequired=true + currentMatrix=&transform into the next draw -> a later custom-UBO
        // batch draw (ground shadow receiver) mis-delivers its uniforms. Track MODELVIEW depth on its
        // own counter instead. (§7.26)
        if (RLVK.State.currentMatrixMode == RL_MODELVIEW && RLVK.State.mvStackDepth > 0)
        {
            RLVK.State.mvStackDepth--;
            if (RLVK.State.mvStackDepth == 0)
            {
                RLVK.State.currentMatrix = &RLVK.State.modelview;
                RLVK.State.transformRequired = false;
            }
        }
    }
    else
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: Matrix stack underflow! rlPopMatrix() called without matching rlPushMatrix()");
    }
}

// Reset current matrix to identity matrix
void rlLoadIdentity(void)
{
    *RLVK.State.currentMatrix = rlvkMatrixIdentity();
}

// Multiply the current matrix by a translation matrix
void rlTranslatef(f32 x, f32 y, f32 z)
{
    Matrix t = {1.0f, 0.0f, 0.0f, x, 0.0f, 1.0f, 0.0f, y, 0.0f, 0.0f, 1.0f, z, 0.0f, 0.0f, 0.0f, 1.0f};
    *RLVK.State.currentMatrix = rlvkMatrixMultiply(t, *RLVK.State.currentMatrix);
}

// Multiply the current matrix by a rotation matrix
// NOTE: The provided angle must be in degrees
void rlRotatef(f32 angleDeg, f32 x, f32 y, f32 z)
{
    f32 a = angleDeg * DEG2RAD;
    f32 c = cosf(a), s = sinf(a);
    f32 lenSq = x * x + y * y + z * z;
    if (lenSq > 1e-12f)
    {
        f32 il = 1.0f / sqrtf(lenSq);
        x *= il;
        y *= il;
        z *= il;
    }
    f32 t = 1.0f - c;
    Matrix r = {
        c + x * x * t, x * y * t - z * s, x * z * t + y * s, 0.0f,
        y * x * t + z * s, c + y * y * t, y * z * t - x * s, 0.0f,
        z * x * t - y * s, z * y * t + x * s, c + z * z * t, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    *RLVK.State.currentMatrix = rlvkMatrixMultiply(r, *RLVK.State.currentMatrix);
}

// Multiply the current matrix by a scaling matrix
void rlScalef(f32 x, f32 y, f32 z)
{
    Matrix s = {x, 0.0f, 0.0f, 0.0f, 0.0f, y, 0.0f, 0.0f, 0.0f, 0.0f, z, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    *RLVK.State.currentMatrix = rlvkMatrixMultiply(s, *RLVK.State.currentMatrix);
}

// Multiply the current matrix by another matrix
void rlMultMatrixf(const f32 *matf)
{
    if (!matf)
        return;
    Matrix m = {
        matf[0], matf[4], matf[8], matf[12],
        matf[1], matf[5], matf[9], matf[13],
        matf[2], matf[6], matf[10], matf[14],
        matf[3], matf[7], matf[11], matf[15]};
    *RLVK.State.currentMatrix = rlvkMatrixMultiply(m, *RLVK.State.currentMatrix);
}

// Multiply the current matrix by a perspective matrix generated by parameters
void rlFrustum(f64 left, f64 right, f64 bottom, f64 top, f64 znear, f64 zfar)
{
    f32 rl = (f32)(right - left), tb = (f32)(top - bottom), fn = (f32)(zfar - znear);
    if (rl == 0.0f || tb == 0.0f || fn == 0.0f)
        return;
    Matrix m = {
        (f32)(2.0 * znear) / rl, 0.0f, (f32)(right + left) / rl, 0.0f,
        0.0f, (f32)(2.0 * znear) / tb, (f32)(top + bottom) / tb, 0.0f,
        0.0f, 0.0f, -(f32)(zfar + znear) / fn, -(f32)(2.0 * zfar * znear) / fn,
        0.0f, 0.0f, -1.0f, 0.0f};
    *RLVK.State.currentMatrix = rlvkMatrixMultiply(m, *RLVK.State.currentMatrix);
}

// Multiply the current matrix by an orthographic matrix generated by parameters
void rlOrtho(f64 left, f64 right, f64 bottom, f64 top, f64 znear, f64 zfar)
{
    f32 rl = (f32)(right - left), tb = (f32)(top - bottom), fn = (f32)(zfar - znear);
    if (rl == 0.0f || tb == 0.0f || fn == 0.0f)
        return;
    Matrix m = {
        2.0f / rl, 0.0f, 0.0f, -(f32)(right + left) / rl,
        0.0f, 2.0f / tb, 0.0f, -(f32)(top + bottom) / tb,
        0.0f, 0.0f, -2.0f / fn, -(f32)(zfar + znear) / fn,
        0.0f, 0.0f, 0.0f, 1.0f};
    *RLVK.State.currentMatrix = rlvkMatrixMultiply(m, *RLVK.State.currentMatrix);
}

// Set the viewport area (transformation from normalized device coordinates to window coordinates)
void rlViewport(int x, int y, int width, int height)
{
    if (RLVK.State.viewportX != x || RLVK.State.viewportY != y ||
        RLVK.State.viewportW != width || RLVK.State.viewportH != height)
    {
        RLVK.State.viewportX = x;
        RLVK.State.viewportY = y;
        RLVK.State.viewportW = width;
        RLVK.State.viewportH = height;
        RLVK.State.stateGeneration++;
    }
}

// Set clip planes distances
void rlSetClipPlanes(f64 n, f64 f)
{
    rlCullDistanceNear = n;
    rlCullDistanceFar = f;
}
// Get cull plane distance near
f64 rlGetCullDistanceNear(void) { return rlCullDistanceNear; }
// Get cull plane distance far
f64 rlGetCullDistanceFar(void) { return rlCullDistanceFar; }

//----------------------------------------------------------------------------------
// Module Functions Definition - Vertex level operations
//----------------------------------------------------------------------------------

// Initialize drawing mode (how to organize vertex)
void rlBegin(int mode)
{
    rlRenderBatch *batch = RLVK.currentBatch;
    if (!batch)
        return;
    rlDrawCall *cur = &batch->draws[batch->drawCounter - 1];

    if (cur->mode != mode)
    {
        if (cur->vertexCount > 0)
        {
            if (cur->mode == RL_LINES)
                cur->vertexAlignment = (cur->vertexCount < 4) ? cur->vertexCount : (cur->vertexCount % 4);
            else if (cur->mode == RL_TRIANGLES)
                cur->vertexAlignment = (cur->vertexCount < 4) ? 1 : (4 - (cur->vertexCount % 4));
            else
                cur->vertexAlignment = 0;

            if (!rlCheckRenderBatchLimit(cur->vertexAlignment))
            {
                RLVK.State.vertexCounter += cur->vertexAlignment;
                batch->drawCounter++;
            }
        }
        if (batch->drawCounter >= RL_DEFAULT_BATCH_DRAWCALLS)
            rlDrawRenderBatch(batch);

        cur = &batch->draws[batch->drawCounter - 1];
        cur->mode = mode;
        cur->vertexCount = 0;
        cur->vertexAlignment = 0;
        cur->textureId = RLVK.State.currentTextureSlot;
        RLVK.State.currentTextureSlot = RLVK.defaultTextureSlot;
    }
}

// Finish vertex providing
void rlEnd(void)
{
    if (RLVK.currentBatch)
        RLVK.currentBatch->currentDepth += (1.0f / 20000.0f);
}

// Hot path: writes go directly into the persistently-mapped GPU buffer behind rlVertexBuffer.
void rlVertex3f(f32 x, f32 y, f32 z)
{
    rlRenderBatch *batch = RLVK.currentBatch;
    if (!batch)
        return;
    rlVertexBuffer *vb = &batch->vertexBuffer[batch->currentBuffer];
    rlDrawCall *cur = &batch->draws[batch->drawCounter - 1];

    f32 tx = x, ty = y, tz = z;
    if (RLVK.State.transformRequired)
    {
        const Matrix *tr = &RLVK.State.transform;
        tx = tr->m0 * x + tr->m4 * y + tr->m8 * z + tr->m12;
        ty = tr->m1 * x + tr->m5 * y + tr->m9 * z + tr->m13;
        tz = tr->m2 * x + tr->m6 * y + tr->m10 * z + tr->m14;
    }

    if (RLVK.State.vertexCounter > (vb->elementCount * 4 - 4))
    {
        bool flushed = false;
        if (cur->mode == RL_LINES && (cur->vertexCount & 1) == 0)
            flushed = rlCheckRenderBatchLimit(2 + 1);
        else if (cur->mode == RL_TRIANGLES && (cur->vertexCount % 3) == 0)
            flushed = rlCheckRenderBatchLimit(3 + 1);
        else if (cur->mode == RL_QUADS && (cur->vertexCount & 3) == 0)
            flushed = rlCheckRenderBatchLimit(4 + 1);

        if (flushed)
        {
            vb = &batch->vertexBuffer[batch->currentBuffer];
            cur = &batch->draws[batch->drawCounter - 1];
        }
    }

    int i = RLVK.State.vertexCounter;

    f32 *v = &vb->vertices[3 * i];
    v[0] = tx;
    v[1] = ty;
    v[2] = tz;

    f32 *tc = &vb->texcoords[2 * i];
    tc[0] = RLVK.State.texcoordx;
    tc[1] = RLVK.State.texcoordy;

    f32 *nrm = &vb->normals[3 * i];
    nrm[0] = RLVK.State.normalx;
    nrm[1] = RLVK.State.normaly;
    nrm[2] = RLVK.State.normalz;

    unsigned char rgba[4] = {RLVK.State.colorr, RLVK.State.colorg, RLVK.State.colorb, RLVK.State.colora};
    memcpy(&vb->colors[4 * i], rgba, 4);

    RLVK.State.vertexCounter = i + 1;
    cur->vertexCount++;
}

void rlVertex2f(f32 x, f32 y) { rlVertex3f(x, y, RLVK.currentBatch ? RLVK.currentBatch->currentDepth : 0.0f); }
void rlVertex2i(int x, int y) { rlVertex3f((f32)x, (f32)y, RLVK.currentBatch ? RLVK.currentBatch->currentDepth : 0.0f); }

void rlTexCoord2f(f32 x, f32 y)
{
    RLVK.State.texcoordx = x;
    RLVK.State.texcoordy = y;
}

void rlNormal3f(f32 x, f32 y, f32 z)
{
    if (RLVK.State.transformRequired)
    {
        const Matrix *tr = &RLVK.State.transform;
        RLVK.State.normalx = tr->m0 * x + tr->m4 * y + tr->m8 * z;
        RLVK.State.normaly = tr->m1 * x + tr->m5 * y + tr->m9 * z;
        RLVK.State.normalz = tr->m2 * x + tr->m6 * y + tr->m10 * z;
    }
    else
    {
        RLVK.State.normalx = x;
        RLVK.State.normaly = y;
        RLVK.State.normalz = z;
    }
}

void rlColor4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    RLVK.State.colorr = r;
    RLVK.State.colorg = g;
    RLVK.State.colorb = b;
    RLVK.State.colora = a;
}

static inline unsigned char rlvkFloatToByte(f32 v)
{
    if (v <= 0.0f)
        return 0;
    if (v >= 1.0f)
        return 255;
    return (unsigned char)(v * 255.0f);
}

void rlColor3f(f32 x, f32 y, f32 z) { rlColor4ub(rlvkFloatToByte(x), rlvkFloatToByte(y), rlvkFloatToByte(z), 255); }
void rlColor4f(f32 r, f32 g, f32 b, f32 a) { rlColor4ub(rlvkFloatToByte(r), rlvkFloatToByte(g), rlvkFloatToByte(b), rlvkFloatToByte(a)); }

//----------------------------------------------------------------------------------
// Module Functions Definition - OpenGL style functions
//----------------------------------------------------------------------------------

// Enable vertex array object (VAO)
bool rlEnableVertexArray(unsigned int v)
{
    if (v == 0 || v >= RLVK_MAX_VAO_SLOTS || !RLVK.vertexArrays[v].inUse)
        return false;
    RLVK.State.currentVAO = v; // true => DrawMesh reuses the VAO's recorded attributes
    return true;
}
// Disable vertex array object (VAO)
void rlDisableVertexArray(void) { RLVK.State.currentVAO = 0; }
// Enable vertex buffer (VBO)
void rlEnableVertexBuffer(unsigned int id) { RLVK.State.currentVBO = id; }
// Disable vertex buffer (VBO)
void rlDisableVertexBuffer(void) {}
// Enable vertex buffer element (VBO element)
void rlEnableVertexBufferElement(unsigned int id)
{
    if (RLVK.State.currentVAO && RLVK.State.currentVAO < RLVK_MAX_VAO_SLOTS && id < RLVK_MAX_BUFFER_SLOTS)
        RLVK.vertexArrays[RLVK.State.currentVAO].indexSlot = id;
}
// Disable vertex buffer element (VBO element)
void rlDisableVertexBufferElement(void) {}
// Enable vertex attribute index
void rlEnableVertexAttribute(unsigned int idx) { (void)idx; }
// Disable vertex attribute index
void rlDisableVertexAttribute(unsigned int idx) { (void)idx; }
// Enable vertex state pointer
void rlEnableStatePointer(int t, void *b)
{
    (void)t;
    (void)b;
}
// Disable vertex state pointer
void rlDisableStatePointer(int t) { (void)t; }

// Select and active a texture slot
void rlActiveTextureSlot(int slot)
{
    if (slot >= 0 && slot < RLVK_MAX_TEXTURE_UNITS)
        RLVK.State.activeTextureUnit = slot;
}

// rlgl semantics: rlEnableTexture = glBindTexture on the ACTIVE UNIT only. It must NOT select
// the batch draw's texture - only rlSetTexture does that (deferred example binds gbuffer maps
// via units right before batching light spheres; aliasing unit 0 into the batch hijacked them).
void rlEnableTexture(unsigned int id)
{
    u32 tex = (id == 0) ? RLVK.defaultTextureSlot : id;
    int unit = RLVK.State.activeTextureUnit;
    if (RLVK.State.activeTextureSlots[unit] != tex)
    {
        RLVK.State.activeTextureSlots[unit] = tex;
        s_bindingValid = false;
    }
}
// Disable texture. GL semantics: glBindTexture(GL_TEXTURE_2D, 0) on the ACTIVE UNIT -
// clears that unit's binding. It must NOT touch the batch's current texture (that is
// rlSetTexture(0)'s job).
void rlDisableTexture(void)
{
    int unit = RLVK.State.activeTextureUnit;
    if (RLVK.State.activeTextureSlots[unit] != 0)
    {
        RLVK.State.activeTextureSlots[unit] = 0;
        s_bindingValid = false;
    }
}
// Enable texture cubemap
void rlEnableTextureCubemap(unsigned int id)
{
    rlEnableTexture(id);
}
// Disable texture cubemap (same active-unit semantics as rlDisableTexture)
void rlDisableTextureCubemap(void)
{
    rlDisableTexture();
}

// Set texture parameters (wrap mode/filter mode)
void rlTextureParameters(unsigned int id, int param, int value)
{
    if (id == 0 || id >= RLVK_MAX_TEXTURE_SLOTS)
        return;
    rlvkTextureSlot *t = &RLVK.textureSlots[id];
    if (!t->image)
        return;

    VkFilter newMag = t->magFilter;
    VkFilter newMin = t->minFilter;
    VkSamplerMipmapMode newMipMode = t->mipMode;
    bool newMipEnabled = t->mipEnabled;
    VkSamplerAddressMode newWrapS = t->wrapS;
    VkSamplerAddressMode newWrapT = t->wrapT;

    switch (param)
    {
    case RL_TEXTURE_MAG_FILTER:
        newMag = (value == RL_TEXTURE_FILTER_NEAREST) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        break;
    case RL_TEXTURE_MIN_FILTER:
        newMipEnabled = value == RL_TEXTURE_FILTER_MIP_NEAREST || value == RL_TEXTURE_FILTER_LINEAR_MIP_NEAREST ||
                        value == RL_TEXTURE_FILTER_NEAREST_MIP_LINEAR || value == RL_TEXTURE_FILTER_MIP_LINEAR;
        newMin = ((value == RL_TEXTURE_FILTER_NEAREST) || (value == RL_TEXTURE_FILTER_MIP_NEAREST) ||
                  (value == RL_TEXTURE_FILTER_NEAREST_MIP_LINEAR))
                     ? VK_FILTER_NEAREST
                     : VK_FILTER_LINEAR;
        newMipMode = ((value == RL_TEXTURE_FILTER_NEAREST_MIP_LINEAR) || (value == RL_TEXTURE_FILTER_MIP_LINEAR))
                         ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                         : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    case RL_TEXTURE_WRAP_S:
    case RL_TEXTURE_WRAP_T:
    {
        VkSamplerAddressMode m = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        if (value == RL_TEXTURE_WRAP_CLAMP)
            m = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        else if (value == RL_TEXTURE_WRAP_MIRROR_REPEAT)
            m = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        else if (value == RL_TEXTURE_WRAP_MIRROR_CLAMP)
            m = VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
        if (param == RL_TEXTURE_WRAP_S)
            newWrapS = m;
        else
            newWrapT = m;
        break;
    }
    default:
        return; // anisotropy / mipmap bias unsupported for now
    }

    // Clamp linear filter to nearest on formats that lack SAMPLED_IMAGE_FILTER_LINEAR support
    if ((t->format == VK_FORMAT_R32_SFLOAT && !RLVK.Caps.floatFilterR32) || (t->format == VK_FORMAT_D32_SFLOAT))
    {
        newMag = VK_FILTER_NEAREST;
        newMin = VK_FILTER_NEAREST;
        newMipMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }

    if (t->sampler != VK_NULL_HANDLE &&
        newMag == t->magFilter && newMin == t->minFilter &&
        newMipMode == t->mipMode && newMipEnabled == t->mipEnabled &&
        newWrapS == t->wrapS && newWrapT == t->wrapT)
    {
        return; // No sampler state changed; skip redundant vkCreateSampler
    }

    t->magFilter = newMag;
    t->minFilter = newMin;
    t->mipMode = newMipMode;
    t->mipEnabled = newMipEnabled;
    t->wrapS = newWrapS;
    t->wrapT = newWrapT;

    VkSampler oldSampler = t->sampler;
    for (u32 b = 0; b < RLVK_MAX_TEXTURE_UNITS; b++)
    {
        if (oldSampler && RLVK.pushedSampler[b] == oldSampler)
        {
            RLVK.pushedSampler[b] = VK_NULL_HANDLE;
            RLVK.set0Dirty = true;
        }
    }
    s_bindingValid = false;

    rlvkDeferDestroy(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, oldSampler, VK_NULL_HANDLE, VK_NULL_HANDLE);
    RLVK_CHECK(vkCreateSampler(RLVK.device,
                               &(VkSamplerCreateInfo){
                                   VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                   .magFilter = t->magFilter,
                                   .minFilter = t->minFilter,
                                   .mipmapMode = t->mipMode,
                                   .addressModeU = t->wrapS,
                                   .addressModeV = t->wrapT,
                                   .addressModeW = t->wrapT,
                                   .maxLod = (t->mipEnabled && t->mipCount > 1) ? (f32)(t->mipCount - 1) : 0.0f,
                               },
                               RLVK_ALLOC, &t->sampler));
}
// Set cubemap parameters (wrap mode/filter mode)
void rlCubemapParameters(unsigned int id, int param, int value) { rlTextureParameters(id, param, value); }

// rlgl semantics: rlEnableShader is glUseProgram - it selects the target for rlSetUniform* and
// the shader mesh/quad draws use. The BATCH's shader (rlgl State.currentShaderId) changes only
// through rlSetShader; a stray SetShaderValue before drawing must NOT hijack the batch.
void rlEnableShader(unsigned int id)
{
    RLVK.State.activeShaderSlot = (id == 0 || id >= RLVK_MAX_SHADER_SLOTS || !RLVK.shaderSlots[id].inUse)
                                      ? RLVK.defaultShaderSlot
                                      : id;
}
// Disable shader program
void rlDisableShader(void) { RLVK.State.activeShaderSlot = RLVK.defaultShaderSlot; }