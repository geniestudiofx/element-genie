// Element Genie installer - copies the effect into Premiere Pro / After Effects' shared plug-in folder
#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <string>
#include <vector>

#define RES_AEX 101
#define RES_SETUP 102

static const wchar_t* TITLE = L"Element Genie Installer";

static std::wstring pluginDir() {
    wchar_t pf[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, 0, pf);
    return std::wstring(pf) + L"\\Adobe\\Common\\Plug-ins\\7.0\\MediaCore\\Element Genie";
}
static bool fileExists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

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

static bool writeResource(int id, const std::wstring& path) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), (LPCWSTR)RT_RCDATA);
    if (!r) return false;
    HGLOBAL h = LoadResource(nullptr, r);
    DWORD n = SizeofResource(nullptr, r);
    const void* p = LockResource(h);
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0; BOOL ok = WriteFile(f, p, n, &w, nullptr);
    CloseHandle(f);
    return ok && w == n;
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
        if (RegOpenKeyExW(HKEY_CURRENT_USER, p.c_str(), 0, KEY_ALL_ACCESS, &vk) == ERROR_SUCCESS) {
            RegDeleteTreeW(vk, L"PluginCache.64");
            RegCloseKey(vk);
        }
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    std::wstring dir = pluginDir();
    std::wstring aex = dir + L"\\ElementGenie.aex", setup = dir + L"\\Element Genie Scene Setup.exe";
    std::wstring app;
    while (adobeRunning(app)) {
        std::wstring m = app + L" is open. Please save your work and close it, then click Retry.";
        if (MessageBoxW(nullptr, m.c_str(), TITLE, MB_RETRYCANCEL | MB_ICONWARNING) != IDRETRY) return 1;
    }
    bool installed = fileExists(aex);
    if (installed) {
        int r = MessageBoxW(nullptr, L"Element Genie is already installed.\n\nYes = install this version (update)\nNo = uninstall it\nCancel = do nothing", TITLE, MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL) return 0;
        if (r == IDNO) {
            DeleteFileW(aex.c_str()); DeleteFileW(setup.c_str()); RemoveDirectoryW(dir.c_str());
            clearPremiereCache();
            MessageBoxW(nullptr, L"Element Genie has been removed.", TITLE, MB_OK | MB_ICONINFORMATION);
            return 0;
        }
    } else {
        if (MessageBoxW(nullptr, L"Install Element Genie for Premiere Pro (and After Effects)?\n\nIt goes into Adobe's shared plug-ins folder:\n"
                                 L"Program Files\\Adobe\\Common\\Plug-ins\\7.0\\MediaCore",
                        TITLE, MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return 0;
    }
    int rc = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    if (rc != ERROR_SUCCESS && rc != ERROR_ALREADY_EXISTS && rc != ERROR_FILE_EXISTS) {
        MessageBoxW(nullptr, L"Couldn't create the plug-in folder. Right-click the installer and choose 'Run as administrator'.", TITLE, MB_OK | MB_ICONERROR);
        return 2;
    }
    if (!writeResource(RES_AEX, aex) || !writeResource(RES_SETUP, setup)) {
        MessageBoxW(nullptr, L"Couldn't copy the files. Make sure Premiere Pro and After Effects are closed, then try again.", TITLE, MB_OK | MB_ICONERROR);
        return 3;
    }
    clearPremiereCache();
    MessageBoxW(nullptr,
                L"Element Genie is installed.\n\n"
                L"1. Open Premiere Pro.\n"
                L"2. File > New > Adjustment Layer, put it on a track above your footage.\n"
                L"3. Effects panel > search \"Element Genie\" > drag it onto the adjustment layer.\n"
                L"4. In Effect Controls click \"Scene Setup...\" to build your 3D text, logo or model.\n"
                L"5. Click OK, then keyframe the Groups and Camera in Effect Controls.",
                TITLE, MB_OK | MB_ICONINFORMATION);
    return 0;
}
