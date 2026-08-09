#!/bin/sh
set -eu

service=cec-injector-mate.service
config=/etc/cec-injector-mate/installation.conf
test -r "$config" || { echo "FAIL installation configuration"; exit 1; }
. "$config"

fail=0
check() { if "$@"; then echo "PASS $*"; else echo "FAIL $*"; fail=1; fi; }
check systemctl is-active --quiet "$service"
restarts=$(systemctl show -p NRestarts --value "$service")
test "$restarts" -lt 3 && echo "PASS restart count $restarts" || { echo "FAIL restart count $restarts"; fail=1; }

invocation=$(systemctl show -p InvocationID --value "$service")
logs=$(journalctl --quiet --no-pager "_SYSTEMD_INVOCATION_ID=$invocation")
for evidence in \
  'discovery Device count 1' \
  'physical address available ' \
  'DeviceSession connected' \
  'Remote Wake authorization Authorized' \
  'Wake configured and armed'
do
  printf '%s\n' "$logs" | grep -Fq "$evidence" && echo "PASS $evidence" || { echo "FAIL $evidence"; fail=1; }
done

device=$(find /dev/serial/by-id -maxdepth 1 -type l -name "*${CEC_MATE_SERIAL}*" -print 2>/dev/null | head -n 2)
test "$(printf '%s\n' "$device" | sed '/^$/d' | wc -l)" -eq 1 && echo "PASS selected Device visible" || { echo "FAIL selected Device visible"; fail=1; }
service_uid=$(id -u cec-injector-mate)
test -n "$device" && test "$(stat -Lc %u "$device")" -eq "$service_uid" && echo "PASS serial owner" || { echo "FAIL serial owner"; fail=1; }
test -n "$device" && test "$((0$(stat -Lc %a "$device") & 7))" -eq 0 && echo "PASS serial no world access" || { echo "FAIL serial world access"; fail=1; }
runuser -u cec-injector-mate -- test -r "$device" && runuser -u cec-injector-mate -- test -w "$device" && echo "PASS serial access" || { echo "FAIL serial access"; fail=1; }
test "$(stat -Lc %u /dev/uinput)" -eq "$service_uid" && echo "PASS uinput owner" || { echo "FAIL uinput owner"; fail=1; }
test "$((0$(stat -Lc %a /dev/uinput) & 7))" -eq 0 && echo "PASS uinput no world access" || { echo "FAIL uinput world access"; fail=1; }
runuser -u cec-injector-mate -- test -r /dev/uinput && runuser -u cec-injector-mate -- test -w /dev/uinput && echo "PASS uinput access" || { echo "FAIL uinput access"; fail=1; }
drm_access=false
for card in /dev/dri/card*; do
  test -c "$card" || continue
  if runuser -u cec-injector-mate -G video -- test -r "$card"; then drm_access=true; break; fi
done
$drm_access && echo "PASS DRM primary-node access" || { echo "FAIL DRM primary-node access"; fail=1; }
video_gid=$(getent group video | cut -d: -f3)
main_pid=$(systemctl show -p MainPID --value "$service")
if test "$main_pid" -gt 0 && grep -E "^Groups:.*(^|[[:space:]])${video_gid}([[:space:]]|$)" "/proc/$main_pid/status" >/dev/null; then
  echo "PASS daemon DRM primary-node group"
else
  echo "FAIL daemon DRM primary-node group"
  fail=1
fi
virtual_inputs=$(grep -lFx 'CEC Injector Mate Remote' /sys/class/input/*/name 2>/dev/null | wc -l)
test "$virtual_inputs" -eq 1 && echo "PASS uinput backend healthy" || { echo "FAIL uinput backend count $virtual_inputs"; fail=1; }
systemd-inhibit --list --no-pager | grep -Fq cec-injector-mate && echo "PASS lifecycle inhibitors" || { echo "FAIL lifecycle inhibitors"; fail=1; }

test "$fail" -eq 0
