// Element Genie - 3D text, logo extrusion (SVG / PNG), image cards and primitives
#include "mesh.h"
#include "scene.h"
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>
#include <array>
#include <algorithm>
#include <random>
#include <functional>
#include <earcut.hpp>
#include <stb_truetype.h>
#include <nanosvg.h>
#include "bevel.h"
#include "icons.h"

using glm::vec2; using glm::vec3; using glm::dvec2;
typedef std::vector<dvec2> Contour;

struct Poly {
    Contour outer;
    std::vector<Contour> holes;
    glm::vec4 color{1, 1, 1, 1};
    bool hasColor = false;
};

// ------------------------------------------------------------------ embedded fonts
extern const unsigned char g_font_inter_bold[]; extern const unsigned g_font_inter_bold_size;
extern const unsigned char g_font_inter_black[]; extern const unsigned g_font_inter_black_size;
extern const unsigned char g_font_inter_regular[]; extern const unsigned g_font_inter_regular_size;

static bool fontBytes(const std::string& font, std::vector<uint8_t>& out) {
    if (!font.empty() && font.rfind("builtin:", 0) != 0 && readFileBytes(font, out)) return true;
    const unsigned char* p = g_font_inter_bold; unsigned n = g_font_inter_bold_size;
    if (font == "builtin:Inter Black") { p = g_font_inter_black; n = g_font_inter_black_size; }
    if (font == "builtin:Inter Regular") { p = g_font_inter_regular; n = g_font_inter_regular_size; }
    out.assign(p, p + n);
    return true;
}

// ------------------------------------------------------------------ polygon helpers
static double area(const Contour& c) {
    double a = 0;
    for (size_t i = 0, n = c.size(); i < n; i++) { auto& p = c[i]; auto& q = c[(i + 1) % n]; a += p.x * q.y - q.x * p.y; }
    return a * .5;
}
static bool pointIn(const Contour& c, dvec2 p) {
    bool in = false;
    for (size_t i = 0, j = c.size() - 1; i < c.size(); j = i++) {
        if (((c[i].y > p.y) != (c[j].y > p.y)) && (p.x < (c[j].x - c[i].x) * (p.y - c[i].y) / (c[j].y - c[i].y) + c[i].x)) in = !in;
    }
    return in;
}
static Contour clean(const Contour& c, double eps) {
    Contour o;
    for (auto& p : c) if (o.empty() || glm::length(p - o.back()) > eps) o.push_back(p);
    while (o.size() > 2 && glm::length(o.front() - o.back()) <= eps) o.pop_back();
    // drop collinear points
    bool changed = true;
    while (changed && o.size() > 3) {
        changed = false;
        for (size_t i = 0; i < o.size() && o.size() > 3; i++) {
            dvec2 a = o[(i + o.size() - 1) % o.size()], b = o[i], cc = o[(i + 1) % o.size()];
            double cr = (b.x - a.x) * (cc.y - a.y) - (b.y - a.y) * (cc.x - a.x);
            if (std::fabs(cr) < eps * eps * 0.01) { o.erase(o.begin() + i); changed = true; }
        }
    }
    return o;
}

// group contours into outer + holes using containment depth (even-odd)
static std::vector<Poly> groupContours(std::vector<Contour> cs, double eps) {
    std::vector<Contour> c2;
    for (auto& c : cs) { auto k = clean(c, eps); if (k.size() >= 3 && std::fabs(area(k)) > eps * eps) c2.push_back(k); }
    int n = (int)c2.size();
    std::vector<int> depth(n, 0);
    std::vector<double> ar(n);
    for (int i = 0; i < n; i++) ar[i] = std::fabs(area(c2[i]));
    auto samplePt = [&](const Contour& c) { return (c[0] + c[1]) * .5; };
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            if (i != j && ar[j] > ar[i] && pointIn(c2[j], samplePt(c2[i]))) depth[i]++;
    std::vector<Poly> polys;
    std::vector<int> polyOf(n, -1);
    for (int i = 0; i < n; i++) if (depth[i] % 2 == 0) {
        Poly p; p.outer = c2[i];
        if (area(p.outer) < 0) std::reverse(p.outer.begin(), p.outer.end());
        polyOf[i] = (int)polys.size(); polys.push_back(p);
    }
    for (int i = 0; i < n; i++) if (depth[i] % 2 == 1) {
        int best = -1; double ba = 1e30;
        for (int j = 0; j < n; j++)
            if (polyOf[j] >= 0 && depth[j] == depth[i] - 1 && ar[j] > ar[i] && ar[j] < ba && pointIn(c2[j], samplePt(c2[i]))) { best = j; ba = ar[j]; }
        if (best < 0) continue;
        Contour h = c2[i];
        if (area(h) > 0) std::reverse(h.begin(), h.end());
        polys[polyOf[best]].holes.push_back(h);
    }
    return polys;
}

// inset (into the solid, i.e. to the left of travel) by d; solid is always on the left
static Contour insetContour(const Contour& c, double d) {
    if (d == 0) return c;
    size_t n = c.size();
    Contour o(n);
    for (size_t i = 0; i < n; i++) {
        dvec2 a = c[(i + n - 1) % n], b = c[i], cc = c[(i + 1) % n];
        dvec2 e0 = glm::normalize(b - a), e1 = glm::normalize(cc - b);
        dvec2 n0(-e0.y, e0.x), n1(-e1.y, e1.x);
        dvec2 m = n0 + n1;
        double ml = glm::length(m);
        if (ml < 1e-9) { o[i] = b + n0 * d; continue; }
        m /= ml;
        double cosh = glm::dot(m, n0);
        double len = d / std::max(cosh, 0.5);
        o[i] = b + m * len;
    }
    return o;
}

struct Ring { double inset, z, no, nz; };  // profile point + profile normal (outward, z)
struct Band { int a, b; bool flat; };

typedef std::function<vec2(dvec2)> UVFn;

static void addVert(MeshData& m, dvec2 p, double z, vec3 n, vec2 uv) {
    Vertex v; v.p = vec3((float)p.x, (float)p.y, (float)z); v.n = n; v.uv = uv; m.v.push_back(v);
}

static void extrudePoly(const Poly& poly, double depth, double bevel, int segs, int style, double bevelDepth, double zOff,
                        MeshData& face, MeshData& side, const UVFn& uvf) {
    double D = std::max(depth, 0.0);
    double b = style == BEVEL_NONE ? 0.0 : bevel;
    {   // keep the bevel thinner than the shape's strokes, or the inset folds over itself
        double A = std::fabs(area(poly.outer)), P = 0;
        auto per = [&](const Contour& c) { for (size_t i = 0; i < c.size(); i++) P += glm::length(c[(i + 1) % c.size()] - c[i]); };
        per(poly.outer);
        for (auto& h : poly.holes) { A -= std::fabs(area(h)); per(h); }
        double thick = P > 0 ? 2.0 * A / P : 0;
        b = std::min(b, std::max(0.0, thick * 0.28));
    }
    if (D <= 1e-6) b = 0;
    // profile rings front -> back
    std::vector<Ring> rings; std::vector<Band> bands;
    double hf = D * .5;
    BevelProfile prof = bevelProfile(style, segs);
    if (D <= 1e-6) b = 0;
    if (b <= 1e-7 || prof.pts.size() < 2) {
        rings.push_back({0, hf, 1, 0}); rings.push_back({0, -hf, 1, 0});
        if (D > 1e-6) bands.push_back({0, 1, true});
    } else {
        double maxD = 0;
        for (auto& q : prof.pts) maxD = std::max(maxD, q.y);
        double bz = b * std::max(0.05, bevelDepth);
        if (maxD * bz > D * 0.49) bz = D * 0.49 / std::max(1e-6, maxD);
        size_t n = prof.pts.size();
        for (size_t i = 0; i < n; i++) rings.push_back({prof.pts[i].x * b, hf - prof.pts[i].y * bz, 0, 0});
        for (size_t i = n; i-- > 0;) rings.push_back({prof.pts[i].x * b, -hf + prof.pts[i].y * bz, 0, 0});
        // segment normals (outward, z) in travel order front -> back
        std::vector<dvec2> sn(rings.size() - 1);
        for (size_t i = 0; i + 1 < rings.size(); i++) {
            double to = -(rings[i + 1].inset - rings[i].inset), tz = rings[i + 1].z - rings[i].z;
            double l = std::sqrt(to * to + tz * tz);
            sn[i] = l > 1e-12 ? dvec2(-tz / l, to / l) : dvec2(1, 0);
        }
        for (size_t i = 0; i < rings.size(); i++) {
            dvec2 a = i > 0 ? sn[i - 1] : sn[0], c = i < sn.size() ? sn[i] : sn.back();
            // do not smooth across the side wall
            if (i == n - 1) c = a;
            if (i == n) a = c;
            dvec2 m = a + c; double l = glm::length(m);
            m = l > 1e-9 ? m / l : c;
            rings[i].no = m.x; rings[i].nz = m.y;
        }
        for (size_t i = 0; i + 1 < rings.size(); i++) {
            bool sideWall = (i == n - 1);
            bands.push_back({(int)i, (int)i + 1, sideWall || !prof.smooth});
        }
    }
    for (auto& r : rings) r.z += zOff;

    std::vector<const Contour*> all; all.push_back(&poly.outer);
    for (auto& h : poly.holes) all.push_back(&h);

    // ---- sides
    for (auto* cp : all) {
        const Contour& c = *cp; size_t n = c.size();
        std::vector<Contour> rc(rings.size());
        for (size_t r = 0; r < rings.size(); r++) rc[r] = insetContour(c, rings[r].inset);
        // per-edge outward normals and per-vertex smoothed outward normals
        std::vector<dvec2> eo(n), vo(n); std::vector<bool> sharp(n);
        for (size_t i = 0; i < n; i++) { dvec2 e = glm::normalize(c[(i + 1) % n] - c[i]); eo[i] = dvec2(e.y, -e.x); }
        for (size_t i = 0; i < n; i++) {
            dvec2 a = eo[(i + n - 1) % n], bb = eo[i];
            sharp[i] = glm::dot(a, bb) < cos(35.0 * M_PI / 180.0);
            dvec2 m = a + bb; vo[i] = glm::length(m) > 1e-9 ? glm::normalize(m) : bb;
        }
        std::vector<double> ulen(n + 1, 0);
        for (size_t i = 0; i < n; i++) ulen[i + 1] = ulen[i] + glm::length(c[(i + 1) % n] - c[i]);
        for (auto& bd : bands) {
            const Ring& A = rings[bd.a]; const Ring& B = rings[bd.b];
            double fno, fnz;
            if (bd.flat) {
                double to = -(B.inset - A.inset), tz = B.z - A.z;
                double l = std::sqrt(to * to + tz * tz); if (l < 1e-9) continue;
                fno = -tz / l; fnz = to / l;
            }
            for (size_t i = 0; i < n; i++) {
                size_t j = (i + 1) % n;
                dvec2 oi = sharp[i] ? eo[i] : vo[i], oj = sharp[j] ? eo[i] : vo[j];
                auto N = [&](dvec2 o, const Ring& R) {
                    double no = bd.flat ? fno : R.no, nz = bd.flat ? fnz : R.nz;
                    return glm::normalize(vec3((float)(o.x * no), (float)(o.y * no), (float)nz));
                };
                uint32_t base = (uint32_t)side.v.size();
                addVert(side, rc[bd.a][i], A.z, N(oi, A), vec2((float)ulen[i], (float)A.z));
                addVert(side, rc[bd.a][j], A.z, N(oj, A), vec2((float)ulen[i + 1], (float)A.z));
                addVert(side, rc[bd.b][j], B.z, N(oj, B), vec2((float)ulen[i + 1], (float)B.z));
                addVert(side, rc[bd.b][i], B.z, N(oi, B), vec2((float)ulen[i], (float)B.z));
                side.i.insert(side.i.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            }
        }
    }
    // ---- caps (triangulate front ring)
    double capInset = rings.front().inset;
    std::vector<std::vector<std::array<double, 2>>> ep;
    std::vector<dvec2> flat;
    for (auto* cp : all) {
        Contour ic = insetContour(*cp, capInset);
        std::vector<std::array<double, 2>> ring;
        for (auto& p : ic) { ring.push_back({p.x, p.y}); flat.push_back(p); }
        ep.push_back(ring);
    }
    std::vector<uint32_t> tri = mapbox::earcut<uint32_t>(ep);
    double zf = rings.front().z, zb = rings.back().z;
    uint32_t fb = (uint32_t)face.v.size();
    for (auto& p : flat) addVert(face, p, zf, vec3(0, 0, 1), uvf(p));
    for (size_t k = 0; k < tri.size(); k += 3) face.i.insert(face.i.end(), {fb + tri[k], fb + tri[k + 1], fb + tri[k + 2]});
    if (D > 1e-6) {
        uint32_t bb = (uint32_t)face.v.size();
        for (auto& p : flat) { vec2 uv = uvf(p); addVert(face, p, zb, vec3(0, 0, -1), vec2(1.f - uv.x, uv.y)); }
        for (size_t k = 0; k < tri.size(); k += 3) face.i.insert(face.i.end(), {bb + tri[k], bb + tri[k + 2], bb + tri[k + 1]});
    }
}

// ------------------------------------------------------------------ curves
static void quadTo(Contour& c, dvec2 p0, dvec2 p1, dvec2 p2, double tol) {
    double l = glm::length(p1 - p0) + glm::length(p2 - p1);
    int n = std::clamp((int)(l / tol), 2, 24);
    for (int k = 1; k <= n; k++) { double t = (double)k / n, u = 1 - t; c.push_back(u * u * p0 + 2 * u * t * p1 + t * t * p2); }
}
static void cubicTo(Contour& c, dvec2 p0, dvec2 p1, dvec2 p2, dvec2 p3, double tol) {
    double l = glm::length(p1 - p0) + glm::length(p2 - p1) + glm::length(p3 - p2);
    int n = std::clamp((int)(l / tol), 2, 32);
    for (int k = 1; k <= n; k++) {
        double t = (double)k / n, u = 1 - t;
        c.push_back(u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3);
    }
}

static uint32_t utf8Next(const char*& s) {
    unsigned char c = (unsigned char)*s++;
    if (c < 0x80) return c;
    int n = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : 1;
    uint32_t cp = c & (0x3F >> n);
    for (int i = 0; i < n && *s; i++) cp = (cp << 6) | ((unsigned char)*s++ & 0x3F);
    return cp;
}

// ------------------------------------------------------------------ 3D text
BuildResult buildText(const Object& o) {
    BuildResult r;
    std::vector<uint8_t> fb;
    fontBytes(o.font, fb);
    stbtt_fontinfo f;
    if (!stbtt_InitFont(&f, fb.data(), stbtt_GetFontOffsetForIndex(fb.data(), 0))) {
        fontBytes("builtin:Inter Bold", fb);
        stbtt_InitFont(&f, fb.data(), 0);
        r.error = "Couldn't read that font, using Inter Bold.";
    }
    int asc, desc, gap; stbtt_GetFontVMetrics(&f, &asc, &desc, &gap);
    int x0, y0, x1, y1;
    double capH = 0.7 * (asc - desc);
    if (stbtt_GetCodepointBox(&f, 'H', &x0, &y0, &x1, &y1)) capH = y1;
    double sc = 1.0 / capH;  // cap height = 1 unit
    double tol = 0.01 / sc;
    double lineH = (asc - desc + gap) * sc * o.lineSpacing;

    struct G { std::vector<Contour> cs; double x; int line; };
    std::vector<G> glyphs; std::vector<double> lineW;
    double pen = 0; int line = 0; int prev = 0;
    const char* s = o.text.c_str();
    while (*s) {
        uint32_t cp = utf8Next(s);
        if (cp == '\r') continue;
        if (cp == '\n') { lineW.push_back(pen); pen = 0; line++; prev = 0; continue; }
        int gi = stbtt_FindGlyphIndex(&f, cp);
        if (prev) pen += stbtt_GetGlyphKernAdvance(&f, prev, gi) * sc;
        int adv, lsb; stbtt_GetGlyphHMetrics(&f, gi, &adv, &lsb);
        stbtt_vertex* vs; int nv = stbtt_GetGlyphShape(&f, gi, &vs);
        G g; g.x = pen; g.line = line;
        Contour cur; dvec2 last(0);
        for (int k = 0; k < nv; k++) {
            auto& v = vs[k];
            dvec2 p(v.x, v.y);
            if (v.type == STBTT_vmove) { if (cur.size() > 2) g.cs.push_back(cur); cur.clear(); cur.push_back(p); }
            else if (v.type == STBTT_vline) cur.push_back(p);
            else if (v.type == STBTT_vcurve) quadTo(cur, last, dvec2(v.cx, v.cy), p, tol);
            else if (v.type == STBTT_vcubic) cubicTo(cur, last, dvec2(v.cx, v.cy), dvec2(v.cx1, v.cy1), p, tol);
            last = p;
        }
        if (cur.size() > 2) g.cs.push_back(cur);
        if (nv) stbtt_FreeShape(&f, vs);
        for (auto& c : g.cs) for (auto& p : c) p *= sc;
        if (!g.cs.empty()) glyphs.push_back(g);
        pen += adv * sc + o.letterSpacing;
        prev = gi;
    }
    lineW.push_back(pen);
    int nLines = line + 1;
    double totalH = (nLines - 1) * lineH;
    int gIndex = 0;
    for (auto& g : glyphs) {
        double w = lineW[g.line];
        double ax = o.align == 0 ? 0 : o.align == 1 ? -w * .5 : -w;
        dvec2 off(g.x + ax, -g.line * lineH + totalH * .5 - 0.5);
        for (auto& c : g.cs) for (auto& p : c) p += off;
        auto polys = groupContours(g.cs, 1e-4);
        if (polys.empty()) continue;
        MeshData face, side; face.slot = 0; side.slot = 1; face.glyph = side.glyph = gIndex;
        dvec2 mn(1e9), mx(-1e9);
        for (auto& c : g.cs) for (auto& p : c) { mn = glm::min(mn, p); mx = glm::max(mx, p); }
        UVFn uvf = [&](dvec2 p) { return vec2((float)((p.x - mn.x) / std::max(1e-6, mx.y - mn.y)), (float)((p.y - mn.y) / std::max(1e-6, mx.y - mn.y))); };
        for (auto& p : polys) extrudePoly(p, o.depth, o.bevel, o.bevelSegs, o.bevelStyle, o.bevelDepth, 0, face, side, uvf);
        r.meshes.push_back(std::move(face));
        if (!side.v.empty()) r.meshes.push_back(std::move(side));
        dvec2 c = (mn + mx) * .5;
        r.glyphCenters.push_back(vec3((float)c.x, (float)c.y, 0));
        gIndex++;
    }
    if (r.meshes.empty() && r.error.empty()) r.error = "Type some text to see it in 3D.";
    return r;
}

// ------------------------------------------------------------------ SVG logo
BuildResult buildSvgLogo(const Object& o) {
    BuildResult r;
    std::vector<uint8_t> bytes;
    if (o.path.rfind("builtin:icon:", 0) == 0) {
        std::string n = o.path.substr(13);
        for (int i = 0; i < ICON_COUNT; i++) if (n == ICONS[i].name) bytes.assign(ICONS[i].svg, ICONS[i].svg + strlen(ICONS[i].svg));
        if (bytes.empty()) { r.error = "Unknown icon."; return r; }
    } else if (!readFileBytes(o.path, bytes)) { r.error = "Couldn't open the SVG file."; return r; }
    bytes.push_back(0);
    NSVGimage* img = nsvgParse((char*)bytes.data(), "px", 96.f);
    if (!img) { r.error = "Couldn't read that SVG."; return r; }
    double s = 2.0 / std::max(1.f, std::max(img->width, img->height));
    double cx = img->width * .5, cy = img->height * .5;
    double tol = 0.004 / s;
    int layer = 0;
    for (NSVGshape* sh = img->shapes; sh; sh = sh->next) {
        if (!(sh->flags & NSVG_FLAGS_VISIBLE)) continue;
        bool fill = sh->fill.type != NSVG_PAINT_NONE;
        if (!fill) continue;
        std::vector<Contour> cs;
        for (NSVGpath* p = sh->paths; p; p = p->next) {
            Contour c; c.push_back(dvec2(p->pts[0], p->pts[1]));
            for (int i = 0; i < p->npts - 1; i += 3) {
                float* q = &p->pts[i * 2];
                cubicTo(c, dvec2(q[0], q[1]), dvec2(q[2], q[3]), dvec2(q[4], q[5]), dvec2(q[6], q[7]), tol);
            }
            for (auto& pt : c) pt = dvec2((pt.x - cx) * s, -(pt.y - cy) * s);
            cs.push_back(c);
        }
        auto polys = groupContours(cs, 1e-4);
        if (polys.empty()) continue;
        unsigned col = sh->fill.type == NSVG_PAINT_COLOR ? sh->fill.color
                     : (sh->fill.gradient && sh->fill.gradient->nstops ? sh->fill.gradient->stops[0].color : 0xffffffff);
        glm::vec4 c4((col & 255) / 255.f, ((col >> 8) & 255) / 255.f, ((col >> 16) & 255) / 255.f, 1.f);
        MeshData face, side; face.slot = 0; side.slot = 1;
        face.mat.has = true; face.mat.base = glm::vec4(glm::pow(vec3(c4), vec3(2.2f)), 1);  // linear
        face.mat.roughness = 0.35f;
        UVFn uvf = [&](dvec2 p) { return vec2((float)(p.x * .5 + .5), (float)(.5 - p.y * .5)); };
        double zOff = layer * 0.004;
        for (auto& p : polys) extrudePoly(p, o.depth, o.bevel, o.bevelSegs, o.bevelStyle, o.bevelDepth, zOff, face, side, uvf);
        side.mat = face.mat;
        r.meshes.push_back(std::move(face));
        if (!side.v.empty()) r.meshes.push_back(std::move(side));
        layer++;
    }
    nsvgDelete(img);
    if (r.meshes.empty()) r.error = "No filled shapes found in that SVG (outlines/strokes aren't supported yet).";
    return r;
}

// ------------------------------------------------------------------ PNG logo (traced from alpha)
static void dp(const Contour& c, size_t a, size_t b, double tol, std::vector<bool>& keep) {
    if (b <= a + 1) return;
    double md = -1; size_t mi = a;
    dvec2 A = c[a], B = c[b], d = B - A; double l = glm::length(d);
    for (size_t i = a + 1; i < b; i++) {
        double dist = l < 1e-12 ? glm::length(c[i] - A) : std::fabs(d.x * (A.y - c[i].y) - d.y * (A.x - c[i].x)) / l;
        if (dist > md) { md = dist; mi = i; }
    }
    if (md > tol) { keep[mi] = true; dp(c, a, mi, tol, keep); dp(c, mi, b, tol, keep); }
}
static Contour simplify(const Contour& c, double tol) {
    if (c.size() < 8) return c;
    // split at farthest point from c[0]
    size_t far = 0; double fd = 0;
    for (size_t i = 0; i < c.size(); i++) { double d = glm::length(c[i] - c[0]); if (d > fd) { fd = d; far = i; } }
    Contour cc = c; cc.push_back(c[0]);
    std::vector<bool> keep(cc.size(), false); keep[0] = keep[far] = keep[cc.size() - 1] = true;
    dp(cc, 0, far, tol, keep); dp(cc, far, cc.size() - 1, tol, keep);
    Contour o; for (size_t i = 0; i + 1 < cc.size(); i++) if (keep[i]) o.push_back(cc[i]);
    return o;
}

static std::vector<Contour> traceMask(const std::vector<float>& m, int W, int H, float thr) {
    // m has a zero border. corner order: 0 (x,y) 1 (x+1,y) 2 (x+1,y+1) 3 (x,y+1); edge k joins corner k and k+1
    auto val = [&](int x, int y) { return m[(size_t)y * W + x]; };
    auto edgeId = [&](int x, int y, int k) -> int64_t {
        if (k == 0) return ((int64_t)y * W + x) * 2;
        if (k == 1) return ((int64_t)y * W + x + 1) * 2 + 1;
        if (k == 2) return ((int64_t)(y + 1) * W + x) * 2;
        return ((int64_t)y * W + x) * 2 + 1;
    };
    struct Seg { dvec2 a, b; int64_t ea, eb; };
    std::unordered_map<int64_t, Seg> byStart;
    for (int y = 0; y + 1 < H; y++) for (int x = 0; x + 1 < W; x++) {
        float v[4] = {val(x, y), val(x + 1, y), val(x + 1, y + 1), val(x, y + 1)};
        dvec2 cp[4] = {{x, y}, {x + 1, y}, {x + 1, y + 1}, {x, y + 1}};
        bool in[4]; int cnt = 0;
        for (int k = 0; k < 4; k++) { in[k] = v[k] >= thr; cnt += in[k]; }
        if (cnt == 0 || cnt == 4) continue;
        dvec2 ep[4]; bool ex[4];
        for (int k = 0; k < 4; k++) {
            int k2 = (k + 1) & 3; ex[k] = in[k] != in[k2];
            if (ex[k]) { double t = (thr - v[k]) / (v[k2] - v[k]); ep[k] = cp[k] + (cp[k2] - cp[k]) * std::clamp(t, 0.0, 1.0); }
        }
        auto emit = [&](int e1, int e2, int refCorner, bool refInside) {
            dvec2 a = ep[e1], b = ep[e2];
            dvec2 c = cp[refCorner];
            double cr = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            bool left = cr > 0;
            if (left != refInside) { std::swap(a, b); std::swap(e1, e2); }
            Seg s{a, b, edgeId(x, y, e1), edgeId(x, y, e2)};
            byStart[s.ea] = s;
        };
        if (cnt == 2 && in[0] == in[2]) {  // saddle
            float centre = (v[0] + v[1] + v[2] + v[3]) * .25f;
            bool cin = centre >= thr;
            for (int k = 0; k < 4; k++) {
                if (in[k] == cin) continue;  // corners that differ from the centre get cut off
                emit((k + 3) & 3, k, k, in[k]);
            }
        } else {
            int es[2], ne = 0; for (int k = 0; k < 4; k++) if (ex[k]) es[ne++] = k;
            int rc = 0; for (int k = 0; k < 4; k++) if (in[k]) { rc = k; break; }
            emit(es[0], es[1], rc, true);
        }
    }
    std::vector<Contour> loops;
    while (!byStart.empty()) {
        auto it = byStart.begin();
        Contour c; int64_t start = it->first; Seg s = it->second; byStart.erase(it);
        c.push_back(s.a);
        int guard = 0;
        while (s.eb != start && guard++ < 10000000) {
            auto nx = byStart.find(s.eb);
            if (nx == byStart.end()) break;
            c.push_back(nx->second.a);
            s = nx->second; byStart.erase(nx);
        }
        if (c.size() >= 3) loops.push_back(c);
    }
    return loops;
}

BuildResult buildPngLogo(const Object& o) {
    BuildResult r;
    auto img = std::make_shared<ImageData>();
    if (!loadImageFile(o.path, *img) || img->rgba.empty()) { r.error = "Couldn't open that image."; return r; }
    int w = img->w, h = img->h;
    // mask: alpha, or (for opaque images) difference from the corner colour
    bool hasAlpha = false;
    for (size_t i = 3; i < img->rgba.size(); i += 4) if (img->rgba[i] < 250) { hasAlpha = true; break; }
    const uint8_t* c0 = &img->rgba[0];
    auto maskAt = [&](int x, int y) -> float {
        const uint8_t* p = &img->rgba[((size_t)y * w + x) * 4];
        if (hasAlpha) return p[3] / 255.f;
        float d = (std::abs(p[0] - c0[0]) + std::abs(p[1] - c0[1]) + std::abs(p[2] - c0[2])) / (3 * 255.f);
        return std::min(1.f, d * 3.f);
    };
    int maxS = 900;
    double ds = std::max(1.0, std::max(w, h) / (double)maxS);
    int mw = std::max(2, (int)(w / ds)), mh = std::max(2, (int)(h / ds));
    int W = mw + 2, H = mh + 2;
    std::vector<float> m((size_t)W * H, 0.f);
    for (int y = 0; y < mh; y++) for (int x = 0; x < mw; x++) {
        int sx0 = (int)(x * ds), sy0 = (int)(y * ds), sx1 = std::max(sx0 + 1, (int)((x + 1) * ds)), sy1 = std::max(sy0 + 1, (int)((y + 1) * ds));
        float acc = 0; int n = 0;
        for (int yy = sy0; yy < std::min(sy1, h); yy++) for (int xx = sx0; xx < std::min(sx1, w); xx++) { acc += maskAt(xx, yy); n++; }
        m[(size_t)(y + 1) * W + x + 1] = n ? acc / n : 0;
    }
    auto loops = traceMask(m, W, H, std::clamp(o.alphaThreshold, 0.05f, 0.95f));
    double s = 2.0 / std::max(mw, mh);
    std::vector<Contour> cs;
    for (auto& l : loops) {
        Contour c = simplify(l, 0.45);
        if (c.size() < 3) continue;
        for (auto& p : c) { p -= dvec2(1, 1); p = dvec2((p.x - mw * .5) * s, -(p.y - mh * .5) * s); }
        cs.push_back(c);
    }
    auto polys = groupContours(cs, 1e-5);
    if (polys.empty()) { r.error = "Couldn't find a shape in that image. Use a PNG with a transparent background."; return r; }
    MeshData face, side; face.slot = 0; side.slot = 1;
    face.mat.has = true; face.mat.base = glm::vec4(1); face.mat.roughness = 0.35f;
    face.mat.baseImg = img;
    double hw = mw * .5 * s, hh = mh * .5 * s;
    UVFn uvf = [&](dvec2 p) { return vec2((float)((p.x + hw) / (2 * hw)), (float)((hh - p.y) / (2 * hh))); };
    for (auto& p : polys) extrudePoly(p, o.depth, o.bevel, o.bevelSegs, o.bevelStyle, o.bevelDepth, 0, face, side, uvf);
    // side colour: average colour of the logo
    double acc[3] = {0, 0, 0}, wsum = 0;
    for (size_t i = 0; i < img->rgba.size(); i += 4) {
        double a = hasAlpha ? img->rgba[i + 3] / 255.0 : 1.0;
        for (int k = 0; k < 3; k++) acc[k] += std::pow(img->rgba[i + k] / 255.0, 2.2) * a;
        wsum += a;
    }
    side.mat.has = true; side.mat.roughness = 0.35f;
    if (wsum > 0) side.mat.base = glm::vec4((float)(acc[0] / wsum), (float)(acc[1] / wsum), (float)(acc[2] / wsum), 1);
    r.meshes.push_back(std::move(face));
    if (!side.v.empty()) r.meshes.push_back(std::move(side));
    return r;
}

// ------------------------------------------------------------------ image card
BuildResult buildImageCard(const Object& o) {
    BuildResult r;
    auto img = std::make_shared<ImageData>();
    if (!loadImageFile(o.path, *img) || img->rgba.empty()) { r.error = "Couldn't open that image."; return r; }
    float a = (float)img->w / std::max(1, img->h);
    float hw = a >= 1 ? 1.f : a, hh = a >= 1 ? 1.f / a : 1.f;
    MeshData m; m.slot = 0;
    m.mat.has = true; m.mat.base = glm::vec4(1); m.mat.roughness = 0.6f; m.mat.baseImg = img;
    m.mat.alphaMode = 2; m.mat.doubleSided = true;
    Vertex v[4] = {{{-hw, -hh, 0}, {0, 0, 1}, {0, 1}}, {{hw, -hh, 0}, {0, 0, 1}, {1, 1}}, {{hw, hh, 0}, {0, 0, 1}, {1, 0}}, {{-hw, hh, 0}, {0, 0, 1}, {0, 0}}};
    m.v.assign(v, v + 4); m.i = {0, 1, 2, 0, 2, 3};
    r.meshes.push_back(m);
    return r;
}

// ------------------------------------------------------------------ primitives
static void quad(MeshData& m, vec3 a, vec3 b, vec3 c, vec3 d, vec3 n) {
    uint32_t s = (uint32_t)m.v.size();
    m.v.push_back({a, n, {0, 1}}); m.v.push_back({b, n, {1, 1}}); m.v.push_back({c, n, {1, 0}}); m.v.push_back({d, n, {0, 0}});
    m.i.insert(m.i.end(), {s, s + 1, s + 2, s, s + 2, s + 3});
}

BuildResult buildPrimitive(const Object& o) {
    BuildResult r; MeshData m;
    const float PI = 3.14159265f;
    switch ((Prim)o.prim) {
    case Prim::Cube: {
        float h = 0.8f;
        vec3 p[8] = {{-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h}, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}};
        quad(m, p[4], p[5], p[6], p[7], {0, 0, 1}); quad(m, p[1], p[0], p[3], p[2], {0, 0, -1});
        quad(m, p[5], p[1], p[2], p[6], {1, 0, 0}); quad(m, p[0], p[4], p[7], p[3], {-1, 0, 0});
        quad(m, p[7], p[6], p[2], p[3], {0, 1, 0}); quad(m, p[0], p[1], p[5], p[4], {0, -1, 0});
        break;
    }
    case Prim::RoundCube: case Prim::Sphere: {
        // cube-sphere; RoundCube pushes the faces out to a rounded box
        int N = 32; float h = 0.8f, rad = (Prim)o.prim == Prim::Sphere ? 1.f : 0.18f;
        vec3 axes[6][3] = {{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}, {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}, {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
                           {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}};
        for (auto& ax : axes) {
            uint32_t s = (uint32_t)m.v.size();
            for (int y = 0; y <= N; y++) for (int x = 0; x <= N; x++) {
                float u = x / (float)N * 2 - 1, v = y / (float)N * 2 - 1;
                vec3 c = ax[0] + ax[1] * u + ax[2] * v;
                vec3 P, Nn;
                if ((Prim)o.prim == Prim::Sphere) { Nn = glm::normalize(c); P = Nn; }
                else {
                    vec3 inner = glm::clamp(c * h, vec3(-(h - rad)), vec3(h - rad));
                    vec3 d = c * h - inner;
                    Nn = glm::length(d) > 1e-6f ? glm::normalize(d) : ax[0];
                    P = inner + Nn * rad;
                }
                m.v.push_back({P, Nn, {x / (float)N, 1 - y / (float)N}});
            }
            for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
                uint32_t a = s + y * (N + 1) + x;
                m.i.insert(m.i.end(), {a, a + 1, a + N + 2, a, a + N + 2, a + N + 1});
            }
        }
        break;
    }
    case Prim::Cylinder: {
        int N = 64; float rr = 0.8f, h = 0.8f;
        uint32_t s = 0;
        for (int i = 0; i <= N; i++) {
            float a = i / (float)N * 2 * PI; vec3 n(cos(a), 0, sin(a));
            m.v.push_back({n * rr + vec3(0, -h, 0), n, {i / (float)N, 1}});
            m.v.push_back({n * rr + vec3(0, h, 0), n, {i / (float)N, 0}});
        }
        for (int i = 0; i < N; i++) { uint32_t a = i * 2; m.i.insert(m.i.end(), {a, a + 1, a + 3, a, a + 3, a + 2}); }
        for (int side = 0; side < 2; side++) {
            float y = side ? h : -h; vec3 n(0, side ? 1.f : -1.f, 0);
            s = (uint32_t)m.v.size(); m.v.push_back({{0, y, 0}, n, {.5f, .5f}});
            for (int i = 0; i <= N; i++) { float a = i / (float)N * 2 * PI; m.v.push_back({{cos(a) * rr, y, sin(a) * rr}, n, {cos(a) * .5f + .5f, sin(a) * .5f + .5f}}); }
            for (int i = 0; i < N; i++) m.i.insert(m.i.end(), {s, s + 1 + i, s + 2 + i});
        }
        break;
    }
    case Prim::Torus: {
        int N = 96, M = 40; float R = 0.75f, rr = 0.28f;
        for (int i = 0; i <= N; i++) for (int j = 0; j <= M; j++) {
            float a = i / (float)N * 2 * PI, b = j / (float)M * 2 * PI;
            vec3 c(cos(a) * R, 0, sin(a) * R);
            vec3 n(cos(a) * cos(b), sin(b), sin(a) * cos(b));
            m.v.push_back({c + n * rr, n, {i / (float)N, j / (float)M}});
        }
        for (int i = 0; i < N; i++) for (int j = 0; j < M; j++) {
            uint32_t a = i * (M + 1) + j, b = a + M + 1;
            m.i.insert(m.i.end(), {a, b, b + 1, a, b + 1, a + 1});
        }
        break;
    }
    case Prim::Gem: {  // brilliant-style cut, flat facets
        const int N = 16; float table = 0.55f, crownR = 1.f, yT = 0.38f, yG = 0.f, yG2 = -0.06f, yA = -0.95f;
        auto tri = [&](vec3 a, vec3 b, vec3 c) {
            vec3 n = glm::normalize(glm::cross(b - a, c - a));
            uint32_t s0 = (uint32_t)m.v.size();
            m.v.push_back({a, n, {0, 0}}); m.v.push_back({b, n, {1, 0}}); m.v.push_back({c, n, {0, 1}});
            m.i.insert(m.i.end(), {s0, s0 + 1, s0 + 2});
        };
        auto ringP = [&](float r, float y, float k) { float a = k / N * 2 * PI; return vec3(cos(a) * r, y, sin(a) * r); };
        for (int k = 0; k < N; k++) {
            vec3 t0 = ringP(table, yT, k), t1 = ringP(table, yT, k + 1), c0 = ringP(crownR, yG, k + .5f - .5f), c1 = ringP(crownR, yG, k + 1);
            vec3 cm = ringP(crownR, yG, k + .5f);
            tri(vec3(0, yT, 0), t1, t0);                 // table
            tri(t0, t1, cm); tri(t0, cm, c0); tri(t1, c1, cm);  // crown facets
            vec3 g0 = ringP(crownR, yG2, k), g1 = ringP(crownR, yG2, k + 1);
            tri(c0, cm, g0); tri(cm, c1, g1); tri(cm, g1, g0);   // girdle
            tri(g0, g1, vec3(0, yA, 0));                 // pavilion
        }
        break;
    }
    case Prim::Cone: {
        int N = 64; float rr = 0.8f, h = 0.8f;
        for (int i = 0; i < N; i++) {
            float a0 = i / (float)N * 2 * PI, a1 = (i + 1) / (float)N * 2 * PI;
            vec3 p0(cos(a0) * rr, -h, sin(a0) * rr), p1(cos(a1) * rr, -h, sin(a1) * rr), tip(0, h, 0);
            auto nrm = [&](float a) { return glm::normalize(vec3(cos(a) * 2 * h, rr, sin(a) * 2 * h)); };
            uint32_t s0 = (uint32_t)m.v.size();
            m.v.push_back({p0, nrm(a0), {0, 1}}); m.v.push_back({p1, nrm(a1), {1, 1}}); m.v.push_back({tip, nrm((a0 + a1) * .5f), {.5f, 0}});
            m.v.push_back({p0, {0, -1, 0}, {0, 0}}); m.v.push_back({p1, {0, -1, 0}, {0, 0}}); m.v.push_back({{0, -h, 0}, {0, -1, 0}, {0, 0}});
            m.i.insert(m.i.end(), {s0, s0 + 2, s0 + 1, s0 + 3, s0 + 4, s0 + 5});
        }
        break;
    }
    case Prim::Capsule: {
        int N = 48, M = 24; float rr = 0.45f, h = 0.45f;
        for (int j = 0; j <= M; j++) for (int i = 0; i <= N; i++) {
            float v = j / (float)M, a = i / (float)N * 2 * PI;
            float th = v * PI;  // 0 top .. PI bottom
            vec3 n(sin(th) * cos(a), cos(th), sin(th) * sin(a));
            float yoff = v < .5f ? h : -h;
            m.v.push_back({n * rr + vec3(0, yoff, 0), n, {i / (float)N, v}});
        }
        for (int j = 0; j < M; j++) for (int i = 0; i < N; i++) {
            uint32_t a = j * (N + 1) + i, b = a + N + 1;
            m.i.insert(m.i.end(), {a, b, b + 1, a, b + 1, a + 1});
        }
        break;
    }
    case Prim::Plane: {
        quad(m, {-1.5f, 0, 1.5f}, {1.5f, 0, 1.5f}, {1.5f, 0, -1.5f}, {-1.5f, 0, -1.5f}, {0, 1, 0});
        m.mat.doubleSided = true;
        break;
    }
    }
    r.meshes.push_back(m);
    return r;
}
