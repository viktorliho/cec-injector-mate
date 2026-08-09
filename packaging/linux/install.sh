#!/bin/sh
set -eu

service_user=cec-injector-mate
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=${CEC_MATE_ROOT:-}

usage() {
  echo "usage: install.sh BUNDLE VID PID SERIAL" >&2
  exit 2
}

test "$#" -eq 4 || usage
bundle=$1
vendor=$2
product=$3
serial=$4
test -d "$bundle/bin" && test -x "$bundle/bin/cec-injector-mate" || {
  echo "invalid Mate bundle" >&2; exit 1;
}
test -f "$bundle/MANIFEST.txt" || { echo "bundle manifest is required" >&2; exit 1; }
test -z "$root" || test "${root#/}" != "$root" || { echo "CEC_MATE_ROOT must be absolute" >&2; exit 2; }
if test -z "$root"; then
  test "$(id -u)" -eq 0 || { echo "installation requires root" >&2; exit 1; }
  for prerequisite in od udevadm systemctl useradd runuser journalctl systemd-inhibit sha256sum; do
    command -v "$prerequisite" >/dev/null || { echo "missing prerequisite: $prerequisite" >&2; exit 1; }
  done
  if systemctl is-active --quiet linux-hdmi-cec.service 2>/dev/null; then
    echo "legacy linux-hdmi-cec service is active; clean cutover is required first" >&2
    exit 1
  fi
fi

# Validate deployment inputs through the public generators before mutation.
serial_rule=$($source_dir/generate-device-access-rule.sh "$vendor" "$product" "$serial" "$service_user")
wake_rule=$($source_dir/generate-remote-wake-rule.sh "$vendor" "$product" "$serial")
polkit_rule=$($source_dir/generate-polkit-rule.sh "$service_user")
vendor=$(printf '%s' "$vendor" | tr 'A-F' 'a-f')
product=$(printf '%s' "$product" | tr 'A-F' 'a-f')
encoded_serial=$(printf '%s' "$serial" | od -An -tx1 | tr -d ' \n' | tr 'a-f' 'A-F')
device_id=linux-usb-serial-v1:$encoded_serial

mate_commit=$(sed -n 's/^Mate commit: //p' "$bundle/MANIFEST.txt" | head -n 1)
test "${#mate_commit}" -eq 40 || { echo "manifest has no valid Mate commit" >&2; exit 1; }
case "$mate_commit" in *[!0-9a-f]*) echo "manifest has no valid Mate commit" >&2; exit 1;; esac
release=/opt/cec-injector-mate/releases/$mate_commit

if test -z "$root"; then
  expected_executable=$(sed -n 's/^Executable SHA-256: //p' "$bundle/MANIFEST.txt")
  expected_cec=$(sed -n 's/^libcec.so.7 SHA-256: //p' "$bundle/MANIFEST.txt")
  expected_p8=$(sed -n 's/^libp8-platform.so.2 SHA-256: //p' "$bundle/MANIFEST.txt")
  test "$(sha256sum "$bundle/bin/cec-injector-mate" | cut -d' ' -f1)" = "$expected_executable"
  test "$(sha256sum "$bundle/lib/libcec.so.7" | cut -d' ' -f1)" = "$expected_cec"
  test "$(sha256sum "$bundle/lib/libp8-platform.so.2" | cut -d' ' -f1)" = "$expected_p8"
  matches=0
  for device in /sys/bus/usb/devices/*; do
    test -f "$device/idVendor" && test -f "$device/idProduct" && test -f "$device/serial" || continue
    test "$(cat "$device/idVendor")" = "$vendor" || continue
    test "$(cat "$device/idProduct")" = "$product" || continue
    test "$(cat "$device/serial")" = "$serial" || continue
    matches=$((matches + 1))
  done
  test "$matches" -eq 1 || { echo "selected USB identity is absent or ambiguous" >&2; exit 1; }
  stable=$(find /dev/serial/by-id -maxdepth 1 -type l -name "*${serial}*" -print 2>/dev/null | head -n 2)
  test "$(printf '%s\n' "$stable" | sed '/^$/d' | wc -l)" -eq 1 || {
    echo "selected Device has no unique stable by-id locator" >&2; exit 1;
  }
fi

if test -z "$root" && ! id "$service_user" >/dev/null 2>&1; then
  useradd --system --home-dir /var/lib/cec-injector-mate --create-home \
    --shell /usr/bin/nologin --user-group "$service_user"
fi
if test -z "$root"; then
  test "$(id -gn "$service_user")" = "$service_user" || {
    echo "existing service account has an unexpected primary group" >&2; exit 1;
  }
  getent group video >/dev/null || { echo "missing required DRM primary-node group: video" >&2; exit 1; }
fi

stage="${root}${release}.new"
rm -rf -- "$stage"
install -d -m 0755 "$stage"
cp -a -- "$bundle/." "$stage/"
install -d -m 0755 "$stage/libexec"
install -m 0755 "$source_dir/health-check.sh" "$stage/libexec/health-check"
find "$stage" -type d -exec chmod 0755 {} +
find "$stage" -type f -exec chmod go-w {} +
chmod 0755 "$stage/bin/cec-injector-mate"
if test -z "$root"; then chown -R root:root "$stage"; fi
rm -rf -- "${root}${release}"
mv -- "$stage" "${root}${release}"
install -d -m 0755 "${root}/opt/cec-injector-mate"
ln -sfn "releases/$mate_commit" "${root}/opt/cec-injector-mate/current"

install -d -m 0755 "${root}/etc/cec-injector-mate"
config_tmp=$(mktemp)
trap 'rm -f -- "$config_tmp"' EXIT
cat >"$config_tmp" <<EOF
CEC_MATE_VENDOR=$vendor
CEC_MATE_PRODUCT=$product
CEC_MATE_SERIAL=$serial
CEC_MATE_DEVICE_ID=$device_id
CEC_MATE_RELEASE=$mate_commit
EOF
install -m 0644 "$config_tmp" "${root}/etc/cec-injector-mate/installation.conf"

install -d -m 0755 "${root}/etc/udev/rules.d" "${root}/etc/polkit-1/rules.d" \
  "${root}/etc/systemd/system"
rm -f -- "${root}/etc/udev/rules.d/70-cec-injector-mate-device.rules" \
  "${root}/etc/udev/rules.d/70-cec-injector-mate-platform.rules"
printf '%s\n' "$serial_rule" >"${root}/etc/udev/rules.d/80-cec-injector-mate-device.rules"
cat >"${root}/etc/udev/rules.d/80-cec-injector-mate-platform.rules" <<EOF
ACTION=="add|change", SUBSYSTEM=="misc", KERNEL=="uinput", GROUP="$service_user", MODE="0660", TAG-="uaccess"
EOF
printf '%s\n' "$wake_rule" >"${root}/etc/udev/rules.d/71-cec-injector-mate-wakeup.rules"
printf '%s\n' "$polkit_rule" >"${root}/etc/polkit-1/rules.d/49-cec-injector-mate-suspend.rules"
chmod 0644 "${root}/etc/udev/rules.d/80-cec-injector-mate-device.rules" \
  "${root}/etc/udev/rules.d/80-cec-injector-mate-platform.rules" \
  "${root}/etc/udev/rules.d/71-cec-injector-mate-wakeup.rules" \
  "${root}/etc/polkit-1/rules.d/49-cec-injector-mate-suspend.rules"

executable=/opt/cec-injector-mate/current/bin/cec-injector-mate
sed -e "s|@EXECUTABLE@|$executable|g" -e "s|@DEVICE_ID@|$device_id|g" "$source_dir/cec-injector-mate.service.in" \
  >"${root}/etc/systemd/system/cec-injector-mate.service"
sed -e "s|@EXECUTABLE@|$executable|g" -e "s|@DEVICE_ID@|$device_id|g" "$source_dir/cec-injector-mate-boot.service.in" \
  >"${root}/etc/systemd/system/cec-injector-mate-boot.service"
chmod 0644 "${root}/etc/systemd/system/cec-injector-mate.service" \
  "${root}/etc/systemd/system/cec-injector-mate-boot.service"
if test -n "$root"; then
  install -d -m 0700 "${root}/var/lib/cec-injector-mate"
  echo "staged installation under $root"
  exit 0
fi

install -d -o "$service_user" -g "$service_user" -m 0700 /var/lib/cec-injector-mate
systemctl daemon-reload
udevadm control --reload-rules
udevadm trigger --subsystem-match=tty --action=change
udevadm trigger --subsystem-match=misc --sysname-match=uinput --action=change
udevadm trigger --subsystem-match=usb --attr-match=idVendor="$vendor" \
  --attr-match=idProduct="$product" --attr-match=serial="$serial" --action=change
udevadm settle

# Fail before enabling or starting Mate unless the installed identity can open
# every production device endpoint through the just-applied permission model.
runuser -u "$service_user" -- sh -c 'exec 3<>"$1"' sh "$stable"
runuser -u "$service_user" -- sh -c 'exec 3>"$1"' sh /dev/uinput
drm_ready=false
for card in /dev/dri/card*; do
  test -c "$card" || continue
  if runuser -u "$service_user" -G video -- sh -c 'exec 3<"$1"' sh "$card"; then
    drm_ready=true
    break
  fi
done
$drm_ready || { echo "service identity cannot open a DRM primary node" >&2; exit 1; }

systemctl enable cec-injector-mate.service cec-injector-mate-boot.service
# Installation is passive. The boot oneshot is enabled for the next real boot,
# but is deliberately not started by the installer.
systemctl restart cec-injector-mate.service
attempt=0
until /opt/cec-injector-mate/current/libexec/health-check >/dev/null 2>&1; do
  attempt=$((attempt + 1))
  test "$attempt" -lt 30 || break
  sleep 1
done
/opt/cec-injector-mate/current/libexec/health-check
