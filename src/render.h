// Element Genie - renderer
#pragma once
#include "scene.h"
#include "mesh.h"
#include <glm/glm.hpp>

struct RenderTarget {
    int w = 0, h = 0, samples = 0;
    unsigned msFbo = 0, msColor = 0, msDepth = 0;     // multisampled HDR (msColor is a multisample texture)
    unsigned hdrFbo = 0, hdrTex = 0;                   // resolved HDR
    unsigned outFbo = 0, outTex = 0;                   // final RGBA8, premultiplied, sRGB encoded
    unsigned accFbo = 0, accTex = 0;                   // motion blur accumulation (HDR)
    unsigned bloomFbo[6] = {}, bloomTex[6] = {}; int bloomW[6] = {}, bloomH[6] = {};
    unsigned ndFbo = 0, ndTex = 0, ndDepth = 0;        // view-space normal + depth (for ambient occlusion)
    unsigned aoFbo = 0, aoTex = 0, ao2Fbo = 0, ao2Tex = 0;
    void ensure(int w, int h, int samples);
    void release();
};

struct CamEval { glm::mat4 view, proj; glm::vec3 pos; };

struct ViewOptions {
    bool preview = false;
    bool grid = false;
    int selected = -1;     // object id to outline
    float aspect = 0;      // display aspect override (pixel aspect ratio)
};

glm::mat4 objectMatrix(const Object& o, float f);
glm::mat4 glyphMatrix(const Object& o, int glyph, int nGlyphs, float f, float& opacity);
CamEval evalCamera(const Scene& s, float f, float aspect);
bool worldBounds(const Scene& s, float f, glm::vec3& mn, glm::vec3& mx);

class Renderer {
public:
    bool init(std::string& err);
    // Draws the scene at frame f into rt.outTex (and returns it). w/h = output size.
    unsigned render(Scene& s, float f, RenderTarget& rt, const ViewOptions& vo);
    // Ensures environment cubemap matches the scene settings.
    void updateEnvironment(const Environment& e);
    std::string envError;
    // Ensure geometry is built for an object (lazy). Returns false if failed.
    bool ensureGeometry(Object& o);
    void drawFullscreen();
    unsigned progBlitPremul = 0;   // draws a texture to screen
private:
    unsigned progPBR = 0, progShadow = 0, progCatcher = 0, progEnv = 0, progPrefilter = 0, progEquirect = 0;
    unsigned progTonemap = 0, progBright = 0, progDown = 0, progUp = 0, progLines = 0, progAccum = 0;
    unsigned progPrepass = 0, progSSAO = 0, progAOBlur = 0, progResolve = 0;
    bool prepassMode = false, reflectPass = false;
    glm::mat4 extraM{1.f};
    float groundYCur = 0, depthRangeCur = 1, texPerWorldCur = 1;
    unsigned aoTexCur = 0;
    glm::vec2 screenCur{1, 1};
    int shadowResCur = 0;
    void ensureShadowMap(int res);
    void drawCatcherQuad(unsigned prog, const glm::vec3& c, float R, float y);
    unsigned envBase = 0, envCube = 0; int envMips = 6;
    std::string envKey;
    unsigned shadowFbo = 0, shadowTex = 0; int shadowRes = 2048;
    unsigned quadVao = 0, emptyVao = 0, lineVao = 0, lineVbo = 0;
    unsigned whiteTex = 0, flatNormalTex = 0;

    void renderPass(Scene& s, float f, RenderTarget& rt, const ViewOptions& vo);  // into rt.hdrTex
    void drawObjects(Scene& s, float f, const glm::mat4& V, const glm::mat4& P, const glm::vec3& camPos, bool shadowPass,
                     const glm::mat4& shadowVP, int shadowOn, bool transparentPass);
    void drawLines(const std::vector<glm::vec3>& pts, const glm::vec4& col, const glm::mat4& VP);
    void buildEnvCube(int preset, const ImageData* img);
};
