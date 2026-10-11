// Element Genie installer - Genie Studio branded window. Installs into the plug-ins folder of each
// Premiere Pro / After Effects version found (user picks the apps), or Adobe's shared MediaCore folder.
#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#define RES_AEX 101
#define RES_SETUP 102
#define RES_LOGO 103
#define RES_MODELS 104   // content pack: sample models + title presets (see writeContent)

static const wchar_t* TITLE = L"Element Genie Installer";
static const wchar_t* VERSION = L"1.0";

// ---------------------------------------------------------------- install logic
static std::wstring pluginDir() {
    wchar_t pf[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, 0, pf);
    return std::wstring(pf) + L"\\Adobe\\Common\\Plug-ins\\7.0\\MediaCore\\Element Genie";
}
static bool fileExists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

// ---- installed Adobe apps
static const int MIN_YEAR = 2020;   // oldest version we support (older 64-bit CC releases may still work)
struct AdobeApp { bool premiere; std::wstring dir, label; int year; };
static std::vector<AdobeApp> g_apps;
static bool g_wantPr = true, g_wantAe = true;
static int yearOf(const std::wstring& n) {
    for (size_t i = 0; i + 4 <= n.size(); i++)
        if (iswdigit(n[i]) && iswdigit(n[i + 1]) && iswdigit(n[i + 2]) && iswdigit(n[i + 3]) && (i + 4 == n.size() || !iswdigit(n[i + 4])) && (i == 0 || !iswdigit(n[i - 1])))
            return _wtoi(n.substr(i, 4).c_str());
    return 0;
}
static void findAdobeApps() {
    g_apps.clear();
    wchar_t pf[MAX_PATH]; SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, 0, pf);
    std::wstring root = std::wstring(pf) + L"\\Adobe\\";
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((root + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        std::wstring n = fd.cFileName, d = root + n;
        if (n.rfind(L"Adobe Premiere Pro", 0) == 0 && fileExists(d + L"\\Adobe Premiere Pro.exe"))
            g_apps.push_back({true, d, n.size() > 19 ? n.substr(19) : L"", yearOf(n)});
        else if (n.rfind(L"Adobe After Effects", 0) == 0 && fileExists(d + L"\\Support Files\\AfterFX.exe"))
            g_apps.push_back({false, d, n.size() > 20 ? n.substr(20) : L"", yearOf(n)});
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static std::wstring appPluginDir(const AdobeApp& a) {
    if (a.premiere) {  // Premiere's own folder is "PlugIns" (older versions: "Plug-ins")
        std::wstring base = fileExists(a.dir + L"\\PlugIns") ? a.dir + L"\\PlugIns" : a.dir + L"\\Plug-ins";
        return base + L"\\Common\\Element Genie";
    }
    return a.dir + L"\\Support Files\\Plug-ins\\Element Genie";
}
static std::wstring foundText(bool premiere) {
    std::wstring t;
    for (auto& a : g_apps) if (a.premiere == premiere) {
        if (!t.empty()) t += L", ";
        t += a.label.empty() ? L"found" : a.label;
        if (a.year && a.year < MIN_YEAR) t += L" (older - may not work)";
    }
    return t.empty() ? L"not found - will use Adobe's shared plug-ins folder" : t;
}
// every folder this install writes to.
// Both apps ticked: Adobe's shared MediaCore folder (Premiere and After Effects both load it - one copy, no duplicates).
// One app ticked: that app's own plug-ins folder(s), so the other app doesn't see it.
static std::vector<std::wstring> targetDirs() {
    if (g_wantPr && g_wantAe) return {pluginDir()};
    std::vector<std::wstring> v;
    for (auto& a : g_apps) if (a.premiere ? g_wantPr : g_wantAe) v.push_back(appPluginDir(a));
    if (v.empty()) v.push_back(pluginDir());
    return v;
}
static std::vector<std::wstring> allKnownDirs() {
    std::vector<std::wstring> v = {pluginDir()};
    for (auto& a : g_apps) {
        v.push_back(appPluginDir(a));
        if (a.premiere) v.push_back(a.dir + L"\\Plug-ins\\Common\\Element Genie");  // wrong folder used by an early 2.1 build
    }
    return v;
}

static bool adobeRunning(std::wstring& which) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (s == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W e = {}; e.dwSize = sizeof e;
    bool found = false;
    if (Process32FirstW(s, &e)) do {
        if (!_wcsicmp(e.szExeFile, L"Adobe Premiere Pro.exe")) { which = L"Premiere Pro"; found = true; break; }
        if (!_wcsicmp(e.szExeFile, L"AfterFX.exe")) { which = L"After Effects"; found = true; break; }
        if (!_wcsicmp(e.szExeFile, L"Adobe Media Encoder.exe")) { which = L"Media Encoder"; found = true; break; }
    } while (Process32NextW(s, &e));
    CloseHandle(s);
    return found;
}

static bool resData(int id, const void*& p, DWORD& n) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), (LPCWSTR)RT_RCDATA);
    if (!r) return false;
    HGLOBAL h = LoadResource(nullptr, r);
    n = SizeofResource(nullptr, r);
    p = LockResource(h);
    return p != nullptr;
}
// big payloads (plug-in, Scene Setup, content pack) are zlib-compressed by build.sh to keep the download small
static bool resUnpacked(int id, std::vector<uint8_t>& out) {
    const void* p; DWORD n;
    if (!resData(id, p, n)) return false;
    int len = 0;
    char* d = stbi_zlib_decode_malloc_guesssize_headerflag((const char*)p, (int)n, (int)n * 3, &len, 1);
    if (!d) return false;
    out.assign((uint8_t*)d, (uint8_t*)d + len);
    STBI_FREE(d);
    return true;
}
static bool writeBytes(const std::wstring& path, const void* p, DWORD n) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0; BOOL ok = WriteFile(f, p, n, &w, nullptr);
    CloseHandle(f);
    return ok && w == n;
}
static bool writeResource(int id, const std::wstring& path) {
    const void* p; DWORD n;
    std::vector<uint8_t> b; (void)p; (void)n;
    return resUnpacked(id, b) && writeBytes(path, b.data(), (DWORD)b.size());
}
// Content pack: [u32 count] then per file [u32 nameLen][utf8 name][u32 size][bytes]; names may hold sub-folders
// ("Models/Genie Lamp.glb", "Presets/Cinematic.egpack").
static void writeContent(const std::wstring& dir) {
    std::vector<uint8_t> pack;
    if (!resUnpacked(RES_MODELS, pack) || pack.size() < 4) return;
    const uint8_t* p = pack.data(); const uint8_t* end = p + pack.size();
    uint32_t count; memcpy(&count, p, 4); p += 4;
    for (uint32_t i = 0; i < count && p + 8 <= end; i++) {
        uint32_t nl; memcpy(&nl, p, 4); p += 4;
        if (p + nl + 4 > end) return;
        std::string name((const char*)p, nl); p += nl;
        uint32_t sz; memcpy(&sz, p, 4); p += 4;
        if (p + sz > end) return;
        int wl = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
        std::wstring wn(wl, 0); MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, &wn[0], wl); wn.resize(wl - 1);
        for (auto& c : wn) if (c == L'/') c = L'\\';
        std::wstring full = dir + L"\\" + wn;
        size_t k = full.find_last_of(L'\\');
        SHCreateDirectoryExW(nullptr, full.substr(0, k).c_str(), nullptr);
        writeBytes(full, p, sz);
        p += sz;
    }
}
static void deleteTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName; if (n == L"." || n == L"..") continue;
            std::wstring p = dir + L"\\" + n;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) deleteTree(p); else DeleteFileW(p.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

// Premiere remembers plug-ins it has scanned; clearing that list makes it pick up the new version.
static void clearPremiereCache() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Adobe\\Premiere Pro", 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    std::vector<std::wstring> vers;
    for (DWORD i = 0;; i++) { wchar_t n[256]; DWORD nl = 256; if (RegEnumKeyExW(k, i, n, &nl, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break; vers.push_back(n); }
    RegCloseKey(k);
    for (auto& v : vers) {
        std::wstring p = L"Software\\Adobe\\Premiere Pro\\" + v;
        HKEY vk;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, p.c_str(), 0, KEY_ALL_ACCESS, &vk) == ERROR_SUCCESS) { RegDeleteTreeW(vk, L"PluginCache.64"); RegCloseKey(vk); }
    }
}

// ---------------------------------------------------------------- window
static const COLORREF BG = RGB(13, 11, 26), CARD = RGB(24, 21, 42), ACCENT = RGB(139, 123, 255), TEXT1 = RGB(240, 238, 250), TEXT2 = RGB(160, 156, 185);
static HBITMAP g_logo = nullptr; static int g_logoW = 0, g_logoH = 0;
static HFONT g_fTitle, g_fBody, g_fSmall, g_fBtn;
static HWND g_wnd, g_btnInstall, g_btnRemove, g_btnClose;
static std::wstring g_status; static COLORREF g_statusCol = TEXT2;
static bool g_done = false;
enum { ID_INSTALL = 1, ID_REMOVE, ID_CLOSE, ID_PR, ID_AE };
static HWND g_chkPr, g_chkAe;

static void loadLogo() {
    const void* p; DWORD n;
    if (!resData(RES_LOGO, p, n)) return;
    int w, h, c;
    unsigned char* px = stbi_load_from_memory((const unsigned char*)p, (int)n, &w, &h, &c, 4);
    if (!px) return;
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    g_logo = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    uint8_t* d = (uint8_t*)bits;
    for (int i = 0; i < w * h; i++) {  // premultiplied BGRA for AlphaBlend
        uint8_t a = px[i * 4 + 3];
        d[i * 4 + 0] = (uint8_t)(px[i * 4 + 2] * a / 255); d[i * 4 + 1] = (uint8_t)(px[i * 4 + 1] * a / 255);
        d[i * 4 + 2] = (uint8_t)(px[i * 4 + 0] * a / 255); d[i * 4 + 3] = a;
    }
    stbi_image_free(px);
    g_logoW = w; g_logoH = h;
}

static void setStatus(const std::wstring& s, COLORREF c) { g_status = s; g_statusCol = c; InvalidateRect(g_wnd, nullptr, FALSE); UpdateWindow(g_wnd); }

static bool anyInstalled() { for (auto& d : allKnownDirs()) if (fileExists(d + L"\\ElementGenie.aex")) return true; return false; }
static void refreshButtons() {
    bool installed = anyInstalled();
    SetWindowTextW(g_btnInstall, g_done ? L"Done" : installed ? L"Update" : L"Install");
    ShowWindow(g_btnRemove, installed && !g_done ? SW_SHOW : SW_HIDE);
    EnableWindow(g_btnInstall, g_done || g_wantPr || g_wantAe);
    InvalidateRect(g_btnInstall, nullptr, FALSE);
}

static bool closeAdobeFirst() {
    std::wstring app;
    while (adobeRunning(app)) {
        std::wstring m = app + L" is open. Please save your work and close it, then click Retry.";
        if (MessageBoxW(g_wnd, m.c_str(), TITLE, MB_RETRYCANCEL | MB_ICONWARNING) != IDRETRY) return false;
    }
    return true;
}

static void doInstall() {
    if (g_done) { DestroyWindow(g_wnd); return; }
    if (!g_wantPr && !g_wantAe) return;
    if (!closeAdobeFirst()) return;
    setStatus(L"Installing...", TEXT2);
    std::vector<std::wstring> dirs = targetDirs();
    // remove every other copy (older installs, unticked apps) so the effect is never listed twice
    for (auto& d : allKnownDirs()) {
        bool keep = false; for (auto& t : dirs) keep |= _wcsicmp(t.c_str(), d.c_str()) == 0;
        if (!keep && fileExists(d)) deleteTree(d);
    }
    int ok = 0;
    for (auto& dir : dirs) {
        int rc = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
        if (rc != ERROR_SUCCESS && rc != ERROR_ALREADY_EXISTS && rc != ERROR_FILE_EXISTS) continue;
        if (!writeResource(RES_AEX, dir + L"\\ElementGenie.aex") || !writeResource(RES_SETUP, dir + L"\\Element Genie Scene Setup.exe")) continue;
        writeContent(dir);
        ok++;
    }
    if (!ok) {
        setStatus(L"Couldn't copy the files. Close Premiere Pro and After Effects, then right-click the installer and choose \"Run as administrator\".", RGB(255, 120, 110));
        return;
    }
    clearPremiereCache();
    g_done = true;
    std::wstring apps = g_wantPr && g_wantAe ? L"Premiere Pro and After Effects" : g_wantPr ? L"Premiere Pro" : L"After Effects";
    setStatus(L"Installed! Open " + apps + L" and find it under Effects \u203a Element Genie.\n"
              L"Drop it on a clip or adjustment layer, click \"Scene Setup...\", pick a title from the Titles tab and press OK.", RGB(150, 230, 170));
    refreshButtons();
}
static void doRemove() {
    if (!closeAdobeFirst()) return;
    if (MessageBoxW(g_wnd, L"Remove Element Genie from Premiere Pro and After Effects?", TITLE, MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
    for (auto& d : allKnownDirs()) if (fileExists(d)) deleteTree(d);
    clearPremiereCache();
    setStatus(L"Element Genie has been removed.", TEXT2);
    refreshButtons();
}

static void drawText(HDC dc, const std::wstring& s, RECT r, HFONT f, COLORREF c, UINT fmt) {
    SelectObject(dc, f); SetTextColor(dc, c); SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s.c_str(), -1, &r, fmt);
}

static void paint(HWND h) {
    PAINTSTRUCT ps; HDC wdc = BeginPaint(h, &ps);
    RECT cr; GetClientRect(h, &cr);
    HDC dc = CreateCompatibleDC(wdc); HBITMAP bb = CreateCompatibleBitmap(wdc, cr.right, cr.bottom); HGDIOBJ ob = SelectObject(dc, bb);
    HBRUSH bg = CreateSolidBrush(BG); FillRect(dc, &cr, bg); DeleteObject(bg);
    int W = cr.right;
    // logo
    if (g_logo) {
        int lh = 178, lw = lh * g_logoW / g_logoH;
        HDC mdc = CreateCompatibleDC(dc); HGDIOBJ o = SelectObject(mdc, g_logo);
        BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        SetStretchBltMode(dc, HALFTONE);
        AlphaBlend(dc, (W - lw) / 2, 12, lw, lh, mdc, 0, 0, g_logoW, g_logoH, bf);
        SelectObject(mdc, o); DeleteDC(mdc);
    }
    // card
    RECT card = {28, 200, W - 28, 352};
    HBRUSH cb = CreateSolidBrush(CARD); HPEN np = CreatePen(PS_SOLID, 1, RGB(48, 42, 80));
    HGDIOBJ obr = SelectObject(dc, cb), op = SelectObject(dc, np);
    RoundRect(dc, card.left, card.top, card.right, card.bottom, 18, 18);
    SelectObject(dc, obr); SelectObject(dc, op); DeleteObject(cb); DeleteObject(np);
    std::wstring t = std::wstring(L"Element Genie ") + VERSION;
    drawText(dc, t, {card.left, card.top + 14, card.right, card.top + 50}, g_fTitle, TEXT1, DT_CENTER | DT_SINGLELINE);
    drawText(dc, L"Free, open-source 3D titles, logos and models", {card.left, card.top + 52, card.right, card.top + 74}, g_fBody, TEXT2, DT_CENTER | DT_SINGLELINE);
    drawText(dc, L"For Premiere Pro and After Effects 2020 or newer", {card.left, card.top + 84, card.right, card.top + 104}, g_fBody, ACCENT, DT_CENTER | DT_SINGLELINE);
    drawText(dc, L"Windows 10 / 11 (64-bit)  \u00b7  graphics card with OpenGL 3.3", {card.left, card.top + 110, card.right, card.top + 130}, g_fSmall, TEXT2, DT_CENTER | DT_SINGLELINE);
    // app choice
    drawText(dc, L"Install into", {36, 366, W - 36, 386}, g_fSmall, TEXT2, DT_LEFT | DT_SINGLELINE);
    // status
    if (!g_status.empty()) drawText(dc, g_status, {36, 456, W - 36, 520}, g_fSmall, g_statusCol, DT_CENTER | DT_WORDBREAK);
    drawText(dc, L"Genie Studio FX  ·  geniestudiofx.github.io", {0, cr.bottom - 26, W, cr.bottom - 6}, g_fSmall, RGB(95, 90, 125), DT_CENTER | DT_SINGLELINE);
    BitBlt(wdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, ob); DeleteObject(bb); DeleteDC(dc);
    EndPaint(h, &ps);
}

static void drawCheck(DRAWITEMSTRUCT* d) {
    bool pr = d->CtlID == ID_PR, on = pr ? g_wantPr : g_wantAe;
    HDC dc = d->hDC; RECT r = d->rcItem;
    HBRUSH bg = CreateSolidBrush(BG); FillRect(dc, &r, bg); DeleteObject(bg);
    RECT box = {r.left + 4, r.top + 5, r.left + 22, r.top + 23};
    COLORREF fill = on ? ACCENT : CARD;
    HBRUSH b = CreateSolidBrush(fill); HPEN p = CreatePen(PS_SOLID, 1, on ? ACCENT : RGB(80, 74, 120));
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, box.left, box.top, box.right, box.bottom, 5, 5);
    SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(b); DeleteObject(p);
    if (on) {  // tick
        HPEN tp = CreatePen(PS_SOLID, 2, RGB(255, 255, 255)); HGDIOBJ o2 = SelectObject(dc, tp);
        MoveToEx(dc, box.left + 4, box.top + 9, nullptr); LineTo(dc, box.left + 8, box.top + 13); LineTo(dc, box.left + 14, box.top + 5);
        SelectObject(dc, o2); DeleteObject(tp);
    }
    drawText(dc, pr ? L"Premiere Pro" : L"After Effects", {r.left + 32, r.top, r.left + 160, r.bottom}, g_fBtn, TEXT1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    drawText(dc, foundText(pr), {r.left + 160, r.top, r.right, r.bottom}, g_fSmall, TEXT2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}
static void drawButton(DRAWITEMSTRUCT* d) {
    if (d->CtlID == ID_PR || d->CtlID == ID_AE) { drawCheck(d); return; }
    bool primary = d->CtlID == ID_INSTALL;
    bool pressed = d->itemState & ODS_SELECTED;
    HDC dc = d->hDC; RECT r = d->rcItem;
    HBRUSH bg = CreateSolidBrush(BG); FillRect(dc, &r, bg); DeleteObject(bg);
    bool disabled = d->itemState & ODS_DISABLED;
    COLORREF fill = primary ? (disabled ? RGB(60, 55, 95) : pressed ? RGB(112, 98, 225) : ACCENT) : (pressed ? RGB(50, 45, 78) : RGB(38, 34, 62));
    HBRUSH b = CreateSolidBrush(fill); HPEN p = CreatePen(PS_SOLID, 1, fill);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, 16, 16);
    SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(b); DeleteObject(p);
    wchar_t txt[64]; GetWindowTextW(d->hwndItem, txt, 64);
    drawText(dc, txt, r, g_fBtn, primary ? RGB(255, 255, 255) : TEXT1, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_wnd = h;
        RECT cr; GetClientRect(h, &cr);
        int W = cr.right, y = cr.bottom - 76, bw = 150, bh = 40;
        g_btnInstall = CreateWindowW(L"BUTTON", L"Install", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, W - 28 - bw, y, bw, bh, h, (HMENU)ID_INSTALL, nullptr, nullptr);
        g_btnRemove = CreateWindowW(L"BUTTON", L"Uninstall", WS_CHILD | BS_OWNERDRAW, 28, y, 130, bh, h, (HMENU)ID_REMOVE, nullptr, nullptr);
        g_btnClose = CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, W - 28 - bw - 12 - 110, y, 110, bh, h, (HMENU)ID_CLOSE, nullptr, nullptr);
        g_chkPr = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 32, 390, W - 64, 28, h, (HMENU)ID_PR, nullptr, nullptr);
        g_chkAe = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 32, 420, W - 64, 28, h, (HMENU)ID_AE, nullptr, nullptr);
        refreshButtons();
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == ID_INSTALL) doInstall();
        else if (LOWORD(w) == ID_REMOVE) doRemove();
        else if (LOWORD(w) == ID_CLOSE) DestroyWindow(h);
        else if (LOWORD(w) == ID_PR || LOWORD(w) == ID_AE) {
            if (!g_done) { (LOWORD(w) == ID_PR ? g_wantPr : g_wantAe) ^= true; InvalidateRect((HWND)l, nullptr, FALSE); refreshButtons(); }
        }
        return 0;
    case WM_DRAWITEM: drawButton((DRAWITEMSTRUCT*)l); return TRUE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware();
    findAdobeApps();
    {   // tick the apps that are installed (both if we can't tell)
        bool pr = false, ae = false; for (auto& a : g_apps) (a.premiere ? pr : ae) = true;
        if (pr || ae) { g_wantPr = pr; g_wantAe = ae; }
    }
    loadLogo();
    auto font = [](int px, int weight) { return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI"); };
    g_fTitle = font(28, FW_BOLD); g_fBody = font(16, FW_NORMAL); g_fSmall = font(13, FW_NORMAL); g_fBtn = font(16, FW_SEMIBOLD);
    WNDCLASSW wc = {}; wc.lpfnWndProc = wndProc; wc.hInstance = inst; wc.lpszClassName = L"EGInstaller";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);
    RECT r = {0, 0, 560, 620};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    HWND h = CreateWindowW(L"EGInstaller", TITLE, style, (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, (GetSystemMetrics(SM_CYSCREEN) - wh) / 2, ww, wh, nullptr, nullptr, inst, nullptr);
    // dark title bar on Windows 10/11
    typedef HRESULT(WINAPI * DSWA)(HWND, DWORD, LPCVOID, DWORD);
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) if (auto f = (DSWA)GetProcAddress(dwm, "DwmSetWindowAttribute")) { BOOL on = TRUE; f(h, 20, &on, sizeof on); }
    ShowWindow(h, show); UpdateWindow(h);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}
