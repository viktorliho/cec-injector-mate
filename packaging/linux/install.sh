#!/bin/sh
set -eu

service_user=cec-injector-mate
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=${CEC_MATE_ROOT:-}
desktop_user=

usage() {
  echo "usage: install.sh [--desktop-user USER] BUNDLE VID PID SERIAL" >&2
  exit 2
}

if test "${1:-}" = --desktop-user; then
  test "$#" -ge 2 || usage
  desktop_user=$2
  shift 2
fi
test "$#" -eq 4 || usage
bundle=$1
vendor=$2
product=$3
serial=$4
test -d "$bundle/bin" && test -x "$bundle/bin/cec-injector-mate" || {
  echo "invalid Mate bundle" >&2; exit 1;
}
test -x "$bundle/bin/cec-injector-mate-gui" || {
  echo "invalid Mate bundle: GUI executable missing" >&2; exit 1;
}
test -f "$bundle/share/icons/hicolor/256x256/apps/cec-injector-mate.png" || {
  echo "invalid Mate bundle: application icon missing" >&2; exit 1;
}
test -f "$bundle/MANIFEST.txt" || { echo "bundle manifest is required" >&2; exit 1; }
test -z "$root" || test "${root#/}" != "$root" || { echo "CEC_MATE_ROOT must be absolute" >&2; exit 2; }
if test -z "$root"; then
  test "$(id -u)" -eq 0 || { echo "installation requires root" >&2; exit 1; }
  for prerequisite in busctl ldd od readelf udevadm systemctl useradd runuser \
      journalctl systemd-inhibit sha256sum tar; do
    command -v "$prerequisite" >/dev/null || { echo "missing prerequisite: $prerequisite" >&2; exit 1; }
  done
  if systemctl is-active --quiet linux-hdmi-cec.service 2>/dev/null; then
    echo "legacy linux-hdmi-cec service is active; clean cutover is required first" >&2
    exit 1
  fi
  # SteamOS supplies the Qt 6 runtime. Fail before changing the installed
  # release if the GUI or either supported desktop platform is unavailable.
  ldd "$bundle/bin/cec-injector-mate-gui" | grep -Fq 'not found' && {
    echo "GUI runtime dependency is missing" >&2; exit 1;
  }
  qt_core=$(ldd "$bundle/bin/cec-injector-mate-gui" |
    sed -n 's|.*libQt6Core\.so\.6 => \([^ ]*\).*|\1|p')
  test -n "$qt_core" && readelf --version-info "$qt_core" |
    grep -Fq 'Name: Qt_6.4' || {
      echo "Qt 6.4-compatible runtime is unavailable" >&2; exit 1;
    }
  qt_plugins=${CEC_MATE_QT_PLUGIN_ROOT:-/usr/lib/qt6/plugins}
  test -x "$qt_plugins/platforms/libqxcb.so" || {
    echo "Qt XCB platform plugin is unavailable" >&2; exit 1;
  }
  test -x "$qt_plugins/platforms/libqwayland-generic.so" ||
    test -x "$qt_plugins/platforms/libqwayland-egl.so" || {
      echo "Qt Wayland platform plugin is unavailable" >&2; exit 1;
    }
  grep -Fq '<includedir>/etc/dbus-1/system.d</includedir>' \
    /usr/share/dbus-1/system.conf || {
      echo "/etc D-Bus system policy directory is unsupported" >&2; exit 1;
    }
fi

desktop_user=${CEC_MATE_DESKTOP_USER:-$desktop_user}
if test -z "$desktop_user" && test -z "$root" &&
   test -n "${SUDO_USER:-}" && test "$SUDO_USER" != root; then
  desktop_user=$SUDO_USER
fi
if test -n "$desktop_user"; then
  case "$desktop_user" in
    root|*[!abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-]*)
      echo "invalid desktop user" >&2; exit 2;;
  esac
  if test -z "$root"; then
    desktop_home=$(getent passwd "$desktop_user" | cut -d: -f6)
    test -n "$desktop_home" && test "${desktop_home#/}" != "$desktop_home" || {
      echo "desktop user has no absolute home" >&2; exit 1;
    }
    id -Gn "$desktop_user" | tr ' ' '\n' | grep -Fxq wheel || {
      echo "desktop user is not authorized for Mate mutations" >&2; exit 1;
    }
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
  expected_gui=$(sed -n 's/^GUI executable SHA-256: //p' "$bundle/MANIFEST.txt")
  expected_cec=$(sed -n 's/^libcec.so.7 SHA-256: //p' "$bundle/MANIFEST.txt")
  expected_p8=$(sed -n 's/^libp8-platform.so.2 SHA-256: //p' "$bundle/MANIFEST.txt")
  test "$(sha256sum "$bundle/bin/cec-injector-mate" | cut -d' ' -f1)" = "$expected_executable"
  test "$(sha256sum "$bundle/bin/cec-injector-mate-gui" | cut -d' ' -f1)" = "$expected_gui"
  test "$(sha256sum "$bundle/lib/libcec.so.7" | cut -d' ' -f1)" = "$expected_cec"
  test "$(sha256sum "$bundle/lib/libp8-platform.so.2" | cut -d' ' -f1)" = "$expected_p8"
  matches=0
  selected_usb=
  for device in /sys/bus/usb/devices/*; do
    test -f "$device/idVendor" && test -f "$device/idProduct" && test -f "$device/serial" || continue
    test "$(cat "$device/idVendor")" = "$vendor" || continue
    test "$(cat "$device/idProduct")" = "$product" || continue
    test "$(cat "$device/serial")" = "$serial" || continue
    matches=$((matches + 1))
    selected_usb=$device
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

# Resolve migration before changing the release symlink or installed units.
runtime_seed=$(mktemp)
config_tmp=$(mktemp)
trap 'rm -f -- "$runtime_seed" "$config_tmp"' EXIT
runtime_existing="${root}/etc/cec-injector-mate/runtime/config.conf"
if test ! -f "$runtime_existing"; then
  old_unit="${root}/etc/systemd/system/cec-injector-mate.service"
  if test -f "$old_unit"; then
    old_exec=$(sed -n 's/^ExecStart=//p' "$old_unit")
    test -n "$old_exec" || { echo "cannot migrate runtime policy: old ExecStart absent" >&2; exit 1; }
    flag_value() { case " $old_exec " in *" $1 "*) printf true;; *) printf false;; esac; }
    old_profile=$(printf '%s\n' "$old_exec" | sed -n 's/.*--remote-profile \([^ ]*\).*/\1/p')
    test -n "$old_profile" || old_profile=steamos-parity
    test "$old_profile" = steamos-parity || {
      echo "cannot migrate unknown remote profile: $old_profile" >&2; exit 1;
    }
    cat >"$runtime_seed" <<EOF
version=1
power_on_boot=$(flag_value --power-on-boot)
power_on_resume=$(flag_value --power-on-resume)
standby_on_suspend=$(flag_value --standby-on-suspend)
standby_on_shutdown=$(flag_value --standby-on-shutdown)
suspend_on_tv_standby=$(flag_value --suspend-on-tv-standby)
wake_host_on_tv_source_request=$(flag_value --wake-host-on-tv-source-request)
remote_input_enabled=true
remote_profile=$old_profile
EOF
  else
    cp "$source_dir/config.conf" "$runtime_seed"
  fi
fi

stage="${root}${release}.new"
rm -rf -- "$stage"
install -d -m 0755 "$stage"
cp -a -- "$bundle/." "$stage/"
install -d -m 0755 "$stage/libexec"
install -m 0755 "$source_dir/health-check.sh" "$stage/libexec/health-check"
install -m 0755 "$source_dir/diagnostics.sh" "$stage/libexec/diagnostics"
install -m 0755 "$source_dir/readiness.sh" "$stage/libexec/readiness"
install -m 0755 "$source_dir/frontend-health.sh" "$stage/libexec/frontend-health"
find "$stage" -type d -exec chmod 0755 {} +
find "$stage" -type f -exec chmod go-w {} +
chmod 0755 "$stage/bin/cec-injector-mate"
chmod 0755 "$stage/bin/cec-injector-mate-gui"
if test -z "$root"; then chown -R root:root "$stage"; fi
rm -rf -- "${root}${release}"
mv -- "$stage" "${root}${release}"
install -d -m 0755 "${root}/opt/cec-injector-mate"
ln -sfn "releases/$mate_commit" "${root}/opt/cec-injector-mate/current"

install -d -m 0755 "${root}/etc/cec-injector-mate"
cat >"$config_tmp" <<EOF
CEC_MATE_VENDOR=$vendor
CEC_MATE_PRODUCT=$product
CEC_MATE_SERIAL=$serial
CEC_MATE_DEVICE_ID=$device_id
CEC_MATE_RELEASE=$mate_commit
CEC_MATE_DESKTOP_USER=$desktop_user
EOF
install -m 0644 "$config_tmp" "${root}/etc/cec-injector-mate/installation.conf"

# Runtime policy is a separate authority. Preserve an existing versioned file;
# otherwise migrate the exact legacy ExecStart flags before replacing the unit.
runtime_dir="${root}/etc/cec-injector-mate/runtime"
runtime_config="$runtime_dir/config.conf"
install -d -m 0700 "$runtime_dir"
if test ! -f "$runtime_config"; then
  install -m 0600 "$runtime_seed" "$runtime_config"
fi
chmod 0700 "$runtime_dir"
chmod 0600 "$runtime_config"
if test -z "$root"; then
  chown "$service_user:$service_user" "$runtime_dir" "$runtime_config"
fi

install -d -m 0755 "${root}/etc/udev/rules.d" "${root}/etc/polkit-1/rules.d" \
  "${root}/etc/systemd/system" "${root}/etc/dbus-1/system.d"
rm -f -- "${root}/etc/udev/rules.d/70-cec-injector-mate-device.rules" \
  "${root}/etc/udev/rules.d/70-cec-injector-mate-platform.rules"
printf '%s\n' "$serial_rule" >"${root}/etc/udev/rules.d/80-cec-injector-mate-device.rules"
cat >"${root}/etc/udev/rules.d/80-cec-injector-mate-platform.rules" <<EOF
ACTION=="add|change", SUBSYSTEM=="misc", KERNEL=="uinput", OWNER="$service_user", GROUP="$service_user", MODE="0600", TAG-="uaccess"
EOF
printf '%s\n' "$wake_rule" >"${root}/etc/udev/rules.d/71-cec-injector-mate-wakeup.rules"
printf '%s\n' "$polkit_rule" >"${root}/etc/polkit-1/rules.d/49-cec-injector-mate-suspend.rules"
chmod 0644 "${root}/etc/udev/rules.d/80-cec-injector-mate-device.rules" \
  "${root}/etc/udev/rules.d/80-cec-injector-mate-platform.rules" \
  "${root}/etc/udev/rules.d/71-cec-injector-mate-wakeup.rules" \
  "${root}/etc/polkit-1/rules.d/49-cec-injector-mate-suspend.rules"
install -m 0644 "$source_dir/org.cecinjector.Mate.conf" \
  "${root}/etc/dbus-1/system.d/org.cecinjector.Mate.conf"

desktop_home=${CEC_MATE_DESKTOP_HOME:-${desktop_home:-}}
if test -n "$desktop_home"; then
  install -d -m 0755 "$desktop_home/.local/share/applications"
  install -m 0644 "$source_dir/cec-injector-mate.desktop" \
    "$desktop_home/.local/share/applications/cec-injector-mate.desktop"
  if test -z "$root"; then
    chown "$desktop_user:$desktop_user" \
      "$desktop_home/.local/share/applications/cec-injector-mate.desktop"
  fi
  for icon in "$bundle"/share/icons/hicolor/*/apps/cec-injector-mate.png; do
    size=$(basename "$(dirname "$(dirname "$icon")")")
    icon_dir="$desktop_home/.local/share/icons/hicolor/$size/apps"
    install -d -m 0755 "$icon_dir"
    install -m 0644 "$icon" "$icon_dir/cec-injector-mate.png"
    if test -z "$root"; then
      chown -R "$desktop_user:$desktop_user" \
        "$desktop_home/.local/share/icons/hicolor/$size"
    fi
  done
fi

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
busctl call org.freedesktop.DBus /org/freedesktop/DBus \
  org.freedesktop.DBus ReloadConfig
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
test "$(cat "$selected_usb/power/wakeup")" = enabled || {
  echo "selected Device Remote Wake is not authorized" >&2; exit 1;
}
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
