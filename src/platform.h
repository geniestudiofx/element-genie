#pragma once
#include <string>
#include <vector>

struct FontEntry { std::string name, path; };

// filter like "Models|*.glb;*.gltf;*.fbx;*.obj|All files|*.*"
std::string openFileDialog(const char* title, const char* filter, void* owner);
std::string saveFileDialog(const char* title, const char* filter, const char* defExt, const std::string& defName, void* owner);
const std::vector<FontEntry>& systemFonts();
void revealInExplorer(const std::string& path);
void openUrl(const std::string& url);
std::string exeDir();
std::string appDataDir();   // %APPDATA%\Element Genie
