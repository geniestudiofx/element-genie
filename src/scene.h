// Element Genie - scene description (everything here is saved in the project file)
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <glm/glm.hpp>
#include "anim.h"

struct Geometry; // built from the object's source params, cached

enum class ObjType { Model = 0, Text = 1, Logo = 2, Primitive = 3, ImageCard = 4 };
enum class Prim { Cube = 0, Sphere, Cylinder, Torus, Plane, RoundCube, Gem, Cone, Capsule };

struct Material {
    std::string preset = "Chrome";
    glm::vec3 color{0.95f, 0.95f, 0.95f};
    float metallic = 1.f;
    float roughness = 0.12f;
    glm::vec3 emissive{0, 0, 0};
    float emissiveStrength = 0.f;
    float clearcoat = 0.f;
    // texture maps (optional image files)
    std::string baseMap, roughMap, normalMap;
    int mapping = 1;         // 0 = model UVs, 1 = box (wraps any shape)
    float mapScale = 1.f;    // repeats per unit
    float normalStrength = 1.f;
};

// Clone an object into an arrangement (Element Genie's take on a particle replicator)
struct Replicator {
    bool enabled = false;
    int shape = 0;           // 0 grid, 1 ring, 2 sphere, 3 spiral, 4 random cloud, 5 line, 6 wave grid
    int cx = 3, cy = 1, cz = 1;   // grid counts
    int count = 12;          // copies for ring / sphere / spiral / cloud / line
    glm::vec3 spacing{1.4f, 1.4f, 1.4f};
    float radius = 2.5f;
    float height = 2.f;      // spiral rise / cloud size
    float turns = 2.f;       // spiral turns
    bool faceCentre = true;  // ring / sphere / spiral copies face outwards
    float waveHeight = 0.5f; // wave grid
    glm::vec3 randPos{0};
    glm::vec3 randRot{0};
    float randScale = 0.f;
    float scaleStep = 0.f;   // each copy grows/shrinks by this much
    float stagger = 0.f;     // Effect Controls "Replicator Build": delay between copies (0-1)
    int seed = 1;
};

// Bend, twist, taper and wobble the shape
struct Deform {
    int axis = 1;            // 0 X, 1 Y, 2 Z
    float twist = 0.f;       // degrees from one end to the other
    float bend = 0.f;        // degrees
    float taper = 0.f;       // -1..1
    float noise = 0.f;       // wobble size (units)
    float noiseScale = 1.5f; // wobble detail
    float noiseSpeed = 0.f;  // wobble speed (per second, Scene Setup preview / standalone)
    bool active() const { return std::fabs(twist) > 1e-3f || std::fabs(bend) > 1e-3f || std::fabs(taper) > 1e-4f || noise > 1e-5f; }
};

// Break the object into pieces that fly apart
struct Shatter {
    bool enabled = false;
    int pieces = 40;
    float force = 2.f;       // distance pieces travel at 100%
    float spin = 360.f;      // degrees pieces turn at 100%
    float gravity = 1.f;
    int direction = 0;       // 0 outwards, 1 towards camera, 2 upwards, 3 random
    float randomness = 0.5f;
    int seed = 3;
};

struct LetterAnim {
    bool enabled = false;
    int mode = 0;            // 0 = build in, 1 = build out
    int start = 0;           // frame
    int duration = 20;       // frames per letter
    float stagger = 3.f;     // frames between letters
    int order = 0;           // 0 L->R, 1 R->L, 2 centre out, 3 random
    glm::vec3 offPos{0, 0, 2.f};
    glm::vec3 offRot{0, 90, 0};
    float offScale = 1.f;
    bool fade = true;
};

struct Object {
    int id = 0;
    std::string name;
    ObjType type = ObjType::Primitive;
    bool visible = true;
    bool castShadow = true;

    // ---- source ----
    std::string path;        // model / svg / png file
    std::string text = "ELEMENT";
    std::string font;        // font file path
    float depth = 0.25f;     // extrusion depth (text/logo)
    float bevel = 0.03f;     // bevel size
    int bevelSegs = 3;
    int bevelStyle = 1;      // see bevel.h presets
    float bevelDepth = 1.f;  // bevel depth relative to its width
    float letterSpacing = 0.f;
    float lineSpacing = 1.15f;
    int align = 1;           // 0 left 1 centre 2 right
    int prim = (int)Prim::Cube;
    bool keepModelMaterials = true;
    bool useImageColours = true;  // logo / image: show the picture's colours on the face
    float alphaThreshold = 0.5f;  // PNG extrusion

    // ---- look ----
    Material mat;            // face material
    bool edgeMat = false;    // separate material for bevel + sides
    Material mat2;

    // ---- animation ----
    Track<glm::vec3> pos{glm::vec3(0)};
    Track<glm::vec3> rot{glm::vec3(0)};   // degrees, XYZ
    Track<glm::vec3> scl{glm::vec3(1)};
    Track<float> opacity{1.f};
    LetterAnim letters;

    int group = 0;           // which Effect Controls group (0-4) moves this object in Premiere

    // ---- effects ----
    Replicator rep;
    Deform deform;
    Shatter shatter;

    // ---- runtime (not saved) ----
    glm::mat4 parentM{1.f};  // group transform from Effect Controls
    float opacityMul = 1.f;
    float depthMul = 1.f;    // Effect Controls > Look > Extrusion Depth (runtime)
    // per-letter transform from Effect Controls (runtime)
    bool lfOn = false;
    glm::vec3 lfRot{0}, lfPos{0};
    float lfScale = 1.f, lfPhase = 0.f, lfRandom = 0.f;
    int lfSpread = 0;
    float letterFrame = -1;  // >= 0: letter animation evaluated at this pseudo-frame
    std::shared_ptr<Geometry> geo;
    std::string geoKey;
    std::string geoError;
};

struct Light {
    std::string name = "Key light";
    int type = 0;            // 0 directional, 1 point
    Track<glm::vec3> pos{glm::vec3(3, 5, 4)};
    glm::vec3 color{1, 1, 1};
    Track<float> intensity{3.f};
    bool shadow = true;
    bool enabled = true;
    // spot cone (After Effects spot lights); spotCos < -1 means no cone
    glm::vec3 spotDir{0, 0, -1};
    float spotCos = -2.f, spotInner = -2.f;
};

struct CameraRig {
    Track<glm::vec3> pos{glm::vec3(0, 0.6f, 6.f)};
    Track<glm::vec3> target{glm::vec3(0, 0, 0)};
    Track<float> fov{35.f};  // vertical degrees
    Track<float> roll{0.f};
};

struct Environment {
    int preset = 6;          // procedural presets (6 = Classic chrome), or -1 = image
    std::string path;        // .hdr / .jpg equirect
    float intensity = 1.f;
    Track<float> rotation{0.f};
    float exposure = 0.f;    // stops
    bool followLight = true; // reflections swing round with the key light
};

struct ShadowCatcher {
    bool enabled = false;
    float opacity = 0.55f;
    float softness = 0.5f;
    float height = 0.f;      // relative to lowest object point
    bool autoHeight = true;
};

// Render quality (set from Effect Controls in Premiere; not saved in the scene)
struct RenderQuality {
    int shadowRes = 4096;
    bool ao = true;          // ambient occlusion / contact shadows
    float aoStrength = 1.f;
    float aoRadius = 0.35f;
};

struct FloorReflection {
    bool enabled = true;
    float strength = 0.35f;
    float fade = 0.8f;       // how far below the floor the reflection fades out (units)
};

struct Lens {
    bool dof = false;
    bool autoFocus = true;   // focus on the middle of the scene
    float focus = 6.f;       // focus distance (units) when not auto
    float aperture = 0.3f;   // blur strength 0-1
};

struct Fog {
    bool enabled = false;
    glm::vec3 color{0.62f, 0.66f, 0.75f};
    float start = 5.f, end = 25.f;  // distance from camera (units)
    float amount = 1.f;
    bool fadeOut = false;    // fade to transparent instead of a colour
};

// Values driven from Effect Controls (not saved)
struct LiveFx {
    float shatter = 0.f;     // 0-1
    float deform = 1.f;      // multiplier
    float noiseEvo = 0.f;    // extra noise time
    float repSpread = 1.f;   // replicator size multiplier
    float repRotate = 0.f;   // degrees
    float repBuild = 1.f;    // 0-1 copies appearing
    float focusOffset = 0.f, blurMul = 1.f, fogMul = 1.f;
    int pass = 0;            // 0 final, 1 matte, 2 depth, 3 normals, 4 ambient occlusion
};

struct Comp {
    int width = 1920, height = 1080;
    float fps = 25.f;
    int frames = 125;        // duration
};

struct Scene {
    Comp comp;
    std::vector<Object> objects;
    std::vector<Light> lights;
    CameraRig cam;
    Environment env;
    ShadowCatcher catcher;
    FloorReflection reflection;
    bool motionBlur = false;
    float shutter = 180.f;
    int mbSamples = 8;
    float glow = 0.f;        // bloom strength
    float glowThreshold = 1.f;
    std::string refPath;     // reference background image (preview only)
    float refOpacity = 1.f;
    int nextId = 1;
    RenderQuality quality;
    Lens lens;
    Fog fog;
    LiveFx live;             // runtime only
    // runtime: camera supplied by the host (After Effects comp camera)
    bool camOv = false;
    glm::vec3 camOvPos{0}, camOvFwd{0, 0, -1}, camOvUp{0, 1, 0};
    float camOvFov = 35.f;
};

void sceneDefaults(Scene& s);
