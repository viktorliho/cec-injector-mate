#!/bin/sh
set -eu
root=${1:-/usr/lib/qt6/plugins}
test -x "$root/platforms/libqxcb.so" || {
  echo 'Qt XCB platform plugin is unavailable' >&2; exit 1;
}
wayland=
for candidate in libqwayland.so libqwayland-generic.so libqwayland-egl.so; do
  if test -x "$root/platforms/$candidate"; then
    wayland=$root/platforms/$candidate
    break
  fi
done
test -n "$wayland" || {
  echo 'Qt Wayland platform plugin is unavailable' >&2; exit 1;
}
if test "${CEC_MATE_QT_CHECK_DEPENDENCIES:-0}" = 1; then
  for plugin in "$root/platforms/libqxcb.so" "$wayland"; do
    if ldd "$plugin" | grep -Fq 'not found'; then
      echo "Qt platform plugin dependency is missing: $plugin" >&2
      exit 1
    fi
  done
fi
