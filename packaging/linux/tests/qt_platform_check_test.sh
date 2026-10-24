#!/bin/sh
set -eu
source_root=$1
check=$source_root/packaging/linux/check-qt-platforms.sh
fixture=$(mktemp -d)
trap 'rm -rf -- "$fixture"' EXIT
mkdir -p "$fixture/platforms"
printf '#!/bin/sh\n' > "$fixture/platforms/libqxcb.so"
chmod 0755 "$fixture/platforms/libqxcb.so"
if "$check" "$fixture" 2>/dev/null; then
  echo 'missing Wayland plugin was accepted' >&2; exit 1
fi
for name in libqwayland-generic.so libqwayland-egl.so libqwayland.so; do
  printf '#!/bin/sh\n' > "$fixture/platforms/$name"
  chmod 0755 "$fixture/platforms/$name"
  "$check" "$fixture"
  rm -f "$fixture/platforms/$name"
done
echo 'old and current Qt Wayland layouts passed'
