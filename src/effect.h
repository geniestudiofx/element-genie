// Element Genie - values that come from Premiere's Effect Controls (keyframeable there)
#pragma once
#include "scene.h"

struct FxGroup {
    glm::vec3 pos{0};       // units
    glm::vec3 rot{0};       // degrees
    float scale = 100.f;    // %
    float opacity = 100.f;  // %
};

struct FxParams {
    FxGroup g[5];
    float orbit = 0, tilt = 0, zoom = 100, panX = 0, panY = 0, roll = 0, fovOff = 0;
    float letters = 100;    // % built
    float envRot = 0, lightPct = 100, glowPct = 100;
    bool composite = true;
    // realism
    int msaa = 8, supersample = 1, shadowRes = 4096;
    bool floorShadow = false; float shadowSoft = 30, shadowDark = 55;
    bool ao = true; float aoStrength = 100, aoRadius = 35;
    bool reflection = true; float reflStrength = 35, reflFade = 80;
};

// Applies Effect Controls values on top of the scene designed in Scene Setup.
void applyFx(Scene& s, const FxParams& p);
// Letter animation driven by a 0-100% 'built' amount.
void setLetterFrames(Scene& s, float pct);
