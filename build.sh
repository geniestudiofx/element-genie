#!/bin/bash
# Element Genie build: plug-in (.aex) + Scene Setup (.exe) + installer
set -e
cd "$(dirname "$0")"
SDK="${AE_SDK:?Set AE_SDK to the Examples folder of the Adobe After Effects SDK}"
CXX="x86_64-w64-mingw32-g++-posix"
CC="x86_64-w64-mingw32-gcc-posix"
INC="-Isrc -Iext/glm -Iext/stb -Iext/nanosvg/src -Iext/earcut/include/mapbox -Iext/cgltf -Iext/ufbx -Iext/glad/include -Iext/glfw/include -Iext/imgui -Iext/imgui/backends -Iext/imguizmo/src -Iext"
DEF="-DGLM_ENABLE_EXPERIMENTAL -DGLM_FORCE_CTOR_INIT -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_USE_MATH_DEFINES"
OPT="-O2 -std=gnu++17 -w"
mkdir -p build/obj
obj() { # src out flags
  local src=$1 out=build/obj/$2.o; shift 2
  if [ ! -f $out ] || [ $src -nt $out ] || [ -n "$FORCE" ]; then echo "  cc $src"; $CXX $OPT $DEF $INC "$@" -c $src -o $out; fi
}
cobj() { local src=$1 out=build/obj/$2.o; shift 2
  if [ ! -f $out ] || [ $src -nt $out ]; then echo "  cc $src"; $CC -O2 -w $INC -c $src -o $out; fi; }
# headers changed -> rebuild our sources
HDRS=$(ls -t src/*.h | head -1)
for f in render extrude import project effect export platform plugin main; do
  if [ -f build/obj/$f.o ] && [ $HDRS -nt build/obj/$f.o ]; then rm -f build/obj/$f.o; fi
done
cobj ext/glad/src/gl.c glad
cobj ext/ufbx/ufbx.c ufbx
obj src/fonts.cpp fonts -O0
obj src/libs.cpp libs
for f in render extrude import project effect export platform; do obj src/$f.cpp $f; done
SDKINC="-I$SDK/Headers -I$SDK/Headers/SP -I$SDK/Headers/adobesdk -I$SDK/Util"
obj src/plugin.cpp plugin $SDKINC -D_WINDOWS -DMSWindows
COMMON="build/obj/glad.o build/obj/ufbx.o build/obj/fonts.o build/obj/libs.o build/obj/render.o build/obj/extrude.o build/obj/import.o build/obj/project.o build/obj/effect.o"
echo "  link ElementGenie.aex"
$CXX -shared -o build/ElementGenie.aex build/obj/plugin.o $COMMON plugin/pipl.o -static -static-libgcc -static-libstdc++ -lopengl32 -lgdi32 -luser32 -lshell32 -lole32 -Wl,--kill-at -s
echo ok
# ---- Scene Setup editor
for f in imgui imgui_draw imgui_tables imgui_widgets; do obj ext/imgui/$f.cpp $f; done
obj ext/imgui/backends/imgui_impl_glfw.cpp imgui_impl_glfw
obj ext/imgui/backends/imgui_impl_opengl3.cpp imgui_impl_opengl3
obj ext/imguizmo/src/ImGuizmo.cpp imguizmo
obj src/logo_data.cpp logo_data
obj src/main.cpp main
x86_64-w64-mingw32-windres -I src src/app.rc -O coff -o build/obj/app_res.o
echo "  link Scene Setup"
$CXX -o "build/Element Genie Scene Setup.exe" build/obj/main.o build/obj/platform.o build/obj/export.o build/obj/logo_data.o $COMMON \
  build/obj/imgui.o build/obj/imgui_draw.o build/obj/imgui_tables.o build/obj/imgui_widgets.o build/obj/imgui_impl_glfw.o build/obj/imgui_impl_opengl3.o build/obj/imguizmo.o \
  build/obj/app_res.o build/glfw/src/libglfw3.a -static -static-libgcc -static-libstdc++ -mwindows -lopengl32 -lgdi32 -lcomdlg32 -lshell32 -lole32 -luuid -lwinmm -s 2>&1 | grep -v "warning" | head -20
echo ok2
# ---- installer
x86_64-w64-mingw32-windres -I src src/installer.rc -O coff -o build/obj/installer_res.o
$CXX -O2 -municode -mwindows -o "build/Element Genie Installer.exe" src/installer.cpp build/obj/installer_res.o -static -static-libgcc -static-libstdc++ -lshell32 -ladvapi32 -s
echo ok3
