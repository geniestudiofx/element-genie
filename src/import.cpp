// Element Genie - 3D model import (FBX / OBJ via ufbx, glTF / GLB via cgltf) and image loading
#include "mesh.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <functional>
#include <map>
#ifdef _WIN32
#include <windows.h>
#endif
#include <ufbx.h>
#include <cgltf.h>
#include <stb_image.h>
#include <glm/gtc/type_ptr.hpp>

using glm::vec2; using glm::vec3; using glm::vec4; using glm::mat4;

// ------------------------------------------------------------------ files / strings
std::wstring widen(const std::string& s) {
#ifdef _WIN32
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
#else
    return std::wstring(s.begin(), s.end());
#endif
}
std::string narrow(const std::wstring& w) {
#ifdef _WIN32
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
#else
    return std::string(w.begin(), w.end());
#endif
}
static FILE* openUtf8(const std::string& p, const char* mode) {
#ifdef _WIN32
    return _wfopen(widen(p).c_str(), widen(mode).c_str());
#else
    return fopen(p.c_str(), mode);
#endif
}
bool readFileBytes(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = openUtf8(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return false; }
    out.resize((size_t)n);
    size_t got = n ? fread(out.data(), 1, (size_t)n, f) : 0;
    fclose(f);
    return got == (size_t)n;
}
static bool fileExists(const std::string& p) { FILE* f = openUtf8(p, "rb"); if (f) { fclose(f); return true; } return false; }
static std::string dirOf(const std::string& p) { size_t k = p.find_last_of("/\\"); return k == std::string::npos ? "" : p.substr(0, k + 1); }
static std::string baseName(const std::string& p) { size_t k = p.find_last_of("/\\"); return k == std::string::npos ? p : p.substr(k + 1); }
static std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

static bool decodeImage(const uint8_t* data, size_t n, ImageData& out) {
    int w, h, c;
    if (stbi_is_hdr_from_memory(data, (int)n)) {
        float* f = stbi_loadf_from_memory(data, (int)n, &w, &h, &c, 3);
        if (!f) return false;
        out.w = w; out.h = h; out.rgbf.assign(f, f + (size_t)w * h * 3); out.srgb = false;
        stbi_image_free(f); return true;
    }
    unsigned char* p = stbi_load_from_memory(data, (int)n, &w, &h, &c, 4);
    if (!p) return false;
    out.w = w; out.h = h; out.rgba.assign(p, p + (size_t)w * h * 4);
    stbi_image_free(p); return true;
}
bool loadImageFile(const std::string& path, ImageData& out) {
    std::vector<uint8_t> b;
    if (!readFileBytes(path, b)) return false;
    return decodeImage(b.data(), b.size(), out);
}

// look for a texture next to the model, in a textures folder, etc.
static std::shared_ptr<ImageData> findTexture(const std::string& modelDir, const std::string& abs, const std::string& rel,
                                              std::map<std::string, std::shared_ptr<ImageData>>& cache) {
    std::vector<std::string> tries;
    if (!abs.empty()) tries.push_back(abs);
    if (!rel.empty()) tries.push_back(modelDir + rel);
    std::string bn = baseName(!rel.empty() ? rel : abs);
    if (!bn.empty()) {
        tries.push_back(modelDir + bn);
        tries.push_back(modelDir + "textures/" + bn);
        tries.push_back(modelDir + "Textures/" + bn);
        tries.push_back(modelDir + "../textures/" + bn);
    }
    for (auto& t : tries) {
        auto it = cache.find(t);
        if (it != cache.end()) return it->second;
        if (!fileExists(t)) continue;
        auto img = std::make_shared<ImageData>();
        if (loadImageFile(t, *img) && !img->rgba.empty()) { cache[t] = img; return img; }
    }
    return nullptr;
}

// ------------------------------------------------------------------ FBX / OBJ (ufbx)
static std::shared_ptr<ImageData> ufbxTex(const ufbx_texture* t, const std::string& dir, std::map<std::string, std::shared_ptr<ImageData>>& cache,
                                          std::map<const void*, std::shared_ptr<ImageData>>& embCache) {
    if (!t) return nullptr;
    if (t->type == UFBX_TEXTURE_FILE || t->content.size) {
        if (t->content.size) {
            auto it = embCache.find(t->content.data);
            if (it != embCache.end()) return it->second;
            auto img = std::make_shared<ImageData>();
            if (decodeImage((const uint8_t*)t->content.data, t->content.size, *img) && !img->rgba.empty()) { embCache[t->content.data] = img; return img; }
        }
        return findTexture(dir, std::string(t->absolute_filename.data, t->absolute_filename.length),
                           std::string(t->relative_filename.data, t->relative_filename.length), cache);
    }
    // layered / procedural: try first layer
    if (t->layers.count) return ufbxTex(t->layers.data[0].texture, dir, cache, embCache);
    return nullptr;
}

static BuildResult importUfbx(const std::string& path) {
    BuildResult r;
    ufbx_load_opts opts = {};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.generate_missing_normals = true;
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    ufbx_error err;
    ufbx_scene* sc = ufbx_load_file(path.c_str(), &opts, &err);
    if (!sc) { char buf[256]; ufbx_format_error(buf, sizeof buf, &err); r.error = std::string("Couldn't load model: ") + buf; return r; }
    std::string dir = dirOf(path);
    std::map<std::string, std::shared_ptr<ImageData>> cache;
    std::map<const void*, std::shared_ptr<ImageData>> emb;
    for (size_t ni = 0; ni < sc->nodes.count; ni++) {
        ufbx_node* node = sc->nodes.data[ni];
        ufbx_mesh* mesh = node->mesh;
        if (!mesh || !node->visible) continue;
        ufbx_matrix nm = ufbx_matrix_for_normals(&node->geometry_to_world);
        std::vector<uint32_t> triIdx(mesh->max_face_triangles * 3);
        for (size_t pi = 0; pi < mesh->material_parts.count; pi++) {
            const ufbx_mesh_part& part = mesh->material_parts.data[pi];
            if (!part.num_triangles) continue;
            MeshData md;
            ufbx_material* mat = (pi < mesh->materials.count) ? mesh->materials.data[pi] : nullptr;
            if (mat) {
                md.mat.has = true;
                auto& p = mat->pbr;
                ufbx_vec4 bc = p.base_color.value_vec4;
                float bf = p.base_factor.has_value ? (float)p.base_factor.value_real : 1.f;
                md.mat.base = vec4((float)bc.x * bf, (float)bc.y * bf, (float)bc.z * bf, 1.f);
                if (p.opacity.has_value) md.mat.base.a = (float)p.opacity.value_real;
                md.mat.metallic = p.metalness.has_value ? (float)p.metalness.value_real : 0.f;
                md.mat.roughness = p.roughness.has_value ? (float)p.roughness.value_real : 0.5f;
                ufbx_vec3 ec = p.emission_color.value_vec3;
                float ef = p.emission_factor.has_value ? (float)p.emission_factor.value_real : 1.f;
                if (p.emission_color.has_value) md.mat.emissive = vec3((float)ec.x, (float)ec.y, (float)ec.z) * ef;
                md.mat.baseImg = ufbxTex(p.base_color.texture ? p.base_color.texture : mat->fbx.diffuse_color.texture, dir, cache, emb);
                md.mat.normalImg = ufbxTex(p.normal_map.texture ? p.normal_map.texture : mat->fbx.normal_map.texture, dir, cache, emb);
                md.mat.emisImg = ufbxTex(p.emission_color.texture, dir, cache, emb);
                if (md.mat.baseImg) md.mat.base = vec4(1, 1, 1, md.mat.base.a);
                if (md.mat.base.a < 0.99f) md.mat.alphaMode = 2;
                if (md.mat.baseImg) {
                    bool cut = false;
                    for (size_t k = 3; k < md.mat.baseImg->rgba.size(); k += 4 * 7) if (md.mat.baseImg->rgba[k] < 128) { cut = true; break; }
                    if (cut) { md.mat.alphaMode = 1; md.mat.doubleSided = true; }
                }
                md.mat.doubleSided = md.mat.doubleSided || mat->features.double_sided.enabled;
            }
            for (size_t fi = 0; fi < part.face_indices.count; fi++) {
                ufbx_face face = mesh->faces.data[part.face_indices.data[fi]];
                uint32_t nt = ufbx_triangulate_face(triIdx.data(), triIdx.size(), mesh, face);
                for (uint32_t k = 0; k < nt * 3; k++) {
                    uint32_t ix = triIdx[k];
                    ufbx_vec3 p = ufbx_transform_position(&node->geometry_to_world, ufbx_get_vertex_vec3(&mesh->vertex_position, ix));
                    ufbx_vec3 n = mesh->vertex_normal.exists ? ufbx_transform_direction(&nm, ufbx_get_vertex_vec3(&mesh->vertex_normal, ix)) : ufbx_vec3{0, 1, 0};
                    ufbx_vec2 uv = mesh->vertex_uv.exists ? ufbx_get_vertex_vec2(&mesh->vertex_uv, ix) : ufbx_vec2{0, 0};
                    Vertex v;
                    v.p = vec3((float)p.x, (float)p.y, (float)p.z);
                    vec3 nn((float)n.x, (float)n.y, (float)n.z);
                    float l = glm::length(nn);
                    v.n = l > 1e-8f ? nn / l : vec3(0, 1, 0);
                    v.uv = vec2((float)uv.x, 1.f - (float)uv.y);
                    md.v.push_back(v);
                    md.i.push_back((uint32_t)md.i.size());
                }
            }
            if (!md.v.empty()) r.meshes.push_back(std::move(md));
        }
    }
    ufbx_free_scene(sc);
    if (r.meshes.empty()) r.error = "That file has no meshes in it.";
    return r;
}

// ------------------------------------------------------------------ glTF / GLB (cgltf)
static cgltf_result gRead(const cgltf_memory_options*, const cgltf_file_options*, const char* path, cgltf_size* size, void** data) {
    std::vector<uint8_t> b;
    if (!readFileBytes(path, b)) return cgltf_result_file_not_found;
    void* p = malloc(b.size() ? b.size() : 1);
    memcpy(p, b.data(), b.size());
    *size = b.size(); *data = p;
    return cgltf_result_success;
}
static void gRelease(const cgltf_memory_options*, const cgltf_file_options*, void* data, cgltf_size) { free(data); }

static std::shared_ptr<ImageData> gltfImage(const cgltf_texture_view& tv, const std::string& dir,
                                            std::map<const void*, std::shared_ptr<ImageData>>& cache) {
    if (!tv.texture) return nullptr;
    const cgltf_image* im = tv.texture->image;
    if (!im) return nullptr;
    auto it = cache.find(im);
    if (it != cache.end()) return it->second;
    auto img = std::make_shared<ImageData>();
    bool ok = false;
    if (im->buffer_view) {
        const uint8_t* d = cgltf_buffer_view_data(im->buffer_view);
        if (d) ok = decodeImage(d, im->buffer_view->size, *img);
    } else if (im->uri) {
        std::string uri = im->uri;
        if (uri.rfind("data:", 0) == 0) {
            size_t comma = uri.find(',');
            if (comma != std::string::npos) {
                void* out = nullptr;
                cgltf_options o = {};
                std::string b64 = uri.substr(comma + 1);
                size_t n = b64.size() * 3 / 4;
                while (!b64.empty() && b64.back() == '=') { b64.pop_back(); n--; }
                if (cgltf_load_buffer_base64(&o, n, b64.c_str(), &out) == cgltf_result_success) {
                    ok = decodeImage((const uint8_t*)out, n, *img); free(out);
                }
            }
        } else {
            std::vector<char> u(uri.begin(), uri.end()); u.push_back(0);
            cgltf_decode_uri(u.data());
            ok = loadImageFile(dir + u.data(), *img);
        }
    }
    if (!ok || img->rgba.empty()) img = nullptr;
    cache[im] = img;
    return img;
}

static BuildResult importGltf(const std::string& path) {
    BuildResult r;
    cgltf_options opts = {};
    opts.file.read = gRead; opts.file.release = gRelease;
    cgltf_data* data = nullptr;
    std::vector<uint8_t> bytes;
    if (!readFileBytes(path, bytes)) { r.error = "Couldn't open that file."; return r; }
    if (cgltf_parse(&opts, bytes.data(), bytes.size(), &data) != cgltf_result_success) { r.error = "Couldn't read that glTF/GLB file."; return r; }
    if (cgltf_load_buffers(&opts, data, path.c_str()) != cgltf_result_success) {
        cgltf_free(data); r.error = "The model's .bin data file is missing - keep it in the same folder as the .gltf."; return r;
    }
    std::string dir = dirOf(path);
    std::map<const void*, std::shared_ptr<ImageData>> cache;
    bool anyUnsupported = false;
    for (size_t ni = 0; ni < data->nodes_count; ni++) {
        cgltf_node* node = &data->nodes[ni];
        if (!node->mesh) continue;
        float wm[16]; cgltf_node_transform_world(node, wm);
        mat4 M = glm::make_mat4(wm);
        glm::mat3 N = glm::transpose(glm::inverse(glm::mat3(M)));
        for (size_t pi = 0; pi < node->mesh->primitives_count; pi++) {
            cgltf_primitive& prim = node->mesh->primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;
            if (prim.has_draco_mesh_compression) { anyUnsupported = true; continue; }
            const cgltf_accessor *pa = nullptr, *na = nullptr, *ta = nullptr, *ca = nullptr;
            for (size_t ai = 0; ai < prim.attributes_count; ai++) {
                auto& a = prim.attributes[ai];
                if (a.type == cgltf_attribute_type_position) pa = a.data;
                else if (a.type == cgltf_attribute_type_normal) na = a.data;
                else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) ta = a.data;
                else if (a.type == cgltf_attribute_type_color && a.index == 0) ca = a.data;
            }
            if (!pa) continue;
            MeshData md;
            md.v.resize(pa->count);
            for (size_t k = 0; k < pa->count; k++) {
                float p[4] = {0, 0, 0, 0}, n[4] = {0, 1, 0, 0}, t[4] = {0, 0, 0, 0};
                cgltf_accessor_read_float(pa, k, p, 3);
                if (na) cgltf_accessor_read_float(na, k, n, 3);
                if (ta) cgltf_accessor_read_float(ta, k, t, 2);
                md.v[k].p = vec3(M * vec4(p[0], p[1], p[2], 1));
                vec3 nn = N * vec3(n[0], n[1], n[2]);
                md.v[k].n = glm::length(nn) > 1e-8f ? glm::normalize(nn) : vec3(0, 1, 0);
                md.v[k].uv = vec2(t[0], t[1]);
            }
            if (prim.indices) { for (size_t k = 0; k < prim.indices->count; k++) md.i.push_back((uint32_t)cgltf_accessor_read_index(prim.indices, k)); }
            else for (size_t k = 0; k < pa->count; k++) md.i.push_back((uint32_t)k);
            // flip winding for mirrored nodes is irrelevant: we render double sided
            if (!na) {  // flat normals
                std::vector<Vertex> nv; std::vector<uint32_t> ni2;
                for (size_t k = 0; k + 2 < md.i.size(); k += 3) {
                    Vertex a = md.v[md.i[k]], b = md.v[md.i[k + 1]], c = md.v[md.i[k + 2]];
                    vec3 fn = glm::cross(b.p - a.p, c.p - a.p);
                    fn = glm::length(fn) > 1e-12f ? glm::normalize(fn) : vec3(0, 1, 0);
                    a.n = b.n = c.n = fn;
                    nv.push_back(a); nv.push_back(b); nv.push_back(c);
                    ni2.push_back((uint32_t)k); ni2.push_back((uint32_t)k + 1); ni2.push_back((uint32_t)k + 2);
                }
                md.v.swap(nv); md.i.swap(ni2);
            }
            if (prim.material) {
                auto* m = prim.material;
                md.mat.has = true;
                if (m->has_pbr_metallic_roughness) {
                    auto& p = m->pbr_metallic_roughness;
                    md.mat.base = vec4(p.base_color_factor[0], p.base_color_factor[1], p.base_color_factor[2], p.base_color_factor[3]);
                    md.mat.metallic = p.metallic_factor; md.mat.roughness = p.roughness_factor;
                    md.mat.baseImg = gltfImage(p.base_color_texture, dir, cache);
                    md.mat.mrImg = gltfImage(p.metallic_roughness_texture, dir, cache);
                } else if (m->has_pbr_specular_glossiness) {
                    auto& p = m->pbr_specular_glossiness;
                    md.mat.base = vec4(p.diffuse_factor[0], p.diffuse_factor[1], p.diffuse_factor[2], p.diffuse_factor[3]);
                    md.mat.roughness = 1.f - p.glossiness_factor; md.mat.metallic = 0;
                    md.mat.baseImg = gltfImage(p.diffuse_texture, dir, cache);
                }
                md.mat.normalImg = gltfImage(m->normal_texture, dir, cache);
                md.mat.emissive = vec3(m->emissive_factor[0], m->emissive_factor[1], m->emissive_factor[2]);
                if (m->has_emissive_strength) md.mat.emissive *= m->emissive_strength.emissive_strength;
                md.mat.emisImg = gltfImage(m->emissive_texture, dir, cache);
                md.mat.alphaMode = m->alpha_mode == cgltf_alpha_mode_mask ? 1 : m->alpha_mode == cgltf_alpha_mode_blend ? 2 : 0;
                md.mat.alphaCutoff = m->alpha_cutoff;
                md.mat.doubleSided = m->double_sided;
                if (m->unlit) { md.mat.emissive = vec3(md.mat.base); md.mat.base = vec4(0, 0, 0, md.mat.base.a); md.mat.emisImg = md.mat.baseImg; }
            }
            (void)ca;
            if (!md.v.empty()) r.meshes.push_back(std::move(md));
        }
    }
    cgltf_free(data);
    if (r.meshes.empty()) r.error = anyUnsupported ? "This model uses Draco compression, which isn't supported yet. Download the uncompressed version."
                                                   : "That file has no meshes in it.";
    return r;
}

BuildResult importModel(const std::string& path) {
    std::string ext = lower(path.substr(path.find_last_of('.') + 1));
    BuildResult r;
    if (ext == "glb" || ext == "gltf") r = importGltf(path);
    else if (ext == "fbx" || ext == "obj") r = importUfbx(path);
    else { r.error = "Supported model types: GLB, glTF, FBX, OBJ."; return r; }
    if (!r.meshes.empty()) fitToUnit(r.meshes, 2.f, false);
    return r;
}

// ------------------------------------------------------------------ bounds / normalise
void computeBounds(const std::vector<MeshData>& m, vec3& mn, vec3& mx) {
    mn = vec3(1e30f); mx = vec3(-1e30f);
    for (auto& d : m) for (auto& v : d.v) { mn = glm::min(mn, v.p); mx = glm::max(mx, v.p); }
    if (mn.x > mx.x) { mn = mx = vec3(0); }
}
void fitToUnit(std::vector<MeshData>& m, float size, bool) {
    vec3 mn, mx; computeBounds(m, mn, mx);
    vec3 c = (mn + mx) * .5f;
    float ext = std::max(mx.x - mn.x, std::max(mx.y - mn.y, mx.z - mn.z));
    float s = ext > 1e-9f ? size / ext : 1.f;
    for (auto& d : m) for (auto& v : d.v) v.p = (v.p - c) * s;
}

bool decodeImageMem(const uint8_t* d, size_t n, ImageData& out) { return decodeImage(d, n, out); }
