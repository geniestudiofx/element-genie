// Element Genie - bevel presets (edge profiles)
// A profile runs from the front face edge (inset 1, depth 0) to the side wall (inset 0, depth 1),
// in units of the bevel size. Depth < 0 sticks out in front of the face (raised rims).
#pragma once
#include <vector>
#include <cmath>
#include <glm/glm.hpp>

struct BevelProfile {
    std::vector<glm::dvec2> pts;  // (inset, depth)
    bool smooth;                  // smooth shading across the curve
};

enum BevelStyle {
    BEVEL_CHAMFER = 0, BEVEL_ROUND = 1, BEVEL_NONE = 2, BEVEL_CONCAVE = 3, BEVEL_STEP = 4,
    BEVEL_RIM = 5, BEVEL_GROOVE = 6, BEVEL_OGEE = 7, BEVEL_PILLOW = 8, BEVEL_DOUBLE = 9, BEVEL_COUNT = 10
};

static const char* BEVEL_NAMES[BEVEL_COUNT] = {"Chamfer", "Round", "None", "Concave", "Step", "Raised rim", "Groove", "S-curve", "Pillow", "Double chamfer"};
static const int BEVEL_ORDER[BEVEL_COUNT] = {BEVEL_NONE, BEVEL_CHAMFER, BEVEL_ROUND, BEVEL_PILLOW, BEVEL_CONCAVE, BEVEL_OGEE, BEVEL_DOUBLE, BEVEL_STEP, BEVEL_RIM, BEVEL_GROOVE};

inline BevelProfile bevelProfile(int style, int segs) {
    BevelProfile p; p.smooth = false;
    int s = segs < 1 ? 1 : segs;
    const double PI = 3.14159265358979;
    switch (style) {
    case BEVEL_CHAMFER: p.pts = {{1, 0}, {0, 1}}; break;
    case BEVEL_ROUND:
        for (int k = 0; k <= s; k++) { double t = PI / 2 * k / s; p.pts.push_back({1 - std::sin(t), 1 - std::cos(t)}); }
        p.smooth = true; break;
    case BEVEL_PILLOW:  // soft, deep round edge
        for (int k = 0; k <= s + 2; k++) { double t = PI / 2 * k / (s + 2); p.pts.push_back({1 - std::sin(t), (1 - std::cos(t)) * 2.2}); }
        p.smooth = true; break;
    case BEVEL_CONCAVE:
        for (int k = 0; k <= s; k++) { double t = PI / 2 * k / s; p.pts.push_back({std::cos(t), std::sin(t)}); }
        p.smooth = true; break;
    case BEVEL_OGEE:
        for (int k = 0; k <= s * 2; k++) { double t = (double)k / (s * 2); p.pts.push_back({1 - t, 0.5 - 0.5 * std::cos(PI * t)}); }
        p.smooth = true; break;
    case BEVEL_DOUBLE: p.pts = {{1, 0}, {0.55, 0.15}, {0.15, 0.6}, {0, 1}}; break;
    case BEVEL_STEP: p.pts = {{1, 0}, {0.55, 0}, {0.55, 0.45}, {0, 0.45}, {0, 1}}; break;
    case BEVEL_RIM: p.pts = {{1, 0}, {0.75, 0}, {0.6, -0.45}, {0.15, -0.45}, {0, 0}, {0, 1}}; break;
    case BEVEL_GROOVE: p.pts = {{1, 0}, {0.72, 0.4}, {0.45, 0}, {0.2, 0}, {0, 0.35}, {0, 1}}; break;
    default: p.pts.clear(); break;  // none
    }
    return p;
}
