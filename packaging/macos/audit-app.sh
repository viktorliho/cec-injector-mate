#!/bin/zsh
set -euo pipefail

verify_codesign=0
if [[ $# -gt 0 && "$1" == "--verify-codesign" ]]; then
    verify_codesign=1
    shift
fi

if [[ $# -ne 1 ]]; then
    echo "usage: $0 [--verify-codesign] /path/to/App.app" >&2
    exit 1
fi

app_path=$1
frameworks_dir="$app_path/Contents/Frameworks"
plugins_dir="$app_path/Contents/PlugIns"
resources_dir="$app_path/Contents/Resources"
info_plist="$app_path/Contents/Info.plist"
gui_bin="$app_path/Contents/MacOS/cec-injector-mate-gui"
libcec_bundle="$frameworks_dir/libcec.7.1.1.dylib"
libp8_bundle="$frameworks_dir/libp8-platform.2.1.1.dylib"
forbidden_prefixes=(
    /opt/homebrew
    /private/tmp
    /tmp
    /Users/ivi_liho/personal/cec-injector/cec-injector-mate/build
    /Users/ivi_liho/personal/cec-injector/cec-injector-mate
)

require_file() {
    [[ -f "$1" ]] || {
        echo "missing required file: $1" >&2
        exit 1
    }
}

require_dir() {
    [[ -d "$1" ]] || {
        echo "missing required directory: $1" >&2
        exit 1
    }
}

check_no_forbidden_deps() {
    local macho=$1
    while IFS= read -r dep; do
        [[ -n "$dep" ]] || continue
        case "$dep" in
            @rpath/*|@executable_path/*|@loader_path/*|/System/*|/usr/lib/*)
                ;;
            *)
                echo "unexpected dependency in $macho: $dep" >&2
                exit 1
                ;;
        esac
        for prefix in "${forbidden_prefixes[@]}"; do
            if [[ "$dep" == ${prefix}* ]]; then
                echo "forbidden dependency prefix in $macho: $dep" >&2
                exit 1
            fi
        done
    done < <(otool -L "$macho" | tail -n +2 | awk '{print $1}')
}

check_no_forbidden_rpaths() {
    local macho=$1
    python3 - "$macho" <<'PY'
import subprocess
import sys

macho = sys.argv[1]
forbidden = [
    "/opt/homebrew",
    "/private/tmp",
    "/tmp",
    "/Users/ivi_liho/personal/cec-injector/cec-injector-mate/build",
    "/Users/ivi_liho/personal/cec-injector/cec-injector-mate",
]

output = subprocess.check_output(["otool", "-l", macho], text=True)
lines = output.splitlines()
rpaths = []
for i, line in enumerate(lines):
    if line.strip() != "cmd LC_RPATH":
        continue
    for j in range(i + 1, min(i + 8, len(lines))):
        candidate = lines[j].strip()
        if not candidate.startswith("path "):
            continue
        path = candidate.split(" (offset ", 1)[0][5:]
        if any(path.startswith(prefix) for prefix in forbidden):
            print(f"forbidden rpath in {macho}: {path}", file=sys.stderr)
            sys.exit(1)
        rpaths.append(path)
        break

if macho.endswith("/Contents/MacOS/cec-injector-mate-gui"):
    required = "@executable_path/../Frameworks"
    if required not in rpaths:
        print(f"missing required rpath in {macho}: {required}", file=sys.stderr)
        sys.exit(1)
PY
}

require_dir "$app_path"
require_file "$gui_bin"
require_file "$info_plist"
require_dir "$frameworks_dir"
require_dir "$plugins_dir"
require_dir "$resources_dir"
require_file "$plugins_dir/platforms/libqcocoa.dylib"
require_file "$libcec_bundle"
require_file "$libp8_bundle"
require_file "$resources_dir/LICENSE"
require_file "$resources_dir/THIRD_PARTY.md"
require_file "$resources_dir/third_party/libcec-7.1.1-project-adapter-v1.patch"

plutil -lint "$info_plist" >/dev/null
nm -gU "$libcec_bundle" | grep -E '(_)?cec_project_adapter_transaction_v1$' >/dev/null

for macho in "$gui_bin" "$libcec_bundle" "$libp8_bundle"; do
    file "$macho" | grep -q 'arm64'
done

while IFS= read -r macho; do
    check_no_forbidden_deps "$macho"
    check_no_forbidden_rpaths "$macho"
done < <(find "$app_path" \( -name '*.dylib' -o -perm -111 \) -type f | sort)

if find "$app_path" -type f | grep -E '/(libqtvirtualkeyboardplugin\.dylib|libqsvgicon\.dylib|libqpdf\.dylib)$' >/dev/null; then
    echo "unexpected optional plugin present in bundle" >&2
    exit 1
fi

if [[ $verify_codesign -eq 1 ]]; then
    codesign --verify --deep --strict --verbose=2 "$app_path" >/dev/null
fi
