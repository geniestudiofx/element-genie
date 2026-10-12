#pragma once
#include <string>
#include <vector>
#include <cstdio>

struct FontEntry { std::string name, path; };

// filter like "Models|*.glb;*.gltf;*.fbx;*.obj|All files|*.*"
std::string openFileDialog(const char* title, const char* filter, void* owner);
std::string saveFileDialog(const char* title, const char* filter, const char* defExt, const std::string& defName, void* owner);
const std::vector<FontEntry>& systemFonts();
void revealInExplorer(const std::string& path);
void openUrl(const std::string& url);
std::string exeDir();       // folder of the running program (ends with a separator)
std::string resDir();       // bundled read-only data (Presets, Models): exeDir() on Windows, Contents/Resources on Mac
std::string appDataDir();   // %APPDATA%\Element Genie  /  ~/Library/Application Support/Element Genie
// portable file helpers (UTF-8 paths)
FILE* fopenU8(const std::string& path, const char* mode);
std::vector<std::string> listFiles(const std::string& dir);   // plain file names, no folders
bool makeDir(const std::string& dir);
bool copyFileU8(const std::string& from, const std::string& to);
void showError(const std::string& msg);
void bringAppToFront(void* glfwWindow);
