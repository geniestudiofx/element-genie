// Element Genie - application state
#pragma once
#include "scene.h"
#include "render.h"
#include "export.h"
#include <string>
#include <vector>
#include <memory>

struct GLFWwindow;
struct ImFont;

struct Chan {
    Track<glm::vec3>* v = nullptr;
    Track<float>* f = nullptr;
    bool valid() const { return v || f; }
    bool animated() const { return v ? v->animated : f ? f->animated : false; }
    void frames(std::vector<int>& out) const {
        if (v && v->animated) for (auto& k : v->keys) out.push_back(k.frame);
        if (f && f->animated) for (auto& k : f->keys) out.push_back(k.frame);
    }
    bool hasKey(int fr) const { return v ? v->keyIndexAt(fr) >= 0 : f ? f->keyIndexAt(fr) >= 0 : false; }
    void* ptr() const { return v ? (void*)v : (void*)f; }
};

struct KeyRef { Chan c; int frame; };

struct Selection {
    enum Kind { None, Obj, Camera, Light, World } kind = None;
    int id = -1;   // object id or light index
};

struct App {
    GLFWwindow* win = nullptr;
    void* hwnd = nullptr;
    Renderer R;
    Scene S;
    RenderTarget viewRT, exportRT;
    float frame = 0;
    bool playing = false, loop = true;
    double playClock = 0; float playFrame0 = 0;
    Selection sel;
    std::vector<KeyRef> keySel;
    int gizmoOp = 0;          // 0 move 1 rotate 2 scale
    bool gizmoWorld = false;
    int bgMode = 0;           // 0 checker 1 dark 2 light 3 reference
    int previewScale = 0;     // 0 full 1 half 2 quarter
    bool showGrid = true;
    std::string projectPath;
    bool dirty = false;
    // undo
    std::vector<std::string> undoStack, redoStack;
    std::string lastJson;
    int idleFrames = 0;
    // export
    bool exportDialog = false, exporting = false, exportFinished = false;
    Exporter ex;
    int exFormat = 0, exScale = 0, exCur = 0, exStart = 0, exEnd = 0;
    std::string exPath, exError;
    double exT0 = 0;
    // reference image
    std::shared_ptr<struct Texture> refTex;
    std::string refLoaded;
    // ui
    std::string toast; double toastUntil = 0;
    bool wantClose = false, showAbout = false, showShortcuts = false;
    ImFont *fUI = nullptr, *fBold = nullptr, *fTitle = nullptr;
    unsigned logoTex = 0; int logoW = 0, logoH = 0;
    int frameCount = 0;
    std::string fontFilter;
    // timeline drag
    bool tlDragging = false; float tlDragX0 = 0; int tlDragDelta = 0;
    struct Snap { Chan c; std::vector<Key<glm::vec3>> kv; std::vector<Key<float>> kf; };
    std::vector<Snap> tlSnap; std::vector<KeyRef> tlSelOrig;
    bool scrubbing = false;
    // Scene Setup mode (launched from the Premiere effect)
    bool setupMode = false;
    std::string setupOut;
    float previewLetters = 100.f;
    float parentPar = 1.f;
    bool askApply = false;
};

extern App g;
int currentFrameInt();
Object* selectedObject();
void notify(const std::string& msg);
