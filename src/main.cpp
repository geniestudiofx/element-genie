// Element Genie - open-source 3D titles & models for Premiere Pro
// Window, main loop, panels, viewport, timeline, export.
#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <set>
#include <map>
#include "app.h"
#include "project.h"
#include "platform.h"
#include "effect.h"
#include "bevel.h"
#include "icons.h"
#include <nanosvg.h>
#include <windows.h>
#include <shellapi.h>

using glm::vec2; using glm::vec3; using glm::vec4; using glm::mat4;

App g;
extern const unsigned char g_font_ui[]; extern const unsigned g_font_ui_size;
extern const unsigned char g_font_ui_bold[]; extern const unsigned g_font_ui_bold_size;
extern const unsigned char g_logo_png[]; extern const unsigned g_logo_png_size;
bool loadImageFile(const std::string& path, ImageData& out);
bool readFileBytes(const std::string& path, std::vector<uint8_t>& out);
std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& w);

static const char* APP_NAME = "Element Genie";
static const char* APP_VERSION = "1.0";

// ============================================================ colours / theme
static const ImVec4 GOLD(0.545f, 0.482f, 1.f, 1.f);  // accent (violet)
static const ImU32 GOLD32 = IM_COL32(139, 123, 255, 255);
static const ImU32 KEY32 = IM_COL32(139, 123, 255, 255);
static const ImU32 KEYSEL32 = IM_COL32(110, 200, 255, 255);

static void applyTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    s.WindowRounding = 12; s.FrameRounding = 8; s.PopupRounding = 10; s.GrabRounding = 8; s.TabRounding = 8; s.ChildRounding = 14;
    s.ScrollbarRounding = 8; s.WindowBorderSize = 0; s.FrameBorderSize = 0; s.PopupBorderSize = 1; s.ChildBorderSize = 0;
    s.WindowPadding = ImVec2(12, 10); s.FramePadding = ImVec2(9, 6); s.ItemSpacing = ImVec2(8, 7); s.ItemInnerSpacing = ImVec2(6, 4);
    s.IndentSpacing = 16; s.ScrollbarSize = 10; s.GrabMinSize = 12; s.TabBarBorderSize = 0; s.DockingSeparatorSize = 6;
    ImVec4* c = s.Colors;
    auto rgb = [](int r, int g2, int b, float a = 1.f) { return ImVec4(r / 255.f, g2 / 255.f, b / 255.f, a); };
    c[ImGuiCol_Text] = rgb(230, 230, 236);
    c[ImGuiCol_TextDisabled] = rgb(128, 128, 140);
    c[ImGuiCol_WindowBg] = rgb(28, 28, 31);
    c[ImGuiCol_ChildBg] = rgb(42, 42, 46);
    c[ImGuiCol_PopupBg] = rgb(36, 36, 42, 0.98f);
    c[ImGuiCol_Border] = rgb(58, 58, 66);
    c[ImGuiCol_FrameBg] = rgb(34, 34, 38);
    c[ImGuiCol_FrameBgHovered] = rgb(48, 48, 56);
    c[ImGuiCol_FrameBgActive] = rgb(58, 53, 96);
    c[ImGuiCol_TitleBg] = rgb(22, 22, 25);
    c[ImGuiCol_TitleBgActive] = rgb(22, 22, 25);
    c[ImGuiCol_MenuBarBg] = rgb(22, 22, 25);
    c[ImGuiCol_ScrollbarBg] = rgb(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = rgb(70, 70, 80);
    c[ImGuiCol_CheckMark] = GOLD;
    c[ImGuiCol_SliderGrab] = rgb(139, 123, 255, 0.9f);
    c[ImGuiCol_SliderGrabActive] = GOLD;
    c[ImGuiCol_Button] = rgb(50, 50, 56);
    c[ImGuiCol_ButtonHovered] = rgb(62, 60, 76);
    c[ImGuiCol_ButtonActive] = rgb(58, 53, 96);
    c[ImGuiCol_Header] = rgb(58, 53, 96);
    c[ImGuiCol_HeaderHovered] = rgb(54, 52, 70);
    c[ImGuiCol_HeaderActive] = rgb(70, 62, 120);
    c[ImGuiCol_Separator] = rgb(52, 52, 60);
    c[ImGuiCol_Tab] = rgb(42, 42, 46);
    c[ImGuiCol_TabHovered] = rgb(62, 60, 76);
    c[ImGuiCol_TabSelected] = rgb(58, 53, 96);
    c[ImGuiCol_TabSelectedOverline] = rgb(0, 0, 0, 0);
    c[ImGuiCol_TabDimmed] = rgb(42, 42, 46);
    c[ImGuiCol_TabDimmedSelected] = rgb(50, 48, 72);
    c[ImGuiCol_DockingPreview] = rgb(139, 123, 255, 0.4f);
    c[ImGuiCol_DockingEmptyBg] = rgb(28, 28, 31);
    c[ImGuiCol_ResizeGrip] = rgb(0, 0, 0, 0);
    c[ImGuiCol_TextSelectedBg] = rgb(139, 123, 255, 0.35f);
    c[ImGuiCol_NavCursor] = GOLD;
}

// ============================================================ helpers
int currentFrameInt() { return (int)std::floor(g.frame + 0.5f); }
Object* selectedObject() {
    if (g.sel.kind != Selection::Obj) return nullptr;
    for (auto& o : g.S.objects) if (o.id == g.sel.id) return &o;
    return nullptr;
}
void notify(const std::string& m) { g.toast = m; g.toastUntil = ImGui::GetTime() + 3.5; }
static std::string baseName(const std::string& p) { size_t k = p.find_last_of("/\\"); return k == std::string::npos ? p : p.substr(k + 1); }
static std::string stem(const std::string& p) { std::string b = baseName(p); size_t d = b.find_last_of('.'); return d == std::string::npos ? b : b.substr(0, d); }
static std::string lowerExt(const std::string& p) { size_t d = p.find_last_of('.'); std::string e = d == std::string::npos ? "" : p.substr(d + 1); for (auto& c : e) c = (char)tolower((unsigned char)c); return e; }

static void tooltip(const char* t) { if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", t); }

static void sectionHeader(const char* t) {
    ImGui::Spacing();
    ImGui::PushFont(g.fBold, 0);
    ImGui::TextColored(ImVec4(0.75f, 0.73f, 0.70f, 1), "%s", t);
    ImGui::PopFont();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y - 2), ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y - 2), IM_COL32(62, 60, 76, 255));
    ImGui::Spacing();
}

static bool goldButton(const char* label, ImVec2 size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.545f, 0.482f, 1.f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.62f, 0.57f, 1.f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.39f, 0.9f, 1));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    ImGui::PushFont(g.fBold, 0);
    bool r = ImGui::Button(label, size);
    ImGui::PopFont();
    ImGui::PopStyleColor(4);
    return r;
}

// ============================================================ undo
static void stripRuntimeKeep(std::map<int, Object>& keep) { for (auto& o : g.S.objects) keep[o.id] = o; }
static void loadSceneKeepGeometry(const std::string& json) {
    std::map<int, Object> old; stripRuntimeKeep(old);
    Scene n; std::string err;
    if (!sceneFromJson(json, n, err)) return;
    for (auto& o : n.objects) {
        auto it = old.find(o.id);
        if (it != old.end() && it->second.geo) { o.geo = it->second.geo; o.geoKey = it->second.geoKey; o.geoError = it->second.geoError; }
    }
    g.S = std::move(n);
}
static void undoCheckpoint() {
    std::string j = sceneToJson(g.S);
    if (g.lastJson.empty()) { g.lastJson = j; return; }
    if (j != g.lastJson) {
        g.undoStack.push_back(g.lastJson);
        if (g.undoStack.size() > 200) g.undoStack.erase(g.undoStack.begin());
        g.redoStack.clear();
        g.lastJson = j; g.dirty = true;
    }
}
static void doUndo() {
    undoCheckpoint();
    if (g.undoStack.empty()) return;
    g.redoStack.push_back(g.lastJson);
    g.lastJson = g.undoStack.back(); g.undoStack.pop_back();
    loadSceneKeepGeometry(g.lastJson); g.keySel.clear();
    notify("Undo");
}
static void doRedo() {
    if (g.redoStack.empty()) return;
    g.undoStack.push_back(g.lastJson);
    g.lastJson = g.redoStack.back(); g.redoStack.pop_back();
    loadSceneKeepGeometry(g.lastJson); g.keySel.clear();
    notify("Redo");
}

// ============================================================ materials
struct MatPreset { const char* name; vec3 col; float metal, rough, coat, emis; };
static const MatPreset MATS[] = {
    {"Chrome", {0.96f, 0.96f, 0.97f}, 1, 0.06f, 0, 0},
    {"Gold", {1.0f, 0.77f, 0.34f}, 1, 0.16f, 0, 0},
    {"Rose Gold", {0.98f, 0.68f, 0.58f}, 1, 0.2f, 0, 0},
    {"Brushed Steel", {0.78f, 0.79f, 0.8f}, 1, 0.4f, 0, 0},
    {"Copper", {0.96f, 0.58f, 0.42f}, 1, 0.25f, 0, 0},
    {"Gunmetal", {0.32f, 0.33f, 0.36f}, 1, 0.28f, 0, 0},
    {"Black Gloss", {0.03f, 0.03f, 0.035f}, 0, 0.1f, 1, 0},
    {"Car Paint", {0.72f, 0.03f, 0.04f}, 0.35f, 0.35f, 1, 0},
    {"Pearl", {0.95f, 0.93f, 0.9f}, 0.25f, 0.25f, 1, 0},
    {"White Plastic", {0.9f, 0.9f, 0.9f}, 0, 0.35f, 0, 0},
    {"Matte Clay", {0.74f, 0.71f, 0.67f}, 0, 0.85f, 0, 0},
    {"Neon Glow", {1.0f, 0.2f, 0.72f}, 0, 0.4f, 0, 4.f},
    {"Platinum", {0.86f, 0.87f, 0.9f}, 1, 0.1f, 0, 0},
    {"Brass", {0.93f, 0.78f, 0.45f}, 1, 0.3f, 0, 0},
    {"Bronze", {0.72f, 0.45f, 0.24f}, 1, 0.32f, 0, 0},
    {"Black Chrome", {0.22f, 0.22f, 0.24f}, 1, 0.05f, 0, 0},
    {"Titanium", {0.6f, 0.58f, 0.56f}, 1, 0.22f, 0, 0},
    {"Diamond", {0.92f, 0.96f, 1.0f}, 0.9f, 0.01f, 1, 0.25f},
    {"Candy Red", {0.8f, 0.02f, 0.1f}, 0.6f, 0.15f, 1, 0},
    {"Candy Blue", {0.05f, 0.25f, 0.9f}, 0.6f, 0.15f, 1, 0},
    {"Emerald", {0.04f, 0.6f, 0.3f}, 0.4f, 0.1f, 1, 0},
    {"Purple Gloss", {0.42f, 0.12f, 0.75f}, 0.2f, 0.12f, 1, 0},
    {"Matte Black", {0.04f, 0.04f, 0.045f}, 0, 0.75f, 0, 0},
    {"Rubber", {0.12f, 0.12f, 0.13f}, 0, 0.95f, 0, 0},
    {"Ice", {0.75f, 0.9f, 1.0f}, 0.3f, 0.05f, 1, 0.15f},
    {"Neon Blue", {0.1f, 0.6f, 1.0f}, 0, 0.4f, 0, 4.f},
    {"Neon Green", {0.3f, 1.0f, 0.35f}, 0, 0.4f, 0, 4.f},
    {"Lava", {1.0f, 0.35f, 0.05f}, 0, 0.6f, 0, 3.f},
};
static void applyMat(Material& m, const MatPreset& p) {
    m.preset = p.name; m.color = p.col; m.metallic = p.metal; m.roughness = p.rough; m.clearcoat = p.coat; m.emissiveStrength = p.emis;
}
// Material previews: real renders of a ball in a small studio, made once with the same renderer.
static GLuint g_matThumb[64] = {};
static bool g_thumbsBuilt = false;
static void buildMatThumbs() {
    if (g_thumbsBuilt) return;
    g_thumbsBuilt = true;
    const int N = 160;
    Scene s; sceneDefaults(s);
    s.env.preset = 0; s.env.intensity = 1.15f;
    s.reflection.enabled = false; s.catcher.enabled = true; s.catcher.opacity = 0.5f; s.catcher.softness = 0.7f; s.quality.ao = true;
    s.lights[0].pos.value = vec3(-2.5f, 4.f, 3.5f); s.lights[0].intensity.value = 2.2f;
    Object o; o.id = 1; o.type = ObjType::Primitive; o.prim = (int)Prim::Sphere; o.name = "ball";
    s.objects.push_back(o);
    s.cam.pos.value = vec3(0, 0.55f, 4.6f); s.cam.target.value = vec3(0, -0.08f, 0); s.cam.fov.value = 34;
    RenderTarget rt; rt.ensure(N, N, 8);
    std::vector<uint8_t> px((size_t)N * N * 4), out((size_t)N * N * 4);
    int n = (int)(sizeof MATS / sizeof MATS[0]);
    for (int i = 0; i < n && i < 64; i++) {
        applyMat(s.objects[0].mat, MATS[i]);
        ViewOptions vo; g.R.render(s, 0.f, rt, vo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, rt.outFbo); glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, N, N, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
            const uint8_t* sp = &px[((size_t)(N - 1 - y) * N + x) * 4];
            uint8_t* dp = &out[((size_t)y * N + x) * 4];
            // studio backdrop: soft vertical falloff with a gentle spotlight
            float fy = y / (float)N, fx = x / (float)N - .5f;
            float spot = std::exp(-(fx * fx * 6.f + (fy - .35f) * (fy - .35f) * 5.f));
            float b = 26 + 22 * spot + 10 * (1 - fy);
            float a = sp[3] / 255.f;
            for (int k = 0; k < 3; k++) { float bg = b * (k == 2 ? 1.08f : 1.f); dp[k] = (uint8_t)std::min(255.f, sp[k] + bg * (1 - a)); }
            dp[3] = 255;
        }
        glGenTextures(1, &g_matThumb[i]); glBindTexture(GL_TEXTURE_2D, g_matThumb[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    rt.release();
}
static int matIndex(const MatPreset& p) { return (int)(&p - MATS); }
static void matSwatch(ImDrawList* dl, ImVec2 c, float r, const MatPreset& p) {
    buildMatThumbs();
    int idx = matIndex(p);
    if (idx >= 0 && idx < 64 && g_matThumb[idx]) {
        dl->AddImageRounded((ImTextureID)(intptr_t)g_matThumb[idx], ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ImVec2(0.1f, 0.06f), ImVec2(0.9f, 0.86f), IM_COL32_WHITE, r * 0.35f);
        return;
    }
    vec3 col = p.col;
    auto toU = [](vec3 v, float a = 1) { v = glm::clamp(v, vec3(0), vec3(1)); return IM_COL32((int)(v.x * 255), (int)(v.y * 255), (int)(v.z * 255), (int)(a * 255)); };
    if (p.emis > 0) dl->AddCircleFilled(c, r + 3, toU(col, 0.35f), 32);
    // fake shading: dark rim -> base -> highlight
    vec3 dark = col * (p.metal > 0.5f ? 0.25f : 0.45f);
    dl->AddCircleFilled(c, r, toU(dark), 32);
    dl->AddCircleFilled(ImVec2(c.x - r * 0.12f, c.y - r * 0.12f), r * 0.82f, toU(col * 0.85f), 32);
    dl->AddCircleFilled(ImVec2(c.x - r * 0.22f, c.y - r * 0.22f), r * 0.55f, toU(glm::mix(col, vec3(1), 0.2f)), 32);
    float hl = std::max(0.08f, 0.45f * (1 - p.rough));
    dl->AddCircleFilled(ImVec2(c.x - r * 0.35f, c.y - r * 0.38f), r * hl, toU(vec3(1), 0.85f), 24);
}
static bool materialPicker(Material& m, const char* id) {
    bool changed = false;
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float avail = ImGui::GetContentRegionAvail().x;
    float cell = 52; int perRow = std::max(1, (int)(avail / cell));
    for (int i = 0; i < (int)(sizeof MATS / sizeof MATS[0]); i++) {
        if (i % perRow) ImGui::SameLine(0, 0);
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("sw", ImVec2(cell, cell))) { applyMat(m, MATS[i]); changed = true; }
        bool hov = ImGui::IsItemHovered();
        bool cur = m.preset == MATS[i].name;
        ImVec2 c(p.x + cell * .5f, p.y + cell * .5f);
        float rr = cell * .5f - 4;
        matSwatch(dl, c, rr, MATS[i]);
        if (cur) dl->AddRect(ImVec2(c.x - rr - 2, c.y - rr - 2), ImVec2(c.x + rr + 2, c.y + rr + 2), GOLD32, rr * 0.4f, 0, 2.f);
        else if (hov) dl->AddRect(ImVec2(c.x - rr - 1, c.y - rr - 1), ImVec2(c.x + rr + 1, c.y + rr + 1), IM_COL32(150, 150, 165, 220), rr * 0.4f, 0, 1.f);
        if (hov) ImGui::SetTooltip("%s", MATS[i].name);
        ImGui::PopID();
    }
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(w * 0.55f);
    if (ImGui::ColorEdit3("Colour", &m.color.x, ImGuiColorEditFlags_NoInputs)) { m.preset = "Custom"; changed = true; }
    ImGui::SameLine(); ImGui::TextDisabled("%s", m.preset.c_str());
    ImGui::SetNextItemWidth(w * 0.6f);
    if (ImGui::SliderFloat("Metal", &m.metallic, 0, 1, "%.2f")) { m.preset = "Custom"; changed = true; }
    ImGui::SetNextItemWidth(w * 0.6f);
    if (ImGui::SliderFloat("Roughness", &m.roughness, 0, 1, "%.2f")) { m.preset = "Custom"; changed = true; }
    tooltip("Low = mirror-like reflections, high = soft and matte.");
    ImGui::SetNextItemWidth(w * 0.6f);
    if (ImGui::SliderFloat("Clear coat", &m.clearcoat, 0, 1, "%.2f")) { m.preset = "Custom"; changed = true; }
    tooltip("A glossy varnish layer on top - like car paint.");
    ImGui::SetNextItemWidth(w * 0.6f);
    if (ImGui::SliderFloat("Self glow", &m.emissiveStrength, 0, 10, "%.1f")) { m.preset = "Custom"; changed = true; }
    tooltip("Makes the surface emit its own light. Combine with Glow in Scene settings.");
    ImGui::PopID();
    return changed;
}

// ============================================================ keyframe-aware controls
static void drawStopwatch(ImDrawList* dl, ImVec2 c, float r, bool on) {
    ImU32 col = on ? GOLD32 : IM_COL32(140, 138, 134, 255);
    dl->AddCircle(c, r, col, 20, 1.6f);
    dl->AddLine(ImVec2(c.x, c.y - r - 1), ImVec2(c.x, c.y - r - 3), col, 2.f);
    dl->AddLine(c, ImVec2(c.x, c.y - r * 0.65f), col, 1.6f);
    dl->AddLine(c, ImVec2(c.x + r * 0.5f, c.y), col, 1.6f);
    if (on) dl->AddCircleFilled(c, 1.8f, col);
}
static void drawDiamond(ImDrawList* dl, ImVec2 c, float r, ImU32 fill, bool filled, ImU32 outline = IM_COL32(20, 20, 20, 255)) {
    ImVec2 p[4] = {{c.x, c.y - r}, {c.x + r, c.y}, {c.x, c.y + r}, {c.x - r, c.y}};
    if (filled) dl->AddConvexPolyFilled(p, 4, fill);
    dl->AddPolyline(p, 4, filled ? outline : fill, ImDrawFlags_Closed, 1.3f);
}

template <class T> static bool keyControls(Track<T>& t) {
    bool changed = false;
    if (g.setupMode) return false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int fr = currentFrameInt();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetFrameHeight();
    if (ImGui::InvisibleButton("sw", ImVec2(h, h))) { t.setAnimated(!t.animated, fr); changed = true; }
    tooltip(t.animated ? "Stop animating (removes all keyframes)" : "Animate this - adds a keyframe now. Any change after this adds keyframes.");
    drawStopwatch(dl, ImVec2(p.x + h * .5f, p.y + h * .5f + 1), h * 0.28f, t.animated);
    ImGui::SameLine(0, 2);
    p = ImGui::GetCursorScreenPos();
    if (t.animated) {
        bool has = t.keyIndexAt(fr) >= 0;
        if (ImGui::InvisibleButton("key", ImVec2(h * 0.8f, h))) {
            if (has) t.removeKey(fr); else t.addKey(fr, t.eval(g.frame));
            changed = true;
        }
        tooltip(has ? "Remove the keyframe at this frame" : "Add a keyframe at this frame");
        drawDiamond(dl, ImVec2(p.x + h * .4f, p.y + h * .5f), h * 0.24f, KEY32, has);
    } else ImGui::Dummy(ImVec2(h * 0.8f, h));
    ImGui::SameLine(0, 4);
    return changed;
}

static bool vec3Row(const char* label, Track<vec3>& t, float speed, const char* fmt, float minV = 0, float maxV = 0) {
    ImGui::PushID(label);
    bool changed = keyControls(t);
    vec3 v = t.eval(g.frame);
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(w - 70);
    if (ImGui::DragFloat3("##v", &v.x, speed, minV, maxV, fmt)) { t.set(currentFrameInt(), v); changed = true; }
    ImGui::SameLine(); ImGui::TextUnformatted(label);
    ImGui::PopID();
    return changed;
}
static bool floatRow(const char* label, Track<float>& t, float speed, const char* fmt, float minV = 0, float maxV = 0, bool slider = false) {
    ImGui::PushID(label);
    bool changed = keyControls(t);
    float v = t.eval(g.frame);
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(w - 70);
    bool ch = slider ? ImGui::SliderFloat("##v", &v, minV, maxV, fmt) : ImGui::DragFloat("##v", &v, speed, minV, maxV, fmt);
    if (ch) { t.set(currentFrameInt(), v); changed = true; }
    ImGui::SameLine(); ImGui::TextUnformatted(label);
    ImGui::PopID();
    return changed;
}

// ============================================================ objects
static Object& addObject(ObjType t, const std::string& name) {
    Object o; o.id = g.S.nextId++; o.type = t; o.name = name;
    // place new things where the camera looks
    o.pos.value = vec3(0);
    g.S.objects.push_back(o);
    g.sel.kind = Selection::Obj; g.sel.id = o.id;
    return g.S.objects.back();
}
static std::string uniqueName(const std::string& base) {
    std::set<std::string> names; for (auto& o : g.S.objects) names.insert(o.name);
    if (!names.count(base)) return base;
    for (int i = 2;; i++) { std::string n = base + " " + std::to_string(i); if (!names.count(n)) return n; }
}
static void addText(const std::string& txt = "TEXT") {
    Object& o = addObject(ObjType::Text, uniqueName("Text"));
    o.text = txt; o.font = "builtin:Inter Black";
    applyMat(o.mat, MATS[1]);
    o.mat2 = o.mat;
}
static void importFile(const std::string& path);
static void addModelDialog() {
    std::string p = openFileDialog("Import 3D model", "3D models (GLB, glTF, FBX, OBJ)|*.glb;*.gltf;*.fbx;*.obj|All files|*.*", g.hwnd);
    if (!p.empty()) importFile(p);
}
static void addLogoDialog() {
    std::string p = openFileDialog("Import logo", "Logos (PNG with transparency, SVG)|*.png;*.svg|Images|*.png;*.jpg;*.jpeg;*.svg|All files|*.*", g.hwnd);
    if (!p.empty()) importFile(p);
}
static void addPrimitive(Prim p) {
    static const char* names[] = {"Cube", "Sphere", "Cylinder", "Torus", "Floor", "Rounded cube", "Gem", "Cone", "Capsule"};
    Object& o = addObject(ObjType::Primitive, uniqueName(names[(int)p]));
    o.prim = (int)p;
    applyMat(o.mat, MATS[(int)p == (int)Prim::Plane ? 10 : p == Prim::Gem ? 17 : 0]);
    if (p == Prim::Plane) { o.pos.value = vec3(0, -1, 0); o.castShadow = false; }
}
static void addIcon(const char* name) {
    Object& o = addObject(ObjType::Logo, uniqueName(name));
    o.path = std::string("builtin:icon:") + name;
    o.depth = 0.35f; o.bevel = 0.04f; o.bevelStyle = BEVEL_ROUND; o.useImageColours = false;
    applyMat(o.mat, MATS[1]); o.mat2 = o.mat;
}
static void drawIconThumb(ImDrawList* dl, ImVec2 p0, float sz, int idx, ImU32 col, ImU32 bg) {
    static NSVGimage* cache[64] = {};
    if (idx < 0 || idx >= 64) return;
    if (!cache[idx]) { std::string s = ICONS[idx].svg; cache[idx] = nsvgParse(&s[0], "px", 96); }
    NSVGimage* im = cache[idx]; if (!im) return;
    float k = sz / 100.f;
    for (NSVGshape* sh = im->shapes; sh; sh = sh->next) {
        std::vector<std::vector<ImVec2>> cs; std::vector<float> ar;
        for (NSVGpath* p = sh->paths; p; p = p->next) {
            std::vector<ImVec2> c;
            for (int i = 0; i < p->npts - 1; i += 3) {
                float* q = &p->pts[i * 2];
                for (int t = 0; t < 8; t++) { float u = t / 8.f, v = 1 - u;
                    float x = v*v*v*q[0] + 3*v*v*u*q[2] + 3*v*u*u*q[4] + u*u*u*q[6], y = v*v*v*q[1] + 3*v*v*u*q[3] + 3*v*u*u*q[5] + u*u*u*q[7];
                    c.push_back(ImVec2(p0.x + x * k, p0.y + y * k)); }
            }
            float a = 0; for (size_t i = 0; i < c.size(); i++) { auto& A = c[i]; auto& B = c[(i + 1) % c.size()]; a += A.x * B.y - B.x * A.y; }
            cs.push_back(c); ar.push_back(std::fabs(a));
        }
        std::vector<int> ord(cs.size()); for (size_t i = 0; i < ord.size(); i++) ord[i] = (int)i;
        std::sort(ord.begin(), ord.end(), [&](int a, int b) { return ar[a] > ar[b]; });
        for (size_t n = 0; n < ord.size(); n++) if (cs[ord[n]].size() > 2) dl->AddConcavePolyFilled(cs[ord[n]].data(), (int)cs[ord[n]].size(), n == 0 ? col : bg);
    }
}

static void addImageCard(const std::string& path) {
    Object& o = addObject(ObjType::ImageCard, uniqueName(stem(path)));
    o.path = path; o.castShadow = false;
    applyMat(o.mat, MATS[10]); o.mat.color = vec3(1); o.mat.roughness = 0.6f;
}

static void openProject(const std::string& p);
static void importFile(const std::string& path) {
    std::string e = lowerExt(path);
    if (e == "egproj" || e == "json") { openProject(path); return; }
    if (e == "glb" || e == "gltf" || e == "fbx" || e == "obj") {
        Object& o = addObject(ObjType::Model, uniqueName(stem(path)));
        o.path = path; o.keepModelMaterials = true; applyMat(o.mat, MATS[9]);
        if (!g.R.ensureGeometry(o)) notify(o.geoError.empty() ? "Couldn't load that model." : o.geoError);
        else notify("Imported " + baseName(path));
        return;
    }
    if (e == "svg" || e == "png") {
        Object& o = addObject(ObjType::Logo, uniqueName(stem(path)));
        o.path = path; o.depth = 0.2f; o.bevel = 0.025f;
        applyMat(o.mat, MATS[1]); o.mat2 = o.mat; o.useImageColours = true;
        o.mat.metallic = 0.3f; o.mat.roughness = 0.3f; o.mat.preset = "Custom";
        if (!g.R.ensureGeometry(o)) notify(o.geoError);
        return;
    }
    if (e == "jpg" || e == "jpeg" || e == "bmp" || e == "tga") { addImageCard(path); return; }
    if (e == "hdr") { g.S.env.preset = -1; g.S.env.path = path; notify("Environment set to " + baseName(path)); return; }
    if (e == "ttf" || e == "otf") {
        if (Object* o = selectedObject()) if (o->type == ObjType::Text) { o->font = path; notify("Font set to " + baseName(path)); return; }
        addText(); g.S.objects.back().font = path; return;
    }
    notify("Can't import ." + e + " files.");
}

static void deleteSelected() {
    if (g.sel.kind == Selection::Obj) {
        auto& v = g.S.objects;
        v.erase(std::remove_if(v.begin(), v.end(), [](const Object& o) { return o.id == g.sel.id; }), v.end());
        g.sel.kind = Selection::None;
    } else if (g.sel.kind == Selection::Light && g.sel.id >= 0 && g.sel.id < (int)g.S.lights.size()) {
        g.S.lights.erase(g.S.lights.begin() + g.sel.id);
        g.sel.kind = Selection::None;
    }
}
static void duplicateSelected() {
    Object* o = selectedObject(); if (!o) return;
    Object c = *o; c.id = g.S.nextId++; c.name = uniqueName(o->name);
    c.pos.value += vec3(0.3f, 0.3f, 0);
    for (auto& k : c.pos.keys) k.v += vec3(0.3f, 0.3f, 0);
    g.S.objects.push_back(c); g.sel.id = c.id;
}

// ============================================================ project files
static void newProject() {
    sceneDefaults(g.S);
    addText("GENIE");
    g.sel.kind = Selection::Obj; g.sel.id = g.S.objects.back().id;
    g.projectPath.clear(); g.frame = 0; g.undoStack.clear(); g.redoStack.clear(); g.lastJson.clear(); g.dirty = false; g.keySel.clear();
}
static bool saveProject(bool saveAs) {
    std::string p = g.projectPath;
    if (p.empty() || saveAs) p = saveFileDialog("Save project", "Element Genie project|*.egproj", "egproj", g.projectPath.empty() ? "Untitled.egproj" : baseName(g.projectPath), g.hwnd);
    if (p.empty()) return false;
    std::string j = sceneToJson(g.S);
    FILE* f = _wfopen(widen(p).c_str(), L"wb");
    if (!f) { notify("Couldn't save - is the file open somewhere else?"); return false; }
    fwrite(j.data(), 1, j.size(), f); fclose(f);
    g.projectPath = p; g.dirty = false;
    notify("Saved " + baseName(p));
    return true;
}
static void openProject(const std::string& p) {
    std::vector<uint8_t> b;
    if (!readFileBytes(p, b)) { notify("Couldn't open " + baseName(p)); return; }
    Scene n; std::string err;
    if (!sceneFromJson(std::string(b.begin(), b.end()), n, err)) { notify(err); return; }
    g.S = std::move(n);
    g.projectPath = p; g.frame = 0; g.undoStack.clear(); g.redoStack.clear(); g.lastJson.clear(); g.dirty = false; g.keySel.clear();
    g.sel.kind = g.S.objects.empty() ? Selection::None : Selection::Obj;
    if (!g.S.objects.empty()) g.sel.id = g.S.objects[0].id;
    notify("Opened " + baseName(p));
}
static void openProjectDialog() {
    std::string p = openFileDialog("Open project", "Element Genie project|*.egproj|All files|*.*", g.hwnd);
    if (!p.empty()) openProject(p);
}

// ============================================================ animation presets
static int fps() { return std::max(1, (int)std::round(g.S.comp.fps)); }
static void presetObject(Object& o, int which) {
    int f0 = currentFrameInt(), D = fps() * 2, end = g.S.comp.frames - 1;
    vec3 p = o.pos.eval(g.frame), r = o.rot.eval(g.frame), s = o.scl.eval(g.frame);
    auto lin = [](auto& tr) { for (auto& k : tr.keys) { k.ease.in = k.ease.out = false; } };
    switch (which) {
    case 0: o.rot.addKey(f0, r); o.rot.addKey(f0 + D, r + vec3(0, 360, 0)); break;
    case 1: o.rot.keys.clear(); o.rot.animated = false; o.rot.addKey(0, r); o.rot.addKey(end, r + vec3(0, 360, 0)); lin(o.rot); break;
    case 2: o.pos.addKey(f0, p + vec3(0, 0, 9)); o.pos.addKey(f0 + D, p); o.opacity.addKey(f0, 0); o.opacity.addKey(f0 + D / 3, 1);
            o.rot.addKey(f0, r + vec3(0, -40, 0)); o.rot.addKey(f0 + D, r); break;
    case 3: o.pos.addKey(f0, p + vec3(0, 5, 0)); o.pos.addKey(f0 + D * 2 / 3, p); o.pos.addKey(f0 + D * 2 / 3 + D / 6, p + vec3(0, 0.25f, 0));
            o.pos.addKey(f0 + D, p); o.pos.keys[o.pos.keyIndexAt(f0)].ease.out = false; o.pos.keys[o.pos.keyIndexAt(f0 + D * 2 / 3)].ease.in = false; break;
    case 4: o.scl.addKey(f0, s * 0.001f); o.scl.addKey(f0 + D * 6 / 10, s * 1.12f); o.scl.addKey(f0 + D, s); break;
    case 5: { int step = fps(); o.pos.keys.clear(); o.pos.animated = false;
              for (int f = 0, i = 0; f <= end + step; f += step, i++) o.pos.addKey(f, p + vec3(0, (i % 2 ? 0.18f : -0.18f), 0)); } break;
    case 6: o.opacity.addKey(f0, 0); o.opacity.addKey(f0 + D / 2, 1); break;
    case 7: o.opacity.addKey(f0, o.opacity.eval(g.frame)); o.opacity.addKey(f0 + D / 2, 0); break;
    }
    notify("Animation added - scrub the timeline to see it");
}
static void presetCamera(int which) {
    auto& c = g.S.cam; int end = g.S.comp.frames - 1, f0 = currentFrameInt();
    vec3 p = c.pos.eval(g.frame), t = c.target.eval(g.frame);
    switch (which) {
    case 0: c.pos.addKey(f0, p); c.pos.addKey(end, t + (p - t) * 0.65f); break;
    case 1: { c.pos.keys.clear(); c.pos.animated = false; vec3 d = p - t;
              for (int i = 0; i <= 4; i++) { float a = glm::radians(90.f * i / 4 - 45.f); vec3 q = vec3(d.x * cos(a) + d.z * sin(a), d.y, -d.x * sin(a) + d.z * cos(a));
                  c.pos.addKey(end * i / 4, t + q); }
              for (auto& k : c.pos.keys) { k.ease.in = k.ease.out = false; }
              c.pos.keys.front().ease.out = true; c.pos.keys.back().ease.in = true; } break;
    case 2: c.pos.addKey(0, p + vec3(0, -1.2f, 0)); c.pos.addKey(end, p + vec3(0, 1.5f, 0)); break;
    case 3: c.pos.addKey(f0, p); c.pos.addKey(end, t + (p - t) * 1.5f); c.fov.addKey(f0, c.fov.eval(g.frame)); c.fov.addKey(end, c.fov.eval(g.frame) / 1.5f); break;
    }
    notify("Camera move added");
}
struct LetterPreset { const char* name; vec3 pos, rot; float scale; };
static const LetterPreset LPRESETS[] = {
    {"Fly in from front", {0, 0, 4}, {0, 0, 0}, 1},
    {"Drop from above", {0, 2.5f, 0}, {-60, 0, 0}, 1},
    {"Spin in", {0, 0, 0}, {0, 180, 0}, 1},
    {"Rise up", {0, -1.2f, 0}, {0, 0, 0}, 1},
    {"Pop in", {0, 0, 0}, {0, 0, 0}, 0},
    {"Tumble", {0, 0.8f, 1.5f}, {180, 0, 90}, 0.5f},
    {"Slide from left", {-3, 0, 0}, {0, -45, 0}, 1},
};

// ============================================================ fonts combo
static bool fontCombo(Object& o) {
    bool changed = false;
    std::string cur = o.font;
    std::string label = cur.rfind("builtin:", 0) == 0 ? cur.substr(8) : baseName(cur);
    for (auto& f : systemFonts()) if (f.path == cur) label = f.name;
    if (cur.empty()) label = "Inter Bold";
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
    if (ImGui::BeginCombo("##font", label.c_str(), ImGuiComboFlags_HeightLarge)) {
        static char filt[64] = "";
        if (ImGui::IsWindowAppearing()) { ImGui::SetKeyboardFocusHere(); filt[0] = 0; }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##filt", "Search fonts...", filt, sizeof filt);
        std::string fl = filt; for (auto& c : fl) c = (char)tolower((unsigned char)c);
        for (const char* b : {"Inter Black", "Inter Bold", "Inter Regular"}) {
            std::string id = std::string("builtin:") + b;
            std::string n = std::string(b) + "  (built in)";
            std::string ln = n; for (auto& c : ln) c = (char)tolower((unsigned char)c);
            if (!fl.empty() && ln.find(fl) == std::string::npos) continue;
            if (ImGui::Selectable(n.c_str(), cur == id)) { o.font = id; changed = true; }
        }
        ImGui::Separator();
        for (auto& f : systemFonts()) {
            std::string ln = f.name; for (auto& c : ln) c = (char)tolower((unsigned char)c);
            if (!fl.empty() && ln.find(fl) == std::string::npos) continue;
            if (ImGui::Selectable(f.name.c_str(), cur == f.path)) { o.font = f.path; changed = true; }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("File...")) {
        std::string p = openFileDialog("Choose a font", "Fonts|*.ttf;*.otf|All files|*.*", g.hwnd);
        if (!p.empty()) { o.font = p; changed = true; }
    }
    tooltip("Use any .ttf / .otf - e.g. from Google Fonts or DaFont");
    return changed;
}

// ============================================================ panels: scene
static void drawEye(ImDrawList* dl, ImVec2 c, bool on) {
    ImU32 col = on ? IM_COL32(210, 208, 204, 255) : IM_COL32(90, 90, 90, 255);
    dl->PathArcTo(ImVec2(c.x, c.y + 4), 7, -2.6f, -0.54f, 12); dl->PathStroke(col, 0, 1.5f);
    dl->PathArcTo(ImVec2(c.x, c.y - 4), 7, 0.54f, 2.6f, 12); dl->PathStroke(col, 0, 1.5f);
    if (on) dl->AddCircleFilled(c, 2.4f, col);
    else dl->AddLine(ImVec2(c.x - 6, c.y + 5), ImVec2(c.x + 6, c.y - 5), col, 1.5f);
}
static const char* typeLabel(const Object& o) {
    switch (o.type) { case ObjType::Text: return "TEXT"; case ObjType::Logo: return "LOGO"; case ObjType::Model: return "MODEL"; case ObjType::ImageCard: return "IMAGE"; default: return "SHAPE"; }
}
static void scenePanel() {
    ImGui::Begin("Scene");
    float bw = (ImGui::GetContentRegionAvail().x - 8) / 2;
    if (ImGui::Button("+ Text", ImVec2(bw, 0))) addText();
    ImGui::SameLine(0, 8);
    if (ImGui::Button("+ Logo", ImVec2(bw, 0))) addLogoDialog();
    tooltip("PNG (transparent background) or SVG - extruded into 3D");
    if (ImGui::Button("+ 3D model", ImVec2(bw, 0))) addModelDialog();
    tooltip("GLB / glTF / FBX / OBJ - e.g. downloaded from Sketchfab");
    ImGui::SameLine(0, 8);
    if (ImGui::Button("+ Shape", ImVec2(bw, 0))) ImGui::OpenPopup("shapes");
    if (ImGui::BeginPopup("shapes")) {
        if (ImGui::MenuItem("Cube")) addPrimitive(Prim::Cube);
        if (ImGui::MenuItem("Rounded cube")) addPrimitive(Prim::RoundCube);
        if (ImGui::MenuItem("Sphere")) addPrimitive(Prim::Sphere);
        if (ImGui::MenuItem("Cylinder")) addPrimitive(Prim::Cylinder);
        if (ImGui::MenuItem("Torus (ring)")) addPrimitive(Prim::Torus);
        if (ImGui::MenuItem("Floor plane")) addPrimitive(Prim::Plane);
        if (ImGui::MenuItem("Gem")) addPrimitive(Prim::Gem);
        if (ImGui::MenuItem("Cone")) addPrimitive(Prim::Cone);
        if (ImGui::MenuItem("Capsule")) addPrimitive(Prim::Capsule);
        ImGui::Separator();
        ImGui::TextDisabled("3D ICONS");
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            for (int i = 0; i < ICON_COUNT; i++) {
                if (i % 6) ImGui::SameLine(0, 4);
                ImGui::PushID(500 + i);
                ImVec2 p = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("ic", ImVec2(44, 44))) { addIcon(ICONS[i].name); ImGui::CloseCurrentPopup(); }
                bool hov = ImGui::IsItemHovered();
                dl->AddRectFilled(p, ImVec2(p.x + 44, p.y + 44), hov ? IM_COL32(58, 53, 96, 255) : IM_COL32(40, 40, 45, 255), 6);
                drawIconThumb(dl, ImVec2(p.x + 7, p.y + 7), 30, i, GOLD32, hov ? IM_COL32(58, 53, 96, 255) : IM_COL32(40, 40, 45, 255));
                if (hov) ImGui::SetTooltip("%s", ICONS[i].name);
                ImGui::PopID();
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Image card...")) {
            std::string p = openFileDialog("Image card", "Images|*.png;*.jpg;*.jpeg|All files|*.*", g.hwnd);
            if (!p.empty()) addImageCard(p);
        }
        ImGui::EndPopup();
    }
    ImGui::Spacing();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto row = [&](const char* label, const char* tag, bool selected, bool* visible, int key) -> bool {
        ImGui::PushID(key);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float h = 28, w = ImGui::GetContentRegionAvail().x;
        bool clicked = false;
        ImGui::InvisibleButton("row", ImVec2(w - 28, h));
        if (ImGui::IsItemClicked()) clicked = true;
        bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), selected ? IM_COL32(58, 53, 96, 255) : hov ? IM_COL32(40, 40, 45, 255) : IM_COL32(0, 0, 0, 0), 5);
        if (selected) dl->AddRectFilled(p, ImVec2(p.x + 3, p.y + h), GOLD32, 2);
        float x = p.x + 10;
        if (tag) {
            ImVec2 ts = ImGui::CalcTextSize(tag);
            ImGui::PushFont(g.fBold, 11);
            ts = ImGui::CalcTextSize(tag);
            dl->AddRectFilled(ImVec2(x, p.y + 7), ImVec2(x + ts.x + 8, p.y + 21), IM_COL32(50, 48, 72, 255), 3);
            dl->AddText(ImVec2(x + 4, p.y + 14 - ts.y * .5f), IM_COL32(196, 188, 255, 255), tag);
            ImGui::PopFont();
            x += ts.x + 14;
        }
        dl->AddText(ImVec2(x, p.y + 14 - ImGui::GetFontSize() * .5f), IM_COL32(230, 228, 224, 255), label);
        if (visible) {
            ImGui::SameLine(0, 0);
            ImVec2 ep = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("eye", ImVec2(28, h))) *visible = !*visible;
            drawEye(dl, ImVec2(ep.x + 14, ep.y + 14), *visible);
        }
        ImGui::PopID();
        return clicked;
    };
    ImGui::TextDisabled("OBJECTS");
    int idx = 0;
    for (auto& o : g.S.objects) {
        std::string label = o.name;
        if (!o.geoError.empty()) label += "  (!)";
        if (row(label.c_str(), typeLabel(o), g.sel.kind == Selection::Obj && g.sel.id == o.id, &o.visible, 1000 + idx++)) { g.sel.kind = Selection::Obj; g.sel.id = o.id; }
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) { g.sel = {Selection::Obj, o.id}; duplicateSelected(); ImGui::EndPopup(); break; }
            if (ImGui::MenuItem("Delete", "Del")) { g.sel = {Selection::Obj, o.id}; deleteSelected(); ImGui::EndPopup(); break; }
            ImGui::EndPopup();
        }
    }
    if (g.S.objects.empty()) ImGui::TextDisabled("Add text, a logo or a 3D model above,\nor drop files onto the window.");
    ImGui::Spacing();
    ImGui::TextDisabled("CAMERA & LIGHTS");
    if (row("Camera", "CAM", g.sel.kind == Selection::Camera, nullptr, 1)) g.sel = {Selection::Camera, 0};
    for (int i = 0; i < (int)g.S.lights.size(); i++)
        if (row(g.S.lights[i].name.c_str(), "LIGHT", g.sel.kind == Selection::Light && g.sel.id == i, &g.S.lights[i].enabled, 100 + i)) g.sel = {Selection::Light, i};
    if (g.S.lights.size() < 4 && ImGui::SmallButton("+ Add light")) {
        Light l; l.name = "Light " + std::to_string(g.S.lights.size() + 1); l.pos.value = vec3(-3, 4, 3); l.intensity.value = 1.5f; l.shadow = false;
        g.S.lights.push_back(l); g.sel = {Selection::Light, (int)g.S.lights.size() - 1};
    }
    ImGui::Spacing();
    if (row("Look & render settings", "WORLD", g.sel.kind == Selection::World, nullptr, 2)) g.sel = {Selection::World, 0};
    ImGui::End();
}

// ============================================================ bevel presets
static void bevelIcon(ImDrawList* dl, ImVec2 p0, float sz, int style, bool sel) {
    BevelProfile pr = bevelProfile(style, 6);
    float m = sz * 0.16f, k = (sz - 2 * m) / 2.2f;
    // side view of a letter's top-right corner: solid to the left/below the profile
    ImVec2 o(p0.x + sz - m - 0.2f * k, p0.y + m + 0.6f * k);   // where the face meets the side with no bevel
    auto P = [&](double inset, double depth) { return ImVec2(o.x - (float)inset * k, o.y + (float)depth * k); };
    std::vector<ImVec2> poly;
    poly.push_back(P(1.9, 0));
    if (pr.pts.empty()) poly.push_back(P(0, 0));
    for (auto& q : pr.pts) poly.push_back(P(q.x, q.y));
    poly.push_back(P(0, 1.5)); poly.push_back(P(1.9, 1.5));
    ImU32 fill = sel ? IM_COL32(139, 123, 255, 255) : IM_COL32(150, 146, 138, 255);
    dl->AddConcavePolyFilled(poly.data(), (int)poly.size(), fill);
    std::vector<ImVec2> edge(poly.begin(), poly.end() - 1);
    dl->AddPolyline(edge.data(), (int)edge.size(), sel ? IM_COL32(232, 228, 255, 255) : IM_COL32(200, 196, 188, 255), 0, 1.5f);
}
static bool bevelPicker(Object& o) {
    bool changed = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float avail = ImGui::GetContentRegionAvail().x;
    float cell = 46; int perRow = std::max(1, (int)(avail / (cell + 4)));
    for (int i = 0; i < BEVEL_COUNT; i++) {
        int st = BEVEL_ORDER[i];
        if (i % perRow) ImGui::SameLine(0, 4);
        ImGui::PushID(st);
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("bv", ImVec2(cell, cell))) { o.bevelStyle = st; changed = true;
            if (st != BEVEL_NONE && o.bevel < 0.005f) o.bevel = 0.03f; }
        bool hov = ImGui::IsItemHovered(), cur = o.bevelStyle == st;
        dl->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell), cur ? IM_COL32(58, 53, 96, 255) : hov ? IM_COL32(52, 52, 58, 255) : IM_COL32(36, 36, 41, 255), 6);
        if (cur) dl->AddRect(p, ImVec2(p.x + cell, p.y + cell), GOLD32, 6, 0, 1.5f);
        dl->PushClipRect(p, ImVec2(p.x + cell, p.y + cell), true);
        bevelIcon(dl, p, cell, st, cur);
        dl->PopClipRect();
        if (hov) ImGui::SetTooltip("%s", BEVEL_NAMES[st]);
        ImGui::PopID();
    }
    ImGui::TextDisabled("Bevel: %s", BEVEL_NAMES[o.bevelStyle]);
    return changed;
}

// ============================================================ panels: properties
static void objectProps(Object& o) {
    char name[128]; snprintf(name, sizeof name, "%s", o.name.c_str());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##name", name, sizeof name)) o.name = name;
    if (!o.geoError.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.55f, 0.4f, 1)); ImGui::TextWrapped("%s", o.geoError.c_str()); ImGui::PopStyleColor(); }

    if (ImGui::BeginTabBar("objtabs")) {
        if (ImGui::BeginTabItem("Object")) {
            if (o.type == ObjType::Text) {
                sectionHeader("Text");
                static char buf[2048];
                snprintf(buf, sizeof buf, "%s", o.text.c_str());
                if (ImGui::InputTextMultiline("##text", buf, sizeof buf, ImVec2(-1, ImGui::GetTextLineHeight() * 3.2f))) o.text = buf;
                fontCombo(o);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
                const char* al[] = {"Left", "Centre", "Right"};
                ImGui::Combo("Align", &o.align, al, 3);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
                ImGui::SliderFloat("Spacing", &o.letterSpacing, -0.2f, 1.f, "%.2f");
                if (o.text.find('\n') != std::string::npos) { ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70); ImGui::SliderFloat("Line gap", &o.lineSpacing, 0.6f, 2.5f, "%.2f"); }
            }
            if (o.type == ObjType::Logo || o.type == ObjType::Model || o.type == ObjType::ImageCard) {
                sectionHeader(o.type == ObjType::Model ? "Model file" : o.type == ObjType::Logo ? "Logo file" : "Image");
                ImGui::TextWrapped("%s", baseName(o.path).c_str());
                if (ImGui::Button("Replace file...")) {
                    std::string p = o.type == ObjType::Model ? openFileDialog("Replace model", "3D models|*.glb;*.gltf;*.fbx;*.obj|All files|*.*", g.hwnd)
                                                           : openFileDialog("Replace image", "Images|*.png;*.svg;*.jpg;*.jpeg|All files|*.*", g.hwnd);
                    if (!p.empty()) o.path = p;
                }
                if (o.type == ObjType::Logo) {
                    ImGui::Checkbox("Use the logo's own colours", &o.useImageColours);
                    tooltip("Paints the picture onto the front. Turn off to make the whole logo one material (e.g. solid gold).");
                    if (lowerExt(o.path) != "svg" && o.path.rfind("builtin:", 0) != 0) {
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
                        ImGui::SliderFloat("Cut-off", &o.alphaThreshold, 0.05f, 0.95f, "%.2f");
                        tooltip("How solid a pixel must be to count as part of the logo.");
                    }
                }
                if (o.type == ObjType::Model) {
                    ImGui::Checkbox("Keep the model's own materials", &o.keepModelMaterials);
                    tooltip("Off = paint the whole model with the material below.");
                }
                if (o.type == ObjType::ImageCard) ImGui::Checkbox("Use the image's colours", &o.useImageColours);
            }
            if (o.type == ObjType::Primitive) {
                sectionHeader("Shape");
                const char* pn[] = {"Cube", "Sphere", "Cylinder", "Torus", "Floor plane", "Rounded cube", "Gem", "Cone", "Capsule"};
                ImGui::SetNextItemWidth(-1);
                ImGui::Combo("##prim", &o.prim, pn, 9);
            }
            if (o.type == ObjType::Text || o.type == ObjType::Logo) {
                sectionHeader("Extrude & bevel");
                float w = ImGui::GetContentRegionAvail().x - 70;
                ImGui::SetNextItemWidth(w); ImGui::SliderFloat("Depth", &o.depth, 0.f, 1.5f, "%.2f");
                bevelPicker(o);
                if (o.bevelStyle != BEVEL_NONE) {
                    ImGui::SetNextItemWidth(w); ImGui::SliderFloat("Bevel size", &o.bevel, 0.f, 0.2f, "%.3f");
                    ImGui::SetNextItemWidth(w); ImGui::SliderFloat("Bevel depth", &o.bevelDepth, 0.2f, 3.f, "%.2f");
                    tooltip("How far the bevel cuts back into the sides. 1 = same as its size.");
                    if (bevelProfile(o.bevelStyle, 1).smooth) { ImGui::SetNextItemWidth(w); ImGui::SliderInt("Smoothness", &o.bevelSegs, 1, 10); }
                }
            }
            if (g.setupMode) {
                sectionHeader("Animate in Premiere with");
                const char* gn[] = {"Group 1", "Group 2", "Group 3", "Group 4", "Group 5"};
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
                ImGui::Combo("##group", &o.group, gn, 5);
                tooltip("Keyframe this object with the matching Group in Effect Controls. Objects in the same group move together.");
            }
            sectionHeader(g.setupMode ? "Placement" : "Transform");
            vec3Row("Position", o.pos, 0.01f, "%.2f");
            vec3Row("Rotation", o.rot, 0.5f, "%.1f°");
            vec3Row("Scale", o.scl, 0.01f, "%.2f");
            floatRow("Opacity", o.opacity, 0.01f, "%.2f", 0, 1, true);
            if (ImGui::SmallButton("Reset transform")) { o.pos = Track<vec3>(vec3(0)); o.rot = Track<vec3>(vec3(0)); o.scl = Track<vec3>(vec3(1)); }
            ImGui::SameLine();
            if (ImGui::SmallButton("Uniform scale...")) ImGui::OpenPopup("uscale");
            if (ImGui::BeginPopup("uscale")) {
                vec3 s = o.scl.eval(g.frame); float u = s.x;
                if (ImGui::DragFloat("Scale", &u, 0.01f, 0.01f, 100.f)) o.scl.set(currentFrameInt(), vec3(u));
                ImGui::EndPopup();
            }
            ImGui::Checkbox("Casts shadow", &o.castShadow);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Material")) {
            bool canKeep = o.type == ObjType::Model && o.keepModelMaterials;
            if (canKeep) ImGui::TextWrapped("Using the model's own materials. Untick \"Keep the model's own materials\" on the Object tab to paint it with these instead.");
            sectionHeader(o.type == ObjType::Text || o.type == ObjType::Logo ? "Front" : "Surface");
            materialPicker(o.mat, "m1");
            if (o.type == ObjType::Text || o.type == ObjType::Logo) {
                sectionHeader("Bevel & sides");
                ImGui::Checkbox("Different material for the edges", &o.edgeMat);
                if (o.edgeMat) materialPicker(o.mat2, "m2");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Animate")) {
            if (g.setupMode) {
                sectionHeader("Keyframing");
                ImGui::TextWrapped("Click OK, then keyframe in Premiere's Effect Controls: this object's Group (position, rotation, scale, opacity), Camera, and Animation > Letters Built.");
            } else {
            sectionHeader("One-click animations");
            ImGui::TextDisabled("Starts at the current frame");
            const char* names[] = {"Spin 360°", "Turntable (whole clip)", "Fly in", "Drop & bounce", "Scale pop", "Float / bob", "Fade in", "Fade out"};
            float bw = (ImGui::GetContentRegionAvail().x - 8) / 2;
            for (int i = 0; i < 8; i++) { if (i % 2) ImGui::SameLine(0, 8); if (ImGui::Button(names[i], ImVec2(bw, 0))) presetObject(o, i); }
            }
            if (!g.setupMode && ImGui::Button("Remove all animation", ImVec2(-1, 0))) {
                int f = currentFrameInt();
                o.pos.setAnimated(false, f); o.rot.setAnimated(false, f); o.scl.setAnimated(false, f); o.opacity.setAnimated(false, f);
                o.letters.enabled = false;
            }
            if (o.type == ObjType::Text) {
                sectionHeader("Letter by letter");
                ImGui::Checkbox("Animate each letter", &o.letters.enabled);
                if (o.letters.enabled) {
                    LetterAnim& la = o.letters;
                    if (g.setupMode) ImGui::TextDisabled("Preview with the slider under the viewer.\nIn Premiere, keyframe Animation > Letters Built 0%% -> 100%%.");
                    float w = ImGui::GetContentRegionAvail().x - 90;
                    ImGui::SetNextItemWidth(w);
                    if (ImGui::BeginCombo("Style", "Choose a style...")) {
                        for (auto& p : LPRESETS) if (ImGui::Selectable(p.name)) { la.offPos = p.pos; la.offRot = p.rot; la.offScale = p.scale; }
                        ImGui::EndCombo();
                    }
                    const char* modes[] = {"Build in", "Build out"};
                    ImGui::SetNextItemWidth(w); ImGui::Combo("Direction", &la.mode, modes, 2);
                    const char* ord[] = {"Left to right", "Right to left", "Centre out", "Random"};
                    ImGui::SetNextItemWidth(w); ImGui::Combo("Order", &la.order, ord, 4);
                    if (g.setupMode) { la.start = 0; }
                    else {
                    ImGui::SetNextItemWidth(w); ImGui::DragInt("Start frame", &la.start, 0.3f, 0, g.S.comp.frames);
                    ImGui::SameLine(); if (ImGui::SmallButton("Now")) la.start = currentFrameInt();
                    }
                    ImGui::SetNextItemWidth(w); ImGui::SliderInt("Letter time", &la.duration, 1, 120, "%d frames");
                    ImGui::SetNextItemWidth(w); ImGui::SliderFloat("Delay each", &la.stagger, 0, 20, "%.1f frames");
                    ImGui::SetNextItemWidth(w); ImGui::DragFloat3("Move from", &la.offPos.x, 0.02f);
                    ImGui::SetNextItemWidth(w); ImGui::DragFloat3("Turn from", &la.offRot.x, 1.f, 0, 0, "%.0f°");
                    ImGui::SetNextItemWidth(w); ImGui::SliderFloat("Scale from", &la.offScale, 0, 3, "%.2f");
                    ImGui::Checkbox("Fade", &la.fade);
                }
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

static void cameraProps() {
    auto& c = g.S.cam;
    sectionHeader("Camera");
    vec3Row("Position", c.pos, 0.02f, "%.2f");
    vec3Row("Look at", c.target, 0.02f, "%.2f");
    floatRow("Lens (FOV)", c.fov, 0.2f, "%.1f°", 5, 120);
    floatRow("Roll", c.roll, 0.3f, "%.1f°");
    ImGui::TextDisabled("Viewport: right-drag orbit, middle-drag pan,\nscroll to zoom. F = frame selected.");
    if (ImGui::Button("Reset camera")) { c = CameraRig(); }
    ImGui::SameLine();
    if (ImGui::Button("Frame everything")) {
        vec3 mn, mx; worldBounds(g.S, g.frame, mn, mx);
        vec3 ctr = (mn + mx) * .5f; float r = glm::length(mx - mn) * .5f;
        float d = r / std::tan(glm::radians(c.fov.eval(g.frame)) * .5f) * 1.15f;
        vec3 dir = glm::normalize(c.pos.eval(g.frame) - c.target.eval(g.frame));
        c.target.set(currentFrameInt(), ctr); c.pos.set(currentFrameInt(), ctr + dir * d);
    }
    if (g.setupMode) { ImGui::Spacing(); ImGui::TextWrapped("This is the starting camera. Animate it in Premiere with Effect Controls > Camera (Orbit, Tilt, Zoom, Pan, Roll)."); return; }
    sectionHeader("Camera moves");
    ImGui::TextDisabled("Adds keyframes over the whole clip");
    const char* cm[] = {"Slow push in", "Orbit 90°", "Crane up", "Dolly zoom"};
    float bw = (ImGui::GetContentRegionAvail().x - 8) / 2;
    for (int i = 0; i < 4; i++) { if (i % 2) ImGui::SameLine(0, 8); if (ImGui::Button(cm[i], ImVec2(bw, 0))) presetCamera(i); }
    if (ImGui::Button("Remove camera animation", ImVec2(-1, 0))) {
        int f = currentFrameInt(); c.pos.setAnimated(false, f); c.target.setAnimated(false, f); c.fov.setAnimated(false, f); c.roll.setAnimated(false, f);
    }
}

static void lightProps(Light& l) {
    char name[64]; snprintf(name, sizeof name, "%s", l.name.c_str());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##lname", name, sizeof name)) l.name = name;
    ImGui::Checkbox("On", &l.enabled);
    const char* types[] = {"Sun (directional)", "Bulb (point)"};
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
    ImGui::Combo("Type", &l.type, types, 2);
    vec3Row(l.type == 0 ? "Direction" : "Position", l.pos, 0.05f, "%.2f");
    floatRow("Brightness", l.intensity, 0.05f, "%.2f", 0, 50);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
    ImGui::ColorEdit3("Colour", &l.color.x);
    ImGui::Checkbox("Casts shadows", &l.shadow);
    tooltip("Only the first shadow-casting light makes shadows.");
    ImGui::Spacing();
    if (ImGui::Button("Delete light")) deleteSelected();
}

static const char* ENV_NAMES[] = {"Studio softboxes", "Golden hour", "Neon night", "Chrome bands", "Soft daylight", "Gold studio", "Classic chrome"};
static const int ENV_ORDER[] = {6, 0, 5, 3, 1, 2, 4};
static void worldProps() {
    auto& s = g.S;
    sectionHeader("Reflections & lighting");
    std::string cur = s.env.preset >= 0 ? ENV_NAMES[std::min(6, s.env.preset)] : baseName(s.env.path);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90);
    if (ImGui::BeginCombo("Environment", cur.c_str())) {
        for (int k = 0; k < 7; k++) { int i = ENV_ORDER[k]; if (ImGui::Selectable(ENV_NAMES[i], s.env.preset == i)) s.env.preset = i; }
        ImGui::Separator();
        if (ImGui::Selectable("Load HDRI image...")) {
            std::string p = openFileDialog("Environment image", "HDRI / panorama|*.hdr;*.jpg;*.jpeg;*.png|All files|*.*", g.hwnd);
            if (!p.empty()) { s.env.preset = -1; s.env.path = p; }
        }
        ImGui::EndCombo();
    }
    tooltip("Free HDRIs: polyhaven.com/hdris (download the .hdr)");
    if (!g.R.envError.empty()) ImGui::TextColored(ImVec4(1, .6f, .4f, 1), "%s", g.R.envError.c_str());
    float w = ImGui::GetContentRegionAvail().x - 90;
    ImGui::SetNextItemWidth(w); ImGui::SliderFloat("Strength", &s.env.intensity, 0, 4, "%.2f");
    floatRow("Rotate", s.env.rotation, 0.5f, "%.0f°");
    ImGui::SetNextItemWidth(w); ImGui::SliderFloat("Exposure", &s.env.exposure, -3, 3, "%.1f stops");

    sectionHeader("Floor reflection");
    ImGui::Checkbox("Reflection under the text", &s.reflection.enabled);
    if (s.reflection.enabled) {
        ImGui::SetNextItemWidth(w); ImGui::SliderFloat("Strength##refl", &s.reflection.strength, 0, 1, "%.2f");
        ImGui::SetNextItemWidth(w); ImGui::SliderFloat("Fade##refl", &s.reflection.fade, 0.05f, 3, "%.2f");
    }
    if (g.setupMode) ImGui::TextDisabled("Also in Effect Controls > Realism.");
    sectionHeader("Shadow on the ground");
    ImGui::Checkbox("Floor shadow", &s.catcher.enabled);
    tooltip("An invisible floor that only shows the shadow - it lands on your footage in Premiere.");
    if (s.catcher.enabled) {
        ImGui::SetNextItemWidth(w); ImGui::SliderFloat("Darkness", &s.catcher.opacity, 0, 1, "%.2f");
        ImGui::SetNextItemWidth(w); ImGui::SliderFloat("Softness", &s.catcher.softness, 0, 1, "%.2f");
        ImGui::SetNextItemWidth(w); ImGui::SliderFloat("Floor offset", &s.catcher.height, -2, 2, "%.2f");
    }
    sectionHeader("Realism (preview)");
    ImGui::Checkbox("Contact shadows (ambient occlusion)", &s.quality.ao);
    if (s.quality.ao) { ImGui::SetNextItemWidth(w); ImGui::SliderFloat("AO strength", &s.quality.aoStrength, 0, 3, "%.2f"); }
    if (g.setupMode) ImGui::TextDisabled("Final quality (anti-aliasing, shadow detail) is set\nin Effect Controls > Realism.");
    sectionHeader("Effects");
    ImGui::SetNextItemWidth(w); ImGui::SliderFloat("Glow", &s.glow, 0, 2, "%.2f");
    tooltip("Bloom around bright highlights and glowing materials.");
    if (s.glow > 0) { ImGui::SetNextItemWidth(w); ImGui::SliderFloat("Glow threshold", &s.glowThreshold, 0.1f, 4, "%.2f"); }
    if (!g.setupMode) ImGui::Checkbox("Motion blur (in export)", &s.motionBlur);
    if (s.motionBlur) {
        ImGui::SetNextItemWidth(w); ImGui::SliderFloat("Shutter", &s.shutter, 30, 360, "%.0f°");
        ImGui::SetNextItemWidth(w); ImGui::SliderInt("Quality", &s.mbSamples, 2, 32);
    }
    if (!g.setupMode) {
    sectionHeader("Composition");
    struct SizeP { const char* n; int w, h; };
    static const SizeP sizes[] = {{"1080p HD  1920×1080", 1920, 1080}, {"4K UHD  3840×2160", 3840, 2160}, {"Vertical  1080×1920 (TikTok / Reels)", 1080, 1920},
                                  {"Square  1080×1080", 1080, 1080}, {"720p  1280×720", 1280, 720}, {"DCI 4K  4096×2160", 4096, 2160}};
    char cs[64]; snprintf(cs, sizeof cs, "%d × %d", s.comp.width, s.comp.height);
    ImGui::SetNextItemWidth(w);
    if (ImGui::BeginCombo("Size", cs)) {
        for (auto& p : sizes) if (ImGui::Selectable(p.n, s.comp.width == p.w && s.comp.height == p.h)) { s.comp.width = p.w; s.comp.height = p.h; }
        ImGui::EndCombo();
    }
    int wh[2] = {s.comp.width, s.comp.height};
    ImGui::SetNextItemWidth(w);
    if (ImGui::InputInt2("Custom", wh)) { s.comp.width = std::clamp(wh[0], 16, 8192); s.comp.height = std::clamp(wh[1], 16, 8192); }
    static const float rates[] = {23.976f, 24, 25, 29.97f, 30, 50, 59.94f, 60};
    char fr[32]; snprintf(fr, sizeof fr, "%g fps", s.comp.fps);
    ImGui::SetNextItemWidth(w);
    if (ImGui::BeginCombo("Frame rate", fr)) {
        for (float r : rates) { char b[32]; snprintf(b, sizeof b, "%g fps", r); if (ImGui::Selectable(b, std::fabs(s.comp.fps - r) < 0.001f)) s.comp.fps = r; }
        ImGui::EndCombo();
    }
    float secs = s.comp.frames / s.comp.fps;
    ImGui::SetNextItemWidth(w);
    if (ImGui::DragFloat("Duration", &secs, 0.05f, 0.1f, 600.f, "%.2f sec")) s.comp.frames = std::max(1, (int)std::round(secs * s.comp.fps));
    ImGui::TextDisabled("Match these to your Premiere sequence.");
    }

    sectionHeader("Reference footage (preview only)");
    ImGui::TextWrapped("%s", s.refPath.empty() ? "None - shows a still from your clip behind the 3D so you can line things up." : baseName(s.refPath).c_str());
    if (ImGui::Button("Choose still...")) {
        std::string p = openFileDialog("Reference image", "Images|*.png;*.jpg;*.jpeg;*.bmp|All files|*.*", g.hwnd);
        if (!p.empty()) { s.refPath = p; g.bgMode = 3; }
    }
    if (!s.refPath.empty()) { ImGui::SameLine(); if (ImGui::Button("Remove")) { s.refPath.clear(); if (g.bgMode == 3) g.bgMode = 0; } }
    if (!s.refPath.empty()) { ImGui::SetNextItemWidth(w); ImGui::SliderFloat("Ref opacity", &s.refOpacity, 0, 1); }
}

static void propertiesPanel() {
    ImGui::Begin("Properties");
    if (Object* o = selectedObject()) objectProps(*o);
    else if (g.sel.kind == Selection::Camera) cameraProps();
    else if (g.sel.kind == Selection::Light && g.sel.id < (int)g.S.lights.size()) lightProps(g.S.lights[g.sel.id]);
    else if (g.sel.kind == Selection::World) worldProps();
    else { ImGui::TextDisabled("Select something in the Scene list\nor click it in the viewer."); }
    ImGui::End();
}

// ============================================================ viewport
static bool rayHitObject(const Object& o, vec3 ro, vec3 rd, float& t) {
    if (!o.geo || !o.visible) return false;
    mat4 inv = glm::inverse(objectMatrix(o, g.frame));
    vec3 lo = vec3(inv * vec4(ro, 1)), ld = vec3(inv * vec4(rd, 0));
    vec3 mn = o.geo->bmin, mx = o.geo->bmax;
    float t0 = -1e30f, t1 = 1e30f;
    for (int a = 0; a < 3; a++) {
        if (std::fabs(ld[a]) < 1e-9f) { if (lo[a] < mn[a] || lo[a] > mx[a]) return false; continue; }
        float ta = (mn[a] - lo[a]) / ld[a], tb = (mx[a] - lo[a]) / ld[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta); t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    if (t1 < 0) return false;
    t = t0 > 0 ? t0 : t1;
    return true;
}

static void frameSelected() {
    Object* o = selectedObject();
    auto& c = g.S.cam;
    vec3 mn, mx;
    if (o && o->geo) {
        mat4 M = objectMatrix(*o, g.frame); mn = vec3(1e30f); mx = vec3(-1e30f);
        for (int i = 0; i < 8; i++) { vec3 p((i & 1) ? o->geo->bmax.x : o->geo->bmin.x, (i & 2) ? o->geo->bmax.y : o->geo->bmin.y, (i & 4) ? o->geo->bmax.z : o->geo->bmin.z);
            vec3 w = vec3(M * vec4(p, 1)); mn = glm::min(mn, w); mx = glm::max(mx, w); }
    } else worldBounds(g.S, g.frame, mn, mx);
    vec3 ctr = (mn + mx) * .5f; float r = std::max(0.2f, glm::length(mx - mn) * .5f);
    float d = r / std::tan(glm::radians(c.fov.eval(g.frame)) * .5f) * 1.1f;
    vec3 dir = c.pos.eval(g.frame) - c.target.eval(g.frame);
    dir = glm::length(dir) > 1e-4f ? glm::normalize(dir) : vec3(0, 0, 1);
    c.target.set(currentFrameInt(), ctr); c.pos.set(currentFrameInt(), ctr + dir * d);
}

static void viewportBody();
static void viewportPanel() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewer", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    viewportBody();
    ImGui::End();
}
static void viewportBody() {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(17, 17, 19, 255), 12);
    float toolbarH = 34;
    ImVec2 area(avail.x - 24, avail.y - toolbarH - 16);
    float aspect = g.S.comp.width / (float)g.S.comp.height;
    ImVec2 sz = area;
    if (sz.x / std::max(1.f, sz.y) > aspect) sz.x = sz.y * aspect; else sz.y = sz.x / aspect;
    sz.x = std::max(sz.x, 16.f); sz.y = std::max(sz.y, 16.f);
    ImVec2 p0(origin.x + (avail.x - sz.x) * .5f, origin.y + toolbarH + (area.y - sz.y) * .5f + 8);
    ImVec2 p1(p0.x + sz.x, p0.y + sz.y);
    // background
    if (g.bgMode == 0) {
        float cs = 12;
        dl->PushClipRect(p0, p1, true);
        for (float y = p0.y; y < p1.y; y += cs) for (float x = p0.x; x < p1.x; x += cs) {
            bool a = ((int)((x - p0.x) / cs) + (int)((y - p0.y) / cs)) & 1;
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cs, y + cs), a ? IM_COL32(52, 52, 56, 255) : IM_COL32(40, 40, 44, 255));
        }
        dl->PopClipRect();
    } else if (g.bgMode == 1) dl->AddRectFilled(p0, p1, IM_COL32(8, 8, 10, 255));
    else if (g.bgMode == 2) dl->AddRectFilled(p0, p1, IM_COL32(200, 200, 204, 255));
    else {
        dl->AddRectFilled(p0, p1, IM_COL32(8, 8, 10, 255));
        if (g.refLoaded != g.S.refPath) {
            g.refLoaded = g.S.refPath; g.refTex = nullptr;
            ImageData im; if (!g.S.refPath.empty() && loadImageFile(g.S.refPath, im) && !im.rgba.empty()) g.refTex = uploadTexture(im, false, true, true);
        }
        if (g.refTex) dl->AddImage((ImTextureID)(intptr_t)g.refTex->id, p0, p1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, (int)(g.S.refOpacity * 255)));
    }
    // render
    int scaleDiv = 1 << g.previewScale;
    float dpi = 1.f;
    int rw = std::max(16, (int)(sz.x * dpi / scaleDiv)), rh = std::max(16, (int)(sz.y * dpi / scaleDiv));
    g.viewRT.ensure(rw, rh, 4);
    ViewOptions vo; vo.preview = true; vo.grid = g.showGrid; vo.selected = g.sel.kind == Selection::Obj ? g.sel.id : -1;
    if (g.setupMode) setLetterFrames(g.S, g.previewLetters);
    unsigned tex = g.R.render(g.S, g.frame, g.viewRT, vo);
    // ImGui renders with straight-alpha blending; our texture is premultiplied, so use a callback to switch blend mode
    dl->AddCallback([](const ImDrawList*, const ImDrawCmd*) { glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); }, nullptr);
    dl->AddImage((ImTextureID)(intptr_t)tex, p0, p1, ImVec2(0, 1), ImVec2(1, 0));
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    dl->AddRect(ImVec2(p0.x - 1, p0.y - 1), ImVec2(p1.x + 1, p1.y + 1), IM_COL32(70, 70, 76, 255));

    // interaction area
    ImGui::SetCursorScreenPos(p0);
    // When the mouse is over the move/rotate/scale gizmo, don't put a button under it,
    // otherwise the button swallows the click and the gizmo never activates.
    static bool gizmoHot = false;
    if (gizmoHot && !ImGui::IsMouseDown(1) && !ImGui::IsMouseDown(2)) ImGui::Dummy(sz);
    else ImGui::InvisibleButton("view", sz, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    CamEval cam = evalCamera(g.S, g.frame, aspect);
    // gizmo
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(dl);
    ImGuizmo::SetRect(p0.x, p0.y, sz.x, sz.y);
    bool gizmoUsing = false;
    Object* so = selectedObject();
    if (!(so && so->geo && so->visible)) gizmoHot = false;
    if (so && so->geo && so->visible) {
        mat4 M = objectMatrix(*so, g.frame);
        ImGuizmo::OPERATION op = g.gizmoOp == 0 ? ImGuizmo::TRANSLATE : g.gizmoOp == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
        ImGuizmo::MODE mode = (g.gizmoOp == 2 || !g.gizmoWorld) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
        ImGuizmo::PushID(so->id);
        if (ImGuizmo::Manipulate(glm::value_ptr(cam.view), glm::value_ptr(cam.proj), op, mode, glm::value_ptr(M))) {
            float t[3], r[3], s[3];
            ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(M), t, r, s);
            int fr = currentFrameInt();
            if (g.gizmoOp == 0) so->pos.set(fr, vec3(t[0], t[1], t[2]));
            else if (g.gizmoOp == 2) so->scl.set(fr, vec3(s[0], s[1], s[2]));
            else {
                mat4 R = M; for (int i = 0; i < 3; i++) R[i] = vec4(glm::normalize(vec3(M[i])), 0); R[3] = vec4(0, 0, 0, 1);
                float y, x, z; glm::extractEulerAngleYXZ(R, y, x, z);
                vec3 nr = glm::degrees(vec3(x, y, z)), old = so->rot.eval(g.frame);
                for (int a = 0; a < 3; a++) nr[a] += 360.f * std::round((old[a] - nr[a]) / 360.f);
                so->rot.set(fr, nr);
            }
        }
        ImGuizmo::PopID();
        gizmoUsing = ImGuizmo::IsUsing() || ImGuizmo::IsOver();
        gizmoHot = gizmoUsing;
    }
    // camera navigation
    ImGuiIO& io = ImGui::GetIO();
    auto& c = g.S.cam;
    int fr = currentFrameInt();
    vec3 cp = c.pos.eval(g.frame), ct = c.target.eval(g.frame);
    bool altLmb = io.KeyAlt && ImGui::IsMouseDragging(0);
    if (active && (ImGui::IsMouseDragging(1) || altLmb) && !gizmoUsing) {
        ImVec2 d = io.MouseDelta;
        vec3 off = cp - ct; float r = glm::length(off);
        float yaw = std::atan2(off.x, off.z) - d.x * 0.008f;
        float pitch = std::asin(std::clamp(off.y / std::max(r, 1e-5f), -1.f, 1.f)) + d.y * 0.008f;
        pitch = std::clamp(pitch, -1.5f, 1.5f);
        vec3 no(std::sin(yaw) * std::cos(pitch) * r, std::sin(pitch) * r, std::cos(yaw) * std::cos(pitch) * r);
        c.pos.set(fr, ct + no);
    } else if (active && ImGui::IsMouseDragging(2)) {
        ImVec2 d = io.MouseDelta;
        float dist = glm::length(cp - ct);
        float k = dist * std::tan(glm::radians(c.fov.eval(g.frame)) * .5f) * 2.f / sz.y;
        vec3 right = vec3(glm::inverse(cam.view)[0]), up = vec3(glm::inverse(cam.view)[1]);
        vec3 mv = (-right * d.x + up * d.y) * k;
        c.pos.set(fr, cp + mv); c.target.set(fr, ct + mv);
    }
    if (hovered && io.MouseWheel != 0) {
        float f = std::pow(0.88f, io.MouseWheel);
        vec3 off = (cp - ct) * f;
        if (glm::length(off) > 0.05f) c.pos.set(fr, ct + off);
    }
    // click to select
    if (hovered && ImGui::IsMouseReleased(0) && !gizmoUsing && ImGui::GetMouseDragDelta(0).x == 0 && ImGui::GetMouseDragDelta(0).y == 0 && !io.KeyAlt) {
        ImVec2 m = io.MousePos;
        float nx = (m.x - p0.x) / sz.x * 2 - 1, ny = 1 - (m.y - p0.y) / sz.y * 2;
        mat4 inv = glm::inverse(cam.proj * cam.view);
        vec4 a = inv * vec4(nx, ny, -1, 1), b = inv * vec4(nx, ny, 1, 1);
        vec3 ro = vec3(a) / a.w, rd = glm::normalize(vec3(b) / b.w - ro);
        float best = 1e30f; int bestId = -1;
        for (auto& o : g.S.objects) { float t; if (rayHitObject(o, ro, rd, t) && t < best) { best = t; bestId = o.id; } }
        if (bestId >= 0) g.sel = {Selection::Obj, bestId};
        else if (g.sel.kind == Selection::Obj) g.sel.kind = Selection::None;
    }
    // toolbar
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 10, origin.y + 6));
    auto tb = [&](const char* label, bool on, const char* tip) {
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.23f, 0.21f, 0.38f, 1));
        bool r = ImGui::Button(label);
        if (on) ImGui::PopStyleColor();
        tooltip(tip); ImGui::SameLine(0, 4);
        return r;
    };
    if (tb("Move", g.gizmoOp == 0, "Move (W)")) g.gizmoOp = 0;
    if (tb("Rotate", g.gizmoOp == 1, "Rotate (E)")) g.gizmoOp = 1;
    if (tb("Scale", g.gizmoOp == 2, "Scale (R)")) g.gizmoOp = 2;
    ImGui::SameLine(0, 10);
    if (tb(g.gizmoWorld ? "World" : "Local", false, "Gizmo axes: world or object")) g.gizmoWorld = !g.gizmoWorld;
    ImGui::SameLine(0, 10);
    if (tb("Frame", false, "Frame the selection (F)")) frameSelected();
    if (tb(g.showGrid ? "Grid" : "Grid", g.showGrid, "Show floor grid (preview only)")) g.showGrid = !g.showGrid;
    ImGui::SameLine(0, 10);
    const char* bgs[] = {"Checker", "Black", "White", "Reference"};
    ImGui::SetNextItemWidth(110);
    ImGui::Combo("##bg", &g.bgMode, bgs, 4); tooltip("Viewer background (never exported - your export stays transparent)");
    ImGui::SameLine(0, 6);
    const char* qs[] = {"Full", "Half", "Quarter"};
    ImGui::SetNextItemWidth(90);
    ImGui::Combo("##q", &g.previewScale, qs, 3); tooltip("Preview resolution - lower is faster");
    ImGui::SameLine(0, 10);
    ImGui::TextDisabled("%d×%d  %g fps", g.S.comp.width, g.S.comp.height, g.S.comp.fps);
}

// ============================================================ timeline
static std::string timecode(int f) {
    int fp = fps(); int s = f / fp, ff = f % fp;
    char b[32]; snprintf(b, sizeof b, "%02d:%02d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60, ff); return b;
}
struct TRow { std::string label; std::vector<Chan> chans; int indent; Selection selOnClick; bool header; };

static bool keySelected(const Chan& c, int f) { for (auto& k : g.keySel) if (k.c.ptr() == c.ptr() && k.frame == f) return true; return false; }

static void applyEase(int mode) {  // 0 easy ease, 1 linear, 2 hold, 3 ease in only, 4 ease out only
    for (auto& k : g.keySel) {
        auto set = [&](KeyEase& e) {
            if (mode == 0) { e.in = e.out = true; e.hold = false; }
            if (mode == 1) { e.in = e.out = false; e.hold = false; }
            if (mode == 2) e.hold = true;
            if (mode == 3) { e.in = true; e.out = false; e.hold = false; }
            if (mode == 4) { e.in = false; e.out = true; e.hold = false; }
        };
        if (k.c.v) { int i = k.c.v->keyIndexAt(k.frame); if (i >= 0) set(k.c.v->keys[i].ease); }
        if (k.c.f) { int i = k.c.f->keyIndexAt(k.frame); if (i >= 0) set(k.c.f->keys[i].ease); }
    }
}
static void deleteSelectedKeys() {
    for (auto& k : g.keySel) { if (k.c.v) k.c.v->removeKey(k.frame); if (k.c.f) k.c.f->removeKey(k.frame); }
    g.keySel.clear();
}
static void jumpKey(int dir) {
    std::vector<int> fs;
    auto addAll = [&](Object& o) { Chan cs[4] = {{&o.pos}, {&o.rot}, {&o.scl}, {nullptr, &o.opacity}}; for (auto& c : cs) c.frames(fs); };
    if (Object* o = selectedObject()) addAll(*o);
    else { for (auto& o : g.S.objects) addAll(o); Chan cs[4] = {{&g.S.cam.pos}, {&g.S.cam.target}, {nullptr, &g.S.cam.fov}, {nullptr, &g.S.cam.roll}}; for (auto& c : cs) c.frames(fs); }
    int cur = currentFrameInt(), best = dir > 0 ? INT32_MAX : INT32_MIN;
    for (int f : fs) { if (dir > 0 && f > cur && f < best) best = f; if (dir < 0 && f < cur && f > best) best = f; }
    if (best != INT32_MAX && best != INT32_MIN) g.frame = (float)best;
}
static void setKeysAllTransform() {
    Object* o = selectedObject(); if (!o) return;
    int f = currentFrameInt();
    o->pos.addKey(f, o->pos.eval(g.frame)); o->rot.addKey(f, o->rot.eval(g.frame)); o->scl.addKey(f, o->scl.eval(g.frame));
    notify("Keyframes set for position, rotation and scale");
}

static void drawPlayIcon(ImDrawList* dl, ImVec2 c, bool playing, ImU32 col) {
    if (playing) { dl->AddRectFilled(ImVec2(c.x - 5, c.y - 6), ImVec2(c.x - 1.5f, c.y + 6), col, 1); dl->AddRectFilled(ImVec2(c.x + 1.5f, c.y - 6), ImVec2(c.x + 5, c.y + 6), col, 1); }
    else dl->AddTriangleFilled(ImVec2(c.x - 4, c.y - 7), ImVec2(c.x - 4, c.y + 7), ImVec2(c.x + 7, c.y), col);
}
static bool iconButton(const char* id, int kind, const char* tip) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool r = ImGui::Button(id, ImVec2(32, 26));
    tooltip(tip);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + 16, p.y + 13); ImU32 col = IM_COL32(225, 222, 216, 255);
    switch (kind) {
    case 0: dl->AddRectFilled(ImVec2(c.x - 6, c.y - 6), ImVec2(c.x - 4, c.y + 6), col); dl->AddTriangleFilled(ImVec2(c.x + 6, c.y - 6), ImVec2(c.x + 6, c.y + 6), ImVec2(c.x - 3, c.y), col); break;
    case 1: drawDiamond(dl, ImVec2(c.x + 3, c.y), 4, col, true); dl->AddTriangleFilled(ImVec2(c.x - 2, c.y - 5), ImVec2(c.x - 2, c.y + 5), ImVec2(c.x - 8, c.y), col); break;
    case 2: drawPlayIcon(dl, c, g.playing, GOLD32); break;
    case 3: drawDiamond(dl, ImVec2(c.x - 3, c.y), 4, col, true); dl->AddTriangleFilled(ImVec2(c.x + 2, c.y - 5), ImVec2(c.x + 2, c.y + 5), ImVec2(c.x + 8, c.y), col); break;
    case 4: dl->AddRectFilled(ImVec2(c.x + 4, c.y - 6), ImVec2(c.x + 6, c.y + 6), col); dl->AddTriangleFilled(ImVec2(c.x - 6, c.y - 6), ImVec2(c.x - 6, c.y + 6), ImVec2(c.x + 3, c.y), col); break;
    }
    return r;
}

static void togglePlay() {
    g.playing = !g.playing;
    if (g.playing) { if (currentFrameInt() >= g.S.comp.frames - 1) g.frame = 0; g.playClock = ImGui::GetTime(); g.playFrame0 = g.frame; }
    else g.frame = (float)currentFrameInt();
}

static void timelinePanel() {
    ImGui::Begin("Timeline", nullptr, ImGuiWindowFlags_NoScrollbar);
    int F = std::max(1, g.S.comp.frames);
    // transport
    if (iconButton("##start", 0, "Go to start (Home)")) g.frame = 0;
    ImGui::SameLine(0, 2); if (iconButton("##pk", 1, "Previous keyframe (,)")) jumpKey(-1);
    ImGui::SameLine(0, 2); if (iconButton("##play", 2, "Play / pause (Space)")) togglePlay();
    ImGui::SameLine(0, 2); if (iconButton("##nk", 3, "Next keyframe (.)")) jumpKey(1);
    ImGui::SameLine(0, 2); if (iconButton("##end", 4, "Go to end (End)")) g.frame = (float)(F - 1);
    ImGui::SameLine(0, 12);
    ImGui::PushFont(g.fBold, 0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(GOLD, "%s", timecode(currentFrameInt()).c_str());
    ImGui::PopFont();
    ImGui::SameLine(0, 8);
    int cf = currentFrameInt();
    ImGui::SetNextItemWidth(70);
    if (ImGui::InputInt("##frame", &cf, 0)) g.frame = (float)std::clamp(cf, 0, F - 1);
    ImGui::SameLine(0, 4); ImGui::TextDisabled("/ %d", F - 1);
    ImGui::SameLine(0, 14); ImGui::Checkbox("Loop", &g.loop);
    ImGui::SameLine(0, 14);
    if (ImGui::Button("Set key (K)")) setKeysAllTransform();
    tooltip("Keyframe the selected object's position, rotation and scale at this frame");
    if (!g.keySel.empty()) {
        ImGui::SameLine(0, 14); ImGui::TextDisabled("%d key%s:", (int)g.keySel.size(), g.keySel.size() == 1 ? "" : "s");
        ImGui::SameLine(); if (ImGui::SmallButton("Easy ease")) applyEase(0);
        ImGui::SameLine(); if (ImGui::SmallButton("Linear")) applyEase(1);
        ImGui::SameLine(); if (ImGui::SmallButton("Hold")) applyEase(2);
        ImGui::SameLine(); if (ImGui::SmallButton("Delete")) deleteSelectedKeys();
    }
    // rows
    std::vector<TRow> rows;
    for (auto& o : g.S.objects) {
        TRow r; r.label = o.name; r.indent = 0; r.selOnClick = {Selection::Obj, o.id}; r.header = true;
        r.chans = {{&o.pos}, {&o.rot}, {&o.scl}, {nullptr, &o.opacity}};
        rows.push_back(r);
        if (g.sel.kind == Selection::Obj && g.sel.id == o.id) {
            const char* n[4] = {"Position", "Rotation", "Scale", "Opacity"};
            for (int i = 0; i < 4; i++) { TRow s; s.label = n[i]; s.indent = 1; s.chans = {r.chans[i]}; s.selOnClick = r.selOnClick; s.header = false; rows.push_back(s); }
        }
    }
    {
        TRow r; r.label = "Camera"; r.indent = 0; r.selOnClick = {Selection::Camera, 0}; r.header = true;
        r.chans = {{&g.S.cam.pos}, {&g.S.cam.target}, {nullptr, &g.S.cam.fov}, {nullptr, &g.S.cam.roll}};
        rows.push_back(r);
        if (g.sel.kind == Selection::Camera) {
            const char* n[4] = {"Position", "Look at", "Lens", "Roll"};
            for (int i = 0; i < 4; i++) { TRow s; s.label = n[i]; s.indent = 1; s.chans = {r.chans[i]}; s.selOnClick = r.selOnClick; s.header = false; rows.push_back(s); }
        }
    }
    for (int i = 0; i < (int)g.S.lights.size(); i++) {
        TRow r; r.label = g.S.lights[i].name; r.indent = 0; r.selOnClick = {Selection::Light, i}; r.header = true;
        r.chans = {{&g.S.lights[i].pos}, {nullptr, &g.S.lights[i].intensity}};
        bool any = false; for (auto& c : r.chans) any |= c.animated();
        if (any || (g.sel.kind == Selection::Light && g.sel.id == i)) rows.push_back(r);
    }
    { TRow r; r.label = "Environment rotate"; r.indent = 0; r.selOnClick = {Selection::World, 0}; r.header = true; r.chans = {{nullptr, &g.S.env.rotation}};
      if (g.S.env.rotation.animated) rows.push_back(r); }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 o = ImGui::GetCursorScreenPos();
    float nameW = 200, rulerH = 24, rowH = 24;
    float trackX0 = o.x + nameW + 10, trackX1 = o.x + avail.x - 14;
    float tw = std::max(50.f, trackX1 - trackX0);
    auto fx = [&](float f) { return trackX0 + (F > 1 ? f / (F - 1) : 0) * tw; };
    auto xf = [&](float x) { return std::clamp((x - trackX0) / tw * (F - 1), 0.f, (float)(F - 1)); };
    // ruler
    dl->AddRectFilled(ImVec2(o.x, o.y), ImVec2(o.x + avail.x, o.y + rulerH), IM_COL32(30, 30, 34, 255), 4);
    int step = 1; float pxPer = tw / std::max(1, F - 1);
    for (int s : {1, 2, 5, 10, 25, 50, 100, 250, 500, 1000}) { step = s; if (s * pxPer >= 48) break; }
    int fpsI = fps();
    if (step > 2 && step < fpsI && fpsI * pxPer >= 48) step = fpsI / 2;
    for (int f = 0; f < F; f += step) {
        float x = fx((float)f);
        dl->AddLine(ImVec2(x, o.y + rulerH - 7), ImVec2(x, o.y + rulerH), IM_COL32(110, 108, 104, 255));
        char b[16]; if (f % fpsI == 0) snprintf(b, sizeof b, "%ds", f / fpsI); else snprintf(b, sizeof b, "%d", f);
        dl->AddText(ImVec2(x + 3, o.y + 3), IM_COL32(150, 148, 144, 255), b);
    }
    ImGui::SetCursorScreenPos(ImVec2(trackX0 - 6, o.y));
    ImGui::InvisibleButton("ruler", ImVec2(tw + 12, rulerH));
    if (ImGui::IsItemActive()) { g.frame = std::round(xf(ImGui::GetIO().MousePos.x)); g.playing = false; }
    // rows area
    ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + rulerH + 2));
    ImGui::BeginChild("rows", ImVec2(avail.x, avail.y - rulerH - 2), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImDrawList* rdl = ImGui::GetWindowDrawList();
    ImVec2 ro = ImGui::GetCursorScreenPos();
    float yy = ro.y;
    struct Hit { Chan c; int frame; ImVec2 pos; std::vector<Chan> all; };
    std::vector<Hit> hits;
    ImVec2 mouse = ImGui::GetIO().MousePos;
    int ri = 0;
    for (auto& r : rows) {
        bool isSel = g.sel.kind == r.selOnClick.kind && g.sel.id == r.selOnClick.id && r.header;
        ImU32 bg = (ri++ % 2) ? IM_COL32(26, 26, 30, 255) : IM_COL32(22, 22, 25, 255);
        if (isSel) bg = IM_COL32(44, 40, 70, 255);
        rdl->AddRectFilled(ImVec2(ro.x, yy), ImVec2(ro.x + avail.x, yy + rowH), bg);
        ImGui::SetCursorScreenPos(ImVec2(ro.x, yy));
        ImGui::PushID(ri);
        if (ImGui::InvisibleButton("lbl", ImVec2(nameW, rowH))) g.sel = r.selOnClick;
        ImGui::PopID();
        ImU32 tc = r.header ? IM_COL32(228, 226, 222, 255) : IM_COL32(170, 168, 164, 255);
        rdl->AddText(ImVec2(ro.x + 10 + r.indent * 18, yy + rowH * .5f - ImGui::GetFontSize() * .5f), tc, r.label.c_str());
        // keys
        std::map<int, std::vector<Chan>> byFrame;
        for (auto& c : r.chans) { std::vector<int> fs; c.frames(fs); for (int f : fs) byFrame[f].push_back(c); }
        if (byFrame.size() > 1) {  // connecting bar
            float x0 = fx((float)byFrame.begin()->first), x1 = fx((float)byFrame.rbegin()->first);
            rdl->AddRectFilled(ImVec2(x0, yy + rowH * .5f - 1.5f), ImVec2(x1, yy + rowH * .5f + 1.5f), r.header ? IM_COL32(74, 66, 128, 255) : IM_COL32(60, 58, 72, 255));
        }
        for (auto& kv : byFrame) {
            ImVec2 c(fx((float)kv.first), yy + rowH * .5f);
            bool sel = true; for (auto& ch : kv.second) sel &= keySelected(ch, kv.first);
            float rr = r.header ? 6.f : 5.f;
            bool hold = false;
            for (auto& ch : kv.second) { if (ch.v) { int i = ch.v->keyIndexAt(kv.first); if (i >= 0 && ch.v->keys[i].ease.hold) hold = true; }
                                         if (ch.f) { int i = ch.f->keyIndexAt(kv.first); if (i >= 0 && ch.f->keys[i].ease.hold) hold = true; } }
            if (hold) rdl->AddRectFilled(ImVec2(c.x - rr * .8f, c.y - rr * .8f), ImVec2(c.x + rr * .8f, c.y + rr * .8f), sel ? KEYSEL32 : KEY32);
            else drawDiamond(rdl, c, rr, sel ? KEYSEL32 : KEY32, true);
            Hit h; h.frame = kv.first; h.pos = c; h.all = kv.second; hits.push_back(h);
        }
        yy += rowH;
    }
    ImGui::SetCursorScreenPos(ImVec2(ro.x, yy));
    ImGui::Dummy(ImVec2(1, 4));
    // playhead
    float px = fx(g.frame);
    ImVec2 wmin = ImGui::GetWindowPos();
    dl->AddLine(ImVec2(px, o.y), ImVec2(px, o.y + avail.y), GOLD32, 1.6f);
    dl->AddTriangleFilled(ImVec2(px - 6, o.y), ImVec2(px + 6, o.y), ImVec2(px, o.y + 8), GOLD32);
    (void)wmin;
    // key interaction
    ImGuiIO& io = ImGui::GetIO();
    bool overTrack = ImGui::IsWindowHovered() && mouse.x >= trackX0 - 8 && mouse.x <= trackX1 + 8;
    const Hit* hov = nullptr;
    for (auto& h : hits) if (std::fabs(mouse.x - h.pos.x) < 7 && std::fabs(mouse.y - h.pos.y) < 9) hov = &h;
    if (overTrack && ImGui::IsMouseClicked(0)) {
        if (hov) {
            bool already = true; for (auto& c : hov->all) already &= keySelected(c, hov->frame);
            if (!io.KeyCtrl && !io.KeyShift && !already) g.keySel.clear();
            if (io.KeyCtrl && already) {
                g.keySel.erase(std::remove_if(g.keySel.begin(), g.keySel.end(), [&](const KeyRef& k) { for (auto& c : hov->all) if (k.c.ptr() == c.ptr() && k.frame == hov->frame) return true; return false; }), g.keySel.end());
            } else for (auto& c : hov->all) if (!keySelected(c, hov->frame)) g.keySel.push_back({c, hov->frame});
            // start drag
            g.tlDragging = true; g.tlDragX0 = mouse.x; g.tlDragDelta = 0; g.tlSnap.clear(); g.tlSelOrig = g.keySel;
            std::set<void*> seen;
            for (auto& k : g.keySel) if (!seen.count(k.c.ptr())) { seen.insert(k.c.ptr()); App::Snap s; s.c = k.c; if (k.c.v) s.kv = k.c.v->keys; if (k.c.f) s.kf = k.c.f->keys; g.tlSnap.push_back(s); }
        } else if (mouse.y > o.y + rulerH) {
            if (!io.KeyCtrl) g.keySel.clear();
            g.frame = std::round(xf(mouse.x)); g.playing = false; g.scrubbing = true;
        }
    }
    if (g.scrubbing) { if (ImGui::IsMouseDown(0)) g.frame = std::round(xf(mouse.x)); else g.scrubbing = false; }
    if (g.tlDragging) {
        if (ImGui::IsMouseDown(0)) {
            int delta = (int)std::round((mouse.x - g.tlDragX0) / std::max(0.001f, pxPer));
            if (delta != g.tlDragDelta) {
                g.tlDragDelta = delta;
                for (auto& s : g.tlSnap) {
                    auto moveKeys = [&](auto& keys, auto& orig) {
                        keys = orig;
                        std::set<int> moved;
                        for (auto& k : g.tlSelOrig) if (k.c.ptr() == s.c.ptr()) moved.insert(k.frame);
                        std::vector<std::remove_reference_t<decltype(keys[0])>> out, mv;
                        for (auto& k : keys) { if (moved.count(k.frame)) { auto c = k; c.frame = std::max(0, k.frame + delta); mv.push_back(c); } else out.push_back(k); }
                        for (auto& m : mv) { out.erase(std::remove_if(out.begin(), out.end(), [&](auto& o2) { return o2.frame == m.frame; }), out.end()); out.push_back(m); }
                        keys = out;
                    };
                    if (s.c.v) { moveKeys(s.c.v->keys, s.kv); s.c.v->sortKeys(); }
                    if (s.c.f) { moveKeys(s.c.f->keys, s.kf); s.c.f->sortKeys(); }
                }
                g.keySel = g.tlSelOrig; for (auto& k : g.keySel) k.frame = std::max(0, k.frame + delta);
            }
            if (delta != 0) ImGui::SetTooltip("%+d frames", delta);
        } else g.tlDragging = false;
    }
    if (hov && ImGui::IsMouseDoubleClicked(0)) g.frame = (float)hov->frame;
    if (overTrack && ImGui::IsMouseClicked(1)) {
        if (hov) { bool already = true; for (auto& c : hov->all) already &= keySelected(c, hov->frame);
                   if (!already) { g.keySel.clear(); for (auto& c : hov->all) g.keySel.push_back({c, hov->frame}); } }
        if (!g.keySel.empty()) ImGui::OpenPopup("keymenu");
    }
    if (ImGui::BeginPopup("keymenu")) {
        if (ImGui::MenuItem("Easy ease", "F9")) applyEase(0);
        if (ImGui::MenuItem("Ease in only")) applyEase(3);
        if (ImGui::MenuItem("Ease out only")) applyEase(4);
        if (ImGui::MenuItem("Linear")) applyEase(1);
        if (ImGui::MenuItem("Hold (jump)")) applyEase(2);
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del")) deleteSelectedKeys();
        ImGui::EndPopup();
    }
    if (hov && !g.tlDragging) ImGui::SetTooltip("Frame %d  -  drag to move, right-click for easing", hov->frame);
    ImGui::EndChild();
    ImGui::End();
}

// ============================================================ Scene Setup (pop-up from Premiere)
static void setupFinish(bool apply) {
    if (apply) {
        Scene copy = g.S; copy.refPath.clear();
        std::string j = sceneToJson(copy);
        FILE* f = _wfopen(widen(g.setupOut).c_str(), L"wb");
        if (!f) { notify("Couldn't hand the scene back to Premiere."); return; }
        fwrite(j.data(), 1, j.size(), f); fclose(f);
    }
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext(); glfwTerminate();
    ExitProcess(apply ? 0 : 1);
}
static void previewPanel() {
    ImGui::Begin("Preview", nullptr, ImGuiWindowFlags_NoScrollbar);
    bool anyLetters = false; for (auto& o : g.S.objects) anyLetters |= o.letters.enabled;
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Letters Built");
    ImGui::SameLine();
    ImGui::BeginDisabled(!anyLetters);
    ImGui::SetNextItemWidth(std::min(420.f, ImGui::GetContentRegionAvail().x * 0.45f));
    ImGui::SliderFloat("##lb", &g.previewLetters, 0, 100, "%.0f%%");
    ImGui::EndDisabled();
    if (!anyLetters) { ImGui::SameLine(); ImGui::TextDisabled("(turn on letter animation in a text's Animate tab)"); }
    ImGui::SameLine(0, 30);
    ImGui::TextDisabled("Keyframe everything in Premiere's Effect Controls after you click OK.");
    ImGui::End();
}

// ============================================================ export
static void startExport() {
    g.exError.clear();
    std::string def = g.projectPath.empty() ? "Element Genie render" : stem(g.projectPath);
    std::string p = g.exFormat == 0 ? saveFileDialog("Export video with transparency", "QuickTime movie with alpha (Animation codec)|*.mov", "mov", def + ".mov", g.hwnd)
                                    : saveFileDialog("Export PNG sequence (first file name)", "PNG image sequence|*.png", "png", def + ".png", g.hwnd);
    if (p.empty()) return;
    int div = g.exScale == 1 ? 2 : 1;
    int W = g.S.comp.width / div, H = g.S.comp.height / div;
    W &= ~1; H &= ~1;
    if (!g.ex.begin(p, g.exFormat, W, H, g.S.comp.fps, g.exEnd - g.exStart + 1, g.exError)) return;
    g.exPath = p; g.exporting = true; g.exportFinished = false; g.exCur = g.exStart; g.exT0 = ImGui::GetTime(); g.playing = false;
    int samples = (W * H > 4000000) ? 4 : 8;
    g.exportRT.ensure(W, H, samples);
}
static void exportStep() {
    if (!g.exporting) return;
    double tEnd = glfwGetTime() + 0.05;
    do {
        ViewOptions vo; vo.preview = false;
        g.R.render(g.S, (float)g.exCur, g.exportRT, vo);
        std::vector<uint8_t> px((size_t)g.exportRT.w * g.exportRT.h * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, g.exportRT.outFbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, g.exportRT.w, g.exportRT.h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!g.ex.addFrame(px.data(), g.exError)) { g.ex.abort(); g.exporting = false; return; }
        g.exCur++;
        if (g.exCur > g.exEnd) {
            g.ex.finish(g.exError);
            g.exporting = false; g.exportFinished = true;
            return;
        }
    } while (glfwGetTime() < tEnd);
}
static void exportDialog() {
    if (g.exportDialog) { ImGui::OpenPopup("Export"); g.exportDialog = false; g.exStart = 0; g.exEnd = g.S.comp.frames - 1; g.exportFinished = false; }
    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Export", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        if (g.exporting) {
            int done = g.exCur - g.exStart, total = g.exEnd - g.exStart + 1;
            float frac = done / (float)std::max(1, total);
            ImVec2 pv(480, 480 * g.exportRT.h / (float)std::max(1, g.exportRT.w));
            if (pv.y > 270) { pv.x *= 270 / pv.y; pv.y = 270; }
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            for (float y = 0; y < pv.y; y += 10) for (float x = 0; x < pv.x; x += 10)
                dl->AddRectFilled(ImVec2(p.x + x, p.y + y), ImVec2(p.x + std::min(x + 10, pv.x), p.y + std::min(y + 10, pv.y)), (((int)(x / 10) + (int)(y / 10)) & 1) ? IM_COL32(52, 52, 56, 255) : IM_COL32(40, 40, 44, 255));
            dl->AddCallback([](const ImDrawList*, const ImDrawCmd*) { glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); }, nullptr);
            dl->AddImage((ImTextureID)(intptr_t)g.exportRT.outTex, p, ImVec2(p.x + pv.x, p.y + pv.y), ImVec2(0, 1), ImVec2(1, 0));
            dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
            ImGui::Dummy(pv);
            double el = ImGui::GetTime() - g.exT0;
            double eta = done > 0 ? el / done * (total - done) : 0;
            char b[96]; snprintf(b, sizeof b, "Frame %d of %d  -  about %ds left", done, total, (int)eta);
            ImGui::ProgressBar(frac, ImVec2(-1, 0), b);
            if (ImGui::Button("Cancel")) { g.ex.abort(); g.exporting = false; notify("Export cancelled"); ImGui::CloseCurrentPopup(); }
        } else if (g.exportFinished) {
            ImGui::PushFont(g.fBold, 0); ImGui::TextColored(GOLD, "Done!"); ImGui::PopFont();
            ImGui::TextWrapped("%s", g.exPath.c_str());
            if (!g.exError.empty()) ImGui::TextColored(ImVec4(1, .5f, .4f, 1), "%s", g.exError.c_str());
            ImGui::Spacing();
            ImGui::TextWrapped(g.exFormat == 0 ? "In Premiere: File > Import the .mov and put it on a track above your footage. The background is already transparent."
                                               : "In Premiere: File > Import, pick the first PNG and tick \"Image Sequence\". Put it on a track above your footage.");
            ImGui::Spacing();
            if (ImGui::Button("Show in folder")) revealInExplorer(g.exFormat == 0 ? g.exPath : g.exPath);
            ImGui::SameLine();
            if (goldButton("Close")) { g.exportFinished = false; ImGui::CloseCurrentPopup(); }
        } else {
            ImGui::TextDisabled("Renders with a transparent background, ready to drop on a track above your footage.");
            ImGui::Spacing();
            ImGui::RadioButton("QuickTime .mov with alpha (Animation codec) - recommended", &g.exFormat, 0);
            ImGui::RadioButton("PNG image sequence", &g.exFormat, 1);
            ImGui::Spacing();
            ImGui::RadioButton("Full size", &g.exScale, 0); ImGui::SameLine(); ImGui::RadioButton("Half size (quick test)", &g.exScale, 1);
            ImGui::Spacing();
            ImGui::SetNextItemWidth(220);
            int rng[2] = {g.exStart, g.exEnd};
            if (ImGui::DragInt2("Frames", rng, 0.3f, 0, g.S.comp.frames - 1)) { g.exStart = std::clamp(rng[0], 0, g.S.comp.frames - 1); g.exEnd = std::clamp(rng[1], g.exStart, g.S.comp.frames - 1); }
            ImGui::SameLine(); ImGui::TextDisabled("%s - %s", timecode(g.exStart).c_str(), timecode(g.exEnd).c_str());
            ImGui::Checkbox("Motion blur", &g.S.motionBlur);
            ImGui::Spacing();
            int div = g.exScale == 1 ? 2 : 1;
            ImGui::TextDisabled("%d × %d  •  %g fps  •  %d frames", g.S.comp.width / div, g.S.comp.height / div, g.S.comp.fps, g.exEnd - g.exStart + 1);
            if (!g.exError.empty()) ImGui::TextColored(ImVec4(1, .5f, .4f, 1), "%s", g.exError.c_str());
            ImGui::Spacing();
            if (goldButton("Export...", ImVec2(140, 0))) startExport();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(90, 0))) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ============================================================ menus, shortcuts, layout
static void menuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (g.logoTex) {
            float h = ImGui::GetFrameHeight() - 4;
            ImGui::Image((ImTextureID)(intptr_t)g.logoTex, ImVec2(h * g.logoW / (float)g.logoH, h));
            ImGui::SameLine(0, 6);
        }
        ImGui::PushFont(g.fBold, 0); ImGui::TextColored(GOLD, "Element Genie"); ImGui::PopFont();
        ImGui::SameLine(0, 18);
        if (ImGui::BeginMenu("File")) {
            if (g.setupMode) {
                if (ImGui::MenuItem("Load scene preset...")) { std::string keepRef = g.S.refPath; openProjectDialog(); if (g.S.refPath.empty()) g.S.refPath = keepRef; }
                if (ImGui::MenuItem("Save scene as preset...")) saveProject(true);
                tooltip("Re-use this design on other clips or in other projects.");
            } else {
            if (ImGui::MenuItem("New", "Ctrl+N")) newProject();
            if (ImGui::MenuItem("Open...", "Ctrl+O")) openProjectDialog();
            if (ImGui::MenuItem("Save", "Ctrl+S")) saveProject(false);
            if (ImGui::MenuItem("Save as...", "Ctrl+Shift+S")) saveProject(true);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Import 3D model...", "Ctrl+I")) addModelDialog();
            if (ImGui::MenuItem("Import logo...")) addLogoDialog();
            ImGui::Separator();
            if (!g.setupMode) { if (ImGui::MenuItem("Export for Premiere...", "Ctrl+M")) g.exportDialog = true; ImGui::Separator(); }
            if (ImGui::MenuItem(g.setupMode ? "Close" : "Quit")) g.wantClose = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !g.undoStack.empty() || sceneToJson(g.S) != g.lastJson)) doUndo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !g.redoStack.empty())) doRedo();
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, selectedObject() != nullptr)) duplicateSelected();
            if (ImGui::MenuItem("Delete", "Del", false, g.sel.kind == Selection::Obj || g.sel.kind == Selection::Light)) deleteSelected();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Add")) {
            if (ImGui::MenuItem("3D text", "T")) addText();
            if (ImGui::MenuItem("Logo (PNG / SVG)...")) addLogoDialog();
            if (ImGui::MenuItem("3D model...")) addModelDialog();
            if (ImGui::BeginMenu("Shape")) {
                if (ImGui::MenuItem("Cube")) addPrimitive(Prim::Cube);
                if (ImGui::MenuItem("Rounded cube")) addPrimitive(Prim::RoundCube);
                if (ImGui::MenuItem("Sphere")) addPrimitive(Prim::Sphere);
                if (ImGui::MenuItem("Cylinder")) addPrimitive(Prim::Cylinder);
                if (ImGui::MenuItem("Torus")) addPrimitive(Prim::Torus);
                if (ImGui::MenuItem("Floor plane")) addPrimitive(Prim::Plane);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Keyboard shortcuts")) g.showShortcuts = true;
            if (ImGui::MenuItem("Free 3D models (Sketchfab)")) openUrl("https://sketchfab.com/search?features=downloadable&type=models");
            if (ImGui::MenuItem("Free HDRI lighting (Poly Haven)")) openUrl("https://polyhaven.com/hdris");
            if (ImGui::MenuItem("Free fonts (Google Fonts)")) openUrl("https://fonts.google.com");
            ImGui::Separator();
            if (ImGui::MenuItem("About Element Genie")) g.showAbout = true;
            ImGui::EndMenu();
        }
        // right side
        float w = ImGui::GetWindowWidth();
        std::string title = g.setupMode ? std::string("Scene Setup") : (g.projectPath.empty() ? "Untitled" : stem(g.projectPath)) + (g.dirty ? " •" : "");
        ImVec2 ts = ImGui::CalcTextSize(title.c_str());
        ImGui::SetCursorPosX(w * .5f - ts.x * .5f);
        ImGui::TextDisabled("%s", title.c_str());
        if (g.setupMode) {
        } else {
        ImGui::SetCursorPosX(w - 190);
        if (goldButton("Export for Premiere", ImVec2(176, 0))) g.exportDialog = true;
        }
        ImGui::EndMainMenuBar();
    }
}

static void shortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    bool ctrl = io.KeyCtrl, shift = io.KeyShift;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (shift) doRedo(); else doUndo(); }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) doRedo();
    if (!g.setupMode) {
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) saveProject(shift);
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) openProjectDialog();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) newProject();
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_I, false)) addModelDialog();
    if (!g.setupMode && ctrl && ImGui::IsKeyPressed(ImGuiKey_M, false)) g.exportDialog = true;
    if (io.WantTextInput) return;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateSelected();
    if (ctrl) return;
    if (!g.setupMode && ImGui::IsKeyPressed(ImGuiKey_Space, false)) togglePlay();
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) g.frame = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) g.frame = (float)(g.S.comp.frames - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) { g.playing = false; g.frame = (float)std::max(0, currentFrameInt() - (shift ? 10 : 1)); }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) { g.playing = false; g.frame = (float)std::min(g.S.comp.frames - 1, currentFrameInt() + (shift ? 10 : 1)); }
    if (ImGui::IsKeyPressed(ImGuiKey_Comma, false)) jumpKey(-1);
    if (ImGui::IsKeyPressed(ImGuiKey_Period, false)) jumpKey(1);
    if (ImGui::IsKeyPressed(ImGuiKey_W, false)) g.gizmoOp = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_E, false)) g.gizmoOp = 1;
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) g.gizmoOp = 2;
    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) frameSelected();
    if (ImGui::IsKeyPressed(ImGuiKey_K, false)) setKeysAllTransform();
    if (ImGui::IsKeyPressed(ImGuiKey_T, false)) addText();
    if (ImGui::IsKeyPressed(ImGuiKey_F9, false)) applyEase(0);
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
        if (!g.keySel.empty()) deleteSelectedKeys(); else deleteSelected();
    }
}

static void buildLayout(ImGuiID dock) {
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
    ImGuiID bottom, top, left, right, centre;
    ImGui::DockBuilderSplitNode(dock, ImGuiDir_Down, g.setupMode ? 0.07f : 0.27f, &bottom, &top);
    ImGui::DockBuilderSplitNode(top, ImGuiDir_Left, 0.17f, &left, &centre);
    ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.27f, &right, &centre);
    ImGui::DockBuilderDockWindow("Scene", left);
    ImGui::DockBuilderDockWindow("Viewer", centre);
    ImGui::DockBuilderDockWindow("Properties", right);
    ImGui::DockBuilderDockWindow(g.setupMode ? "Preview" : "Timeline", bottom);
    for (ImGuiID id : {left, centre, right, bottom}) if (ImGuiDockNode* n = ImGui::DockBuilderGetNode(id)) n->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
    ImGui::DockBuilderFinish(dock);
}

static void popups() {
    if (g.showAbout) { ImGui::OpenPopup("About"); g.showAbout = false; }
    if (ImGui::BeginPopupModal("About", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (g.logoTex) { ImGui::Image((ImTextureID)(intptr_t)g.logoTex, ImVec2(64.f * g.logoW / g.logoH, 64)); ImGui::SameLine(); }
        ImGui::BeginGroup();
        ImGui::PushFont(g.fBold, 22); ImGui::TextColored(GOLD, "Element Genie"); ImGui::PopFont();
        ImGui::Text("Version %s  -  open-source 3D titles & models for Premiere Pro", APP_VERSION);
        ImGui::EndGroup();
        ImGui::Spacing();
        ImGui::TextDisabled("Built with: Dear ImGui, ImGuizmo, GLFW, ufbx, cgltf, stb, nanosvg,\nearcut, GLM, nlohmann/json, Inter typeface (OFL).");
        ImGui::Spacing();
        if (goldButton("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (g.showShortcuts) { ImGui::OpenPopup("Keyboard shortcuts"); g.showShortcuts = false; }
    if (ImGui::BeginPopupModal("Keyboard shortcuts", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const char* k[][2] = {{"Space", "Play / pause"}, {"Left / Right", "Step one frame (Shift = 10)"}, {", / .", "Previous / next keyframe"},
                              {"Home / End", "Start / end"}, {"K", "Keyframe position, rotation, scale"}, {"F9", "Easy ease selected keys"},
                              {"W / E / R", "Move / rotate / scale tool"}, {"F", "Frame selection"}, {"T", "Add 3D text"},
                              {"Right-drag", "Orbit camera"}, {"Middle-drag", "Pan camera"}, {"Scroll", "Zoom camera"},
                              {"Ctrl+Z / Ctrl+Y", "Undo / redo"}, {"Ctrl+D", "Duplicate"}, {"Delete", "Delete selected keys or object"},
                              {"Ctrl+S", "Save"}, {"Ctrl+M", "Export"}, {"Drop files", "Models, logos, fonts, HDRIs, projects"}};
        if (ImGui::BeginTable("k", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX)) {
            for (auto& r : k) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextColored(GOLD, "%s", r[0]); ImGui::TableNextColumn(); ImGui::TextUnformatted(r[1]); }
            ImGui::EndTable();
        }
        if (goldButton("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    // unsaved changes
    if (g.wantClose && g.setupMode) { g.wantClose = false; if (!g.dirty) setupFinish(false); ImGui::OpenPopup("Apply changes?"); }
    if (ImGui::BeginPopupModal("Apply changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Send this scene back to Premiere?");
        if (goldButton("Apply")) setupFinish(true);
        ImGui::SameLine(); if (ImGui::Button("Discard")) setupFinish(false);
        ImGui::SameLine(); if (ImGui::Button("Keep editing")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (g.wantClose) { ImGui::OpenPopup("Unsaved changes"); g.wantClose = false; }
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!g.dirty) { glfwSetWindowShouldClose(g.win, 1); ImGui::CloseCurrentPopup(); }
        ImGui::Text("Save changes before closing?");
        if (goldButton("Save")) { if (saveProject(false)) glfwSetWindowShouldClose(g.win, 1); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if (ImGui::Button("Don't save")) { glfwSetWindowShouldClose(g.win, 1); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    // toast
    if (!g.toast.empty() && ImGui::GetTime() < g.toastUntil) {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImVec2 ts = ImGui::CalcTextSize(g.toast.c_str());
        ImVec2 pos(vp->WorkPos.x + vp->WorkSize.x * .5f - ts.x * .5f - 16, vp->WorkPos.y + 50);
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        float a = std::min(1.0, (g.toastUntil - ImGui::GetTime()) * 2);
        dl->AddRectFilled(pos, ImVec2(pos.x + ts.x + 32, pos.y + ts.y + 18), IM_COL32(36, 34, 52, (int)(240 * a)), 8);
        dl->AddRect(pos, ImVec2(pos.x + ts.x + 32, pos.y + ts.y + 18), IM_COL32(139, 123, 255, (int)(160 * a)), 8);
        dl->AddText(ImVec2(pos.x + 16, pos.y + 9), IM_COL32(240, 236, 228, (int)(255 * a)), g.toast.c_str());
    }
}

// ============================================================ Scene Setup layout (preset browser · viewport · scene tree · edit)
static int g_presetTab = 0, g_browserTab = 0;
static void beginCard(const char* id, ImVec2 size) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
    ImGui::BeginChild(id, size, ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
}
static void cardTitle(const char* t) {
    ImGui::PushFont(g.fBold, 0); ImGui::TextUnformatted(t); ImGui::PopFont();
}
static bool pillTabs(const char* const* names, int n, int& cur) {
    bool ch = false;
    for (int i = 0; i < n; i++) {
        if (i) ImGui::SameLine(0, 4);
        bool on = cur == i;
        ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.23f, 0.21f, 0.38f, 1) : ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? GOLD : ImVec4(0.62f, 0.62f, 0.68f, 1));
        if (ImGui::Button(names[i])) { cur = i; ch = true; }
        ImGui::PopStyleColor(2);
    }
    return ch;
}
static void toolIcon(ImDrawList* dl, ImVec2 c, int kind, ImU32 col) {
    float t = 1.8f;
    switch (kind) {
    case 0: dl->AddLine(ImVec2(c.x, c.y - 8), ImVec2(c.x, c.y + 3), col, t); dl->AddLine(ImVec2(c.x - 5, c.y - 2), ImVec2(c.x, c.y + 3), col, t);
            dl->AddLine(ImVec2(c.x + 5, c.y - 2), ImVec2(c.x, c.y + 3), col, t); dl->AddLine(ImVec2(c.x - 8, c.y + 8), ImVec2(c.x + 8, c.y + 8), col, t); break;  // import
    case 1: dl->AddLine(ImVec2(c.x - 8, c.y - 7), ImVec2(c.x + 8, c.y - 7), col, t + .6f); dl->AddLine(ImVec2(c.x, c.y - 7), ImVec2(c.x, c.y + 8), col, t + .6f); break;  // text
    case 2: dl->AddRect(ImVec2(c.x - 8, c.y - 8), ImVec2(c.x + 8, c.y + 8), col, 3, 0, t); dl->AddLine(ImVec2(c.x - 4, c.y + 4), ImVec2(c.x, c.y - 1), col, t); dl->AddLine(ImVec2(c.x, c.y - 1), ImVec2(c.x + 4, c.y + 3), col, t); break;  // logo
    case 3: { ImVec2 p[6] = {{c.x, c.y - 9}, {c.x + 8, c.y - 4.5f}, {c.x + 8, c.y + 4.5f}, {c.x, c.y + 9}, {c.x - 8, c.y + 4.5f}, {c.x - 8, c.y - 4.5f}};
            dl->AddPolyline(p, 6, col, ImDrawFlags_Closed, t); dl->AddLine(ImVec2(c.x, c.y), ImVec2(c.x, c.y + 9), col, t);
            dl->AddLine(ImVec2(c.x, c.y), ImVec2(c.x + 8, c.y - 4.5f), col, t); dl->AddLine(ImVec2(c.x, c.y), ImVec2(c.x - 8, c.y - 4.5f), col, t); break; }  // model
    case 4: dl->AddCircle(c, 8.5f, col, 24, t); dl->AddLine(ImVec2(c.x - 4, c.y), ImVec2(c.x + 4, c.y), col, t); dl->AddLine(ImVec2(c.x, c.y - 4), ImVec2(c.x, c.y + 4), col, t); break;  // shape
    case 5: { ImVec2 p[10]; for (int i = 0; i < 10; i++) { float a = -1.5708f + i * 0.6283f, r = i % 2 ? 4.f : 9.f; p[i] = ImVec2(c.x + cosf(a) * r, c.y + sinf(a) * r); }
            dl->AddPolyline(p, 10, col, ImDrawFlags_Closed, t); break; }  // icons
    case 6: dl->AddCircle(c, 8.5f, col, 24, t); dl->AddLine(ImVec2(c.x - 8.5f, c.y), ImVec2(c.x + 8.5f, c.y), col, t); dl->AddEllipse(c, ImVec2(4, 8.5f), col, 0, 20, t); break;  // env
    case 7: dl->AddCircle(c, 4, col, 16, t); for (int i = 0; i < 8; i++) { float a = i * 0.785f; dl->AddLine(ImVec2(c.x + cosf(a) * 6.5f, c.y + sinf(a) * 6.5f), ImVec2(c.x + cosf(a) * 9.5f, c.y + sinf(a) * 9.5f), col, t); } break;  // light
    case 8: dl->AddLine(ImVec2(c.x - 8, c.y - 6), ImVec2(c.x + 8, c.y - 6), col, t); dl->AddLine(ImVec2(c.x - 8, c.y), ImVec2(c.x + 3, c.y), col, t); dl->AddLine(ImVec2(c.x - 8, c.y + 6), ImVec2(c.x + 6, c.y + 6), col, t); break;  // render
    }
}
static bool toolButton(const char* label, int kind, bool active, const char* tip) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    bool r = ImGui::InvisibleButton("tb", ImVec2(70, 52));
    bool hov = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active || hov) dl->AddRectFilled(p, ImVec2(p.x + 70, p.y + 52), active ? IM_COL32(58, 53, 96, 255) : IM_COL32(50, 50, 58, 255), 10);
    ImU32 col = active ? GOLD32 : IM_COL32(215, 215, 222, 255);
    toolIcon(dl, ImVec2(p.x + 35, p.y + 19), kind, col);
    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(p.x + 35 - ts.x * .5f, p.y + 33), col, label);
    if (hov && tip) ImGui::SetTooltip("%s", tip);
    return r;
}
static void materialPresetGrid(Material& m) {
    buildMatThumbs();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float avail = ImGui::GetContentRegionAvail().x;
    int per = std::max(2, (int)(avail / 82)); float gap = 8;
    float cw = (avail - gap * (per - 1)) / per;
    int n = (int)(sizeof MATS / sizeof MATS[0]);
    for (int i = 0; i < n; i++) {
        if (i % per) ImGui::SameLine(0, gap);
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float th = cw + 22;
        if (ImGui::InvisibleButton("m", ImVec2(cw, th))) applyMat(m, MATS[i]);
        bool hov = ImGui::IsItemHovered(), cur = m.preset == MATS[i].name;
        if (g_matThumb[i]) dl->AddImageRounded((ImTextureID)(intptr_t)g_matThumb[i], p, ImVec2(p.x + cw, p.y + cw), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 10);
        if (cur) dl->AddRect(ImVec2(p.x - 1, p.y - 1), ImVec2(p.x + cw + 1, p.y + cw + 1), GOLD32, 11, 0, 2.f);
        else if (hov) dl->AddRect(p, ImVec2(p.x + cw, p.y + cw), IM_COL32(120, 120, 135, 255), 10, 0, 1.f);
        const char* nm = MATS[i].name;
        ImGui::PushFont(nullptr, 11.5f);
        ImVec2 ts = ImGui::CalcTextSize(nm);
        dl->PushClipRect(ImVec2(p.x, p.y + cw), ImVec2(p.x + cw, p.y + th), true);
        dl->AddText(ImVec2(p.x + std::max(0.f, (cw - ts.x) * .5f), p.y + cw + 4), cur ? GOLD32 : IM_COL32(190, 190, 200, 255), nm);
        dl->PopClipRect();
        ImGui::PopFont();
        if (hov) ImGui::SetTooltip("%s", nm);
        ImGui::PopID();
    }
}
static void envPresetGrid() {
    struct EP { int id; ImU32 top, bot; };
    static const EP eps[] = {{6, IM_COL32(150, 180, 235, 255), IM_COL32(60, 50, 40, 255)}, {0, IM_COL32(200, 200, 205, 255), IM_COL32(20, 20, 22, 255)},
                             {5, IM_COL32(255, 210, 140, 255), IM_COL32(40, 28, 14, 255)}, {3, IM_COL32(240, 240, 240, 255), IM_COL32(5, 5, 5, 255)},
                             {1, IM_COL32(255, 160, 80, 255), IM_COL32(50, 70, 140, 255)}, {2, IM_COL32(255, 40, 160, 255), IM_COL32(20, 180, 255, 255)},
                             {4, IM_COL32(235, 240, 248, 255), IM_COL32(120, 118, 115, 255)}};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float avail = ImGui::GetContentRegionAvail().x; float cw = (avail - 8) / 2;
    for (int i = 0; i < 7; i++) {
        if (i % 2) ImGui::SameLine(0, 8);
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("e", ImVec2(cw, 54))) g.S.env.preset = eps[i].id;
        bool cur = g.S.env.preset == eps[i].id;
        dl->AddRectFilledMultiColor(ImVec2(p.x + 6, p.y + 6), ImVec2(p.x + 46, p.y + 48), eps[i].top, eps[i].top, eps[i].bot, eps[i].bot);
        dl->AddRect(p, ImVec2(p.x + cw, p.y + 54), cur ? GOLD32 : IM_COL32(60, 60, 68, 255), 10, 0, cur ? 2.f : 1.f);
        dl->AddText(ImVec2(p.x + 54, p.y + 18), cur ? GOLD32 : IM_COL32(215, 215, 222, 255), ENV_NAMES[eps[i].id]);
        ImGui::PopID();
    }
    if (ImGui::Button("Load HDRI image...", ImVec2(-1, 0))) {
        std::string pth = openFileDialog("Environment image", "HDRI / panorama|*.hdr;*.jpg;*.jpeg;*.png|All files|*.*", g.hwnd);
        if (!pth.empty()) { g.S.env.preset = -1; g.S.env.path = pth; }
    }
    if (g.S.env.preset < 0) ImGui::TextDisabled("Using %s", baseName(g.S.env.path).c_str());
    ImGui::TextDisabled("Free HDRIs: polyhaven.com");
}
static void presetsCard(ImVec2 size) {
    beginCard("presets", size);
    cardTitle("Presets");
    static const char* tabs[] = {"Materials", "Bevels", "Lighting"};
    pillTabs(tabs, 3, g_presetTab);
    ImGui::BeginChild("presetsBody", ImVec2(0, 0), ImGuiChildFlags_None);
    Object* o = selectedObject();
    if (g_presetTab == 0) {
        if (!o) ImGui::TextDisabled("Select an object to give it a material.");
        else if (o->type == ObjType::Model && o->keepModelMaterials) {
            ImGui::TextWrapped("This model uses its own materials.");
            if (ImGui::Button("Use a preset instead")) o->keepModelMaterials = false;
        } else materialPresetGrid(o->mat);
    } else if (g_presetTab == 1) {
        if (o && (o->type == ObjType::Text || o->type == ObjType::Logo)) bevelPicker(*o);
        else ImGui::TextDisabled("Select a text or logo to choose its bevel.");
    } else envPresetGrid();
    ImGui::EndChild();
    ImGui::EndChild();
}
static void browserCard(ImVec2 size) {
    beginCard("browser", size);
    cardTitle("Model Browser");
    static const char* tabs[] = {"Icons", "Shapes", "Models"};
    pillTabs(tabs, 3, g_browserTab);
    ImGui::BeginChild("browserBody", ImVec2(0, 0), ImGuiChildFlags_None);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float avail = ImGui::GetContentRegionAvail().x;
    if (g_browserTab == 0) {
        int per = std::max(1, (int)(avail / 58)); float cw = (avail - (per - 1) * 6) / per;
        for (int i = 0; i < ICON_COUNT; i++) {
            if (i % per) ImGui::SameLine(0, 6);
            ImGui::PushID(800 + i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("ic", ImVec2(cw, cw))) addIcon(ICONS[i].name);
            bool hov = ImGui::IsItemHovered();
            ImU32 bg = hov ? IM_COL32(58, 53, 96, 255) : IM_COL32(50, 50, 56, 255);
            dl->AddRectFilled(p, ImVec2(p.x + cw, p.y + cw), bg, 10);
            drawIconThumb(dl, ImVec2(p.x + cw * .2f, p.y + cw * .2f), cw * .6f, i, IM_COL32(211, 208, 232, 255), bg);
            if (hov) ImGui::SetTooltip("Add %s", ICONS[i].name);
            ImGui::PopID();
        }
    } else if (g_browserTab == 1) {
        struct SP { const char* n; Prim p; };
        static const SP sp[] = {{"Cube", Prim::Cube}, {"Rounded cube", Prim::RoundCube}, {"Sphere", Prim::Sphere}, {"Cylinder", Prim::Cylinder},
                                {"Torus", Prim::Torus}, {"Cone", Prim::Cone}, {"Capsule", Prim::Capsule}, {"Gem", Prim::Gem}, {"Floor plane", Prim::Plane}};
        float cw = (avail - 6) / 2;
        for (int i = 0; i < 9; i++) { if (i % 2) ImGui::SameLine(0, 6); if (ImGui::Button(sp[i].n, ImVec2(cw, 34))) addPrimitive(sp[i].p); }
        if (ImGui::Button("Image card...", ImVec2(-1, 34))) {
            std::string pth = openFileDialog("Image card", "Images|*.png;*.jpg;*.jpeg|All files|*.*", g.hwnd);
            if (!pth.empty()) addImageCard(pth);
        }
    } else {
        if (goldButton("Import 3D model...", ImVec2(-1, 36))) addModelDialog();
        ImGui::TextDisabled("GLB, glTF, FBX or OBJ.");
        ImGui::Spacing();
        ImGui::TextUnformatted("Free models:");
        if (ImGui::Button("Sketchfab (downloadable)", ImVec2(-1, 0))) openUrl("https://sketchfab.com/search?features=downloadable&type=models");
        if (ImGui::Button("Poly Haven", ImVec2(-1, 0))) openUrl("https://polyhaven.com/models");
        bool any = false;
        for (auto& ob : g.S.objects) if (ob.type == ObjType::Model) {
            if (!any) { ImGui::Spacing(); ImGui::TextDisabled("In this scene"); any = true; }
            if (ImGui::Selectable(ob.name.c_str(), g.sel.kind == Selection::Obj && g.sel.id == ob.id)) g.sel = {Selection::Obj, ob.id};
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();
}
static const ImU32 GROUP_COLS[5] = {IM_COL32(139, 123, 255, 255), IM_COL32(63, 163, 255, 255), IM_COL32(47, 209, 181, 255), IM_COL32(255, 107, 139, 255), IM_COL32(255, 176, 90, 255)};
static void sceneCard(ImVec2 size) {
    beginCard("scene", size);
    cardTitle("Scene");
    ImGui::BeginChild("sceneBody", ImVec2(0, 0), ImGuiChildFlags_None);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int gi = 0; gi < 5; gi++) {
        int count = 0; for (auto& o : g.S.objects) if (o.group == gi) count++;
        ImGui::PushID(gi);
        ImVec2 p = ImGui::GetCursorScreenPos();
        char lbl[32]; snprintf(lbl, sizeof lbl, "     Group %d", gi + 1);
        ImGui::SetNextItemOpen(count > 0, ImGuiCond_Once);
        bool open = ImGui::TreeNodeEx(lbl, ImGuiTreeNodeFlags_SpanAvailWidth | (count == 0 ? ImGuiTreeNodeFlags_Leaf : 0));
        dl->AddRectFilled(ImVec2(p.x + 22, p.y + 6), ImVec2(p.x + 32, p.y + 16), GROUP_COLS[gi], 3);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("EG_OBJ")) {
                int id = *(const int*)pl->Data;
                for (auto& o : g.S.objects) if (o.id == id) o.group = gi;
            }
            ImGui::EndDragDropTarget();
        }
        if (open) {
            for (auto& o : g.S.objects) {
                if (o.group != gi) continue;
                ImGui::PushID(o.id);
                std::string label = std::string(o.visible ? "" : "(hidden) ") + o.name + (o.geoError.empty() ? "" : "  (!)");
                if (ImGui::Selectable(label.c_str(), g.sel.kind == Selection::Obj && g.sel.id == o.id, ImGuiSelectableFlags_AllowOverlap)) g.sel = {Selection::Obj, o.id};
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("EG_OBJ", &o.id, sizeof(int));
                    ImGui::Text("Move %s to another group", o.name.c_str());
                    ImGui::EndDragDropSource();
                }
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60 + ImGui::GetCursorPosX() - ImGui::GetCursorPosX());
                ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 54);
                ImGui::TextDisabled("%s", typeLabel(o));
                int del = -1;
                if (ImGui::BeginPopupContextItem("ctx")) {
                    g.sel = {Selection::Obj, o.id};
                    if (ImGui::MenuItem(o.visible ? "Hide" : "Show")) o.visible = !o.visible;
                    if (ImGui::BeginMenu("Move to group")) {
                        for (int k = 0; k < 5; k++) { char b[16]; snprintf(b, sizeof b, "Group %d", k + 1); if (ImGui::MenuItem(b, nullptr, o.group == k)) o.group = k; }
                        ImGui::EndMenu();
                    }
                    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) del = -2;
                    if (ImGui::MenuItem("Delete", "Del")) del = o.id;
                    ImGui::EndPopup();
                }
                ImGui::PopID();
                if (del == -2) { duplicateSelected(); break; }
                if (del >= 0) { g.sel = {Selection::Obj, del}; deleteSelected(); break; }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::TreeNodeEx("Camera & lights", ImGuiTreeNodeFlags_SpanAvailWidth)) {
        if (ImGui::Selectable("Camera", g.sel.kind == Selection::Camera)) g.sel = {Selection::Camera, 0};
        for (int i = 0; i < (int)g.S.lights.size(); i++) {
            ImGui::PushID(300 + i);
            std::string l = g.S.lights[i].name + (g.S.lights[i].enabled ? "" : " (off)");
            if (ImGui::Selectable(l.c_str(), g.sel.kind == Selection::Light && g.sel.id == i)) g.sel = {Selection::Light, i};
            ImGui::PopID();
        }
        if (ImGui::Selectable("Look & render", g.sel.kind == Selection::World)) g.sel = {Selection::World, 0};
        if (g.S.lights.size() < 4 && ImGui::SmallButton("+ Add light")) {
            Light l; l.name = "Light " + std::to_string(g.S.lights.size() + 1); l.pos.value = vec3(-3, 4, 3); l.intensity.value = 1.5f; l.shadow = false;
            g.S.lights.push_back(l); g.sel = {Selection::Light, (int)g.S.lights.size() - 1};
        }
        ImGui::TreePop();
    }
    ImGui::TextDisabled("Drag objects between groups.");
    ImGui::EndChild();
    ImGui::EndChild();
}
static void editCard(ImVec2 size) {
    beginCard("edit", size);
    cardTitle("Edit");
    ImGui::BeginChild("editBody", ImVec2(0, 0), ImGuiChildFlags_None);
    if (Object* o = selectedObject()) objectProps(*o);
    else if (g.sel.kind == Selection::Camera) cameraProps();
    else if (g.sel.kind == Selection::Light && g.sel.id < (int)g.S.lights.size()) lightProps(g.S.lights[g.sel.id]);
    else if (g.sel.kind == Selection::World) worldProps();
    else ImGui::TextDisabled("Select something in the Scene panel\nor click it in the viewport.");
    ImGui::EndChild();
    ImGui::EndChild();
}
static void setupLayout() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::Begin("##setup", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(2);
    // toolbar
    Object* so = selectedObject();
    if (toolButton("Import", 0, false, "Import a model, logo, font or HDRI")) {
        std::string pth = openFileDialog("Import", "Everything supported|*.glb;*.gltf;*.fbx;*.obj;*.png;*.svg;*.jpg;*.jpeg;*.ttf;*.otf;*.hdr|All files|*.*", g.hwnd);
        if (!pth.empty()) importFile(pth);
    }
    ImGui::SameLine(0, 4); if (toolButton("Text", 1, so && so->type == ObjType::Text, "Add 3D text")) addText();
    ImGui::SameLine(0, 4); if (toolButton("Logo", 2, so && so->type == ObjType::Logo, "Extrude a PNG or SVG logo")) addLogoDialog();
    ImGui::SameLine(0, 4); if (toolButton("Model", 3, so && so->type == ObjType::Model, "Import a 3D model")) addModelDialog();
    ImGui::SameLine(0, 4); if (toolButton("Shapes", 4, false, "Basic shapes")) g_browserTab = 1;
    ImGui::SameLine(0, 4); if (toolButton("Icons", 5, false, "3D icon pack")) g_browserTab = 0;
    ImGui::SameLine(0, 14); if (toolButton("Lighting", 6, g_presetTab == 2, "Reflections & lighting presets")) g_presetTab = 2;
    ImGui::SameLine(0, 4); if (toolButton("Lights", 7, g.sel.kind == Selection::Light, "Edit lights")) g.sel = {Selection::Light, 0};
    ImGui::SameLine(0, 4); if (toolButton("Render", 8, g.sel.kind == Selection::World, "Look & render settings")) g.sel = {Selection::World, 0};
    float right = ImGui::GetWindowContentRegionMax().x;
    ImGui::SameLine(right - 232);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
    if (ImGui::Button("Cancel", ImVec2(100, 34))) { g.wantClose = true; }
    ImGui::SameLine(0, 8);
    if (goldButton("OK", ImVec2(120, 34))) setupFinish(true);
    // columns
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float gap = 8, leftW = std::max(260.f, avail.x * 0.2f), rightW = std::max(300.f, avail.x * 0.23f);
    float midW = avail.x - leftW - rightW - gap * 2;
    ImGui::BeginGroup();
    presetsCard(ImVec2(leftW, (avail.y - gap) * 0.55f));
    browserCard(ImVec2(leftW, avail.y - (avail.y - gap) * 0.55f - gap));
    ImGui::EndGroup();
    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    float stripH = 46;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("viewcard", ImVec2(midW, avail.y - stripH - gap), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    viewportBody();
    ImGui::EndChild();
    beginCard("strip", ImVec2(midW, stripH));
    bool anyLetters = false; for (auto& o : g.S.objects) anyLetters |= o.letters.enabled;
    ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Letters Built"); ImGui::SameLine();
    ImGui::BeginDisabled(!anyLetters);
    ImGui::SetNextItemWidth(std::min(360.f, midW * 0.4f));
    ImGui::SliderFloat("##lb", &g.previewLetters, 0, 100, "%.0f%%");
    ImGui::EndDisabled();
    ImGui::SameLine(0, 20);
    ImGui::TextDisabled(anyLetters ? "Preview only - keyframe it in Effect Controls" : "Turn on letter animation in a text's Animate tab");
    ImGui::EndChild();
    ImGui::EndGroup();
    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    sceneCard(ImVec2(rightW, (avail.y - gap) * 0.4f));
    editCard(ImVec2(rightW, avail.y - (avail.y - gap) * 0.4f - gap));
    ImGui::EndGroup();
    ImGui::End();
}

// ============================================================ main
static std::vector<std::string> g_dropped;
static void onDrop(GLFWwindow*, int n, const char** paths) { for (int i = 0; i < n; i++) g_dropped.push_back(paths[i]); }
static void onClose(GLFWwindow* w) { if (g.dirty || g.setupMode) { glfwSetWindowShouldClose(w, 0); g.wantClose = true; } }

static void setWindowIcon() {
    ImageData im;
    std::vector<uint8_t> b(g_logo_png, g_logo_png + g_logo_png_size);
    // decode via stb through a temp path-free helper
    extern bool decodeImageMem(const uint8_t*, size_t, ImageData&);
    if (!decodeImageMem(b.data(), b.size(), im) || im.rgba.empty()) return;
    GLFWimage gi; gi.width = im.w; gi.height = im.h; gi.pixels = im.rgba.data();
    glfwSetWindowIcon(g.win, 1, &gi);
    auto t = uploadTexture(im, true, true, true);
    g.logoTex = t->id; t->id = 0; g.logoW = im.w; g.logoH = im.h;
}

static int headlessRender(const std::string& proj, const std::string& out, int a, int b) {
    std::vector<uint8_t> bytes; std::string err;
    if (!readFileBytes(proj, bytes)) { fprintf(stderr, "can't read %s\n", proj.c_str()); return 2; }
    if (!sceneFromJson(std::string(bytes.begin(), bytes.end()), g.S, err)) { fprintf(stderr, "%s\n", err.c_str()); return 2; }
    int fmt = lowerExt(out) == "png" ? 1 : 0;
    if (a < 0) a = 0; if (b < 0 || b >= g.S.comp.frames) b = g.S.comp.frames - 1;
    Exporter ex;
    if (!ex.begin(out, fmt, g.S.comp.width, g.S.comp.height, g.S.comp.fps, b - a + 1, err)) { fprintf(stderr, "%s\n", err.c_str()); return 3; }
    RenderTarget rt; rt.ensure(g.S.comp.width, g.S.comp.height, 8);
    std::vector<uint8_t> px((size_t)rt.w * rt.h * 4);
    for (int f = a; f <= b; f++) {
        ViewOptions vo; g.R.render(g.S, (float)f, rt, vo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, rt.outFbo); glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, rt.w, rt.h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        if (!ex.addFrame(px.data(), err)) { fprintf(stderr, "%s\n", err.c_str()); return 4; }
        fprintf(stderr, "frame %d\n", f);
    }
    for (auto& o : g.S.objects) if (!o.geoError.empty()) fprintf(stderr, "object %s: %s\n", o.name.c_str(), o.geoError.c_str());
    return ex.finish(err) ? 0 : 5;
}

int main(int, char**) {
    // UTF-8 command line
    int argc = 0; LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args; for (int i = 0; i < argc; i++) args.push_back(narrow(wargv[i]));
    bool headless = args.size() >= 4 && args[1] == "--render";
    std::string uiShot; int uiShotFrames = 0;
    for (size_t i = 1; i + 1 < args.size(); i++) if (args[i] == "--screenshot") { uiShot = args[i + 1]; uiShotFrames = 30; }
    std::string setupIn, refPath; int compW = 1920, compH = 1080; float compFps = 25.f; unsigned long long parentHwnd = 0;
    for (size_t i = 1; i < args.size(); i++) {
        if (args[i] == "--setup" && i + 2 < args.size()) { g.setupMode = true; setupIn = args[i + 1]; g.setupOut = args[i + 2]; i += 2; }
        else if (args[i] == "--size" && i + 2 < args.size()) { compW = atoi(args[i + 1].c_str()); compH = atoi(args[i + 2].c_str()); i += 2; }
        else if (args[i] == "--par" && i + 1 < args.size()) { g.parentPar = (float)atof(args[++i].c_str()); }
        else if (args[i] == "--fps" && i + 1 < args.size()) { compFps = (float)atof(args[++i].c_str()); }
        else if (args[i] == "--parent" && i + 1 < args.size()) { parentHwnd = strtoull(args[++i].c_str(), nullptr, 10); }
        else if (args[i] == "--ref" && i + 1 < args.size()) { refPath = args[++i]; }
    }

    if (!glfwInit()) { MessageBoxW(nullptr, L"Couldn't start the graphics system.", L"Element Genie", MB_ICONERROR); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    if (headless) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    else if (!g.setupMode) glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    int winW = 1600, winH = 950;
    if (g.setupMode) {
        const GLFWvidmode* vm = glfwGetVideoMode(glfwGetPrimaryMonitor());
        if (vm) { winW = std::min(1600, (int)(vm->width * 0.9)); winH = std::min(980, (int)(vm->height * 0.88)); }
    }
    g.win = glfwCreateWindow(winW, winH, g.setupMode ? "Element Genie - Scene Setup" : "Element Genie", nullptr, nullptr);
    if (!g.win) { MessageBoxW(nullptr, L"Your graphics driver needs OpenGL 3.3 or newer. Try updating your graphics driver.", L"Element Genie", MB_ICONERROR); return 1; }
    glfwMakeContextCurrent(g.win);
    glfwSwapInterval(1);
    gladLoadGL(glfwGetProcAddress);
    g.hwnd = glfwGetWin32Window(g.win);
    if (g.setupMode) {
        const GLFWvidmode* vm = glfwGetVideoMode(glfwGetPrimaryMonitor());
        if (vm) glfwSetWindowPos(g.win, (vm->width - winW) / 2, (vm->height - winH) / 2);
        if (parentHwnd) SetWindowLongPtrW((HWND)g.hwnd, GWLP_HWNDPARENT, (LONG_PTR)parentHwnd);
        SetForegroundWindow((HWND)g.hwnd);
    }
    std::string err;
    if (!g.R.init(err)) {
        std::wstring m = L"Graphics setup failed:\n" + widen(err);
        MessageBoxW(nullptr, m.c_str(), L"Element Genie", MB_ICONERROR);
        if (headless) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
    }
    if (headless) {
        int a = -1, b = -1;
        for (size_t i = 4; i + 2 < args.size() + 1; i++) if (args[i] == "--frames" && i + 2 < args.size()) { a = atoi(args[i + 1].c_str()); b = atoi(args[i + 2].c_str()); }
        int rc = headlessRender(args[2], args[3], a, b);
        glfwTerminate();
        return rc;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    ImFontConfig fc; fc.FontDataOwnedByAtlas = false;
    g.fUI = io.Fonts->AddFontFromMemoryTTF((void*)g_font_ui, (int)g_font_ui_size, 15.f, &fc);
    g.fBold = io.Fonts->AddFontFromMemoryTTF((void*)g_font_ui_bold, (int)g_font_ui_bold_size, 15.f, &fc);
    applyTheme();
    {
        float sx = 1, sy = 1; glfwGetWindowContentScale(g.win, &sx, &sy);
        if (sx > 1.05f) { ImGui::GetStyle().ScaleAllSizes(sx); ImGui::GetStyle().FontScaleDpi = sx; }
    }
    ImGui_ImplGlfw_InitForOpenGL(g.win, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    glfwSetDropCallback(g.win, onDrop);
    glfwSetWindowCloseCallback(g.win, onClose);
    setWindowIcon();

    newProject();
    if (g.setupMode) {
        std::vector<uint8_t> b; std::string e; Scene n;
        if (readFileBytes(setupIn, b) && sceneFromJson(std::string(b.begin(), b.end()), n, e)) g.S = std::move(n);
        g.S.comp.width = std::max(16, (int)std::lround(compW * g.parentPar)); g.S.comp.height = std::max(16, compH); g.S.comp.fps = compFps;
        if (!refPath.empty()) { g.S.refPath = refPath; g.bgMode = 3; }
        g.sel = g.S.objects.empty() ? Selection{Selection::World, 0} : Selection{Selection::Obj, g.S.objects[0].id};
    } else
    for (size_t i = 1; i < args.size(); i++) {
        if (args[i] == "--screenshot") { i++; continue; }
        if (args[i] == "--size" || args[i] == "--par" || args[i] == "--fps" || args[i] == "--parent" || args[i] == "--ref") { i++; continue; }
        if (args[i].rfind("--", 0) == 0) continue;
        importFile(args[i]);
    }
    g.lastJson = sceneToJson(g.S); g.dirty = false;

    bool layoutDone = false;
    while (!glfwWindowShouldClose(g.win)) {
        if (g.exporting) glfwPollEvents(); else glfwWaitEventsTimeout(g.playing ? 0.0 : 0.05);
        if (glfwGetWindowAttrib(g.win, GLFW_ICONIFIED)) { glfwWaitEventsTimeout(0.1); continue; }
        for (auto& p : g_dropped) importFile(p);
        g_dropped.clear();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        // playback
        if (g.playing) {
            float f = g.playFrame0 + (float)((ImGui::GetTime() - g.playClock) * g.S.comp.fps);
            if (f >= g.S.comp.frames - 1) {
                if (g.loop) { g.playClock = ImGui::GetTime(); g.playFrame0 = 0; f = 0; }
                else { f = (float)(g.S.comp.frames - 1); g.playing = false; }
            }
            g.frame = std::floor(f);
        }
        g.frame = std::clamp(g.frame, 0.f, (float)std::max(0, g.S.comp.frames - 1));
        menuBar();
        if (g.setupMode) setupLayout();
        else {
        ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_None);
        if (!layoutDone) { buildLayout(dock); layoutDone = true; }
        scenePanel();
        viewportPanel();
        propertiesPanel();
        timelinePanel();
        }
        if (!g.setupMode) exportDialog();
        popups();
        shortcuts();
        exportStep();
        // undo checkpoints when the user isn't mid-drag
        if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(1) && !ImGui::IsMouseDown(2) && !g.exporting) {
            if (++g.idleFrames % 6 == 0) undoCheckpoint();
        }
        {
            std::string t = g.setupMode ? std::string("Element Genie - Scene Setup") : std::string(g.projectPath.empty() ? "Untitled" : stem(g.projectPath)) + (g.dirty ? " *" : "") + " - Element Genie";
            static std::string lastT; if (t != lastT) { glfwSetWindowTitle(g.win, t.c_str()); lastT = t; }
        }
        ImGui::Render();
        int dw, dh; glfwGetFramebufferSize(g.win, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.06f, 0.06f, 0.07f, 1); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        g.frameCount++;
        if (!uiShot.empty() && g.frameCount == uiShotFrames) {
            std::vector<uint8_t> px((size_t)dw * dh * 4);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, dw, dh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            extern void writePngFlip(const std::string&, int, int, const uint8_t*);
            writePngFlip(uiShot, dw, dh, px.data());
        }
        glfwSwapBuffers(g.win);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
    return 0;
}
