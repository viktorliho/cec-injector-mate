#!/bin/sh
set -eu

generator=$1
source_root=$2
serial_a=TEST-SERIAL-A
serial_b=TEST-SERIAL-B

rule_a=$($generator CAFE CEC1 "$serial_a")
rule_a_again=$($generator cafe cec1 "$serial_a")
rule_b=$($generator cafe cec1 "$serial_b")

test "$rule_a" = "$rule_a_again"
test "$rule_a" != "$rule_b"
test "$(printf '%s\n' "$rule_a" | wc -l)" -eq 1

for field in \
  'ACTION=="add|change"' \
  'SUBSYSTEM=="usb"' \
  'ENV{DEVTYPE}=="usb_device"' \
  'ATTR{idVendor}=="cafe"' \
  'ATTR{idProduct}=="cec1"' \
  "ATTR{serial}==\"$serial_a\"" \
  'TEST=="power/wakeup"' \
  'ATTR{power/wakeup}="enabled"'
do
  printf '%s\n' "$rule_a" | grep -Fq "$field"
done

! printf '%s\n' "$rule_a" | grep -Fq ttyACM
! printf '%s\n' "$rule_a" | grep -Fq "$serial_b"
# Exact USB-device VID/PID/serial matching excludes a normal Linux root hub
# and another Injector carrying a different serial.
! printf '%s\n' "$rule_a" | grep -Fq 'ATTR{idVendor}=="1d6b"'

if $generator cafe cec1 'unsafe"serial' >/dev/null 2>&1; then exit 1; fi
if $generator cafe xyz1 "$serial_a" >/dev/null 2>&1; then exit 1; fi

current_serial=53032847289E399
current_serial=${current_serial}C
! grep -R --exclude-dir=.git --exclude-dir=build --exclude='CMakeCache.txt' \
  -F "$current_serial" "$source_root"

manager=$(dirname "$generator")/manage-remote-wake-rule.sh
grep -Fq 'rule=/etc/udev/rules.d/71-cec-injector-mate-wakeup.rules' "$manager"
test "$(grep -Fc 'rule=/etc/udev/rules.d/71-cec-injector-mate-wakeup.rules' "$manager")" -eq 1
