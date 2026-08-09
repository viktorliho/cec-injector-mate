#!/bin/sh
set -eu

source_root=$1
packaging=$source_root/packaging/linux
fixture=$(mktemp -d)
trap 'rm -rf -- "$fixture"' EXIT
bundle=$fixture/bundle
root=$fixture/root
mkdir -p "$bundle/bin" "$bundle/lib" "$root"
printf '#!/bin/sh\nexit 0\n' >"$bundle/bin/cec-injector-mate"
chmod 0755 "$bundle/bin/cec-injector-mate"
printf 'Mate commit: 0123456789abcdef0123456789abcdef01234567\n' >"$bundle/MANIFEST.txt"

serial=TEST-PRODUCTION-SERIAL
CEC_MATE_ROOT=$root "$packaging/install.sh" "$bundle" CAFE CEC1 "$serial"
snapshot_a=$fixture/a
find "$root" -type f -print0 | sort -z | xargs -0 sha256sum >"$snapshot_a"
CEC_MATE_ROOT=$root "$packaging/install.sh" "$bundle" cafe cec1 "$serial"
find "$root" -type f -print0 | sort -z | xargs -0 sha256sum >"$fixture/b"
cmp "$snapshot_a" "$fixture/b"

device_rule=$root/etc/udev/rules.d/70-cec-injector-mate-device.rules
wake_rule=$root/etc/udev/rules.d/71-cec-injector-mate-wakeup.rules
platform_rule=$root/etc/udev/rules.d/70-cec-injector-mate-platform.rules
polkit=$root/etc/polkit-1/rules.d/49-cec-injector-mate-suspend.rules
service=$root/etc/systemd/system/cec-injector-mate.service
boot=$root/etc/systemd/system/cec-injector-mate-boot.service

grep -Fq "ATTRS{serial}==\"$serial\"" "$device_rule"
grep -Fq 'RUN+="/usr/bin/setfacl -m u:cec-injector-mate:rw /dev/%k"' "$device_rule"
! grep -Fq ttyACM "$device_rule"
grep -Fq "ATTR{serial}==\"$serial\"" "$wake_rule"
grep -Fq 'KERNEL=="uinput"' "$platform_rule"
grep -Fq 'u:cec-injector-mate:rw' "$platform_rule"
grep -Fq 'KERNEL=="card[0-9]*"' "$platform_rule"
! grep -Fq 'MODE="0666"' "$platform_rule"

test "$(grep -Fc 'org.freedesktop.login1.suspend"' "$polkit")" -eq 1
test "$(grep -Fc 'org.freedesktop.login1.suspend-multiple-sessions"' "$polkit")" -eq 1
grep -Fq 'subject.user == "cec-injector-mate"' "$polkit"
grep -Fq 'User=cec-injector-mate' "$service"
! grep -Fq 'User=root' "$service"
grep -Fq 'NoNewPrivileges=yes' "$service"
grep -Fq 'Restart=on-failure' "$service"
grep -Fq -- '--device-id linux-usb-serial-v1:544553542D50524F44554354494F4E2D53455249414C' "$service"
grep -Fq 'After=cec-injector-mate-boot.service' "$service"
! grep -Fq 'Wants=cec-injector-mate-boot.service' "$service"
! grep -Fq -- '--boot-once' "$service"
grep -Fq -- '--boot-once --startup-timeout 30' "$boot"
grep -Fq 'Before=cec-injector-mate.service' "$boot"

# Reinstalling for a new serial replaces, rather than accumulates, rules.
CEC_MATE_ROOT=$root "$packaging/install.sh" "$bundle" cafe cec1 TEST-SECOND-SERIAL
grep -Fq 'TEST-SECOND-SERIAL' "$device_rule"
! grep -Fq "$serial" "$device_rule"
grep -Fq -- '--device-id linux-usb-serial-v1:544553542D5345434F4E442D53455249414C' "$service"

CEC_MATE_ROOT=$root "$packaging/uninstall.sh"
test ! -e "$service"
test ! -e "$device_rule"
test ! -e "$root/opt/cec-injector-mate"
test -e "$root/etc/cec-injector-mate/installation.conf"
test -e "$root/var/lib/cec-injector-mate"

current_serial=53032847289E399
current_serial=${current_serial}C
! grep -R --exclude-dir=.git --exclude-dir=build -F "$current_serial" "$source_root"
legacy_path=/opt/linux-hdmi
legacy_path=${legacy_path}-cec
! grep -R -F "$legacy_path" "$packaging"
