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
    // Effect Controls should do something even if the effect wasn't set up in Scene Setup:
    // 'Letters Built' below 100% gives text a default letter build, 'Shatter' above 0 shatters everything.
    if (p.letters < 99.99f)
        for (auto& o : s.objects) if (o.type == ObjType::Text && !o.letters.enabled) o.letters.enabled = true;
    if (p.shatter > 0.f) {
        bool any = false;
        for (auto& o : s.objects) any |= o.shatter.enabled;
        if (!any) for (auto& o : s.objects) if (o.type != ObjType::ImageCard) o.shatter.enabled = true;
    }
    // Twist & Bend from Effect Controls (adds to any Deform designed in Scene Setup)
    if (std::fabs(p.twist) > 1e-3f || std::fabs(p.bend) > 1e-3f || std::fabs(p.taper) > 1e-3f || p.wobble > 1e-3f)
        for (auto& o : s.objects) {
            if (p.defTarget > 0 && p.defTarget - 1 != o.group) continue;
            if (!o.deform.active()) o.deform.axis = std::clamp(p.defAxis, 0, 2);
            o.deform.twist += p.twist; o.deform.bend += p.bend; o.deform.taper = std::clamp(o.deform.taper + p.taper / 100.f, -1.f, 1.f);
            o.deform.noise += p.wobble / 100.f * 0.2f;
        }
    // Look: self glow (material emission) and extrusion depth
    {
        float sg = std::max(0.f, p.selfGlow) / 100.f;
        float dz = std::max(0.01f, p.depthPct / 100.f);
        for (auto& o : s.objects) {
            if (std::fabs(sg - 1.f) > 1e-4f) {
                auto glowUp = [&](Material& m) { m.emissiveStrength = m.emissiveStrength * std::min(sg, 1.f) + std::max(0.f, sg - 1.f) * (m.emissiveStrength > 0 ? m.emissiveStrength : 1.f); };
                glowUp(o.mat); glowUp(o.mat2);
            }
            o.depthMul = (o.type == ObjType::Text || o.type == ObjType::Logo) ? dz : 1.f;
        }
    }
    for (auto& o : s.objects) {
        const FxGroup& g = p.g[std::clamp(o.group, 0, 4)];
        vec3 r = glm::radians(g.rot);
        o.parentM = glm::translate(mat4(1), g.pos) * glm::eulerAngleYXZ(r.y, r.x, r.z) * glm::scale(mat4(1), vec3(std::max(0.f, g.scale) / 100.f));
        o.opacityMul = std::clamp(g.opacity / 100.f, 0.f, 1.f);
        {
            bool lt = p.lTarget == 0 || p.lTarget - 1 == o.group;
            o.lfOn = lt && (glm::length(p.lRot) > 1e-4f || glm::length(p.lPos) > 1e-6f || std::fabs(p.lScale - 100.f) > 1e-3f);
            o.lfRot = p.lRot; o.lfPos = p.lPos; o.lfScale = std::max(0.f, p.lScale) / 100.f;
            o.lfSpread = p.lSpread; o.lfPhase = p.lPhase; o.lfRandom = std::clamp(p.lRandom / 100.f, 0.f, 1.f);
        }
    }
    setLetterFrames(s, std::min(p.letters, p.autoLetters));
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
    // Glow: scales the Scene Setup glow; above 100% it also adds glow when the scene had none
    s.glow = s.glow > 0.f ? s.glow * std::max(0.f, p.glowPct) / 100.f : std::max(0.f, p.glowPct - 100.f) / 200.f;
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
    auto& lv = s.live;
    lv.shatter = std::clamp(p.shatter / 100.f, 0.f, 1.f);
    lv.deform = std::max(0.f, p.deform / 100.f);
    lv.noiseEvo = p.noiseEvo / 360.f;
    lv.repSpread = std::max(0.f, p.repSpread / 100.f);
    lv.repRotate = p.repRotate;
    lv.repBuild = std::clamp(p.repBuild / 100.f, 0.f, 1.f);
    if (p.dofMode == 1) s.lens.dof = true; else if (p.dofMode == 2) s.lens.dof = false;
    if (p.fogMode == 1) s.fog.enabled = true; else if (p.fogMode == 2) s.fog.enabled = false;
    lv.focusOffset = p.focusOffset;
    lv.blurMul = std::max(0.f, p.blur / 100.f);
    lv.fogMul = std::max(0.f, p.fog / 100.f);
    lv.pass = std::clamp(p.pass, 0, 4);
    s.camOv = p.aeCam;
    if (p.aeCam) { s.camOvPos = p.aePos; s.camOvFwd = p.aeFwd; s.camOvUp = p.aeUp; s.camOvFov = p.aeFov; }
    if (p.aeCam && p.aeDof) { s.lens.dof = true; s.lens.autoFocus = false; s.lens.focus = p.aeFocus; s.lens.aperture = p.aeAperture; }
    // After Effects light layers: replace (or join) the Scene Setup lights
    if (p.aeLightN > 0) {
        std::vector<Light> ae; float ambient = 0;
        for (int i = 0; i < p.aeLightN; i++) {
            const auto& a = p.aeLights[i];
            if (a.type == 2) { ambient += (a.color.r + a.color.g + a.color.b) / 3.f; continue; }
            Light l; l.name = "AE light"; l.type = a.type == 0 ? 0 : 2;  // AE point/spot lights have no falloff by default
            l.pos = Track<glm::vec3>(a.pos); l.color = a.color; l.intensity = Track<float>(2.5f);
            l.shadow = a.shadow; l.enabled = true;
            l.spotDir = a.dir; l.spotCos = a.cosOuter; l.spotInner = a.cosInner;
            ae.push_back(l);
        }
        if (p.aeKeepLights) { ae.insert(ae.end(), s.lights.begin(), s.lights.end()); }
        // shader takes 4 lights: AE lights first
        if (ae.size() > 4) ae.resize(4);
        if (!ae.empty() || ambient > 0) s.lights = ae;
        if (ambient > 0) s.env.intensity *= 1.f + ambient;
    }
    for (auto& l : s.lights) l.intensity = Track<float>(l.intensity.eval(0) * std::max(0.f, p.lightPct) / 100.f);
    // Light Rotate: swing every light round the scene (reflections follow when 'follow key light' is on)
    if (std::fabs(p.lightRot) > 1e-3f) {
        mat4 R = glm::rotate(mat4(1), glm::radians(-p.lightRot), vec3(0, 1, 0));
        for (auto& l : s.lights) { l.pos = Track<vec3>(vec3(R * glm::vec4(l.pos.eval(0), 1))); l.spotDir = vec3(R * glm::vec4(l.spotDir, 0)); }
    }
}
