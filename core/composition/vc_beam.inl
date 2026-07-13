// Straight Beam visual composition wrapper.
// delegates straight line beams to the unified path link composer.

void VFX_ComposeBeam(VC_MaterialId matId, Vector3 start, Vector3 end, float width, float progress, float time)
{
    Vector3 points[2] = { start, end };
    VFX_ComposePathLink(matId, points, 2, width, progress, time);
}