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
runuser -u cec-injector-mate -- test -r "$device" && runuser -u cec-injector-mate -- test -w "$device" && echo "PASS serial access" || { echo "FAIL serial access"; fail=1; }
runuser -u cec-injector-mate -- test -r /dev/uinput && runuser -u cec-injector-mate -- test -w /dev/uinput && echo "PASS uinput access" || { echo "FAIL uinput access"; fail=1; }
virtual_inputs=$(grep -lFx 'CEC Injector Mate Remote' /sys/class/input/*/name 2>/dev/null | wc -l)
test "$virtual_inputs" -eq 1 && echo "PASS uinput backend healthy" || { echo "FAIL uinput backend count $virtual_inputs"; fail=1; }
systemd-inhibit --list --no-pager | grep -Fq cec-injector-mate && echo "PASS lifecycle inhibitors" || { echo "FAIL lifecycle inhibitors"; fail=1; }

test "$fail" -eq 0
