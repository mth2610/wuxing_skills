#ifndef WUXING_GAS_PROFILE_INTERNAL_INL
#define WUXING_GAS_PROFILE_INTERNAL_INL

#include <stdbool.h>

typedef struct GasQualityProfile {
    int effectiveTier;
    int gridWidth;
    int gridHeight;
    int gridDepth;
    int renderDivisor;
    int raymarchSteps;
    float fixedStep;
    int atlasTilesX;
    int atlasWidth;
    int atlasHeight;
    unsigned int atlasBytes;
    int targetWidth;
    int targetHeight;
    int noiseSamplesPerStep;
} GasQualityProfile;

static GasQualityProfile GasQualityProfile_Make(int requestedTier,
                                                int screenWidth,
                                                int screenHeight)
{
    GasQualityProfile profile = {0};
    profile.effectiveTier = requestedTier < 1 ? 1 :
                            (requestedTier > 3 ? 3 : requestedTier);
    profile.atlasTilesX = 8;
    if (profile.effectiveTier >= 3) {
        profile.gridWidth = 28;
        profile.gridHeight = 32;
        profile.gridDepth = 28;
        profile.renderDivisor = 3;
        profile.raymarchSteps = 40;
        profile.fixedStep = 1.0f / 20.0f;
        profile.noiseSamplesPerStep = 2;
    } else if (profile.effectiveTier >= 2) {
        profile.gridWidth = 20;
        profile.gridHeight = 28;
        profile.gridDepth = 20;
        profile.renderDivisor = 4;
        profile.raymarchSteps = 24;
        profile.fixedStep = 1.0f / 15.0f;
        profile.noiseSamplesPerStep = 2;
    } else {
        profile.gridWidth = 16;
        profile.gridHeight = 24;
        profile.gridDepth = 16;
        profile.renderDivisor = 4;
        profile.raymarchSteps = 16;
        profile.fixedStep = 1.0f / 10.0f;
        profile.noiseSamplesPerStep = 1;
    }
    profile.atlasWidth = profile.gridWidth * profile.atlasTilesX;
    int tileRows = (profile.gridDepth + profile.atlasTilesX - 1) /
                   profile.atlasTilesX;
    profile.atlasHeight = profile.gridHeight * tileRows;
    profile.atlasBytes = (unsigned int)(profile.atlasWidth *
                                        profile.atlasHeight * 4);
    profile.targetWidth = screenWidth / profile.renderDivisor;
    profile.targetHeight = screenHeight / profile.renderDivisor;
    if (profile.targetWidth < 1) profile.targetWidth = 1;
    if (profile.targetHeight < 1) profile.targetHeight = 1;
    return profile;
}

static bool GasQualityProfile_NeedsRebuild(GasQualityProfile current,
                                           int requestedTier)
{
    int effective = requestedTier < 1 ? 1 : (requestedTier > 3 ? 3 : requestedTier);
    return current.effectiveTier != effective;
}

#endif
