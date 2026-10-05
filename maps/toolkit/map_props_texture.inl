// Configure the immutable variant before it enters the shared cache.
static Texture2D MapLoadMippedTexture(const char *path, int wrap)
{
    Texture2D texture = ResourceManager_LoadTextureVariant(path, true,
        TEXTURE_FILTER_ANISOTROPIC_16X, wrap);
    if (!texture.id) {
        // Preserve the legacy load-time setup on constrained/fallback devices.
        texture = ResourceManager_LoadTexture(path);
        GenTextureMipmaps(&texture);
        SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
        SetTextureFilter(texture, TEXTURE_FILTER_ANISOTROPIC_16X);
        SetTextureWrap(texture, wrap);
    }
    return texture;
}
