#!/bin/bash
# Element Genie - macOS build (run on a Mac with Xcode command line tools)
# Makes a universal (Apple silicon + Intel) plug-in bundle with Scene Setup inside, and an installer .pkg.
#   AE_SDK=/path/to/AfterEffectsSDK/Examples ./build_mac.sh
set -e
cd "$(dirname "$0")"
SDK="${AE_SDK:?Set AE_SDK to the Examples folder of the Adobe After Effects SDK}"
VERSION=1.0
ARCH="-arch arm64 -arch x86_64"
MINOS="-mmacosx-version-min=10.14"
CXX="clang++ $ARCH $MINOS"
CC="clang $ARCH $MINOS"
INC="-Isrc -Iext/glm -Iext/stb -Iext/nanosvg/src -Iext/earcut/include/mapbox -Iext/cgltf -Iext/ufbx -Iext/glad/include -Iext/glfw/include -Iext/imgui -Iext/imgui/backends -Iext/imguizmo/src -Iext"
DEF="-DGLM_ENABLE_EXPERIMENTAL -DGLM_FORCE_CTOR_INIT -DGL_SILENCE_DEPRECATION"
OPT="-O2 -std=gnu++17 -w -fvisibility=hidden -fvisibility-inlines-hidden"
B=build-mac; O=$B/obj
mkdir -p $O
obj() { local src=$1 out=$O/$2.o; shift 2; echo "  cc $src"; $CXX $OPT $DEF $INC "$@" -c "$src" -o $out; }
cobj() { local src=$1 out=$O/$2.o; shift 2; echo "  cc $src"; $CC -O2 -w -fvisibility=hidden $INC -c "$src" -o $out; }

# ---- GLFW (static, universal)
if [ ! -f $B/glfw/src/libglfw3.a ]; then
  cmake -S ext/glfw -B $B/glfw -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.14 \
    -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build $B/glfw -j >/dev/null
fi

# ---- shared code
cobj ext/glad/src/gl.c glad
cobj ext/ufbx/ufbx.c ufbx
obj src/fonts.cpp fonts -O0
[ -f src/fonts_cine.cpp ] || python3 tools/embed_fonts.py
obj src/fonts_cine.cpp fonts_cine -O0
obj src/libs.cpp libs
for f in render extrude import project effect export platform fxgeo; do obj src/$f.cpp $f; done
COMMON="$O/fxgeo.o $O/glad.o $O/ufbx.o $O/fonts.o $O/fonts_cine.o $O/libs.o $O/render.o $O/extrude.o $O/import.o $O/project.o $O/effect.o"

# ---- plug-in binary
SDKINC="-I$SDK/Headers -I$SDK/Headers/SP -I$SDK/Headers/adobesdk -I$SDK/Util"
obj src/plugin.cpp plugin $SDKINC
echo "  link ElementGenie (plug-in)"
$CXX -bundle -o $O/ElementGenie $O/plugin.o $COMMON -framework OpenGL -framework CoreFoundation -Wl,-dead_strip
strip -x $O/ElementGenie

# ---- Scene Setup app
for f in imgui imgui_draw imgui_tables imgui_widgets; do obj ext/imgui/$f.cpp $f; done
obj ext/imgui/backends/imgui_impl_glfw.cpp imgui_impl_glfw
obj ext/imgui/backends/imgui_impl_opengl3.cpp imgui_impl_opengl3
obj ext/imguizmo/src/ImGuizmo.cpp imguizmo
obj src/logo_data.cpp logo_data
obj src/main.cpp main
echo "  cc src/platform_mac.mm"
$CXX -O2 -std=gnu++17 -w -fobjc-arc $INC -c src/platform_mac.mm -o $O/platform_mac.o
echo "  link Scene Setup"
$CXX -o "$O/Element Genie Scene Setup" $O/main.o $O/platform.o $O/platform_mac.o $O/export.o $O/logo_data.o $COMMON \
  $O/imgui.o $O/imgui_draw.o $O/imgui_tables.o $O/imgui_widgets.o $O/imgui_impl_glfw.o $O/imgui_impl_opengl3.o $O/imguizmo.o \
  $B/glfw/src/libglfw3.a -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL -framework CoreText -framework QuartzCore -Wl,-dead_strip
strip -x "$O/Element Genie Scene Setup"

# ---- app icon
ICON=$O/AppIcon.iconset; rm -rf $ICON; mkdir -p $ICON
for s in 16 32 128 256; do
  sips -z $s $s src/logo.png --out $ICON/icon_${s}x${s}.png >/dev/null
  sips -z $((s*2)) $((s*2)) src/logo.png --out $ICON/icon_${s}x${s}@2x.png >/dev/null
done
iconutil -c icns $ICON -o $O/AppIcon.icns

# ---- PiPL resource
echo "  rez PiPL"
REZ=$(xcrun -f Rez 2>/dev/null || echo Rez)
"$REZ" -d __MACH__ -d __x86_64__ -i "$SDK/Headers" -i "$SDK/Headers/SP" -i "$SDK/Resources" -s "$SDK/Resources" \
  -useDF -o $O/ElementGenie.rsrc plugin/ElementGeniePiPL_mac.r

# ---- bundles
P="$B/ElementGenie.plugin"; rm -rf "$P"
mkdir -p "$P/Contents/MacOS" "$P/Contents/Resources"
cp $O/ElementGenie "$P/Contents/MacOS/ElementGenie"
cp $O/ElementGenie.rsrc "$P/Contents/Resources/ElementGenie.rsrc"
printf 'eFKTFXTC' > "$P/Contents/PkgInfo"
cat > "$P/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>ElementGenie</string>
  <key>CFBundleIdentifier</key><string>com.geniestudiofx.elementgenie</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleName</key><string>Element Genie</string>
  <key>CFBundlePackageType</key><string>eFKT</string>
  <key>CFBundleSignature</key><string>FXTC</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>LSMinimumSystemVersion</key><string>10.14</string>
  <key>NSHumanReadableCopyright</key><string>Genie Studio FX. Free and open source (MIT).</string>
</dict></plist>
EOF
A="$P/Contents/Resources/Element Genie Scene Setup.app"
mkdir -p "$A/Contents/MacOS" "$A/Contents/Resources/Presets" "$A/Contents/Resources/Models"
cp "$O/Element Genie Scene Setup" "$A/Contents/MacOS/"
cp $O/AppIcon.icns "$A/Contents/Resources/AppIcon.icns"
cp presets/*.egpack "$A/Contents/Resources/Presets/"
cp samples/*.glb "$A/Contents/Resources/Models/"
printf 'APPL????' > "$A/Contents/PkgInfo"
cat > "$A/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>Element Genie Scene Setup</string>
  <key>CFBundleIdentifier</key><string>com.geniestudiofx.elementgenie.scenesetup</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleName</key><string>Element Genie</string>
  <key>CFBundleDisplayName</key><string>Element Genie Scene Setup</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>LSMinimumSystemVersion</key><string>10.14</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSPrincipalClass</key><string>NSApplication</string>
</dict></plist>
EOF
# ad-hoc signatures (required for Apple silicon); inside out
codesign --force --sign - "$A"
codesign --force --sign - "$P"

# ---- installer: drops the plug-in into the shared Adobe folder used by Premiere Pro and After Effects
ROOT=$B/pkgroot; rm -rf $ROOT; mkdir -p $ROOT
cp -R "$P" $ROOT/
pkgbuild --analyze --root $ROOT $B/components.plist >/dev/null
plutil -replace 0.BundleIsRelocatable -bool NO $B/components.plist
pkgbuild --root $ROOT --component-plist $B/components.plist \
  --install-location "/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore" \
  --identifier com.geniestudiofx.elementgenie --version $VERSION $B/component.pkg
cat > $B/distribution.xml <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>Element Genie $VERSION</title>
  <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <welcome mime-type="text/plain"><![CDATA[Element Genie $VERSION (Beta for Mac)

Free 3D titles, logos and models for Premiere Pro and After Effects 2020 or newer.

This installs the Element Genie effect for both apps. Close Premiere Pro and After Effects first.

After installing, find it under Effects > Element Genie.

Genie Studio FX - geniestudiofx.github.io/genie-studio]]></welcome>
  <choices-outline><line choice="default"><line choice="eg"/></line></choices-outline>
  <choice id="default"/>
  <choice id="eg" visible="false"><pkg-ref id="com.geniestudiofx.elementgenie"/></choice>
  <pkg-ref id="com.geniestudiofx.elementgenie" version="$VERSION" onConclusion="none">component.pkg</pkg-ref>
</installer-gui-script>
EOF
productbuild --distribution $B/distribution.xml --package-path $B "$B/Element Genie $VERSION Mac Installer.pkg"
echo "done: $B/Element Genie $VERSION Mac Installer.pkg"
