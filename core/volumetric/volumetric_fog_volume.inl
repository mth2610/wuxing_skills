/* Private CPU -> shader packet encoding. Included by the renderer and its
 * headless upload-contract test; shader ids are not public FogVolumeShape ids. */
static inline Vector4 VolumetricFog_PackPositionShape(const LocalFogVolume *volume) {
    float shaderShape;
    switch (volume->shape) {
        case FOG_SHAPE_SPHERE: shaderShape = 1.0f; break;
        case FOG_SHAPE_CYLINDER: shaderShape = 2.0f; break;
        case FOG_SHAPE_BOX:
        default: shaderShape = 0.0f; break;
    }
    return (Vector4){volume->position.x, volume->position.y, volume->position.z,
                     shaderShape};
}
