#!/bin/sh
set -eu

readonly rule=/etc/udev/rules.d/71-cec-injector-mate-wakeup.rules
readonly generator="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/generate-remote-wake-rule.sh"

test "$(id -u)" -eq 0 || {
  echo "remote-wake integration must be managed as root" >&2
  exit 1
}

reload_rules() {
  udevadm control --reload-rules
  udevadm settle
}

case "${1:-}" in
  install)
    vendor=${2:?usage: manage-remote-wake-rule.sh install VID PID SERIAL}
    product=${3:?usage: manage-remote-wake-rule.sh install VID PID SERIAL}
    serial=${4:?usage: manage-remote-wake-rule.sh install VID PID SERIAL}
    test "$#" -eq 4 || exit 2
    vendor=$(printf '%s' "$vendor" | tr 'A-F' 'a-f')
    product=$(printf '%s' "$product" | tr 'A-F' 'a-f')

    temporary=$(mktemp "${rule}.tmp.XXXXXX")
    trap 'rm -f -- "$temporary"' EXIT
    "$generator" "$vendor" "$product" "$serial" >"$temporary"
    chmod 0644 "$temporary"
    chown root:root "$temporary"
    if ! cmp -s "$temporary" "$rule"; then
      mv -f "$temporary" "$rule"
    else
      rm -f -- "$temporary"
    fi
    trap - EXIT
    reload_rules
    udevadm trigger --subsystem-match=usb \
      --attr-match=idVendor="$vendor" \
      --attr-match=idProduct="$product" \
      --attr-match=serial="$serial" --action=change
    udevadm settle

    matches=0
    selected=
    for device in /sys/bus/usb/devices/*; do
      test -f "$device/idVendor" || continue
      test -f "$device/idProduct" || continue
      test -f "$device/serial" || continue
      test "$(cat "$device/idVendor")" = "$vendor" || continue
      test "$(cat "$device/idProduct")" = "$product" || continue
      test "$(cat "$device/serial")" = "$serial" || continue
      matches=$((matches + 1))
      selected=$device
    done
    test "$matches" -eq 1 || {
      echo "selected USB identity is absent or ambiguous" >&2
      exit 1
    }
    test "$(cat "$selected/power/wakeup" 2>/dev/null || true)" = enabled || {
      echo "selected USB Device did not become wake-authorized" >&2
      exit 1
    }
    ;;
  remove)
    test "$#" -eq 1 || exit 2
    rm -f -- "$rule"
    reload_rules
    ;;
  *)
    echo "usage: manage-remote-wake-rule.sh {install VID PID SERIAL|remove}" >&2
    exit 2
    ;;
esac
