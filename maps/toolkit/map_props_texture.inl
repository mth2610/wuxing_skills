// Configure the immutable variant before it enters the shared cache.
static Texture2D MapLoadMippedTexture(const char *path, int wrap)
{
    if (!path || path[0] == '\0')
        return (Texture2D){0};

    Texture2D texture = ResourceManager_LoadTextureVariant(path, true,
                                                           TEXTURE_FILTER_ANISOTROPIC_16X, wrap);
    if (texture.id == 0)
    {
        // Preserve the legacy load-time setup on constrained/fallback devices.
        texture = ResourceManager_LoadTexture(path);
        if (texture.id != 0)
        {
            if (texture.mipmaps <= 1)
                GenTextureMipmaps(&texture);
            SetTextureFilter(texture, TEXTURE_FILTER_ANISOTROPIC_16X);
            SetTextureWrap(texture, wrap);
        }
    }
    return texture;
}