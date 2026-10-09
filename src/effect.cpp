#include "effect.h"
#include "mesh.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <cmath>

using glm::vec3; using glm::mat4;

void setLetterFrames(Scene& s, float pct) {
    for (auto& o : s.objects) {
        if (o.letters.enabled) {
            int n = o.geo ? (int)o.geo->glyphCenters.size() : (int)o.text.size();
            float span = (std::max(1, n) - 1) * o.letters.stagger + o.letters.duration;
            float built = std::clamp(pct / 100.f, 0.f, 1.f);
            o.letterFrame = (o.letters.mode == 0 ? built : 1.f - built) * span + o.letters.start;
        } else o.letterFrame = -1;
    }
}

void applyFx(Scene& s, const FxParams& p) {
    for (auto& o : s.objects) {
        const FxGroup& g = p.g[std::clamp(o.group, 0, 4)];
        vec3 r = glm::radians(g.rot);
        o.parentM = glm::translate(mat4(1), g.pos) * glm::eulerAngleYXZ(r.y, r.x, r.z) * glm::scale(mat4(1), vec3(std::max(0.f, g.scale) / 100.f));
        o.opacityMul = std::clamp(g.opacity / 100.f, 0.f, 1.f);
        bool lt = p.lTarget == 0 || p.lTarget - 1 == o.group;
        o.lfOn = lt && (glm::length(p.lRot) > 1e-4f || glm::length(p.lPos) > 1e-6f || std::fabs(p.lScale - 100.f) > 1e-3f);
        o.lfRot = p.lRot; o.lfPos = p.lPos; o.lfScale = std::max(0.f, p.lScale) / 100.f;
        o.lfSpread = p.lSpread; o.lfPhase = p.lPhase; o.lfRandom = std::clamp(p.lRandom / 100.f, 0.f, 1.f);
        bool tt = p.twistTarget == 0 || p.twistTarget - 1 == o.group;
        o.twist = tt ? p.twist : 0.f; o.twistAxis = p.twistAxis; o.twistOffset = p.twistOffset / 100.f;
    }
    setLetterFrames(s, p.letters);
    // camera: orbit / tilt / zoom / pan on top of the Scene Setup camera
    auto& c = s.cam;
    vec3 pos = c.pos.eval(0), tgt = c.target.eval(0);
    vec3 off = pos - tgt; float r = std::max(1e-4f, glm::length(off));
    float yaw = std::atan2(off.x, off.z) + glm::radians(p.orbit);
    float pitch = std::asin(std::clamp(off.y / r, -1.f, 1.f)) + glm::radians(p.tilt);
    pitch = std::clamp(pitch, -1.55f, 1.55f);
    r *= 100.f / std::max(1.f, p.zoom);
    vec3 no(std::sin(yaw) * std::cos(pitch) * r, std::sin(pitch) * r, std::cos(yaw) * std::cos(pitch) * r);
    vec3 fwd = glm::normalize(-no);
    vec3 right = glm::normalize(glm::cross(fwd, vec3(0, 1, 0)) + vec3(1e-6f, 0, 0));
    vec3 up = glm::cross(right, fwd);
    vec3 pan = right * p.panX + up * p.panY;
    c.pos = Track<vec3>(tgt + no + pan);
    c.target = Track<vec3>(tgt + pan);
    c.roll = Track<float>(c.roll.eval(0) + p.roll);
    c.fov = Track<float>(std::clamp(c.fov.eval(0) + p.fovOff, 2.f, 150.f));
    s.env.rotation = Track<float>(s.env.rotation.eval(0) + p.envRot);
    for (auto& l : s.lights) l.intensity = Track<float>(l.intensity.eval(0) * std::max(0.f, p.lightPct) / 100.f);
    s.glow *= std::max(0.f, p.glowPct) / 100.f;
    s.catcher.enabled = p.floorShadow;
    s.catcher.softness = std::clamp(p.shadowSoft / 100.f, 0.f, 1.f);
    s.catcher.opacity = std::clamp(p.shadowDark / 100.f, 0.f, 1.f);
    s.quality.shadowRes = p.shadowRes;
    s.quality.ao = p.ao;
    s.quality.aoStrength = std::max(0.f, p.aoStrength / 100.f);
    s.quality.aoRadius = std::max(0.02f, p.aoRadius / 100.f);
    s.reflection.enabled = p.reflection;
    s.reflection.strength = std::clamp(p.reflStrength / 100.f, 0.f, 1.f);
    s.reflection.fade = std::max(0.02f, p.reflFade / 100.f);
    s.camOv = p.aeCam;
    if (p.aeCam) { s.camOvPos = p.aePos; s.camOvFwd = p.aeFwd; s.camOvUp = p.aeUp; s.camOvFov = p.aeFov; }
}
