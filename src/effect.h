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
    float autoLetters = 100; // % built from Auto Animate (clip time)
    float twist = 0, bend = 0, taper = 0;  // Twist & Bend (degrees, degrees, %)
    int defAxis = 0, defTarget = 0;         // 0 X,1 Y,2 Z; 0 all, 1-5 group
    float wobble = 0;                       // % (noise deform, animate with Noise Evolution)
    float lightRot = 0, selfGlow = 100, depthPct = 100;  // Look
    float envRot = 0, lightPct = 100, glowPct = 100;
    bool composite = true;
    // realism
    int msaa = 8, supersample = 1, shadowRes = 4096;
    bool floorShadow = false; float shadowSoft = 30, shadowDark = 55;
    bool ao = true; float aoStrength = 100, aoRadius = 35;
    bool reflection = true; float reflStrength = 35, reflFade = 80;
    // effects
    float shatter = 0, deform = 100, noiseEvo = 0, repSpread = 100, repRotate = 0, repBuild = 100;
    int dofMode = 0, fogMode = 0;   // 0 use Scene Setup, 1 on, 2 off
    float focusOffset = 0, blur = 100, fog = 100;
    int pass = 0;
    // per letter (text only)
    glm::vec3 lRot{0}, lPos{0};
    float lScale = 100;
    int lSpread = 0;             // 0 same, 1 ramp L->R, 2 ramp R->L, 3 centre out, 4 wave, 5 random
    float lPhase = 0, lRandom = 0;
    int lTarget = 0;             // 0 all groups, 1-5 group
    // After Effects comp camera (already in scene units)
    bool aeCam = false;
    glm::vec3 aePos{0}, aeFwd{0, 0, -1}, aeUp{0, 1, 0};
    float aeFov = 35;
    bool aeDof = false; float aeFocus = 6.f, aeAperture = 0.f;  // AE camera depth of field
    // After Effects comp lights (already in scene units)
    static constexpr int MAX_AE_LIGHTS = 8;
    struct AELight { int type = 1; glm::vec3 pos{0}; glm::vec3 color{1}; bool shadow = false; glm::vec3 dir{0, 0, -1}; float cosOuter = -2.f, cosInner = -2.f; };  // type 0 parallel, 1 point/spot, 2 ambient
    int aeLightN = 0;
    bool aeKeepLights = false;
    AELight aeLights[MAX_AE_LIGHTS];
};

// Applies Effect Controls values on top of the scene designed in Scene Setup.
void applyFx(Scene& s, const FxParams& p);
// Letter animation driven by a 0-100% 'built' amount.
void setLetterFrames(Scene& s, float pct);
