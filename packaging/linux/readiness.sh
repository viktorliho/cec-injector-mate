#!/bin/sh
set -eu

profile=${1:-}
timeout=${2:-30}
root=${CEC_MATE_READINESS_ROOT:-}

case "$profile" in boot|daemon) ;; *) echo "usage: readiness PROFILE TIMEOUT" >&2; exit 2;; esac
case "$timeout" in ''|*[!0-9]*) echo "invalid readiness timeout" >&2; exit 2;; esac
test "$timeout" -gt 0 || { echo "invalid readiness timeout" >&2; exit 2; }

config=${root}/etc/cec-injector-mate/installation.conf
test -r "$config" || { echo "readiness timeout profile=$profile resource=config duration_ms=0" >&2; exit 1; }
value() { sed -n "s/^$1=//p" "$config" | head -n 1; }
vendor=$(value CEC_MATE_VENDOR)
product=$(value CEC_MATE_PRODUCT)
serial=$(value CEC_MATE_SERIAL)
test -n "$vendor" && test -n "$product" && test -n "$serial" || {
  echo "readiness timeout profile=$profile resource=config duration_ms=0" >&2
  exit 1
}

started=$(date +%s)
deadline=$((started + timeout))
last=

resource_state() {
  stable=$(find "${root}/dev/serial/by-id" -maxdepth 1 -type l -name "*${serial}*" -print 2>/dev/null | head -n 2)
  test "$(printf '%s\n' "$stable" | sed '/^$/d' | wc -l)" -eq 1 || { echo serial; return; }
  sh -c 'exec 3<>"$1"' sh "$stable" 2>/dev/null || { echo serial; return; }

  drm_ready=false
  for card in "${root}"/dev/dri/card*; do
    test -e "$card" || continue
    if sh -c 'exec 3<"$1"' sh "$card" 2>/dev/null; then drm_ready=true; break; fi
  done
  $drm_ready || { echo drm; return; }

  test "$profile" = daemon || { echo ready; return; }
  test -e "${root}/dev/uinput" &&
    sh -c 'exec 3>"$1"' sh "${root}/dev/uinput" 2>/dev/null || { echo uinput; return; }

  selected_usb=
  for device in "${root}"/sys/bus/usb/devices/*; do
    test -f "$device/idVendor" && test -f "$device/idProduct" &&
      test -f "$device/serial" || continue
    test "$(cat "$device/idVendor")" = "$vendor" || continue
    test "$(cat "$device/idProduct")" = "$product" || continue
    test "$(cat "$device/serial")" = "$serial" || continue
    test -z "$selected_usb" || { echo wake; return; }
    selected_usb=$device
  done
  test -n "$selected_usb" && test "$(cat "$selected_usb/power/wakeup" 2>/dev/null)" = enabled || {
    echo wake; return;
  }
  echo ready
}

while :; do
  resource=$(resource_state)
  now=$(date +%s)
  if test "$resource" = ready; then
    echo "readiness profile=$profile ready duration_ms=$(((now - started) * 1000))" >&2
    exit 0
  fi
  if test "$resource" != "$last"; then
    echo "readiness profile=$profile waiting resource=$resource" >&2
    last=$resource
  fi
  if test "$now" -ge "$deadline"; then
    echo "readiness timeout profile=$profile resource=$resource duration_ms=$(((now - started) * 1000))" >&2
    exit 1
  fi
  sleep 0.1
done
