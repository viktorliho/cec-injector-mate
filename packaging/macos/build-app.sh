#!/bin/zsh
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
audit_script="$repo_root/packaging/macos/audit-app.sh"

libcec_commit=3519af0715e654c43382423d843ad658f2e01cb4
p8_commit=2748be52ae27e6007ef548b697d4a03ff7de4291
libcec_patch_sha=2f7eb96e157735be3ea9a64cb933a61ad48e95887cdf5d3a701a98ee73fc4c2f
qt_prefix=${QT_PREFIX:-$(brew --prefix qt)}
qt_cmake_prefix=${QT_CMAKE_PREFIX:-$qt_prefix/lib/cmake}
macdeployqt_bin=${MACDEPLOYQT:-$(dirname "$qt_prefix")/qt/bin/macdeployqt}
build_root=${BUILD_ROOT:-"$repo_root/build/macos-app"}
third_party_root=${THIRD_PARTY_BUILD_ROOT:-/private/tmp/cec-injector-mate-m12c}
prefix_dir=${CEC_PREFIX:-"$third_party_root/prefix"}
dist_root=${DIST_ROOT:-"$repo_root/dist"}
app_name="CEC Injector Mate.app"
stage_root="$dist_root/.staging"
stage_app_name="CECInjectorMate.app"
app_path="$dist_root/$app_name"
stage_app_path="$stage_root/$stage_app_name"
mate_build="$build_root/mate"
p8_src="$third_party_root/p8-platform"
libcec_src="$third_party_root/libcec"
p8_build="$third_party_root/p8-build"
libcec_build="$third_party_root/libcec-build"

require_tool() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "missing required tool: $1" >&2
        exit 1
    }
}

require_tool git
require_tool cmake
require_tool install_name_tool
require_tool otool
require_tool codesign
require_tool plutil
require_tool shasum
require_tool sips
require_tool iconutil
require_tool "$macdeployqt_bin"
require_tool "$audit_script"

mkdir -p "$third_party_root" "$build_root" "$dist_root" "$stage_root"

if [ ! -d "$p8_src/.git" ]; then
    rm -rf "$p8_src"
    git clone https://github.com/Pulse-Eight/platform.git "$p8_src"
fi
git -C "$p8_src" fetch --tags origin
git -C "$p8_src" checkout --detach "$p8_commit"
test "$(git -C "$p8_src" rev-parse HEAD)" = "$p8_commit"

if [ ! -d "$libcec_src/.git" ]; then
    rm -rf "$libcec_src"
    git clone https://github.com/Pulse-Eight/libcec.git "$libcec_src"
fi
git -C "$libcec_src" fetch --tags origin
git -C "$libcec_src" checkout --detach "$libcec_commit"
test "$(git -C "$libcec_src" rev-parse HEAD)" = "$libcec_commit"

echo "$libcec_patch_sha  $repo_root/third_party/libcec/libcec-7.1.1-project-adapter-v1.patch" \
    | shasum -a 256 -c
git -C "$libcec_src" reset --hard "$libcec_commit"
git -C "$libcec_src" clean -fdx
rm -rf "$libcec_src/src/platform"
cp -R "$p8_src" "$libcec_src/src/platform"
git -C "$libcec_src/src/platform" checkout --detach "$p8_commit"
git -C "$libcec_src" apply \
    "$repo_root/third_party/libcec/libcec-7.1.1-project-adapter-v1.patch"

rm -rf "$prefix_dir" "$p8_build" "$libcec_build"
cmake -S "$libcec_src/src/platform" -B "$p8_build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix_dir" \
    -DBUILD_SHARED_LIBS=ON
cmake --build "$p8_build" --parallel
cmake --install "$p8_build"

PKG_CONFIG_PATH="$prefix_dir/lib/pkgconfig" \
cmake -S "$libcec_src" -B "$libcec_build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix_dir" \
    -DBUILD_SHARED_LIBS=ON
cmake --build "$libcec_build" --parallel
cmake --install "$libcec_build"

nm -gU "$prefix_dir/lib/libcec.dylib" \
    | grep -E '(_)?cec_project_adapter_transaction_v1$'

rm -rf "$mate_build"
PKG_CONFIG_PATH="$prefix_dir/lib/pkgconfig" \
cmake -S "$repo_root" -B "$mate_build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON \
    -DCEC_INJECTOR_MATE_BUILD_MACOS=ON \
    -DCEC_INJECTOR_MATE_BUILD_GUI=ON \
    -DCEC_INJECTOR_MATE_BUILD_LIBCEC=ON \
    -DCMAKE_PREFIX_PATH="$qt_cmake_prefix"
cmake --build "$mate_build" --parallel

rm -rf "$app_path" "$stage_root"
mkdir -p "$stage_root"
cp -R "$mate_build/applications/gui/cec-injector-mate-gui.app" "$stage_app_path"
mkdir -p "$stage_app_path/Contents/Frameworks" "$stage_app_path/Contents/Resources/third_party"

icon_source="$repo_root/applications/gui/resources/icon/cec-injector-mate.png"
iconset="$stage_root/cec-injector-mate.iconset"
mkdir -p "$iconset"
for spec in \
    '16 icon_16x16.png' \
    '32 icon_16x16@2x.png' \
    '32 icon_32x32.png' \
    '64 icon_32x32@2x.png' \
    '128 icon_128x128.png' \
    '256 icon_128x128@2x.png' \
    '256 icon_256x256.png' \
    '512 icon_256x256@2x.png' \
    '512 icon_512x512.png' \
    '1024 icon_512x512@2x.png'; do
    size=${spec%% *}
    name=${spec#* }
    sips -z "$size" "$size" "$icon_source" --out "$iconset/$name" >/dev/null
done
iconutil -c icns "$iconset" \
    -o "$stage_app_path/Contents/Resources/cec-injector-mate.icns"

copy_runtime() {
    src=$1
    base=$(basename "$src")
    cp -f "$src" "$stage_app_path/Contents/Frameworks/$base"
}

copy_runtime "$prefix_dir/lib/libcec.7.1.1.dylib"
copy_runtime "$prefix_dir/lib/libp8-platform.2.1.1.dylib"

ln -sf libcec.7.1.1.dylib "$stage_app_path/Contents/Frameworks/libcec.7.dylib"
ln -sf libcec.7.dylib "$stage_app_path/Contents/Frameworks/libcec.dylib"
ln -sf libp8-platform.2.1.1.dylib "$stage_app_path/Contents/Frameworks/libp8-platform.2.dylib"
ln -sf libp8-platform.2.dylib "$stage_app_path/Contents/Frameworks/libp8-platform.dylib"

cp -f "$repo_root/LICENSE" "$stage_app_path/Contents/Resources/"
cp -f "$repo_root/THIRD_PARTY.md" "$stage_app_path/Contents/Resources/"
cp -f "$repo_root/third_party/README.md" \
    "$stage_app_path/Contents/Resources/third_party/README.md"
cp -f "$repo_root/third_party/libcec/README.md" \
    "$stage_app_path/Contents/Resources/third_party/libcec-README.md"
cp -f "$repo_root/third_party/libcec/libcec-7.1.1-project-adapter-v1.patch" \
    "$stage_app_path/Contents/Resources/third_party/"

strip_forbidden_rpaths() {
    local macho=$1
    local rpath
    while IFS= read -r rpath; do
        case "$rpath" in
            /opt/homebrew/*|/private/tmp/*|/tmp/*|"$repo_root"/*)
                install_name_tool -delete_rpath "$rpath" "$macho" || true
                ;;
        esac
    done < <(otool -l "$macho" | awk '
        $1 == "cmd" && $2 == "LC_RPATH" { in_rpath = 1; next }
        in_rpath && $1 == "path" { print $2; in_rpath = 0 }
    ')
}

gui_bin="$stage_app_path/Contents/MacOS/cec-injector-mate-gui"
libcec_bundle="$stage_app_path/Contents/Frameworks/libcec.7.1.1.dylib"
libp8_bundle="$stage_app_path/Contents/Frameworks/libp8-platform.2.1.1.dylib"
libdbus_bundle="$stage_app_path/Contents/Frameworks/libdbus-1.3.dylib"
libbrotlicommon_bundle="$stage_app_path/Contents/Frameworks/libbrotlicommon.1.dylib"
libbrotlidec_bundle="$stage_app_path/Contents/Frameworks/libbrotlidec.1.dylib"

install_name_tool -id "@rpath/libcec.7.dylib" "$libcec_bundle"
install_name_tool -id "@rpath/libp8-platform.2.dylib" "$libp8_bundle"
install_name_tool -change "$prefix_dir/lib/libp8-platform.2.dylib" \
    "@rpath/libp8-platform.2.dylib" "$libcec_bundle" || true
install_name_tool -change "$prefix_dir/lib/libp8-platform.2.1.1.dylib" \
    "@rpath/libp8-platform.2.dylib" "$libcec_bundle" || true
install_name_tool -change "$prefix_dir/lib/libcec.7.dylib" \
    "@rpath/libcec.7.dylib" "$gui_bin" || true
install_name_tool -change "$prefix_dir/lib/libcec.7.1.1.dylib" \
    "@rpath/libcec.7.dylib" "$gui_bin" || true
"$macdeployqt_bin" "$stage_app_path" -always-overwrite -verbose=1

rm -f "$stage_app_path/Contents/PlugIns/platforminputcontexts/libqtvirtualkeyboardplugin.dylib"
rm -f "$stage_app_path/Contents/PlugIns/iconengines/libqsvgicon.dylib"
rm -f "$stage_app_path/Contents/PlugIns/imageformats/libqpdf.dylib"
rmdir "$stage_app_path/Contents/PlugIns/platforminputcontexts" 2>/dev/null || true
rmdir "$stage_app_path/Contents/PlugIns/iconengines" 2>/dev/null || true

rm -rf "$stage_app_path/Contents/Frameworks/QtQuick.framework"
rm -rf "$stage_app_path/Contents/Frameworks/QtOpenGL.framework"
rm -rf "$stage_app_path/Contents/Frameworks/QtQml.framework"
rm -rf "$stage_app_path/Contents/Frameworks/QtQmlMeta.framework"
rm -rf "$stage_app_path/Contents/Frameworks/QtQmlModels.framework"
rm -rf "$stage_app_path/Contents/Frameworks/QtQmlWorkerScript.framework"
rm -rf "$stage_app_path/Contents/Frameworks/QtNetwork.framework"

qtcore_bundle="$stage_app_path/Contents/Frameworks/QtCore.framework/Versions/A/QtCore"
qtgui_bundle="$stage_app_path/Contents/Frameworks/QtGui.framework/Versions/A/QtGui"
qtwidgets_bundle="$stage_app_path/Contents/Frameworks/QtWidgets.framework/Versions/A/QtWidgets"
qtdbus_bundle="$stage_app_path/Contents/Frameworks/QtDBus.framework/Versions/A/QtDBus"

install_name_tool -delete_rpath "$qt_prefix/lib" "$gui_bin" 2>/dev/null || true
install_name_tool -id "@rpath/QtCore.framework/Versions/A/QtCore" "$qtcore_bundle"
install_name_tool -id "@rpath/QtGui.framework/Versions/A/QtGui" "$qtgui_bundle"
install_name_tool -id "@rpath/QtWidgets.framework/Versions/A/QtWidgets" "$qtwidgets_bundle"
if [ -f "$qtdbus_bundle" ]; then
    install_name_tool -id "@rpath/QtDBus.framework/Versions/A/QtDBus" "$qtdbus_bundle"
    install_name_tool -change "/opt/homebrew/opt/dbus/lib/libdbus-1.3.dylib" \
        "@executable_path/../Frameworks/libdbus-1.3.dylib" \
        "$qtdbus_bundle"
fi
install_name_tool -change "@executable_path/../Frameworks/QtCore.framework/Versions/A/QtCore" \
    "@rpath/QtCore.framework/Versions/A/QtCore" "$qtgui_bundle"
install_name_tool -change "@executable_path/../Frameworks/QtCore.framework/Versions/A/QtCore" \
    "@rpath/QtCore.framework/Versions/A/QtCore" "$qtwidgets_bundle"
install_name_tool -change "@executable_path/../Frameworks/QtGui.framework/Versions/A/QtGui" \
    "@rpath/QtGui.framework/Versions/A/QtGui" "$qtwidgets_bundle"
if [ -f "$libdbus_bundle" ]; then
    install_name_tool -id "@executable_path/../Frameworks/libdbus-1.3.dylib" "$libdbus_bundle"
fi
if [ -f "$libbrotlicommon_bundle" ]; then
    install_name_tool -id "@executable_path/../Frameworks/libbrotlicommon.1.dylib" \
        "$libbrotlicommon_bundle"
fi
if [ -f "$libbrotlidec_bundle" ]; then
    install_name_tool -id "@executable_path/../Frameworks/libbrotlidec.1.dylib" \
        "$libbrotlidec_bundle"
    install_name_tool -change "@rpath/libbrotlicommon.1.dylib" \
        "@executable_path/../Frameworks/libbrotlicommon.1.dylib" \
        "$libbrotlidec_bundle"
fi

while IFS= read -r macho; do
    strip_forbidden_rpaths "$macho"
done < <(find "$stage_app_path" \( -name '*.dylib' -o -perm -111 \) -type f | sort)

"$audit_script" "$stage_app_path"

codesign --force --deep --sign - "$stage_app_path"
codesign --verify --deep --strict --verbose=2 "$stage_app_path"
"$audit_script" --verify-codesign "$stage_app_path"

mv "$stage_app_path" "$app_path"
rmdir "$stage_root" 2>/dev/null || true

echo "App bundle ready: $app_path"
shasum -a 256 "$app_path/Contents/MacOS/cec-injector-mate-gui"
