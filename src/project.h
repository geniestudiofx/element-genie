#pragma once
#include "scene.h"
#include <string>
std::string sceneToJson(const Scene& s);
bool sceneFromJson(const std::string& text, Scene& s, std::string& err);
