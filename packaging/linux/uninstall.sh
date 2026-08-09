#!/bin/sh
set -eu

root=${CEC_MATE_ROOT:-}
purge_state=false
test "${1:-}" != "--purge-state" || { purge_state=true; shift; }
test "$#" -eq 0 || { echo "usage: uninstall.sh [--purge-state]" >&2; exit 2; }
if test -z "$root"; then
  test "$(id -u)" -eq 0 || { echo "uninstall requires root" >&2; exit 1; }
  systemctl disable --now cec-injector-mate.service cec-injector-mate-boot.service 2>/dev/null || true
  if test -r /etc/cec-injector-mate/installation.conf; then
    . /etc/cec-injector-mate/installation.conf
    for device in /sys/bus/usb/devices/*; do
      test -f "$device/idVendor" && test -f "$device/idProduct" && test -f "$device/serial" || continue
      test "$(cat "$device/idVendor")" = "$CEC_MATE_VENDOR" || continue
      test "$(cat "$device/idProduct")" = "$CEC_MATE_PRODUCT" || continue
      test "$(cat "$device/serial")" = "$CEC_MATE_SERIAL" || continue
      test ! -w "$device/power/wakeup" || printf '%s\n' disabled >"$device/power/wakeup"
    done
    selected=$(find /dev/serial/by-id -maxdepth 1 -type l -name "*${CEC_MATE_SERIAL}*" -print 2>/dev/null | head -n 1)
    test -z "$selected" || setfacl -x u:cec-injector-mate "$(readlink -f "$selected")" 2>/dev/null || true
  fi
  setfacl -x u:cec-injector-mate /dev/uinput /dev/dri/card* 2>/dev/null || true
fi

rm -f -- \
  "${root}/etc/systemd/system/cec-injector-mate.service" \
  "${root}/etc/systemd/system/cec-injector-mate-boot.service" \
  "${root}/etc/udev/rules.d/70-cec-injector-mate-device.rules" \
  "${root}/etc/udev/rules.d/70-cec-injector-mate-platform.rules" \
  "${root}/etc/udev/rules.d/71-cec-injector-mate-wakeup.rules" \
  "${root}/etc/polkit-1/rules.d/49-cec-injector-mate-suspend.rules" \
  "${root}/usr/libexec/cec-injector-mate/health-check"
rm -rf -- "${root}/opt/cec-injector-mate" "${root}/usr/libexec/cec-injector-mate"
if $purge_state; then rm -rf -- "${root}/var/lib/cec-injector-mate"; fi

if test -n "$root"; then
  echo "staged integration removed; configuration and state preserved"
  exit 0
fi

systemctl daemon-reload
udevadm control --reload-rules
udevadm settle
if id cec-injector-mate >/dev/null 2>&1; then userdel cec-injector-mate; fi
echo "configuration and state preserved under /etc and /var/lib; use --purge-state only for state"
