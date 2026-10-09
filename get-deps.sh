#!/bin/bash
# Downloads the open-source libraries Element Genie uses, at the versions it was built with.
set -e
cd "$(dirname "$0")/ext"
dep() { [ -d "$1" ] || git clone -q "$2" "$1"; git -C "$1" fetch -q --all; git -C "$1" checkout -q "$3"; echo "  $1 @ $3"; }
dep cgltf    https://github.com/jkuhlmann/cgltf.git       85cd623
dep earcut   https://github.com/mapbox/earcut.hpp.git     177bd66
dep glfw     https://github.com/glfw/glfw.git             7b6aead
dep glm      https://github.com/g-truc/glm.git            6f14f47
dep imgui    https://github.com/ocornut/imgui.git         778992f
dep imguizmo https://github.com/CedricGuillemet/ImGuizmo.git 18cef5e
dep nanosvg  https://github.com/memononen/nanosvg.git     239e102
dep stb      https://github.com/nothings/stb.git          2c980bb
dep ufbx     https://github.com/ufbx/ufbx.git             5955c5c
[ -f json.hpp ] || curl -sSLo json.hpp https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
# GLFW static library (MinGW cross-compile)
cmake -S glfw -B ../build/glfw -DCMAKE_TOOLCHAIN_FILE=../mingw.cmake -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF >/dev/null
cmake --build ../build/glfw -j >/dev/null
echo "Dependencies ready."
