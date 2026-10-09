// Element Genie - mesh data (CPU side) and GPU geometry
#pragma once
#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include <glm/glm.hpp>

struct Vertex {
    glm::vec3 p;
    glm::vec3 n;
    glm::vec2 uv;
};

// Decoded image, RGBA8 or float RGB
struct ImageData {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    std::vector<float> rgbf;
    bool srgb = true;
};

struct Texture {
    unsigned id = 0;
    int w = 0, h = 0;
    ~Texture();
};
std::shared_ptr<Texture> uploadTexture(const ImageData& img, bool srgb, bool mips = true, bool clampEdge = false);

struct MeshMat {
    bool has = false;            // came from the file (model) or picture (logo)
    glm::vec4 base{1, 1, 1, 1};
    float metallic = 0.f, roughness = 0.5f;
    glm::vec3 emissive{0};
    int alphaMode = 0;           // 0 opaque, 1 cutout, 2 blend
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    std::shared_ptr<ImageData> baseImg, mrImg, normalImg, emisImg;  // CPU until uploaded
    std::shared_ptr<Texture> baseTex, mrTex, normalTex, emisTex;
};

struct MeshData {
    std::vector<Vertex> v;
    std::vector<uint32_t> i;
    int slot = 0;                // 0 face material, 1 edge material
    int glyph = -1;              // letter index for text animation
    MeshMat mat;
};

struct Part {
    unsigned vao = 0, vbo = 0, ibo = 0;
    int count = 0;
    int slot = 0;
    int glyph = -1;
    MeshMat mat;
};

struct Geometry {
    std::vector<Part> parts;
    std::vector<glm::vec3> glyphCenters;
    glm::vec3 bmin{0}, bmax{0};
    ~Geometry();
};

struct BuildResult {
    std::vector<MeshData> meshes;
    std::vector<glm::vec3> glyphCenters;
    std::string error;
};

std::shared_ptr<Geometry> uploadGeometry(BuildResult& r);
void computeBounds(const std::vector<MeshData>& m, glm::vec3& mn, glm::vec3& mx);
void fitToUnit(std::vector<MeshData>& m, float size, bool centreY);  // centre + scale largest side to size

// builders
struct Object;
BuildResult buildText(const Object& o);
BuildResult buildSvgLogo(const Object& o);
BuildResult buildPngLogo(const Object& o);
BuildResult buildImageCard(const Object& o);
BuildResult buildPrimitive(const Object& o);
BuildResult importModel(const std::string& path);

// helpers
bool loadImageFile(const std::string& path, ImageData& out);
bool readFileBytes(const std::string& path, std::vector<uint8_t>& out);
std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);
