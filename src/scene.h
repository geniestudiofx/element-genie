// Element Genie - scene description (everything here is saved in the project file)
#pragma once
#include <string>
#include <vector>
#include <memory>
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

    // ---- runtime (not saved) ----
    glm::mat4 parentM{1.f};  // group transform from Effect Controls
    float opacityMul = 1.f;
    float letterFrame = -1;  // >= 0: letter animation evaluated at this pseudo-frame
    // per-letter transform from Effect Controls
    bool lfOn = false;
    glm::vec3 lfRot{0}, lfPos{0};
    float lfScale = 1.f, lfPhase = 0.f, lfRandom = 0.f;
    int lfSpread = 0;
    // twist deform from Effect Controls
    float twist = 0.f, twistOffset = 0.f;
    int twistAxis = 0;
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
};

void sceneDefaults(Scene& s);
