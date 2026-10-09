// Element Genie - Windows helpers: file dialogs, installed fonts, shell
#include "platform.h"
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#endif

std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& w);

#ifdef _WIN32
static std::wstring makeFilter(const char* f) {
    std::wstring w = widen(f);
    for (auto& c : w) if (c == L'|') c = 0;
    w.push_back(0); w.push_back(0);
    return w;
}
std::string openFileDialog(const char* title, const char* filter, void* owner) {
    wchar_t buf[4096] = {0};
    std::wstring flt = makeFilter(filter), t = widen(title);
    OPENFILENAMEW ofn = {}; ofn.lStructSize = sizeof ofn; ofn.hwndOwner = (HWND)owner;
    ofn.lpstrFilter = flt.c_str(); ofn.lpstrFile = buf; ofn.nMaxFile = 4096; ofn.lpstrTitle = t.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return "";
    return narrow(buf);
}
std::string saveFileDialog(const char* title, const char* filter, const char* defExt, const std::string& defName, void* owner) {
    wchar_t buf[4096] = {0};
    std::wstring dn = widen(defName); wcsncpy(buf, dn.c_str(), 4095);
    std::wstring flt = makeFilter(filter), t = widen(title), de = widen(defExt);
    OPENFILENAMEW ofn = {}; ofn.lStructSize = sizeof ofn; ofn.hwndOwner = (HWND)owner;
    ofn.lpstrFilter = flt.c_str(); ofn.lpstrFile = buf; ofn.nMaxFile = 4096; ofn.lpstrTitle = t.c_str(); ofn.lpstrDefExt = de.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) return "";
    return narrow(buf);
}
static void readFontKey(HKEY root, const std::wstring& fontsDir, std::vector<FontEntry>& out) {
    HKEY k;
    if (RegOpenKeyExW(root, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    for (DWORD i = 0;; i++) {
        wchar_t name[512]; DWORD nl = 512; BYTE data[2048]; DWORD dl = sizeof data - 2; DWORD type;
        if (RegEnumValueW(k, i, name, &nl, nullptr, &type, data, &dl) != ERROR_SUCCESS) break;
        if (type != REG_SZ) continue;
        data[dl] = 0; data[dl + 1] = 0;
        std::wstring file((wchar_t*)data);
        std::wstring lowerF = file; for (auto& c : lowerF) c = towlower(c);
        if (!(lowerF.size() > 4 && (lowerF.substr(lowerF.size() - 4) == L".ttf" || lowerF.substr(lowerF.size() - 4) == L".otf" || lowerF.substr(lowerF.size() - 4) == L".ttc"))) continue;
        if (file.find(L'\\') == std::wstring::npos && file.find(L'/') == std::wstring::npos) file = fontsDir + file;
        std::wstring n(name);
        for (const wchar_t* suf : {L" (TrueType)", L" (OpenType)"}) { size_t p = n.find(suf); if (p != std::wstring::npos) n = n.substr(0, p); }
        out.push_back({narrow(n), narrow(file)});
    }
    RegCloseKey(k);
}
const std::vector<FontEntry>& systemFonts() {
    static std::vector<FontEntry> list; static bool done = false;
    if (done) return list;
    done = true;
    wchar_t win[MAX_PATH]; GetWindowsDirectoryW(win, MAX_PATH);
    std::wstring fontsDir = std::wstring(win) + L"\\Fonts\\";
    readFontKey(HKEY_LOCAL_MACHINE, fontsDir, list);
    wchar_t la[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, la) == S_OK) readFontKey(HKEY_CURRENT_USER, std::wstring(la) + L"\\Microsoft\\Windows\\Fonts\\", list);
    std::sort(list.begin(), list.end(), [](const FontEntry& a, const FontEntry& b) { return _stricmp(a.name.c_str(), b.name.c_str()) < 0; });
    list.erase(std::unique(list.begin(), list.end(), [](const FontEntry& a, const FontEntry& b) { return a.name == b.name; }), list.end());
    return list;
}
void revealInExplorer(const std::string& path) {
    std::wstring args = L"/select,\"" + widen(path) + L"\"";
    for (auto& c : args) if (c == L'/' && &c != &args[0]) c = L'\\';
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}
void openUrl(const std::string& url) { ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
std::string exeDir() {
    wchar_t b[MAX_PATH]; GetModuleFileNameW(nullptr, b, MAX_PATH);
    std::wstring s(b); size_t k = s.find_last_of(L"\\/"); return narrow(s.substr(0, k + 1));
}
std::string appDataDir() {
    wchar_t p[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, p) != S_OK) return exeDir();
    std::wstring d = std::wstring(p) + L"\\Element Genie";
    CreateDirectoryW(d.c_str(), nullptr);
    return narrow(d) + "\\";
}
#else
std::string openFileDialog(const char*, const char*, void*) { return ""; }
std::string saveFileDialog(const char*, const char*, const char*, const std::string&, void*) { return ""; }
const std::vector<FontEntry>& systemFonts() { static std::vector<FontEntry> l; return l; }
void revealInExplorer(const std::string&) {}
void openUrl(const std::string&) {}
std::string exeDir() { return "./"; }
std::string appDataDir() { return "./"; }
#endif
