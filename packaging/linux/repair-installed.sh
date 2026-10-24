#!/bin/sh
set -eu
root=${CEC_MATE_ROOT:-}
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
identity="${root}/var/lib/cec-injector-mate/installation/installation.conf"
bundle="${root}/opt/cec-injector-mate/current"
test -f "$identity" && test "$(wc -l < "$identity")" -eq 6 || {
  echo 'protected installation identity is missing or malformed' >&2; exit 1;
}
field() {
  key=$1
  test "$(grep -c "^${key}=" "$identity")" -eq 1 || exit 1
  sed -n "s/^${key}=//p" "$identity"
}
vendor=$(field CEC_MATE_VENDOR)
product=$(field CEC_MATE_PRODUCT)
serial=$(field CEC_MATE_SERIAL)
device_id=$(field CEC_MATE_DEVICE_ID)
release=$(field CEC_MATE_RELEASE)
desktop_user=$(field CEC_MATE_DESKTOP_USER)
case "$release" in *[!0-9a-f]*|'') exit 1;; esac
test "${#release}" -eq 40
case "$desktop_user" in *[!A-Za-z0-9_-]*) exit 1;; esac
"$source_dir/generate-device-access-rule.sh" "$vendor" "$product" "$serial" >/dev/null
encoded=$(printf '%s' "$serial" | od -An -tx1 | tr -d ' \n' | tr 'a-f' 'A-F')
test "$device_id" = "linux-usb-serial-v1:$encoded"
test "$(readlink -f "$bundle")" = \
  "$(readlink -f "${root}/opt/cec-injector-mate/releases/$release")" || {
  echo 'installed release identity mismatch' >&2; exit 1;
}
test "$(sed -n 's/^Mate commit: //p' "$bundle/MANIFEST.txt")" = "$release"
if test -n "$desktop_user"; then
  exec "$source_dir/install.sh" --repair-only --desktop-user "$desktop_user" \
    "$bundle" "$vendor" "$product" "$serial"
fi
exec "$source_dir/install.sh" --repair-only "$bundle" "$vendor" "$product" "$serial"
