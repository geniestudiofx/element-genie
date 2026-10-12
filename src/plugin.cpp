// Element Genie - Premiere Pro / After Effects effect plug-in
// "Scene Setup" opens the 3D editor pop-up; everything you keyframe lives in Effect Controls.
#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AE_GeneralPlug.h"
#include "PrSDKAESupport.h"
#ifdef _WIN32
#include <windows.h>
#define EG_EXPORT __declspec(dllexport)
#else
#include <dlfcn.h>
#include <unistd.h>
#include <spawn.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#ifdef __APPLE__
#include <OpenGL/OpenGL.h>   // CGL
#endif
extern char** environ;
#define EG_EXPORT __attribute__((visibility("default")))
#endif
#include <glad/gl.h>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <algorithm>
#include <cerrno>
#include "scene.h"
#include "render.h"
#include "project.h"
#include "effect.h"
#include <stb_image_write.h>

std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& w);
bool readFileBytes(const std::string& path, std::vector<uint8_t>& out);

#define EG_MAJOR 1
#define EG_MINOR 0
#define EG_BUG 0
#define EG_STAGE PF_Stage_RELEASE
#define EG_BUILD 1

// ------------------------------------------------------------------ parameter layout
enum {
    P_INPUT = 0,
    P_SETUP,
    P_SCENE,
    P_G0,  // 5 groups x 10 params (topic, px, py, pz, rx, ry, rz, scale, opacity, end)
    P_CAM = P_G0 + 50,  // topic, orbit, tilt, zoom, panx, pany, roll, fov, end
    P_ANIM = P_CAM + 9, // topic, letters, envrot, light, glow, end
    P_REND = P_ANIM + 6, // topic, composite, motion blur, shutter, quality, end
    P_REAL = P_REND + 6, // topic + 12 + end
    P_FX = P_REAL + 14,  // topic, shatter, deform, noise evo, rep spread, rep rotate, rep build, end
    P_LENS = P_FX + 8,   // topic, dof mode, focus offset, blur, fog mode, fog amount, end
    P_OUT = P_LENS + 7,  // topic, output pass, end
    P_LET = P_OUT + 3,   // topic, rx, ry, rz, px, py, pz, scale, spread, wave offset, randomness, apply to, end
    P_AE = P_LET + 13,   // topic, use comp camera, world scale, use comp lights, keep scene lights, end
    P_AUTO = P_AE + 6,   // topic, build in, build in time, build out, build out time, end
    P_DEF = P_AUTO + 6,  // topic, twist, bend, taper, wobble, axis, apply to, end
    P_LOOK = P_DEF + 8,  // topic, light rotate, self glow, extrusion depth, end
    P_NUM = P_LOOK + 5
};
enum { G_TOPIC = 0, G_PX, G_PY, G_PZ, G_RX, G_RY, G_RZ, G_SCALE, G_OPACITY, G_END };

static const uint32_t ARB_MAGIC = 0x45473344;  // 'EG3D'

// ------------------------------------------------------------------ default scene
static std::string defaultSceneJson() {
    Scene s; sceneDefaults(s);
    Object o; o.id = s.nextId++; o.type = ObjType::Text; o.name = "Text"; o.text = "TEXT"; o.font = "builtin:Inter Black";
    o.mat.preset = "Chrome"; o.mat.color = glm::vec3(0.96f, 0.96f, 0.97f); o.mat.metallic = 1; o.mat.roughness = 0.06f; o.mat2 = o.mat;
    s.objects.push_back(o);
    return sceneToJson(s);
}

// ------------------------------------------------------------------ arbitrary data (scene JSON)
static PF_Handle makeArb(PF_InData* in_data, const std::string& json) {
    PF_Handle h = PF_NEW_HANDLE(8 + json.size() + 1);
    if (!h) return nullptr;
    char* p = (char*)PF_LOCK_HANDLE(h);
    uint32_t m = ARB_MAGIC, n = (uint32_t)json.size();
    memcpy(p, &m, 4); memcpy(p + 4, &n, 4); memcpy(p + 8, json.c_str(), json.size() + 1);
    PF_UNLOCK_HANDLE(h);
    return h;
}
static std::string readArb(PF_InData* in_data, PF_Handle h) {
    if (!h) return "";
    size_t sz = PF_GET_HANDLE_SIZE(h);
    if (sz < 8) return "";
    const char* p = (const char*)PF_LOCK_HANDLE(h);
    uint32_t m, n; memcpy(&m, p, 4); memcpy(&n, p + 4, 4);
    std::string s;
    if (m == ARB_MAGIC && n <= sz - 8) s.assign(p + 8, n);
    PF_UNLOCK_HANDLE(h);
    return s;
}
static bool writeArb(PF_InData* in_data, PF_Handle& h, const std::string& json) {
    size_t need = 8 + json.size() + 1;
    if (!h) { h = makeArb(in_data, json); return h != nullptr; }
    if (PF_GET_HANDLE_SIZE(h) != need) { if ((*in_data->utils->host_resize_handle)(need, &h) != PF_Err_NONE) return false; }
    char* p = (char*)PF_LOCK_HANDLE(h);
    uint32_t m = ARB_MAGIC, n = (uint32_t)json.size();
    memcpy(p, &m, 4); memcpy(p + 4, &n, 4); memcpy(p + 8, json.c_str(), json.size() + 1);
    PF_UNLOCK_HANDLE(h);
    return true;
}

static PF_Err arbCallback(PF_InData* in_data, PF_OutData*, PF_ArbParamsExtra* extra) {
    switch (extra->which_function) {
    case PF_Arbitrary_NEW_FUNC:
        *extra->u.new_func_params.arbPH = makeArb(in_data, defaultSceneJson());
        break;
    case PF_Arbitrary_DISPOSE_FUNC:
        if (extra->u.dispose_func_params.arbH) PF_DISPOSE_HANDLE(extra->u.dispose_func_params.arbH);
        break;
    case PF_Arbitrary_COPY_FUNC:
        *extra->u.copy_func_params.dst_arbPH = makeArb(in_data, readArb(in_data, extra->u.copy_func_params.src_arbH));
        break;
    case PF_Arbitrary_FLAT_SIZE_FUNC:
        *extra->u.flat_size_func_params.flat_data_sizePLu = (A_u_long)(8 + readArb(in_data, extra->u.flat_size_func_params.arbH).size());
        break;
    case PF_Arbitrary_FLATTEN_FUNC: {
        std::string s = readArb(in_data, extra->u.flatten_func_params.arbH);
        if (extra->u.flatten_func_params.buf_sizeLu < 8 + s.size()) return PF_Err_INTERNAL_STRUCT_DAMAGED;
        char* p = (char*)extra->u.flatten_func_params.flat_dataPV;
        uint32_t m = ARB_MAGIC, n = (uint32_t)s.size();
        memcpy(p, &m, 4); memcpy(p + 4, &n, 4); memcpy(p + 8, s.data(), s.size());
        break;
    }
    case PF_Arbitrary_UNFLATTEN_FUNC: {
        const char* p = (const char*)extra->u.unflatten_func_params.flat_dataPV;
        A_u_long sz = extra->u.unflatten_func_params.buf_sizeLu;
        std::string s;
        if (sz >= 8) { uint32_t m, n; memcpy(&m, p, 4); memcpy(&n, p + 4, 4); if (m == ARB_MAGIC && n <= sz - 8) s.assign(p + 8, n); }
        if (s.empty()) s = defaultSceneJson();
        *extra->u.unflatten_func_params.arbPH = makeArb(in_data, s);
        break;
    }
    case PF_Arbitrary_INTERP_FUNC:
        *extra->u.interp_func_params.interpPH = makeArb(in_data, readArb(in_data, extra->u.interp_func_params.left_arbH));
        break;
    case PF_Arbitrary_COMPARE_FUNC: {
        std::string a = readArb(in_data, extra->u.compare_func_params.a_arbH), b = readArb(in_data, extra->u.compare_func_params.b_arbH);
        *extra->u.compare_func_params.compareP = a == b ? PF_ArbCompare_EQUAL : PF_ArbCompare_NOT_EQUAL;
        break;
    }
    case PF_Arbitrary_PRINT_SIZE_FUNC:
        *extra->u.print_size_func_params.print_sizePLu = 32;
        break;
    case PF_Arbitrary_PRINT_FUNC:
        if (extra->u.print_func_params.print_sizeLu >= 16) strcpy(extra->u.print_func_params.print_bufferPC, "3D scene");
        break;
    case PF_Arbitrary_SCAN_FUNC:
        break;
    }
    return PF_Err_NONE;
}

// ------------------------------------------------------------------ OpenGL worker thread
#ifdef _WIN32
static HMODULE g_module = nullptr;
#endif
static void dbg(const char* fmt, ...) {
    static int on = -1;
    if (on < 0) on = getenv("EG_LOG") != nullptr;
    if (!on) return;
    char b[512]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    FILE* f = fopen("eg_log.txt", "a"); if (f) { fprintf(f, "%s\n", b); fclose(f); }
}
static void* glLoad(const char* name) {
#ifdef _WIN32
    void* p = (void*)wglGetProcAddress(name);
    if (!p || p == (void*)1 || p == (void*)2 || p == (void*)3 || p == (void*)-1) {
        static HMODULE gl = LoadLibraryA("opengl32.dll");
        p = (void*)GetProcAddress(gl, name);
    }
    return p;
#else
    static void* gl = dlopen("/System/Library/Frameworks/OpenGL.framework/Versions/Current/OpenGL", RTLD_LAZY | RTLD_GLOBAL);
    void* p = gl ? dlsym(gl, name) : nullptr;
    return p ? p : dlsym(RTLD_DEFAULT, name);
#endif
}

struct Job {
    const std::string* json;
    FxParams fx;
    std::vector<FxParams> blur;  // motion blur sub-frame samples (empty = off)
    int w, h; float par;
    const uint8_t* in; ptrdiff_t inRB;
    uint8_t* out; ptrdiff_t outRB;
    bool done = false;
};

class GLWorker {
public:
    static GLWorker& get() { static GLWorker w; return w; }
    bool render(Job& j, std::string& err) {
        dbg("render submit");
        std::lock_guard<std::mutex> serial(submitMx);
        std::unique_lock<std::mutex> lk(mx);
        if (!started) { th = std::thread([this] { loop(); }); th.detach(); started = true; }
        cv.wait(lk, [&] { return initDone; });
        if (!initErr.empty()) { err = initErr; return false; }
        job = &j; j.done = false;
        cv.notify_all();
        cv.wait(lk, [&] { return j.done; });
        job = nullptr;
        return true;
    }
    // most recent input frame, kept for the Scene Setup background
    std::mutex refMx; std::vector<uint8_t> refRGBA; int refW = 0, refH = 0;

private:
    std::mutex mx, submitMx; std::condition_variable cv;
    std::thread th; bool started = false, initDone = false;
    std::string initErr;
    Job* job = nullptr;
#ifdef _WIN32
    HWND hwnd = nullptr; HDC dc = nullptr; HGLRC rc = nullptr;
#elif defined(__APPLE__)
    CGLContextObj cgl = nullptr;
#endif
    Renderer R;
    RenderTarget rt;
    struct Cached { std::string json; Scene scene; };
    std::list<Cached> cache;
    std::vector<uint8_t> px;

    bool initGL() {
        dbg("initGL");
#ifdef _WIN32
        WNDCLASSW wc = {}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = g_module; wc.lpszClassName = L"ElementGenieGL";
        RegisterClassW(&wc);
        hwnd = CreateWindowW(L"ElementGenieGL", L"", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr, g_module, nullptr);
        if (!hwnd) { initErr = "Couldn't create a hidden window for OpenGL."; return false; }
        dc = GetDC(hwnd);
        PIXELFORMATDESCRIPTOR pfd = {}; pfd.nSize = sizeof pfd; pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER; pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24;
        int pf = ChoosePixelFormat(dc, &pfd);
        if (!pf || !SetPixelFormat(dc, pf, &pfd)) { initErr = "No usable OpenGL pixel format."; return false; }
        HGLRC tmp = wglCreateContext(dc);
        if (!tmp || !wglMakeCurrent(dc, tmp)) { initErr = "Couldn't start OpenGL. Update your graphics driver."; return false; }
        typedef HGLRC(WINAPI * CCA)(HDC, HGLRC, const int*);
        CCA cca = (CCA)wglGetProcAddress("wglCreateContextAttribsARB");
        if (cca) {
            const int attrs[] = {0x2091, 3, 0x2092, 3, 0x9126, 0x00000001, 0};  // 3.3 core
            HGLRC core = cca(dc, nullptr, attrs);
            if (core) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(tmp); tmp = core; wglMakeCurrent(dc, core); }
        }
        rc = tmp; dbg("context ok");
#elif defined(__APPLE__)
        // Offscreen core-profile context (macOS gives 4.1); we only ever draw into our own framebuffers.
        CGLPixelFormatAttribute attrs[] = {kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
                                           kCGLPFAColorSize, (CGLPixelFormatAttribute)24, kCGLPFAAlphaSize, (CGLPixelFormatAttribute)8,
                                           kCGLPFADepthSize, (CGLPixelFormatAttribute)24, kCGLPFAAccelerated, kCGLPFAAllowOfflineRenderers,
                                           (CGLPixelFormatAttribute)0};
        CGLPixelFormatObj pf = nullptr; GLint npf = 0;
        if (CGLChoosePixelFormat(attrs, &pf, &npf) != kCGLNoError || !pf) {
            attrs[8] = kCGLPFAAllowOfflineRenderers; attrs[9] = (CGLPixelFormatAttribute)0;  // drop "accelerated" and retry
            if (CGLChoosePixelFormat(attrs, &pf, &npf) != kCGLNoError || !pf) { initErr = "Couldn't start OpenGL on this Mac."; return false; }
        }
        CGLError ce = CGLCreateContext(pf, nullptr, &cgl);
        CGLDestroyPixelFormat(pf);
        if (ce != kCGLNoError || !cgl || CGLSetCurrentContext(cgl) != kCGLNoError) { initErr = "Couldn't start OpenGL on this Mac."; return false; }
        dbg("context ok");
#else
        initErr = "This platform isn't supported."; return false;
#endif
        if (!gladLoadGL((GLADloadfunc)glLoad)) { initErr = "Couldn't load OpenGL functions."; return false; }
        if (!GLAD_GL_VERSION_3_3) { initErr = "Element Genie needs OpenGL 3.3. Update your graphics driver."; return false; }
        dbg("glad ok %s", (const char*)glGetString(GL_VERSION));
        std::string e;
        if (!R.init(e)) { initErr = "Shader setup failed: " + e; return false; }
        return true;
    }
    Scene& sceneFor(const std::string& json) {
        for (auto it = cache.begin(); it != cache.end(); ++it)
            if (it->json == json) { cache.splice(cache.begin(), cache, it); return cache.front().scene; }
        Cached c; c.json = json; std::string err;
        if (!sceneFromJson(json, c.scene, err)) sceneFromJson(defaultSceneJson(), c.scene, err);
        cache.push_front(std::move(c));
        while (cache.size() > 6) cache.pop_back();
        return cache.front().scene;
    }
    std::vector<float> acc;
    GLuint smallFbo = 0, smallTex = 0; int smallW = 0, smallH = 0, curSS = 1;
    void ensureSmall(int w, int h, bool need) {
        if (!need || (w == smallW && h == smallH && smallFbo)) return;
        if (smallTex) glDeleteTextures(1, &smallTex);
        if (!smallFbo) glGenFramebuffers(1, &smallFbo);
        glGenTextures(1, &smallTex); glBindTexture(GL_TEXTURE_2D, smallTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, smallFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, smallTex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        smallW = w; smallH = h;
    }
    void renderOne(Scene& base, const FxParams& fx, int w, int h, float aspect) {
        Scene s = base;
        applyFx(s, fx);
        ViewOptions vo; vo.aspect = aspect;
        R.render(s, 0.f, rt, vo);
        px.resize((size_t)w * h * 4);
        if (curSS > 1) {  // supersampled: shrink to the output size
            glBindFramebuffer(GL_READ_FRAMEBUFFER, rt.outFbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, smallFbo);
            glBlitFramebuffer(0, 0, rt.w, rt.h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, smallFbo);
        } else glBindFramebuffer(GL_READ_FRAMEBUFFER, rt.outFbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    void doJob(Job& j) {
        dbg("job %dx%d", j.w, j.h);
        Scene& base = sceneFor(*j.json);
        for (auto& o : base.objects) if (o.visible) R.ensureGeometry(o);
        int ss = std::clamp(j.fx.supersample, 1, 3);
        while (ss > 1 && (size_t)j.w * j.h * ss * ss > 36000000ull) ss--;  // keep within GPU limits
        rt.ensure(j.w * ss, j.h * ss, j.fx.msaa);
        ensureSmall(j.w, j.h, ss > 1);
        curSS = ss;
        float aspect = j.w * j.par / (float)j.h;
        dbg("scene ready");
        if (j.blur.empty()) renderOne(base, j.fx, j.w, j.h, aspect);
        else {
            size_t n = (size_t)j.w * j.h * 4;
            acc.assign(n, 0.f);
            for (auto& fx : j.blur) {
                renderOne(base, fx, j.w, j.h, aspect);
                for (size_t i = 0; i < n; i++) acc[i] += px[i];
            }
            float inv = 1.f / j.blur.size();
            for (size_t i = 0; i < n; i++) px[i] = (uint8_t)std::min(255.f, acc[i] * inv + .5f);
        }
        // composite (AE 8-bit pixels are ARGB, straight alpha)
        for (int y = 0; y < j.h; y++) {
            const uint8_t* src = px.data() + (size_t)(j.h - 1 - y) * j.w * 4;
            const uint8_t* inr = j.in ? j.in + y * j.inRB : nullptr;
            uint8_t* o = j.out + y * j.outRB;
            for (int x = 0; x < j.w; x++) {
                float a3 = src[x * 4 + 3] / 255.f;
                float r = src[x * 4 + 0], g = src[x * 4 + 1], b = src[x * 4 + 2];  // premultiplied
                float oa = a3;
                if (inr && j.fx.composite) {
                    const uint8_t* ip = inr + x * 4;
                    float ia = ip[0] / 255.f, k = ia * (1 - a3);
                    r += ip[1] * k; g += ip[2] * k; b += ip[3] * k;
                    oa = a3 + k;
                }
                uint8_t* op = o + x * 4;
                if (oa <= 0.0001f) { op[0] = op[1] = op[2] = op[3] = 0; continue; }
                float inv = 1.f / oa;
                op[0] = (uint8_t)std::min(255.f, oa * 255.f + .5f);
                op[1] = (uint8_t)std::min(255.f, r * inv + .5f);
                op[2] = (uint8_t)std::min(255.f, g * inv + .5f);
                op[3] = (uint8_t)std::min(255.f, b * inv + .5f);
            }
        }
    }
    void loop() {
        bool ok = initGL();
        {
            std::lock_guard<std::mutex> lk(mx);
            initDone = true;
        }
        cv.notify_all();
        if (!ok) return;
        for (;;) {
            std::unique_lock<std::mutex> lk(mx);
            cv.wait(lk, [&] { return job && !job->done; });
            Job* j = job;
            lk.unlock();
            try { doJob(*j); } catch (...) {}
            lk.lock();
            j->done = true;
            cv.notify_all();
        }
    }
};

// ------------------------------------------------------------------ commands
static PF_Err About(PF_InData* in_data, PF_OutData* out_data) {
    PF_SPRINTF(out_data->return_msg, "Element Genie %d.%d\rOpen-source 3D titles, logos and models.\rClick Scene Setup to design, then keyframe in Effect Controls.", EG_MAJOR, EG_MINOR);
    return PF_Err_NONE;
}

static PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data) {
    out_data->my_version = PF_VERSION(EG_MAJOR, EG_MINOR, EG_BUG, EG_STAGE, EG_BUILD);
    // NON_PARAM_VARY: the picture changes over time even without keyframes (Auto Animate, wobble, replicator...),
    // so Premiere / After Effects must not reuse one rendered frame across the clip
    out_data->out_flags = PF_OutFlag_I_DO_DIALOG | PF_OutFlag_NON_PARAM_VARY;
    // After Effects needs CUSTOM_UI to accept the hidden Scene parameter; Premiere works as before without it.
    if (in_data && in_data->appl_id == 'FXTC') out_data->out_flags |= PF_OutFlag_CUSTOM_UI;
    out_data->out_flags2 = PF_OutFlag2_PARAM_GROUP_START_COLLAPSED_FLAG | PF_OutFlag2_SUPPORTS_THREADED_RENDERING | PF_OutFlag2_I_USE_3D_CAMERA | PF_OutFlag2_I_USE_3D_LIGHTS;
    return PF_Err_NONE;
}

static PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data) {
    PF_ParamDef def;
    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON("Scene Setup", "Scene Setup...", 0, PF_ParamFlag_SUPERVISE, 1);
    AEFX_CLR_STRUCT(def);
    PF_Handle dflt = makeArb(in_data, defaultSceneJson());
    PF_ADD_ARBITRARY2("Scene", 1, 1, PF_ParamFlag_CANNOT_TIME_VARY, PF_PUI_NO_ECW_UI, dflt, 2, nullptr);
    int id = 10;
    for (int g = 0; g < 5; g++) {
        char nm[32];
        snprintf(nm, sizeof nm, "Group %d", g + 1);
        PF_ADD_TOPICX(nm, g == 0 ? 0 : PF_ParamFlag_START_COLLAPSED, id++);
        AEFX_CLR_STRUCT(def);
        PF_ADD_FLOAT_SLIDERX("Position X", -10000, 10000, -10, 10, 0, 2, 0, 0, id++);
        PF_ADD_FLOAT_SLIDERX("Position Y", -10000, 10000, -10, 10, 0, 2, 0, 0, id++);
        PF_ADD_FLOAT_SLIDERX("Position Z", -10000, 10000, -10, 10, 0, 2, 0, 0, id++);
        AEFX_CLR_STRUCT(def); PF_ADD_ANGLE("Rotation X", 0, id++);
        AEFX_CLR_STRUCT(def); PF_ADD_ANGLE("Rotation Y", 0, id++);
        AEFX_CLR_STRUCT(def); PF_ADD_ANGLE("Rotation Z", 0, id++);
        PF_ADD_FLOAT_SLIDERX("Scale", 0, 10000, 0, 400, 100, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
        PF_ADD_FLOAT_SLIDERX("Opacity", 0, 100, 0, 100, 100, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
        AEFX_CLR_STRUCT(def); PF_END_TOPIC(id++);
    }
    PF_ADD_TOPICX("Camera", 0, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_ANGLE("Orbit", 0, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_ANGLE("Tilt", 0, id++);
    PF_ADD_FLOAT_SLIDERX("Zoom", 1, 10000, 10, 400, 100, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
    PF_ADD_FLOAT_SLIDERX("Pan X", -10000, 10000, -5, 5, 0, 2, 0, 0, id++);
    PF_ADD_FLOAT_SLIDERX("Pan Y", -10000, 10000, -5, 5, 0, 2, 0, 0, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_ANGLE("Roll", 0, id++);
    PF_ADD_FLOAT_SLIDERX("Lens (FOV +/-)", -100, 100, -30, 60, 0, 1, 0, 0, id++);
    AEFX_CLR_STRUCT(def); PF_END_TOPIC(id++);
    PF_ADD_TOPICX("Animation", 0, id++);
    PF_ADD_FLOAT_SLIDERX("Letters Built", 0, 100, 0, 100, 100, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_ANGLE("Environment Rotate", 0, id++);
    PF_ADD_FLOAT_SLIDERX("Light Brightness", 0, 1000, 0, 300, 100, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
    PF_ADD_FLOAT_SLIDERX("Glow", 0, 1000, 0, 300, 100, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
    AEFX_CLR_STRUCT(def); PF_END_TOPIC(id++);
    PF_ADD_TOPICX("Render", 0, id++);
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Show clip underneath", TRUE, 0, id++);
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Motion Blur", FALSE, 0, id++);
    PF_ADD_FLOAT_SLIDERX("Shutter Angle", 1, 720, 1, 360, 180, 0, 0, 0, id++);
    PF_ADD_FLOAT_SLIDERX("Blur Quality", 2, 32, 2, 16, 8, 0, 0, 0, id++);
    AEFX_CLR_STRUCT(def); PF_END_TOPIC(id++);
    PF_ADD_TOPICX("Realism", 0, id++);
    PF_ADD_POPUPX("Anti-aliasing", 5, 4, "Off|2x|4x|8x|16x", 0, id++);
    PF_ADD_POPUPX("Supersampling", 3, 1, "Off|2x (sharper, slower)|3x (best, slowest)", 0, id++);
    PF_ADD_POPUPX("Shadow Quality", 4, 3, "Low|Medium|High|Ultra", 0, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOXX("Floor Shadow", FALSE, 0, id++);
    PF_ADD_FLOAT_SLIDERX("Shadow Softness", 0, 100, 0, 100, 30, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
    PF_ADD_FLOAT_SLIDERX("Shadow Darkness", 0, 100, 0, 100, 55, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOXX("Ambient Occlusion", TRUE, 0, id++);
    PF_ADD_FLOAT_SLIDERX("AO Strength", 0, 300, 0, 200, 100, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
    PF_ADD_FLOAT_SLIDERX("AO Radius", 1, 300, 5, 150, 35, 1, 0, 0, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOXX("Floor Reflection", TRUE, 0, id++);
    PF_ADD_FLOAT_SLIDERX("Reflection Strength", 0, 100, 0, 100, 35, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
    PF_ADD_FLOAT_SLIDERX("Reflection Fade", 1, 1000, 5, 300, 80, 1, 0, 0, id++);
    AEFX_CLR_STRUCT(def); PF_END_TOPIC(id++);
    PF_ADD_TOPICX("Effects", 0, id++);
    PF_ADD_FLOAT_SLIDERX("Shatter", 0, 100, 0, 100, 0, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
    PF_ADD_FLOAT_SLIDERX("Deform Amount", 0, 1000, 0, 200, 100, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_ANGLE("Noise Evolution", 0, id++);
    PF_ADD_FLOAT_SLIDERX("Replicator Spread", 0, 10000, 0, 300, 100, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_ANGLE("Replicator Rotate", 0, id++);
    PF_ADD_FLOAT_SLIDERX("Replicator Build", 0, 100, 0, 100, 100, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
    AEFX_CLR_STRUCT(def); PF_END_TOPIC(id++);
    PF_ADD_TOPICX("Lens & Fog", PF_ParamFlag_START_COLLAPSED, id++);
    PF_ADD_POPUPX("Depth of Field", 3, 1, "As in Scene Setup|On|Off", 0, id++);
    PF_ADD_FLOAT_SLIDERX("Focus Offset", -1000, 1000, -10, 10, 0, 2, 0, 0, id++);
    PF_ADD_FLOAT_SLIDERX("Blur Amount", 0, 1000, 0, 300, 100, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
    PF_ADD_POPUPX("Fog", 3, 1, "As in Scene Setup|On|Off", 0, id++);
    PF_ADD_FLOAT_SLIDERX("Fog Amount", 0, 100, 0, 100, 100, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
    AEFX_CLR_STRUCT(def); PF_END_TOPIC(id++);
    PF_ADD_TOPICX("Output", PF_ParamFlag_START_COLLAPSED, id++);
    PF_ADD_POPUPX("Render Pass", 5, 1, "Final|Matte (alpha)|Depth|Normals|Ambient Occlusion", 0, id++);
    AEFX_CLR_STRUCT(def); PF_END_TOPIC(id++);
    // per letter: turn / move / scale every letter around its own centre (text only)
    PF_ADD_TOPICX("Per Letter", PF_ParamFlag_START_COLLAPSED, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_ANGLE("Letter Rotation X", 0, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_ANGLE("Letter Rotation Y", 0, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_ANGLE("Letter Rotation Z", 0, id++);
    PF_ADD_FLOAT_SLIDERX("Letter Position X", -10000, 10000, -5, 5, 0, 2, 0, 0, id++);
    PF_ADD_FLOAT_SLIDERX("Letter Position Y", -10000, 10000, -5, 5, 0, 2, 0, 0, id++);
    PF_ADD_FLOAT_SLIDERX("Letter Position Z", -10000, 10000, -5, 5, 0, 2, 0, 0, id++);
    PF_ADD_FLOAT_SLIDERX("Letter Scale", 0, 10000, 0, 400, 100, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
    PF_ADD_POPUPX("Spread", 6, 1, "Same for every letter|Ramp left to right|Ramp right to left|Centre out|Wave|Random", 0, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_ANGLE("Wave Offset", 0, id++);
    PF_ADD_FLOAT_SLIDERX("Randomness", 0, 100, 0, 100, 0, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
    PF_ADD_POPUPX("Letters In", 6, 1, "All groups|Group 1|Group 2|Group 3|Group 4|Group 5", 0, id++);
    AEFX_CLR_STRUCT(def); PF_END_TOPIC(id++);
    // After Effects: follow the comp's 3D camera
    PF_ADD_TOPICX("After Effects 3D", PF_ParamFlag_START_COLLAPSED, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOXX("Use Comp Camera", TRUE, 0, id++);
    PF_ADD_FLOAT_SLIDERX("World Scale", 1, 10000, 10, 400, 100, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOXX("Use Comp Lights", TRUE, 0, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOXX("Keep Scene Setup Lights", FALSE, 0, id++);
    AEFX_CLR_STRUCT(def); PF_END_TOPIC(id++);
    // Auto Animate: letter animations from Scene Setup play by themselves at the start / end of the clip
    PF_ADD_TOPICX("Auto Animate", 0, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOXX("Build In at Start", TRUE, 0, id++);
    PF_ADD_FLOAT_SLIDERX("Build In Time (sec)", 0.1, 30, 0.1, 6, 1.5, 2, PF_ValueDisplayFlag_NONE, 0, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOXX("Build Out at End", FALSE, 0, id++);
    PF_ADD_FLOAT_SLIDERX("Build Out Time (sec)", 0.1, 30, 0.1, 6, 1.0, 2, PF_ValueDisplayFlag_NONE, 0, id++);
    AEFX_CLR_STRUCT(def); PF_END_TOPIC(id++);
    // Twist & Bend: works on its own, no Scene Setup needed (adds to any Deform set in Scene Setup)
    PF_ADD_TOPICX("Twist & Bend", 0, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_ANGLE("Twist", 0, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_ANGLE("Bend", 0, id++);
    PF_ADD_FLOAT_SLIDERX("Taper", -100, 100, -100, 100, 0, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
    PF_ADD_FLOAT_SLIDERX("Wobble", 0, 500, 0, 200, 0, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_POPUPX("Twist Along", 3, 1, "Width (left to right)|Height (bottom to top)|Depth (front to back)", 0, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_POPUPX("Apply To", 6, 1, "All groups|Group 1|Group 2|Group 3|Group 4|Group 5", 0, id++);
    AEFX_CLR_STRUCT(def); PF_END_TOPIC(id++);
    // Look: animatable lighting / material / depth
    PF_ADD_TOPICX("Look", 0, id++);
    AEFX_CLR_STRUCT(def); PF_ADD_ANGLE("Light Rotate", 0, id++);
    PF_ADD_FLOAT_SLIDERX("Self Glow", 0, 1000, 0, 300, 100, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
    PF_ADD_FLOAT_SLIDERX("Extrusion Depth", 0, 1000, 0, 300, 100, 1, PF_ValueDisplayFlag_PERCENT, 0, id++);
    AEFX_CLR_STRUCT(def); PF_END_TOPIC(id++);
    out_data->num_params = P_NUM;
    return PF_Err_NONE;
}

static FxParams readFx(PF_ParamDef* params[]) {
    FxParams p;
    for (int g = 0; g < 5; g++) {
        int b = P_G0 + g * 10;
        p.g[g].pos = glm::vec3((float)params[b + G_PX]->u.fs_d.value, (float)params[b + G_PY]->u.fs_d.value, (float)params[b + G_PZ]->u.fs_d.value);
        p.g[g].rot = glm::vec3((float)FIX_2_FLOAT(params[b + G_RX]->u.ad.value), (float)FIX_2_FLOAT(params[b + G_RY]->u.ad.value), (float)FIX_2_FLOAT(params[b + G_RZ]->u.ad.value));
        p.g[g].scale = (float)params[b + G_SCALE]->u.fs_d.value;
        p.g[g].opacity = (float)params[b + G_OPACITY]->u.fs_d.value;
    }
    p.orbit = (float)FIX_2_FLOAT(params[P_CAM + 1]->u.ad.value);
    p.tilt = (float)FIX_2_FLOAT(params[P_CAM + 2]->u.ad.value);
    p.zoom = (float)params[P_CAM + 3]->u.fs_d.value;
    p.panX = (float)params[P_CAM + 4]->u.fs_d.value;
    p.panY = (float)params[P_CAM + 5]->u.fs_d.value;
    p.roll = (float)FIX_2_FLOAT(params[P_CAM + 6]->u.ad.value);
    p.fovOff = (float)params[P_CAM + 7]->u.fs_d.value;
    p.letters = (float)params[P_ANIM + 1]->u.fs_d.value;
    p.twist = (float)FIX_2_FLOAT(params[P_DEF + 1]->u.ad.value);
    p.bend = (float)FIX_2_FLOAT(params[P_DEF + 2]->u.ad.value);
    p.taper = (float)params[P_DEF + 3]->u.fs_d.value;
    p.wobble = (float)params[P_DEF + 4]->u.fs_d.value;
    p.defAxis = (int)params[P_DEF + 5]->u.pd.value - 1;
    p.defTarget = (int)params[P_DEF + 6]->u.pd.value - 1;
    p.lightRot = (float)FIX_2_FLOAT(params[P_LOOK + 1]->u.ad.value);
    p.selfGlow = (float)params[P_LOOK + 2]->u.fs_d.value;
    p.depthPct = (float)params[P_LOOK + 3]->u.fs_d.value;
    p.envRot = (float)FIX_2_FLOAT(params[P_ANIM + 2]->u.ad.value);
    p.lightPct = (float)params[P_ANIM + 3]->u.fs_d.value;
    p.glowPct = (float)params[P_ANIM + 4]->u.fs_d.value;
    p.composite = params[P_REND + 1]->u.bd.value != 0;
    static const int aa[] = {0, 2, 4, 8, 16};
    p.msaa = aa[std::clamp((int)params[P_REAL + 1]->u.pd.value - 1, 0, 4)];
    p.supersample = std::clamp((int)params[P_REAL + 2]->u.pd.value, 1, 3);
    static const int sr[] = {1024, 2048, 4096, 8192};
    p.shadowRes = sr[std::clamp((int)params[P_REAL + 3]->u.pd.value - 1, 0, 3)];
    p.floorShadow = params[P_REAL + 4]->u.bd.value != 0;
    p.shadowSoft = (float)params[P_REAL + 5]->u.fs_d.value;
    p.shadowDark = (float)params[P_REAL + 6]->u.fs_d.value;
    p.ao = params[P_REAL + 7]->u.bd.value != 0;
    p.aoStrength = (float)params[P_REAL + 8]->u.fs_d.value;
    p.aoRadius = (float)params[P_REAL + 9]->u.fs_d.value;
    p.reflection = params[P_REAL + 10]->u.bd.value != 0;
    p.reflStrength = (float)params[P_REAL + 11]->u.fs_d.value;
    p.reflFade = (float)params[P_REAL + 12]->u.fs_d.value;
    p.shatter = (float)params[P_FX + 1]->u.fs_d.value;
    p.deform = (float)params[P_FX + 2]->u.fs_d.value;
    p.noiseEvo = (float)FIX_2_FLOAT(params[P_FX + 3]->u.ad.value);
    p.repSpread = (float)params[P_FX + 4]->u.fs_d.value;
    p.repRotate = (float)FIX_2_FLOAT(params[P_FX + 5]->u.ad.value);
    p.repBuild = (float)params[P_FX + 6]->u.fs_d.value;
    p.dofMode = std::clamp((int)params[P_LENS + 1]->u.pd.value - 1, 0, 2);
    p.focusOffset = (float)params[P_LENS + 2]->u.fs_d.value;
    p.blur = (float)params[P_LENS + 3]->u.fs_d.value;
    p.fogMode = std::clamp((int)params[P_LENS + 4]->u.pd.value - 1, 0, 2);
    p.fog = (float)params[P_LENS + 5]->u.fs_d.value;
    p.pass = std::clamp((int)params[P_OUT + 1]->u.pd.value - 1, 0, 4);
    p.lRot = glm::vec3((float)FIX_2_FLOAT(params[P_LET + 1]->u.ad.value), (float)FIX_2_FLOAT(params[P_LET + 2]->u.ad.value), (float)FIX_2_FLOAT(params[P_LET + 3]->u.ad.value));
    p.lPos = glm::vec3((float)params[P_LET + 4]->u.fs_d.value, (float)params[P_LET + 5]->u.fs_d.value, (float)params[P_LET + 6]->u.fs_d.value);
    p.lScale = (float)params[P_LET + 7]->u.fs_d.value;
    p.lSpread = std::clamp((int)params[P_LET + 8]->u.pd.value - 1, 0, 5);
    p.lPhase = (float)FIX_2_FLOAT(params[P_LET + 9]->u.ad.value);
    p.lRandom = (float)params[P_LET + 10]->u.fs_d.value;
    p.lTarget = std::clamp((int)params[P_LET + 11]->u.pd.value - 1, 0, 5);
    if (p.pass > 0) p.composite = false;  // passes are for compositing: no clip underneath
    return p;
}

// After Effects: read the comp's active 3D camera and convert it to scene units.
// AE space: pixels, x right, y down, z away from viewer, origin top-left of the comp.
// Scene space: comp centre at the origin, y up, z toward viewer, 1 unit = comp height / 4 * scale.
static bool readAECamera(PF_InData* in_data, float worldScalePct, FxParams& fx) {
    if (in_data->appl_id != 'FXTC' || !in_data->pica_basicP || !in_data->effect_ref) return false;
    SPBasicSuite* sp = in_data->pica_basicP;
    const AEGP_PFInterfaceSuite1* pfi = nullptr; const AEGP_LayerSuite5* ls = nullptr;
    const AEGP_StreamSuite2* ss = nullptr; const AEGP_CompSuite4* cs = nullptr; const AEGP_ItemSuite6* is = nullptr;
    bool ok = false;
    if (sp->AcquireSuite(kAEGPPFInterfaceSuite, kAEGPPFInterfaceSuiteVersion1, (const void**)&pfi) || !pfi) return false;
    sp->AcquireSuite(kAEGPLayerSuite, kAEGPLayerSuiteVersion5, (const void**)&ls);
    sp->AcquireSuite(kAEGPStreamSuite, kAEGPStreamSuiteVersion2, (const void**)&ss);
    sp->AcquireSuite(kAEGPCompSuite, kAEGPCompSuiteVersion4, (const void**)&cs);
    sp->AcquireSuite(kAEGPItemSuite, kAEGPItemSuiteVersion6, (const void**)&is);
    if (ls && ss) {
        A_Time t = {0, 1};
        AEGP_LayerH cam = nullptr, me = nullptr;
        if (!pfi->AEGP_ConvertEffectToCompTime(in_data->effect_ref, in_data->current_time, in_data->time_scale, &t) &&
            !pfi->AEGP_GetEffectCamera(in_data->effect_ref, &t, &cam) && cam) {
            A_Matrix4 m; AEGP_StreamVal zoom; AEFX_CLR_STRUCT(zoom);
            if (!ls->AEGP_GetLayerToWorldXform(cam, &t, &m) &&
                !ss->AEGP_GetLayerStreamValue(cam, AEGP_LayerStream_ZOOM, AEGP_LTimeMode_CompTime, &t, FALSE, &zoom, nullptr)) {
                double W = in_data->width, H = in_data->height;
                if (cs && is && !pfi->AEGP_GetEffectLayer(in_data->effect_ref, &me) && me) {
                    AEGP_CompH comp = nullptr; AEGP_ItemH item = nullptr; A_long cw = 0, ch = 0;
                    if (!ls->AEGP_GetLayerParentComp(me, &comp) && comp && !cs->AEGP_GetItemFromComp(comp, &item) && item &&
                        !is->AEGP_GetItemDimensions(item, &cw, &ch) && cw > 0 && ch > 0) { W = cw; H = ch; }
                }
                double u = H / 4.0 * 100.0 / std::max(1.0, (double)worldScalePct);
                auto axis = [&](int r) { glm::dvec3 a(m.mat[r][0], m.mat[r][1], m.mat[r][2]); double l = glm::length(a); return l > 1e-9 ? a / l : a; };
                glm::dvec3 X = axis(0), Y = axis(1), Z = axis(2);
                glm::dvec3 P(m.mat[3][0], m.mat[3][1], m.mat[3][2]);
                fx.aePos = glm::vec3((float)((P.x - W / 2) / u), (float)(-(P.y - H / 2) / u), (float)(-P.z / u));
                fx.aeFwd = glm::vec3((float)Z.x, (float)-Z.y, (float)-Z.z);
                fx.aeUp = glm::vec3((float)-Y.x, (float)Y.y, (float)Y.z);
                double zv = zoom.one_d > 1 ? zoom.one_d : 1;
                fx.aeFov = (float)glm::degrees(2.0 * std::atan(H / 2.0 / zv));
                // camera depth of field (Camera Options > Depth of Field)
                AEGP_StreamVal dof, fd, ap, bl; AEFX_CLR_STRUCT(dof); AEFX_CLR_STRUCT(fd); AEFX_CLR_STRUCT(ap); AEFX_CLR_STRUCT(bl);
                fx.aeDof = false;
                if (!ss->AEGP_GetLayerStreamValue(cam, AEGP_LayerStream_DEPTH_OF_FIELD, AEGP_LTimeMode_CompTime, &t, FALSE, &dof, nullptr) && dof.one_d > 0.5 &&
                    !ss->AEGP_GetLayerStreamValue(cam, AEGP_LayerStream_FOCUS_DISTANCE, AEGP_LTimeMode_CompTime, &t, FALSE, &fd, nullptr)) {
                    double apPx = 25, blur = 100;
                    if (!ss->AEGP_GetLayerStreamValue(cam, AEGP_LayerStream_APERTURE, AEGP_LTimeMode_CompTime, &t, FALSE, &ap, nullptr)) apPx = ap.one_d;
                    if (!ss->AEGP_GetLayerStreamValue(cam, AEGP_LayerStream_BLUR_LEVEL, AEGP_LTimeMode_CompTime, &t, FALSE, &bl, nullptr)) blur = bl.one_d;
                    fx.aeDof = true;
                    fx.aeFocus = (float)(fd.one_d / u);
                    fx.aeAperture = (float)std::clamp(apPx / (H * 0.05) * blur / 100.0 * 0.5, 0.0, 1.0);
                }
                fx.aeCam = true; ok = true;
            }
        }
    }
    if (is) sp->ReleaseSuite(kAEGPItemSuite, kAEGPItemSuiteVersion6);
    if (cs) sp->ReleaseSuite(kAEGPCompSuite, kAEGPCompSuiteVersion4);
    if (ss) sp->ReleaseSuite(kAEGPStreamSuite, kAEGPStreamSuiteVersion2);
    if (ls) sp->ReleaseSuite(kAEGPLayerSuite, kAEGPLayerSuiteVersion5);
    sp->ReleaseSuite(kAEGPPFInterfaceSuite, kAEGPPFInterfaceSuiteVersion1);
    return ok;
}

// After Effects: read the comp's light layers (parallel, spot, point, ambient) into scene units.
static void readAELights(PF_InData* in_data, float worldScalePct, FxParams& fx) {
    fx.aeLightN = 0;
    if (in_data->appl_id != 'FXTC' || !in_data->pica_basicP || !in_data->effect_ref) return;
    SPBasicSuite* sp = in_data->pica_basicP;
    const AEGP_PFInterfaceSuite1* pfi = nullptr; const AEGP_LayerSuite5* ls = nullptr; const AEGP_StreamSuite2* ss = nullptr;
    const AEGP_CompSuite4* cs = nullptr; const AEGP_ItemSuite6* is = nullptr; const AEGP_LightSuite2* lt = nullptr;
    if (sp->AcquireSuite(kAEGPPFInterfaceSuite, kAEGPPFInterfaceSuiteVersion1, (const void**)&pfi) || !pfi) return;
    sp->AcquireSuite(kAEGPLayerSuite, kAEGPLayerSuiteVersion5, (const void**)&ls);
    sp->AcquireSuite(kAEGPStreamSuite, kAEGPStreamSuiteVersion2, (const void**)&ss);
    sp->AcquireSuite(kAEGPCompSuite, kAEGPCompSuiteVersion4, (const void**)&cs);
    sp->AcquireSuite(kAEGPItemSuite, kAEGPItemSuiteVersion6, (const void**)&is);
    sp->AcquireSuite(kAEGPLightSuite, kAEGPLightSuiteVersion2, (const void**)&lt);
    AEGP_LayerH me = nullptr; AEGP_CompH comp = nullptr; A_Time t = {0, 1};
    if (ls && ss && cs && is && lt && !pfi->AEGP_GetEffectLayer(in_data->effect_ref, &me) && me &&
        !ls->AEGP_GetLayerParentComp(me, &comp) && comp &&
        !pfi->AEGP_ConvertEffectToCompTime(in_data->effect_ref, in_data->current_time, in_data->time_scale, &t)) {
        double W = in_data->width, H = in_data->height;
        AEGP_ItemH item = nullptr; A_long cw = 0, ch = 0;
        if (!cs->AEGP_GetItemFromComp(comp, &item) && item && !is->AEGP_GetItemDimensions(item, &cw, &ch) && cw > 0 && ch > 0) { W = cw; H = ch; }
        double u = H / 4.0 * 100.0 / std::max(1.0, (double)worldScalePct);
        A_long n = 0; ls->AEGP_GetCompNumLayers(comp, &n);
        for (A_long i = 0; i < n && fx.aeLightN < FxParams::MAX_AE_LIGHTS; i++) {
            AEGP_LayerH L = nullptr; AEGP_ObjectType ot = AEGP_ObjectType_NONE; A_Boolean on = FALSE;
            if (ls->AEGP_GetCompLayerByIndex(comp, i, &L) || !L) continue;
            if (ls->AEGP_GetLayerObjectType(L, &ot) || ot != AEGP_ObjectType_LIGHT) continue;
            if (ls->AEGP_IsVideoActive(L, AEGP_LTimeMode_CompTime, &t, &on) || !on) continue;
            AEGP_LightType ty = AEGP_LightType_POINT; lt->AEGP_GetLightType(L, &ty);
            AEGP_StreamVal iv, cv, sv; AEFX_CLR_STRUCT(iv); AEFX_CLR_STRUCT(cv); AEFX_CLR_STRUCT(sv);
            double inten = 100; glm::vec3 col(1);
            if (!ss->AEGP_GetLayerStreamValue(L, AEGP_LayerStream_INTENSITY, AEGP_LTimeMode_CompTime, &t, FALSE, &iv, nullptr)) inten = iv.one_d;
            if (!ss->AEGP_GetLayerStreamValue(L, AEGP_LayerStream_COLOR, AEGP_LTimeMode_CompTime, &t, FALSE, &cv, nullptr))
                col = glm::vec3((float)cv.color.redF, (float)cv.color.greenF, (float)cv.color.blueF);
            bool shadow = false;
            if (ty != AEGP_LightType_AMBIENT && !ss->AEGP_GetLayerStreamValue(L, AEGP_LayerStream_CASTS_SHADOWS, AEGP_LTimeMode_CompTime, &t, FALSE, &sv, nullptr))
                shadow = sv.one_d > 0.5;
            A_Matrix4 m;
            if (ls->AEGP_GetLayerToWorldXform(L, &t, &m)) continue;
            glm::dvec3 Z(m.mat[2][0], m.mat[2][1], m.mat[2][2]); double zl = glm::length(Z); if (zl > 1e-9) Z /= zl;
            glm::dvec3 P(m.mat[3][0], m.mat[3][1], m.mat[3][2]);
            auto& o = fx.aeLights[fx.aeLightN++];
            o.color = col * (float)(std::max(0.0, inten) / 100.0);
            o.shadow = shadow;
            if (ty == AEGP_LightType_AMBIENT) { o.type = 2; o.pos = glm::vec3(0); }
            else if (ty == AEGP_LightType_PARALLEL) {  // direction towards the light
                o.type = 0; o.pos = glm::vec3((float)-Z.x, (float)Z.y, (float)Z.z) * 10.f;
            } else {
                o.type = 1; o.pos = glm::vec3((float)((P.x - W / 2) / u), (float)(-(P.y - H / 2) / u), (float)(-P.z / u));
                if (ty == AEGP_LightType_SPOT) {
                    AEGP_StreamVal ca, cf; AEFX_CLR_STRUCT(ca); AEFX_CLR_STRUCT(cf);
                    double cone = 90, feather = 50;
                    if (!ss->AEGP_GetLayerStreamValue(L, AEGP_LayerStream_CONE_ANGLE, AEGP_LTimeMode_CompTime, &t, FALSE, &ca, nullptr)) cone = ca.one_d;
                    if (!ss->AEGP_GetLayerStreamValue(L, AEGP_LayerStream_CONE_FEATHER, AEGP_LTimeMode_CompTime, &t, FALSE, &cf, nullptr)) feather = cf.one_d;
                    double half = glm::radians(std::clamp(cone, 1.0, 179.0) * 0.5);
                    o.dir = glm::vec3((float)Z.x, (float)-Z.y, (float)-Z.z);
                    o.cosOuter = (float)std::cos(half);
                    o.cosInner = (float)std::cos(half * (1.0 - std::clamp(feather, 0.0, 100.0) / 100.0));
                }
            }
        }
    }
    if (lt) sp->ReleaseSuite(kAEGPLightSuite, kAEGPLightSuiteVersion2);
    if (is) sp->ReleaseSuite(kAEGPItemSuite, kAEGPItemSuiteVersion6);
    if (cs) sp->ReleaseSuite(kAEGPCompSuite, kAEGPCompSuiteVersion4);
    if (ss) sp->ReleaseSuite(kAEGPStreamSuite, kAEGPStreamSuiteVersion2);
    if (ls) sp->ReleaseSuite(kAEGPLayerSuite, kAEGPLayerSuiteVersion5);
    sp->ReleaseSuite(kAEGPPFInterfaceSuite, kAEGPPFInterfaceSuiteVersion1);
}

// Premiere hands effects sequence/media time; Auto Animate needs time since the clip starts so the
// build moves with the clip when it's dragged along the timeline.
static void clipRelativeTime(PF_InData* in_data, double& t, double& dur) {
    if (in_data->appl_id == 'FXTC' || !in_data->pica_basicP || in_data->time_step <= 0) return;
    const PF_UtilitySuite4* us = nullptr;
    if (in_data->pica_basicP->AcquireSuite(kPFUtilitySuite, kPFUtilitySuiteVersion4, (const void**)&us) || !us) return;
    A_long clipStart = 0, trackStart = 0, clipDur = 0;
    bool okC = us->GetClipStart && !us->GetClipStart(in_data->effect_ref, &clipStart);
    bool okT = us->GetTrackItemStart && !us->GetTrackItemStart(in_data->effect_ref, &trackStart);
    bool okD = us->GetClipDuration && !us->GetClipDuration(in_data->effect_ref, &clipDur);
    in_data->pica_basicP->ReleaseSuite(kPFUtilitySuite, kPFUtilitySuiteVersion4);
    double ts = in_data->time_scale ? (double)in_data->time_scale : 1.0;
    double frame = (double)in_data->time_step / ts;
    // Premiere reports these in frames on some versions and in time_scale units on others: try both
    double d = dur;
    if (okD && clipDur > 0) { double a = clipDur * frame, b = clipDur / ts; d = (dur > 0 && std::fabs(b - dur) < std::fabs(a - dur)) ? b : a; }
    double cands[4] = {okT ? trackStart * frame : -1.0, okT ? trackStart / ts : -1.0, okC ? clipStart * frame : -1.0, okC ? clipStart / ts : -1.0};
    for (double o : cands) {
        if (o <= 0) continue;
        double r = t - o;
        if (r >= -frame * 0.5 && (d <= 0 || r <= d + frame)) { t = std::max(0.0, r); break; }
    }
    if (d > 0) dur = d;
}

static PF_Err Render(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
    dbg("Render enter");
    PF_EffectWorld* input = &params[P_INPUT]->u.ld;
    std::string json = readArb(in_data, (PF_Handle)params[P_SCENE]->u.arb_d.value);
    if (json.empty()) json = defaultSceneJson();
    int w = output->width, h = output->height;
    if (w <= 0 || h <= 0) return PF_Err_NONE;
    dbg("json %zu", json.size());
    bool sameSize = input && input->data && input->width == w && input->height == h;
    // remember the frame for the Scene Setup background
    if (sameSize && (size_t)w * h <= 3840ull * 2160ull) {
        auto& wk = GLWorker::get();
        std::lock_guard<std::mutex> lk(wk.refMx);
        wk.refW = w; wk.refH = h; wk.refRGBA.resize((size_t)w * h * 4);
        for (int y = 0; y < h; y++) {
            const uint8_t* s = (const uint8_t*)input->data + (ptrdiff_t)y * input->rowbytes;
            uint8_t* d = wk.refRGBA.data() + (size_t)y * w * 4;
            for (int x = 0; x < w; x++) { d[x * 4] = s[x * 4 + 1]; d[x * 4 + 1] = s[x * 4 + 2]; d[x * 4 + 2] = s[x * 4 + 3]; d[x * 4 + 3] = 255; }
        }
    }
    dbg("ref copied");
    Job j;
    j.json = &json; j.fx = readFx(params); j.w = w; j.h = h;
    j.par = in_data->pixel_aspect_ratio.den ? (float)in_data->pixel_aspect_ratio.num / in_data->pixel_aspect_ratio.den : 1.f;
    // downsampled previews keep the same shape
    if (in_data->downsample_x.den && in_data->downsample_y.den) {
        float dx = (float)in_data->downsample_x.num / in_data->downsample_x.den, dy = (float)in_data->downsample_y.num / in_data->downsample_y.den;
        if (dx > 0 && dy > 0) j.par *= dx / dy;
    }
    // motion blur: sample the keyframed values across the shutter
    if (params[P_REND + 2]->u.bd.value && in_data->time_step != 0) {
        int n = std::clamp((int)std::lround(params[P_REND + 4]->u.fs_d.value), 2, 32);
        double shutter = params[P_REND + 3]->u.fs_d.value / 360.0;
        PF_ParamDef* tmp[P_NUM];
        std::vector<PF_ParamDef> defs(P_NUM);
        for (int i = 0; i < n; i++) {
            double off = (-0.5 + (i + 0.5) / n) * shutter;  // centred on the frame
            A_long t = in_data->current_time + (A_long)std::lround(off * in_data->time_step);
            bool ok = true;
            for (int k = 0; k < P_NUM; k++) tmp[k] = params[k];
            for (int k = P_G0; k < P_NUM; k++) {
                if (k >= P_REND && k < P_FX) continue;
                PF_ParamType ty = params[k]->param_type;
                if (ty != PF_Param_FLOAT_SLIDER && ty != PF_Param_ANGLE) continue;
                AEFX_CLR_STRUCT(defs[k]);
                if (PF_CHECKOUT_PARAM(in_data, k, t, in_data->time_step, in_data->time_scale, &defs[k]) != PF_Err_NONE) { ok = false; break; }
                tmp[k] = &defs[k];
            }
            if (ok) j.blur.push_back(readFx(tmp));
            for (int k = P_G0; k < P_NUM; k++) if (tmp[k] == &defs[k]) PF_CHECKIN_PARAM(in_data, &defs[k]);
            if (!ok) { j.blur.clear(); break; }
        }
        // nothing moving? skip the extra renders
        bool moving = false;
        for (size_t i = 1; i < j.blur.size() && !moving; i++) moving = memcmp(&j.blur[i], &j.blur[0], sizeof(FxParams)) != 0;
        if (!moving) j.blur.clear();
    }
    // After Effects comp camera
    if (params[P_AE + 1]->u.bd.value) {
        FxParams cam;
        if (readAECamera(in_data, (float)params[P_AE + 2]->u.fs_d.value, cam)) {
            auto copyCam = [&](FxParams& d) { d.aeCam = true; d.aePos = cam.aePos; d.aeFwd = cam.aeFwd; d.aeUp = cam.aeUp; d.aeFov = cam.aeFov; d.aeDof = cam.aeDof; d.aeFocus = cam.aeFocus; d.aeAperture = cam.aeAperture; };
            copyCam(j.fx);
            for (auto& b : j.blur) copyCam(b);
        }
    }
    // Auto Animate: letters build in after the clip starts / out before it ends
    {
        double ts = in_data->time_scale ? (double)in_data->time_scale : 1.0;
        double t = in_data->current_time / ts, dur = in_data->total_time / ts;
        clipRelativeTime(in_data, t, dur);
        float a = 100.f;
        if (params[P_AUTO + 1]->u.bd.value) a = std::min(a, (float)(t / std::max(0.05, params[P_AUTO + 2]->u.fs_d.value) * 100.0));
        if (params[P_AUTO + 3]->u.bd.value && dur > 0) a = std::min(a, (float)((dur - t) / std::max(0.05, params[P_AUTO + 4]->u.fs_d.value) * 100.0));
        a = std::clamp(a, 0.f, 100.f);
        j.fx.autoLetters = a;
        for (auto& b : j.blur) b.autoLetters = a;
    }
    // After Effects comp lights
    if (params[P_AE + 3]->u.bd.value) {
        FxParams lg;
        readAELights(in_data, (float)params[P_AE + 2]->u.fs_d.value, lg);
        if (lg.aeLightN > 0) {
            bool keep = params[P_AE + 4]->u.bd.value != 0;
            auto copyL = [&](FxParams& d) { d.aeLightN = lg.aeLightN; d.aeKeepLights = keep; for (int i = 0; i < lg.aeLightN; i++) d.aeLights[i] = lg.aeLights[i]; };
            copyL(j.fx);
            for (auto& b : j.blur) copyL(b);
        }
    }
    j.in = sameSize ? (const uint8_t*)input->data : nullptr; j.inRB = sameSize ? input->rowbytes : 0;
    j.out = (uint8_t*)output->data; j.outRB = output->rowbytes;
    std::string err;
    if (!GLWorker::get().render(j, err)) {
        if (sameSize) PF_COPY(input, output, NULL, NULL);
        PF_STRCPY(out_data->return_msg, err.c_str());
        out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE;
    }
    return PF_Err_NONE;
}

// ------------------------------------------------------------------ Scene Setup pop-up
static FILE* openWrite(const std::string& p) {
#ifdef _WIN32
    return _wfopen(widen(p).c_str(), L"wb");
#else
    return fopen(p.c_str(), "wb");
#endif
}
#ifdef _WIN32
static std::string tempDir() {
    wchar_t t[MAX_PATH]; GetTempPathW(MAX_PATH, t);
    std::wstring d = std::wstring(t) + L"ElementGenie\\";
    CreateDirectoryW(d.c_str(), nullptr);
    return narrow(d);
}
static std::string moduleDir() {
    wchar_t b[MAX_PATH]; GetModuleFileNameW(g_module, b, MAX_PATH);
    std::wstring s(b); return narrow(s.substr(0, s.find_last_of(L"\\/") + 1));
}
#else
static std::string tempDir() {
    const char* t = getenv("TMPDIR"); std::string d = (t && *t) ? t : "/tmp/";
    if (d.back() != '/') d += '/';
    d += "ElementGenie/"; mkdir(d.c_str(), 0700);
    return d;
}
static std::string moduleDir() {  // .../ElementGenie.plugin/Contents/MacOS/
    Dl_info info = {};
    if (!dladdr((void*)&moduleDir, &info) || !info.dli_fname) return "./";
    std::string s = info.dli_fname; return s.substr(0, s.find_last_of('/') + 1);
}
#endif
static bool writeText(const std::string& p, const std::string& s) {
    FILE* f = openWrite(p); if (!f) return false;
    fwrite(s.data(), 1, s.size(), f); fclose(f); return true;
}
static void removeFile(const std::string& p) {
#ifdef _WIN32
    DeleteFileW(widen(p).c_str());
#else
    unlink(p.c_str());
#endif
}

// After Effects: turn the letter animation into real, editable keyframes on Animation > Letters Built
// (0% at the layer's in-point, 100% after Build In Time) and switch Auto Animate's build-in off.
// Premiere doesn't let effects create keyframes, so there Auto Animate does the job.
static void addBuildKeyframes(PF_InData* in_data, PF_ParamDef* params[], const std::string& json) {
    if (in_data->appl_id != 'FXTC' || !in_data->pica_basicP || !params[P_AUTO + 1]->u.bd.value) return;
    Scene sc; std::string err;
    if (!sceneFromJson(json, sc, err)) return;
    bool anyLetters = false; for (auto& o : sc.objects) anyLetters |= o.letters.enabled && o.type == ObjType::Text;
    if (!anyLetters) return;
    SPBasicSuite* sp = in_data->pica_basicP;
    const AEGP_PFInterfaceSuite1* pfi = nullptr; const AEGP_StreamSuite2* ss = nullptr; const AEGP_KeyframeSuite4* ks = nullptr;
    const AEGP_EffectSuite2* es = nullptr; const AEGP_LayerSuite5* ls = nullptr; const AEGP_UtilitySuite3* us = nullptr;
    sp->AcquireSuite(kAEGPPFInterfaceSuite, kAEGPPFInterfaceSuiteVersion1, (const void**)&pfi);
    sp->AcquireSuite(kAEGPStreamSuite, kAEGPStreamSuiteVersion2, (const void**)&ss);
    sp->AcquireSuite(kAEGPKeyframeSuite, kAEGPKeyframeSuiteVersion4, (const void**)&ks);
    sp->AcquireSuite(kAEGPEffectSuite, kAEGPEffectSuiteVersion2, (const void**)&es);
    sp->AcquireSuite(kAEGPLayerSuite, kAEGPLayerSuiteVersion5, (const void**)&ls);
    sp->AcquireSuite(kAEGPUtilitySuite, kAEGPUtilitySuiteVersion3, (const void**)&us);
    if (pfi && ss && ks && es && ls) {
        AEGP_EffectRefH eff = nullptr; AEGP_LayerH layer = nullptr; AEGP_StreamRefH st = nullptr;
        if (!pfi->AEGP_GetNewEffectForEffect(0, in_data->effect_ref, &eff) && eff && !pfi->AEGP_GetEffectLayer(in_data->effect_ref, &layer) && layer &&
            !ss->AEGP_GetNewEffectStreamByIndex(0, eff, P_ANIM + 1, &st) && st) {
            A_long nk = 0;
            if (!ks->AEGP_GetStreamNumKFs(st, &nk) && nk == 0) {
                A_Time in = {0, 1};
                ls->AEGP_GetLayerInPoint(layer, AEGP_LTimeMode_LayerTime, &in);
                double secs = std::max(0.1, (double)params[P_AUTO + 2]->u.fs_d.value);
                A_Time t1 = in; t1.value = in.value + (A_long)std::lround(secs * in.scale);
                if (us) us->AEGP_StartUndoGroup("Element Genie letter build");
                AEGP_AddKeyframesInfoH ak = nullptr;
                if (!ks->AEGP_StartAddKeyframes(st, &ak) && ak) {
                    A_long i0 = 0, i1 = 0;
                    AEGP_StreamValue2 v; AEFX_CLR_STRUCT(v); v.streamH = st;
                    if (!ks->AEGP_AddKeyframes(ak, AEGP_LTimeMode_LayerTime, &in, &i0)) { v.val.one_d = 0; ks->AEGP_SetAddKeyframe(ak, i0, &v); }
                    if (!ks->AEGP_AddKeyframes(ak, AEGP_LTimeMode_LayerTime, &t1, &i1)) { v.val.one_d = 100; ks->AEGP_SetAddKeyframe(ak, i1, &v); }
                    ks->AEGP_EndAddKeyframes(TRUE, ak);
                    // the keyframes now drive the build: stop Auto Animate doing it twice
                    params[P_AUTO + 1]->u.bd.value = FALSE;
                    params[P_AUTO + 1]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
                }
                if (us) us->AEGP_EndUndoGroup();
            }
        }
        if (st) ss->AEGP_DisposeStream(st);
        if (eff) es->AEGP_DisposeEffect(eff);
    }
    if (us) sp->ReleaseSuite(kAEGPUtilitySuite, kAEGPUtilitySuiteVersion3);
    if (ls) sp->ReleaseSuite(kAEGPLayerSuite, kAEGPLayerSuiteVersion5);
    if (es) sp->ReleaseSuite(kAEGPEffectSuite, kAEGPEffectSuiteVersion2);
    if (ks) sp->ReleaseSuite(kAEGPKeyframeSuite, kAEGPKeyframeSuiteVersion4);
    if (ss) sp->ReleaseSuite(kAEGPStreamSuite, kAEGPStreamSuiteVersion2);
    if (pfi) sp->ReleaseSuite(kAEGPPFInterfaceSuite, kAEGPPFInterfaceSuiteVersion1);
}

static PF_Err RunSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[]) {
    std::string json = readArb(in_data, (PF_Handle)params[P_SCENE]->u.arb_d.value);
    if (json.empty()) json = defaultSceneJson();
    std::string dir = tempDir();
    char tag[48];
#ifdef _WIN32
    snprintf(tag, sizeof tag, "%lu_%lu", GetCurrentProcessId(), GetTickCount());
#else
    { timeval tv; gettimeofday(&tv, nullptr); snprintf(tag, sizeof tag, "%d_%ld%06ld", (int)getpid(), (long)tv.tv_sec, (long)tv.tv_usec); }
#endif
    std::string inP = dir + "scene_in_" + tag + ".json", outP = dir + "scene_out_" + tag + ".json", refP = dir + "frame_" + tag + ".png";
    writeText(inP, json);
    bool haveRef = false;
    {
        auto& wk = GLWorker::get();
        std::lock_guard<std::mutex> lk(wk.refMx);
        if (wk.refW > 0) {
            FILE* f = openWrite(refP);
            if (f) {
                stbi_write_png_to_func([](void* c, void* d, int n) { fwrite(d, 1, n, (FILE*)c); }, f, wk.refW, wk.refH, 4, wk.refRGBA.data(), wk.refW * 4);
                fclose(f); haveRef = true;
            }
        }
    }
#ifdef _WIN32
    HWND parent = nullptr;
    PF_GET_PLATFORM_DATA(PF_PlatData_MAIN_WND, &parent);
#else
    void* parent = nullptr;
#endif
    float par = in_data->pixel_aspect_ratio.den ? (float)in_data->pixel_aspect_ratio.num / in_data->pixel_aspect_ratio.den : 1.f;
    double fps = in_data->time_step > 0 ? (double)in_data->time_scale / in_data->time_step : 25.0;
    int W = in_data->width, H = in_data->height;
#ifdef _WIN32
    std::string exe = moduleDir() + "Element Genie Scene Setup.exe";
#else
    // the Scene Setup app ships inside the plug-in bundle: Contents/Resources/Element Genie Scene Setup.app
    std::string exe = moduleDir() + "../Resources/Element Genie Scene Setup.app/Contents/MacOS/Element Genie Scene Setup";
#endif
    char nums[256];
    snprintf(nums, sizeof nums, " --size %d %d --par %.6f --fps %.4f --parent %llu", W, H, par, fps, (unsigned long long)(uintptr_t)parent);
    std::string cmd = "\"" + exe + "\" --setup \"" + inP + "\" \"" + outP + "\"" + nums;
    if (haveRef) cmd += " --ref \"" + refP + "\"";
    int code = 1;
#ifdef _WIN32
    std::wstring wcmd = widen(cmd);
    STARTUPINFOW si = {}; si.cb = sizeof si;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(widen(exe).c_str(), &wcmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        PF_STRCPY(out_data->return_msg, "Couldn't open Scene Setup. Reinstall Element Genie (the Scene Setup program should sit next to the plug-in).");
        out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE;
        return PF_Err_NONE;
    }
    if (parent) EnableWindow(parent, FALSE);
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, INFINITE, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0) break;
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    if (parent) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }
    DWORD wcode = 1; GetExitCodeProcess(pi.hProcess, &wcode); code = (int)wcode;
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
#else
    {
        std::vector<std::string> av = {exe, "--setup", inP, outP, "--size", std::to_string(W), std::to_string(H)};
        char b1[32], b2[32]; snprintf(b1, sizeof b1, "%.6f", par); snprintf(b2, sizeof b2, "%.4f", fps);
        av.insert(av.end(), {"--par", b1, "--fps", b2});
        if (haveRef) { av.push_back("--ref"); av.push_back(refP); }
        std::vector<char*> argv; for (auto& a : av) argv.push_back(&a[0]); argv.push_back(nullptr);
        pid_t pid = 0;
        if (posix_spawn(&pid, exe.c_str(), nullptr, nullptr, argv.data(), environ) != 0) {
            PF_STRCPY(out_data->return_msg, "Couldn't open Scene Setup. Reinstall Element Genie.");
            out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE;
            return PF_Err_NONE;
        }
        int st = 0;
        while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
        code = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
    }
    (void)cmd; (void)parent;
#endif
    std::vector<uint8_t> out;
    if (code == 0 && readFileBytes(outP, out) && !out.empty()) {
        std::string nj(out.begin(), out.end());
        PF_Handle h = (PF_Handle)params[P_SCENE]->u.arb_d.value;
        if (writeArb(in_data, h, nj)) {
            params[P_SCENE]->u.arb_d.value = (PF_ArbitraryH)h;
            params[P_SCENE]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
            out_data->out_flags |= PF_OutFlag_FORCE_RERENDER | PF_OutFlag_REFRESH_UI;
            addBuildKeyframes(in_data, params, nj);
        }
    }
    removeFile(inP); removeFile(outP); removeFile(refP);
    return PF_Err_NONE;
}

extern "C" EG_EXPORT PF_Err PluginDataEntryFunction2(PF_PluginDataPtr inPtr, PF_PluginDataCB2 inPluginDataCallBackPtr, SPBasicSuite*, const char*, const char*) {
    PF_Err result = PF_Err_INVALID_CALLBACK;
    result = PF_REGISTER_EFFECT_EXT2(inPtr, inPluginDataCallBackPtr, "Element Genie", "TK Element Genie", "Element Genie", AE_RESERVED_INFO, "EffectMain", "https://github.com");
    return result;
}

extern "C" EG_EXPORT PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output, void* extra) {
    PF_Err err = PF_Err_NONE;
    try {
        switch (cmd) {
        case PF_Cmd_ABOUT: err = About(in_data, out_data); break;
        case PF_Cmd_GLOBAL_SETUP: err = GlobalSetup(in_data, out_data); break;
        case PF_Cmd_PARAMS_SETUP: err = ParamsSetup(in_data, out_data); break;
        case PF_Cmd_SEQUENCE_SETUP:
            // After Effects ignores scene changes made from the options dialog, so there the
            // Scene Setup button is the way in. Premiere opens Scene Setup straight away.
            if (in_data->appl_id != 'FXTC') out_data->out_flags |= PF_OutFlag_SEND_DO_DIALOG;
            break;
        case PF_Cmd_EVENT: break;  // no custom drawing; the Scene param is hidden from Effect Controls
        case PF_Cmd_RENDER: err = Render(in_data, out_data, params, output); break;
        case PF_Cmd_DO_DIALOG: err = RunSetup(in_data, out_data, params); break;
        case PF_Cmd_USER_CHANGED_PARAM: {
            PF_UserChangedParamExtra* e = (PF_UserChangedParamExtra*)extra;
            if (e && e->param_index == P_SETUP) err = RunSetup(in_data, out_data, params);
            break;
        }
        case PF_Cmd_ARBITRARY_CALLBACK: err = arbCallback(in_data, out_data, (PF_ArbParamsExtra*)extra); break;
        default: break;
        }
    } catch (...) { err = PF_Err_INTERNAL_STRUCT_DAMAGED; }
    return err;
}

#ifdef _WIN32
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) g_module = h;
    return TRUE;
}
#endif
