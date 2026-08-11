//----------------------------------------------------------------------------------
// rlvk.h implementation fragment: Math helpers + pixel-format mapping
//
// Part of the rlvk single-header backend. NOT a standalone header - it is textually
// included by rlvk.h inside the ONE RLVK_IMPLEMENTATION translation unit. No include
// guard: order is fixed by the #include chain in rlvk.h. Do not #include directly.
//----------------------------------------------------------------------------------

//----------------------------------------------------------------------------------
// Internal Functions Definition - Math and format mapping
//----------------------------------------------------------------------------------

// Get identity matrix
static Matrix rlvkMatrixIdentity(void)
{
    return (Matrix){ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
}

// Get two matrix multiplication result
static Matrix rlvkMatrixMultiply(Matrix l, Matrix r)
{
    Matrix m;
    m.m0  = l.m0 *r.m0  + l.m1 *r.m4  + l.m2 *r.m8   + l.m3 *r.m12;
    m.m1  = l.m0 *r.m1  + l.m1 *r.m5  + l.m2 *r.m9   + l.m3 *r.m13;
    m.m2  = l.m0 *r.m2  + l.m1 *r.m6  + l.m2 *r.m10  + l.m3 *r.m14;
    m.m3  = l.m0 *r.m3  + l.m1 *r.m7  + l.m2 *r.m11  + l.m3 *r.m15;
    m.m4  = l.m4 *r.m0  + l.m5 *r.m4  + l.m6 *r.m8   + l.m7 *r.m12;
    m.m5  = l.m4 *r.m1  + l.m5 *r.m5  + l.m6 *r.m9   + l.m7 *r.m13;
    m.m6  = l.m4 *r.m2  + l.m5 *r.m6  + l.m6 *r.m10  + l.m7 *r.m14;
    m.m7  = l.m4 *r.m3  + l.m5 *r.m7  + l.m6 *r.m11  + l.m7 *r.m15;
    m.m8  = l.m8 *r.m0  + l.m9 *r.m4  + l.m10*r.m8   + l.m11*r.m12;
    m.m9  = l.m8 *r.m1  + l.m9 *r.m5  + l.m10*r.m9   + l.m11*r.m13;
    m.m10 = l.m8 *r.m2  + l.m9 *r.m6  + l.m10*r.m10  + l.m11*r.m14;
    m.m11 = l.m8 *r.m3  + l.m9 *r.m7  + l.m10*r.m11  + l.m11*r.m15;
    m.m12 = l.m12*r.m0  + l.m13*r.m4  + l.m14*r.m8   + l.m15*r.m12;
    m.m13 = l.m12*r.m1  + l.m13*r.m5  + l.m14*r.m9   + l.m15*r.m13;
    m.m14 = l.m12*r.m2  + l.m13*r.m6  + l.m14*r.m10  + l.m15*r.m14;
    m.m15 = l.m12*r.m3  + l.m13*r.m7  + l.m14*r.m11  + l.m15*r.m15;
    return m;
}

// Get transposed input matrix
static Matrix rlvkMatrixTranspose(Matrix m)
{
    return (Matrix){
        m.m0,  m.m4,  m.m8,  m.m12,
        m.m1,  m.m5,  m.m9,  m.m13,
        m.m2,  m.m6,  m.m10, m.m14,
        m.m3,  m.m7,  m.m11, m.m15
    };
}

// Get inverted input matrix
static Matrix rlvkMatrixInvert(Matrix m)
{
    f32 a00=m.m0, a01=m.m1, a02=m.m2, a03=m.m3;
    f32 a10=m.m4, a11=m.m5, a12=m.m6, a13=m.m7;
    f32 a20=m.m8, a21=m.m9, a22=m.m10, a23=m.m11;
    f32 a30=m.m12, a31=m.m13, a32=m.m14, a33=m.m15;
    f32 b00=a00*a11-a01*a10, b01=a00*a12-a02*a10, b02=a00*a13-a03*a10;
    f32 b03=a01*a12-a02*a11, b04=a01*a13-a03*a11, b05=a02*a13-a03*a12;
    f32 b06=a20*a31-a21*a30, b07=a20*a32-a22*a30, b08=a20*a33-a23*a30;
    f32 b09=a21*a32-a22*a31, b10=a21*a33-a23*a31, b11=a22*a33-a23*a32;
    f32 det = b00*b11 - b01*b10 + b02*b09 + b03*b08 - b04*b07 + b05*b06;
    if (fabsf(det) < 1e-8f) return rlvkMatrixIdentity();
    f32 inv = 1.0f/det;
    Matrix r;
    r.m0 =( a11*b11-a12*b10+a13*b09)*inv;
    r.m1 =(-a01*b11+a02*b10-a03*b09)*inv;
    r.m2 =( a31*b05-a32*b04+a33*b03)*inv;
    r.m3 =(-a21*b05+a22*b04-a23*b03)*inv;
    r.m4 =(-a10*b11+a12*b08-a13*b07)*inv;
    r.m5 =( a00*b11-a02*b08+a03*b07)*inv;
    r.m6 =(-a30*b05+a32*b02-a33*b01)*inv;
    r.m7 =( a20*b05-a22*b02+a23*b01)*inv;
    r.m8 =( a10*b10-a11*b08+a13*b06)*inv;
    r.m9 =(-a00*b10+a01*b08-a03*b06)*inv;
    r.m10=( a30*b04-a31*b02+a33*b00)*inv;
    r.m11=(-a20*b04+a21*b02-a23*b00)*inv;
    r.m12=(-a10*b09+a11*b07-a12*b06)*inv;
    r.m13=( a00*b09-a01*b07+a02*b06)*inv;
    r.m14=(-a30*b03+a31*b01-a32*b00)*inv;
    r.m15=( a20*b03-a21*b01+a22*b00)*inv;
    return r;
}

// Get pixel data size in bytes (image or texture), mirrors rlGetPixelDataSize()
static int rlvkGetPixelDataSize(int width, int height, int format)
{
    int bpp = 0;
    switch (format)
    {
        case RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE:    bpp = 8;   break;
        case RL_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA:
        case RL_PIXELFORMAT_UNCOMPRESSED_R5G6B5:
        case RL_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1:
        case RL_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4:     bpp = 16;  break;
        case RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8:       bpp = 24;  break;
        case RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:
        case RL_PIXELFORMAT_UNCOMPRESSED_R32:          bpp = 32;  break;
        case RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32:    bpp = 96;  break;
        case RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32: bpp = 128; break;
        default: bpp = 32; break;
    }
    return (width*height*bpp)/8;
}

// Get the Vulkan format equivalent to a raylib pixel format
static VkFormat rlvkGetVkTextureFormat(int format)
{
    switch (format)
    {
        case RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE:    return VK_FORMAT_R8_UNORM;
        case RL_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA:   return VK_FORMAT_R8G8_UNORM;
        case RL_PIXELFORMAT_UNCOMPRESSED_R5G6B5:       return VK_FORMAT_R5G6B5_UNORM_PACK16;
        case RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8:       return VK_FORMAT_R8G8B8_UNORM;
        case RL_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1:     return VK_FORMAT_R5G5B5A1_UNORM_PACK16;
        case RL_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4:     return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
        case RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:     return VK_FORMAT_R8G8B8A8_UNORM;
        case RL_PIXELFORMAT_UNCOMPRESSED_R32:          return VK_FORMAT_R32_SFLOAT;
        case RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32:    return VK_FORMAT_R32G32B32_SFLOAT;
        case RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case RL_PIXELFORMAT_UNCOMPRESSED_R16:          return VK_FORMAT_R16_SFLOAT;
        case RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16:    return VK_FORMAT_R16G16B16_SFLOAT;
        case RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case RL_PIXELFORMAT_COMPRESSED_DXT1_RGB:       return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        case RL_PIXELFORMAT_COMPRESSED_DXT1_RGBA:      return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case RL_PIXELFORMAT_COMPRESSED_DXT3_RGBA:      return VK_FORMAT_BC2_UNORM_BLOCK;
        case RL_PIXELFORMAT_COMPRESSED_DXT5_RGBA:      return VK_FORMAT_BC3_UNORM_BLOCK;
        case RL_PIXELFORMAT_COMPRESSED_ETC1_RGB:
        case RL_PIXELFORMAT_COMPRESSED_ETC2_RGB:       return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
        case RL_PIXELFORMAT_COMPRESSED_ETC2_EAC_RGBA:  return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
        case RL_PIXELFORMAT_COMPRESSED_ASTC_4x4_RGBA:  return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
        case RL_PIXELFORMAT_COMPRESSED_ASTC_8x8_RGBA:  return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
        default: return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

//----------------------------------------------------------------------------------
// Format capability query
//----------------------------------------------------------------------------------
// The device's own answer, never a table baked in here: what a driver supports beyond
// the spec's mandatory minimum is exactly the part that differs between the desktop this
// was written on and the phone it has to run on. See rlvk.h for which bits the spec does
// and does not guarantee for the float formats.
static VkFormatFeatureFlags rlvkQueryFormatFeatures(int rlFormat)
{
    if (RLVK.physicalDevice == VK_NULL_HANDLE) return 0;
    VkFormatProperties props = {0};
    vkGetPhysicalDeviceFormatProperties(RLVK.physicalDevice, rlvkGetVkTextureFormat(rlFormat), &props);
    return props.optimalTilingFeatures;
}

bool rlvkFormatSupportsColorAttachment(int rlFormat)
{
    return (rlvkQueryFormatFeatures(rlFormat) & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0;
}

bool rlvkFormatSupportsBlend(int rlFormat)
{
    return (rlvkQueryFormatFeatures(rlFormat) & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) != 0;
}

bool rlvkFormatSupportsLinearFilter(int rlFormat)
{
    return (rlvkQueryFormatFeatures(rlFormat) & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
}

