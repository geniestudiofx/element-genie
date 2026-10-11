// Element Genie - geometry prep for Deform (smooth subdivision) and Shatter (pieces)
#include "mesh.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

using glm::vec2; using glm::vec3; using glm::vec4;

static Vertex mid(const Vertex& a, const Vertex& b) {
    Vertex v;
    v.p = (a.p + b.p) * .5f;
    vec3 n = a.n + b.n; float l = glm::length(n);
    v.n = l > 1e-8f ? n / l : a.n;
    v.uv = (a.uv + b.uv) * .5f;
    v.piece = a.piece;
    return v;
}

// Splits every edge longer than maxEdge (the same edge gets split the same way in both neighbouring
// triangles, so no cracks open when the shape bends).
void subdivideForDeform(std::vector<MeshData>& meshes, float maxEdge, size_t triBudget) {
    size_t total = 0;
    for (auto& m : meshes) total += m.i.size() / 3;
    float L2 = maxEdge * maxEdge;
    for (int pass = 0; pass < 8; pass++) {
        bool any = false;
        for (auto& m : meshes) {
            if (total >= triBudget) return;
            std::vector<Vertex> V = m.v; std::vector<uint32_t> I;
            I.reserve(m.i.size() * 2);
            // share midpoints between triangles that share an edge (by vertex index pair)
            std::map<std::pair<uint32_t, uint32_t>, uint32_t> mids;
            auto midIdx = [&](uint32_t a, uint32_t b) {
                auto k = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
                auto it = mids.find(k);
                if (it != mids.end()) return it->second;
                V.push_back(mid(m.v[a], m.v[b]));
                uint32_t id = (uint32_t)V.size() - 1; mids[k] = id; return id;
            };
            size_t before = m.i.size() / 3;
            for (size_t t = 0; t + 2 < m.i.size(); t += 3) {
                uint32_t a = m.i[t], b = m.i[t + 1], c = m.i[t + 2];
                bool ab = glm::dot(m.v[a].p - m.v[b].p, m.v[a].p - m.v[b].p) > L2;
                bool bc = glm::dot(m.v[b].p - m.v[c].p, m.v[b].p - m.v[c].p) > L2;
                bool ca = glm::dot(m.v[c].p - m.v[a].p, m.v[c].p - m.v[a].p) > L2;
                int n = ab + bc + ca;
                if (!n) { I.insert(I.end(), {a, b, c}); continue; }
                any = true;
                if (n == 3) {
                    uint32_t x = midIdx(a, b), y = midIdx(b, c), z = midIdx(c, a);
                    I.insert(I.end(), {a, x, z, x, b, y, z, y, c, x, y, z});
                } else if (n == 1) {
                    if (ab) { uint32_t x = midIdx(a, b); I.insert(I.end(), {a, x, c, x, b, c}); }
                    else if (bc) { uint32_t y = midIdx(b, c); I.insert(I.end(), {a, b, y, a, y, c}); }
                    else { uint32_t z = midIdx(c, a); I.insert(I.end(), {a, b, z, z, b, c}); }
                } else {  // two long edges
                    if (!ab) { uint32_t y = midIdx(b, c), z = midIdx(c, a); I.insert(I.end(), {a, b, y, a, y, z, z, y, c}); }
                    else if (!bc) { uint32_t x = midIdx(a, b), z = midIdx(c, a); I.insert(I.end(), {a, x, z, x, b, c, x, c, z}); }
                    else { uint32_t x = midIdx(a, b), y = midIdx(b, c); I.insert(I.end(), {a, x, c, x, b, y, x, y, c}); }
                }
            }
            total += I.size() / 3 - before;
            m.v.swap(V); m.i.swap(I);
        }
        if (!any) break;
    }
}

static float rnd(unsigned& s) { s = s * 1664525u + 1013904223u; return ((s >> 8) & 0xffffff) / float(0xffffff); }

// Voronoi-style pieces: every triangle joins the nearest of N random seed points.
// Each piece gets its own copy of its vertices, its centre and a random number.
void makeShatterPieces(std::vector<MeshData>& meshes, int pieces, int seed) {
    vec3 mn, mx; computeBounds(meshes, mn, mx);
    vec3 ext = glm::max(mx - mn, vec3(1e-4f));
    pieces = std::clamp(pieces, 2, 2000);
    unsigned st = 0x9e3779b9u ^ (unsigned)seed * 7919u;
    std::vector<vec3> seeds(pieces);
    for (auto& p : seeds) p = mn + vec3(rnd(st), rnd(st), rnd(st)) * ext;
    vec3 w(1.f);
    std::vector<vec3> sum(pieces, vec3(0)); std::vector<float> cnt(pieces, 0);
    struct Tri { int mesh; size_t t; int piece; };
    std::vector<Tri> tris;
    for (int mi = 0; mi < (int)meshes.size(); mi++) {
        auto& m = meshes[mi];
        for (size_t t = 0; t + 2 < m.i.size(); t += 3) {
            vec3 c = (m.v[m.i[t]].p + m.v[m.i[t + 1]].p + m.v[m.i[t + 2]].p) / 3.f;
            int best = 0; float bd = 1e30f;
            for (int k = 0; k < pieces; k++) { vec3 d = (c - seeds[k]) * w; float dd = glm::dot(d, d); if (dd < bd) { bd = dd; best = k; } }
            tris.push_back({mi, t, best});
            sum[best] += c; cnt[best] += 1;
        }
    }
    std::vector<vec4> info(pieces);
    for (int k = 0; k < pieces; k++) info[k] = vec4(cnt[k] > 0 ? sum[k] / cnt[k] : seeds[k], rnd(st));
    std::vector<MeshData> out(meshes.size());
    for (size_t mi = 0; mi < meshes.size(); mi++) { out[mi].slot = meshes[mi].slot; out[mi].glyph = meshes[mi].glyph; out[mi].mat = meshes[mi].mat; }
    for (auto& tr : tris) {
        auto& src = meshes[tr.mesh]; auto& dst = out[tr.mesh];
        for (int k = 0; k < 3; k++) {
            Vertex v = src.v[src.i[tr.t + k]];
            v.piece = info[tr.piece];
            dst.i.push_back((uint32_t)dst.v.size());
            dst.v.push_back(v);
        }
    }
    meshes.swap(out);
}
