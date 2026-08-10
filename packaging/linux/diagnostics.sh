#!/bin/sh
set -eu

service=cec-injector-mate.service
boot_service=cec-injector-mate-boot.service
root=${CEC_MATE_ROOT:-}
health=${root}/opt/cec-injector-mate/current/libexec/health-check
config=${root}/etc/cec-injector-mate/installation.conf
output=${1:-cec-injector-mate-diagnostics-$(date -u +%Y%m%dT%H%M%SZ).tar.gz}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

test -n "$root" || test "$(id -u)" -eq 0 || {
  echo "diagnostics requires root" >&2; exit 1;
}
test -x "$health" && test -r "$config"
. "$config"

{
  echo "timestamp_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "boot_id=$(cat /proc/sys/kernel/random/boot_id)"
  echo "release=${CEC_MATE_RELEASE:-unknown}"
  echo "selected_device=${CEC_MATE_DEVICE_ID:-unknown}"
} >"$work/identity.txt"

"$health" --diagnostics >"$work/health.txt" 2>&1 || true
systemctl status "$service" "$boot_service" --no-pager >"$work/unit-status.txt" 2>&1 || true
systemctl show "$service" "$boot_service" >"$work/unit-properties.txt" 2>&1 || true
journalctl -u "$service" --since '-2 hours' --no-pager -o short-iso \
  >"$work/main-journal.txt" 2>&1 || true
journalctl -u "$boot_service" -b --no-pager -o short-iso \
  >"$work/boot-journal.txt" 2>&1 || true

device=$(find /dev/serial/by-id -maxdepth 1 -type l \
  -name "*${CEC_MATE_SERIAL}*" -print 2>/dev/null | head -n 1)
{
  echo "stable_locator_present=$([ -n "$device" ] && echo yes || echo no)"
  if test -n "$device"; then
    stat -Lc 'tty_owner_uid=%u tty_group_gid=%g tty_mode=%a' "$device"
    tty=$(readlink -f "$device")
    usb=$(readlink -f "/sys/class/tty/${tty##*/}/device/..")
    echo "usb_wakeup=$(cat "$usb/power/wakeup" 2>/dev/null || echo unavailable)"
  fi
  stat -Lc 'uinput_owner_uid=%u uinput_group_gid=%g uinput_mode=%a' /dev/uinput 2>/dev/null || true
} >"$work/device-state.txt"

cp "${root}/opt/cec-injector-mate/current/MANIFEST.txt" "$work/manifest.txt"
tar -czf "$output" -C "$work" .
echo "$output"
