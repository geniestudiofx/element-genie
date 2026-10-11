// Element Genie - project save / load (JSON) and scene defaults
#include "project.h"
#include <json.hpp>
#include <cstdio>

using nlohmann::json;
using glm::vec3;

static json jv(const vec3& v) { return json::array({v.x, v.y, v.z}); }
static vec3 vj(const json& j, vec3 d = vec3(0)) { if (!j.is_array() || j.size() < 3) return d; return vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>()); }

template <class T> static json jt(const Track<T>& t);
template <> json jt(const Track<float>& t) {
    json j; j["v"] = t.value; j["a"] = t.animated;
    json k = json::array(); for (auto& key : t.keys) k.push_back({key.frame, key.v, key.ease.in, key.ease.out, key.ease.hold});
    j["k"] = k; return j;
}
template <> json jt(const Track<vec3>& t) {
    json j; j["v"] = jv(t.value); j["a"] = t.animated;
    json k = json::array(); for (auto& key : t.keys) k.push_back({key.frame, jv(key.v), key.ease.in, key.ease.out, key.ease.hold});
    j["k"] = k; return j;
}
static void tj(const json& j, Track<float>& t) {
    if (!j.is_object()) return;
    t.value = j.value("v", t.value); t.animated = j.value("a", false); t.keys.clear();
    if (j.contains("k")) for (auto& k : j["k"]) { Key<float> key; key.frame = k[0]; key.v = k[1]; key.ease.in = k[2]; key.ease.out = k[3]; key.ease.hold = k[4]; t.keys.push_back(key); }
    t.sortKeys();
}
static void tj(const json& j, Track<vec3>& t) {
    if (!j.is_object()) return;
    t.value = vj(j["v"], t.value); t.animated = j.value("a", false); t.keys.clear();
    if (j.contains("k")) for (auto& k : j["k"]) { Key<vec3> key; key.frame = k[0]; key.v = vj(k[1]); key.ease.in = k[2]; key.ease.out = k[3]; key.ease.hold = k[4]; t.keys.push_back(key); }
    t.sortKeys();
}
static json jm(const Material& m) {
    return json{{"preset", m.preset}, {"color", jv(m.color)}, {"metallic", m.metallic}, {"roughness", m.roughness},
                {"emissive", m.emissiveStrength}, {"clearcoat", m.clearcoat},
                {"baseMap", m.baseMap}, {"roughMap", m.roughMap}, {"normalMap", m.normalMap}, {"mapping", m.mapping}, {"mapScale", m.mapScale}, {"normalStrength", m.normalStrength}};
}
static void mj(const json& j, Material& m) {
    if (!j.is_object()) return;
    m.preset = j.value("preset", m.preset); m.color = vj(j["color"], m.color); m.metallic = j.value("metallic", m.metallic);
    m.roughness = j.value("roughness", m.roughness); m.emissiveStrength = j.value("emissive", 0.f); m.clearcoat = j.value("clearcoat", 0.f);
    m.baseMap = j.value("baseMap", ""); m.roughMap = j.value("roughMap", ""); m.normalMap = j.value("normalMap", "");
    m.mapping = j.value("mapping", 1); m.mapScale = j.value("mapScale", 1.f); m.normalStrength = j.value("normalStrength", 1.f);
}

static json jrep(const Replicator& r) {
    return json{{"on", r.enabled}, {"shape", r.shape}, {"cx", r.cx}, {"cy", r.cy}, {"cz", r.cz}, {"count", r.count}, {"spacing", jv(r.spacing)},
                {"radius", r.radius}, {"height", r.height}, {"turns", r.turns}, {"face", r.faceCentre}, {"wave", r.waveHeight},
                {"randPos", jv(r.randPos)}, {"randRot", jv(r.randRot)}, {"randScale", r.randScale}, {"scaleStep", r.scaleStep}, {"stagger", r.stagger}, {"seed", r.seed}};
}
static void repj(const json& j, Replicator& r) {
    if (!j.is_object()) return;
    r.enabled = j.value("on", false); r.shape = j.value("shape", 0); r.cx = j.value("cx", 3); r.cy = j.value("cy", 1); r.cz = j.value("cz", 1);
    r.count = j.value("count", 12); r.spacing = vj(j.value("spacing", json()), r.spacing); r.radius = j.value("radius", r.radius); r.height = j.value("height", r.height);
    r.turns = j.value("turns", r.turns); r.faceCentre = j.value("face", true); r.waveHeight = j.value("wave", r.waveHeight);
    r.randPos = vj(j.value("randPos", json())); r.randRot = vj(j.value("randRot", json())); r.randScale = j.value("randScale", 0.f); r.scaleStep = j.value("scaleStep", 0.f);
    r.stagger = j.value("stagger", 0.f); r.seed = j.value("seed", 1);
}
static json jdef(const Deform& d) {
    return json{{"axis", d.axis}, {"twist", d.twist}, {"bend", d.bend}, {"taper", d.taper}, {"noise", d.noise}, {"noiseScale", d.noiseScale}, {"noiseSpeed", d.noiseSpeed}};
}
static void defj(const json& j, Deform& d) {
    if (!j.is_object()) return;
    d.axis = j.value("axis", 1); d.twist = j.value("twist", 0.f); d.bend = j.value("bend", 0.f); d.taper = j.value("taper", 0.f);
    d.noise = j.value("noise", 0.f); d.noiseScale = j.value("noiseScale", 1.5f); d.noiseSpeed = j.value("noiseSpeed", 0.f);
}
static json jsh(const Shatter& h) {
    return json{{"on", h.enabled}, {"pieces", h.pieces}, {"force", h.force}, {"spin", h.spin}, {"gravity", h.gravity}, {"dir", h.direction}, {"random", h.randomness}, {"seed", h.seed}};
}
static void shj(const json& j, Shatter& h) {
    if (!j.is_object()) return;
    h.enabled = j.value("on", false); h.pieces = j.value("pieces", 40); h.force = j.value("force", 2.f); h.spin = j.value("spin", 360.f);
    h.gravity = j.value("gravity", 1.f); h.direction = j.value("dir", 0); h.randomness = j.value("random", .5f); h.seed = j.value("seed", 3);
}

std::string sceneToJson(const Scene& s) {
    json j;
    j["app"] = "Element Genie"; j["version"] = 2;
    j["comp"] = {{"w", s.comp.width}, {"h", s.comp.height}, {"fps", s.comp.fps}, {"frames", s.comp.frames}};
    json objs = json::array();
    for (auto& o : s.objects) {
        json a;
        a["id"] = o.id; a["name"] = o.name; a["type"] = (int)o.type; a["visible"] = o.visible; a["castShadow"] = o.castShadow;
        a["path"] = o.path; a["text"] = o.text; a["font"] = o.font; a["depth"] = o.depth; a["bevel"] = o.bevel;
        a["bevelSegs"] = o.bevelSegs; a["bevelStyle"] = o.bevelStyle; a["bevelDepth"] = o.bevelDepth; a["letterSpacing"] = o.letterSpacing; a["lineSpacing"] = o.lineSpacing;
        a["align"] = o.align; a["prim"] = o.prim; a["keepModelMaterials"] = o.keepModelMaterials; a["useImageColours"] = o.useImageColours;
        a["alphaThreshold"] = o.alphaThreshold; a["group"] = o.group;
        a["mat"] = jm(o.mat); a["edgeMat"] = o.edgeMat; a["mat2"] = jm(o.mat2);
        a["pos"] = jt(o.pos); a["rot"] = jt(o.rot); a["scl"] = jt(o.scl); a["opacity"] = jt(o.opacity);
        auto& l = o.letters;
        a["letters"] = {{"on", l.enabled}, {"mode", l.mode}, {"start", l.start}, {"duration", l.duration}, {"stagger", l.stagger},
                        {"order", l.order}, {"offPos", jv(l.offPos)}, {"offRot", jv(l.offRot)}, {"offScale", l.offScale}, {"fade", l.fade}};
        a["rep"] = jrep(o.rep); a["deform"] = jdef(o.deform); a["shatter"] = jsh(o.shatter);
        objs.push_back(a);
    }
    j["objects"] = objs;
    json ls = json::array();
    for (auto& l : s.lights)
        ls.push_back({{"name", l.name}, {"type", l.type}, {"pos", jt(l.pos)}, {"color", jv(l.color)}, {"intensity", jt(l.intensity)},
                      {"shadow", l.shadow}, {"on", l.enabled}});
    j["lights"] = ls;
    j["camera"] = {{"pos", jt(s.cam.pos)}, {"target", jt(s.cam.target)}, {"fov", jt(s.cam.fov)}, {"roll", jt(s.cam.roll)}};
    j["env"] = {{"preset", s.env.preset}, {"path", s.env.path}, {"intensity", s.env.intensity}, {"rotation", jt(s.env.rotation)}, {"exposure", s.env.exposure}, {"followLight", s.env.followLight}};
    j["catcher"] = {{"on", s.catcher.enabled}, {"opacity", s.catcher.opacity}, {"softness", s.catcher.softness}, {"height", s.catcher.height}, {"auto", s.catcher.autoHeight}};
    j["reflection"] = {{"on", s.reflection.enabled}, {"strength", s.reflection.strength}, {"fade", s.reflection.fade}};
    j["render"] = {{"motionBlur", s.motionBlur}, {"shutter", s.shutter}, {"mbSamples", s.mbSamples}, {"glow", s.glow}, {"glowThreshold", s.glowThreshold}};
    j["lens"] = {{"dof", s.lens.dof}, {"auto", s.lens.autoFocus}, {"focus", s.lens.focus}, {"aperture", s.lens.aperture}};
    j["fog"] = {{"on", s.fog.enabled}, {"color", jv(s.fog.color)}, {"start", s.fog.start}, {"end", s.fog.end}, {"amount", s.fog.amount}, {"fade", s.fog.fadeOut}};
    j["ref"] = {{"path", s.refPath}, {"opacity", s.refOpacity}};
    j["nextId"] = s.nextId;
    return j.dump(1);
}

bool sceneFromJson(const std::string& text, Scene& s, std::string& err) {
    json j;
    try { j = json::parse(text); } catch (std::exception& e) { err = std::string("Not a valid project file: ") + e.what(); return false; }
    try {
        Scene n;
        if (j.contains("comp")) { auto& c = j["comp"]; n.comp.width = c.value("w", 1920); n.comp.height = c.value("h", 1080); n.comp.fps = c.value("fps", 25.f); n.comp.frames = c.value("frames", 125); }
        for (auto& a : j.value("objects", json::array())) {
            Object o;
            o.id = a.value("id", 0); o.name = a.value("name", ""); o.type = (ObjType)a.value("type", 3); o.visible = a.value("visible", true);
            o.castShadow = a.value("castShadow", true); o.path = a.value("path", ""); o.text = a.value("text", ""); o.font = a.value("font", "");
            o.depth = a.value("depth", o.depth); o.bevel = a.value("bevel", o.bevel); o.bevelSegs = a.value("bevelSegs", o.bevelSegs);
            o.bevelStyle = a.value("bevelStyle", o.bevelStyle); o.bevelDepth = a.value("bevelDepth", 1.f); o.letterSpacing = a.value("letterSpacing", 0.f); o.lineSpacing = a.value("lineSpacing", 1.15f);
            o.align = a.value("align", 1); o.prim = a.value("prim", 0); o.keepModelMaterials = a.value("keepModelMaterials", true);
            o.useImageColours = a.value("useImageColours", true); o.alphaThreshold = a.value("alphaThreshold", .5f); o.group = a.value("group", 0);
            mj(a["mat"], o.mat); o.edgeMat = a.value("edgeMat", false); mj(a["mat2"], o.mat2);
            tj(a["pos"], o.pos); tj(a["rot"], o.rot); tj(a["scl"], o.scl); tj(a["opacity"], o.opacity);
            if (a.contains("letters")) {
                auto& l = a["letters"]; auto& d = o.letters;
                d.enabled = l.value("on", false); d.mode = l.value("mode", 0); d.start = l.value("start", 0); d.duration = l.value("duration", 20);
                d.stagger = l.value("stagger", 3.f); d.order = l.value("order", 0); d.offPos = vj(l["offPos"], d.offPos); d.offRot = vj(l["offRot"], d.offRot);
                d.offScale = l.value("offScale", 1.f); d.fade = l.value("fade", true);
            }
            repj(a.value("rep", json()), o.rep); defj(a.value("deform", json()), o.deform); shj(a.value("shatter", json()), o.shatter);
            n.objects.push_back(o);
        }
        for (auto& a : j.value("lights", json::array())) {
            Light l; l.name = a.value("name", "Light"); l.type = a.value("type", 0); tj(a["pos"], l.pos); l.color = vj(a["color"], vec3(1));
            tj(a["intensity"], l.intensity); l.shadow = a.value("shadow", true); l.enabled = a.value("on", true);
            if (a.contains("spotCos")) { l.spotDir = vj(a["spotDir"], vec3(0, 0, -1)); l.spotCos = a.value("spotCos", -2.f); l.spotInner = a.value("spotInner", -2.f); }
            n.lights.push_back(l);
        }
        if (j.contains("camera")) { auto& c = j["camera"]; tj(c["pos"], n.cam.pos); tj(c["target"], n.cam.target); tj(c["fov"], n.cam.fov); tj(c["roll"], n.cam.roll); }
        if (j.contains("env")) { auto& e = j["env"]; n.env.preset = e.value("preset", 0); n.env.path = e.value("path", ""); n.env.intensity = e.value("intensity", 1.f); tj(e["rotation"], n.env.rotation); n.env.exposure = e.value("exposure", 0.f); n.env.followLight = e.value("followLight", true); }
        if (j.contains("catcher")) { auto& c = j["catcher"]; n.catcher.enabled = c.value("on", true); n.catcher.opacity = c.value("opacity", .55f); n.catcher.softness = c.value("softness", .5f); n.catcher.height = c.value("height", 0.f); n.catcher.autoHeight = c.value("auto", true); }
        if (j.contains("reflection")) { auto& r = j["reflection"]; n.reflection.enabled = r.value("on", true); n.reflection.strength = r.value("strength", .35f); n.reflection.fade = r.value("fade", .8f); }
        if (j.contains("render")) { auto& r = j["render"]; n.motionBlur = r.value("motionBlur", false); n.shutter = r.value("shutter", 180.f); n.mbSamples = r.value("mbSamples", 8); n.glow = r.value("glow", 0.f); n.glowThreshold = r.value("glowThreshold", 1.f); }
        if (j.contains("lens")) { auto& l = j["lens"]; n.lens.dof = l.value("dof", false); n.lens.autoFocus = l.value("auto", true); n.lens.focus = l.value("focus", 6.f); n.lens.aperture = l.value("aperture", .3f); }
        if (j.contains("fog")) { auto& f = j["fog"]; n.fog.enabled = f.value("on", false); n.fog.color = vj(f["color"], n.fog.color); n.fog.start = f.value("start", 5.f); n.fog.end = f.value("end", 25.f); n.fog.amount = f.value("amount", 1.f); n.fog.fadeOut = f.value("fade", false); }
        if (j.contains("ref")) { n.refPath = j["ref"].value("path", ""); n.refOpacity = j["ref"].value("opacity", 1.f); }
        n.nextId = j.value("nextId", 1);
        for (auto& o : n.objects) n.nextId = std::max(n.nextId, o.id + 1);
        s = std::move(n);
    } catch (std::exception& e) { err = std::string("Couldn't read the project: ") + e.what(); return false; }
    return true;
}

void sceneDefaults(Scene& s) {
    s = Scene();
    Light key; key.name = "Key light"; key.pos.value = vec3(3, 6, 4); key.intensity.value = 2.5f; key.shadow = true;
    Light rim; rim.name = "Rim light"; rim.pos.value = vec3(-4, 3, -5); rim.intensity.value = 1.5f; rim.shadow = false; rim.color = vec3(0.85f, 0.9f, 1.f);
    s.lights = {key, rim};
}
