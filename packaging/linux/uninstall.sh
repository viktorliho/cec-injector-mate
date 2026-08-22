#!/bin/sh
set -eu

root=${CEC_MATE_ROOT:-}
purge_state=false
desktop_user=${CEC_MATE_DESKTOP_USER:-}
while test "$#" -gt 0; do
  case "$1" in
    --purge-state) purge_state=true; shift;;
    --desktop-user)
      test "$#" -ge 2 || { echo "--desktop-user requires USER" >&2; exit 2; }
      desktop_user=$2; shift 2;;
    *) echo "usage: uninstall.sh [--desktop-user USER] [--purge-state]" >&2; exit 2;;
  esac
done
if test -z "$root"; then
  test "$(id -u)" -eq 0 || { echo "uninstall requires root" >&2; exit 1; }
  systemctl disable --now cec-injector-mate.service cec-injector-mate-boot.service 2>/dev/null || true
  if test -r /etc/cec-injector-mate/installation.conf; then
    . /etc/cec-injector-mate/installation.conf
    desktop_user=${desktop_user:-${CEC_MATE_DESKTOP_USER:-}}
    for device in /sys/bus/usb/devices/*; do
      test -f "$device/idVendor" && test -f "$device/idProduct" && test -f "$device/serial" || continue
      test "$(cat "$device/idVendor")" = "$CEC_MATE_VENDOR" || continue
      test "$(cat "$device/idProduct")" = "$CEC_MATE_PRODUCT" || continue
      test "$(cat "$device/serial")" = "$CEC_MATE_SERIAL" || continue
      test ! -w "$device/power/wakeup" || printf '%s\n' disabled >"$device/power/wakeup"
    done
  fi
fi

rm -f -- \
  "${root}/etc/systemd/system/cec-injector-mate.service" \
  "${root}/etc/systemd/system/cec-injector-mate-boot.service" \
  "${root}/etc/udev/rules.d/70-cec-injector-mate-device.rules" \
  "${root}/etc/udev/rules.d/70-cec-injector-mate-platform.rules" \
  "${root}/etc/udev/rules.d/80-cec-injector-mate-device.rules" \
  "${root}/etc/udev/rules.d/80-cec-injector-mate-platform.rules" \
  "${root}/etc/udev/rules.d/71-cec-injector-mate-wakeup.rules" \
  "${root}/etc/polkit-1/rules.d/49-cec-injector-mate-suspend.rules" \
  "${root}/etc/dbus-1/system.d/org.cecinjector.Mate.conf"
desktop_home=${CEC_MATE_DESKTOP_HOME:-}
if test -z "$desktop_home" && test -n "$desktop_user"; then
  desktop_home=$(getent passwd "$desktop_user" | cut -d: -f6)
fi
if test -n "$desktop_home"; then
  rm -f -- "$desktop_home/.local/share/applications/cec-injector-mate.desktop"
  for size in 16x16 32x32 48x48 64x64 128x128 256x256 512x512; do
    rm -f -- "$desktop_home/.local/share/icons/hicolor/$size/apps/cec-injector-mate.png"
  done
fi
rm -rf -- "${root}/opt/cec-injector-mate"
if $purge_state; then
  rm -rf -- "${root}/var/lib/cec-injector-mate" \
    "${root}/etc/cec-injector-mate/runtime"
fi

if test -n "$root"; then
  echo "staged integration removed; configuration and state preserved"
  exit 0
fi

systemctl daemon-reload
busctl call org.freedesktop.DBus /org/freedesktop/DBus \
  org.freedesktop.DBus ReloadConfig
udevadm control --reload-rules
udevadm trigger --subsystem-match=tty --action=change
udevadm trigger --subsystem-match=misc --sysname-match=uinput --action=change
udevadm settle
if id cec-injector-mate >/dev/null 2>&1; then userdel cec-injector-mate; fi
if getent group cec-injector-mate >/dev/null 2>&1; then groupdel cec-injector-mate; fi
echo "configuration and state preserved under /etc and /var/lib; use --purge-state only for state"
