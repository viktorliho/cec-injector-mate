#!/bin/sh
set -eu
source_root=$1
packaging=$source_root/packaging/linux
fixture=$(mktemp -d)
trap 'rm -rf -- "$fixture"' EXIT
bundle=$fixture/bundle
before=$fixture/before
after=$fixture/after
serial=TEST-STEAMOS-SERIAL
release=0123456789abcdef0123456789abcdef01234567
mkdir -p "$bundle/bin" "$bundle/lib" "$bundle/share/icons/hicolor" "$before"
cp -a "$source_root/applications/gui/resources/icon/hicolor/." \
  "$bundle/share/icons/hicolor/"
printf '#!/bin/sh\necho UNEXPECTED_CEC_ACTION >> "%s"\nexit 1\n' \
  "$fixture/actions" > "$bundle/bin/cec-injector-mate"
printf '#!/bin/sh\nexit 0\n' > "$bundle/bin/cec-injector-mate-gui"
chmod 0755 "$bundle/bin/cec-injector-mate" "$bundle/bin/cec-injector-mate-gui"
printf 'Mate commit: %s\n' "$release" > "$bundle/MANIFEST.txt"
cat > "$fixture/base.conf" <<'EOF'
/etc/atomic-update.conf.d/*.conf
/etc/systemd/system/*.service
/etc/systemd/system/*.wants/**
EOF
cat > "$fixture/holo-sync-var" <<'EOF'
ATOMIC_UPDATE_CONF_D=/etc/atomic-update.conf.d
rsync --include="*/" --include-from="${config}" --exclude="*"
EOF
export CEC_MATE_STEAMOS_TEST=1
export CEC_MATE_STEAMOS_KEEP_CONF=$fixture/base.conf
export CEC_MATE_STEAMOS_SYNC_SCRIPT=$fixture/holo-sync-var
export CEC_MATE_DESKTOP_HOME=$before/home/deck
export CEC_MATE_ROOT=$before

"$packaging/install.sh" --desktop-user deck "$bundle" cafe cec1 "$serial"
authority=$before/etc/cec-injector-mate/runtime/config.conf
backup=$before/var/lib/cec-injector-mate/runtime-config.backup
identity_backup=$before/var/lib/cec-injector-mate/installation/installation.conf
registration=$before/etc/atomic-update.conf.d/cec-injector-mate.conf
cmp "$authority" "$backup"
cmp "$before/etc/cec-injector-mate/installation.conf" "$identity_backup"
test "$(stat -c %a "$identity_backup")" = 600
test "$(stat -c %a "$(dirname "$identity_backup")")" = 700
test "$(stat -c %a "$authority")" = 600
test "$(stat -c %a "$backup")" = 600
test "$(stat -c %a "$(dirname "$backup")")" = 700
test "$(sed '/^#/d; /^$/d' "$registration" | wc -l)" -eq 7
! grep -Fq '/etc/**' "$registration"
! grep -Fq '/etc/systemd/system' "$registration"

# Persist a non-default, revised policy. Same-release installation must keep it.
sed -i 's/^power_on_boot=true$/power_on_boot=false/; /version=1/a revision=17' "$authority"
"$packaging/install.sh" --desktop-user deck "$bundle" cafe cec1 "$serial"
cmp "$authority" "$backup"
grep -Fxq 'revision=17' "$backup"
grep -Fxq 'power_on_boot=false' "$backup"

# Enabled unit links are part of SteamOS's built-in preservation policy.
mkdir -p "$before/etc/systemd/system/multi-user.target.wants"
for unit in cec-injector-mate.service cec-injector-mate-boot.service; do
  ln -s "../$unit" \
    "$before/etc/systemd/system/multi-user.target.wants/$unit"
done

# Model the installed SteamOS rsync filter twice. /opt and /var are retained.
simulate_update() {
  old=$1
  new=$2
  mkdir -p "$new/etc"
  cp -a "$old/opt" "$new/opt"
  cp -a "$old/var" "$new/var"
  sed 's#^/etc##' "$fixture/base.conf" > "$fixture/filter"
  sed 's#^/etc##' "$old/etc/atomic-update.conf.d/cec-injector-mate.conf" >> "$fixture/filter"
  rsync -rlpgoD --include='*/' --include-from="$fixture/filter" --exclude='*' \
    "$old/etc/" "$new/etc/"
}
simulate_update "$before" "$after"
for relative in \
    cec-injector-mate/installation.conf \
    cec-injector-mate/runtime/config.conf \
    udev/rules.d/71-cec-injector-mate-wakeup.rules \
    udev/rules.d/80-cec-injector-mate-device.rules \
    udev/rules.d/80-cec-injector-mate-platform.rules \
    dbus-1/system.d/org.cecinjector.Mate.conf \
    polkit-1/rules.d/49-cec-injector-mate-suspend.rules \
    systemd/system/cec-injector-mate.service \
    systemd/system/cec-injector-mate-boot.service; do
  cmp "$before/etc/$relative" "$after/etc/$relative"
done
test "$(stat -c %a "$after/etc/cec-injector-mate/runtime/config.conf")" = 600
test "$(stat -c %u:%g "$before/etc/cec-injector-mate/runtime/config.conf")" = \
  "$(stat -c %u:%g "$after/etc/cec-injector-mate/runtime/config.conf")"
cmp "$after/etc/cec-injector-mate/runtime/config.conf" \
  "$after/var/lib/cec-injector-mate/runtime-config.backup"
test -f "$after/etc/atomic-update.conf.d/cec-injector-mate.conf"
for unit in cec-injector-mate.service cec-injector-mate-boot.service; do
  test "$(readlink "$after/etc/systemd/system/multi-user.target.wants/$unit")" = \
    "../$unit"
done

third=$fixture/third
simulate_update "$after" "$third"
cmp "$after/etc/cec-injector-mate/runtime/config.conf" \
  "$third/etc/cec-injector-mate/runtime/config.conf"

# A broken update loses all Mate integration. The retained release repairs it
# without starting either binary or applying the boot CEC policy.
rm -f "$after/etc/cec-injector-mate/runtime/config.conf" \
  "$after/etc/cec-injector-mate/installation.conf" \
  "$after/etc/udev/rules.d/"*cec-injector-mate* \
  "$after/etc/dbus-1/system.d/org.cecinjector.Mate.conf" \
  "$after/etc/polkit-1/rules.d/49-cec-injector-mate-suspend.rules"
export CEC_MATE_ROOT=$after
export CEC_MATE_DESKTOP_HOME=$after/home/deck
installed=$after/opt/cec-injector-mate/current
"$installed/installation/linux/repair-installed.sh"
cmp "$after/etc/cec-injector-mate/runtime/config.conf" \
  "$after/var/lib/cec-injector-mate/runtime-config.backup"
grep -Fxq 'revision=17' "$after/etc/cec-injector-mate/runtime/config.conf"
test -f "$after/etc/udev/rules.d/80-cec-injector-mate-device.rules"
cmp "$after/etc/cec-injector-mate/installation.conf" \
  "$after/var/lib/cec-injector-mate/installation/installation.conf"
test ! -e "$fixture/actions"
"$installed/installation/linux/repair-installed.sh"
test ! -e "$fixture/actions"

# An interrupted write can leave /etc newer than the retained backup. Repair
# must copy the authoritative revision forward without losing typed settings.
sed -i 's/revision=17/revision=19/' \
  "$after/etc/cec-injector-mate/runtime/config.conf"
"$installed/installation/linux/repair-installed.sh"
cmp "$after/etc/cec-injector-mate/runtime/config.conf" \
  "$after/var/lib/cec-injector-mate/runtime-config.backup"
grep -Fxq 'revision=19' "$after/var/lib/cec-injector-mate/runtime-config.backup"

# Corrupt, conflicting, or newer backups fail before derived files change.
cp "$after/var/lib/cec-injector-mate/runtime-config.backup" "$fixture/good-backup"
printf 'version=9\n' > "$after/var/lib/cec-injector-mate/runtime-config.backup"
if "$installed/installation/linux/install.sh" --repair-only "$installed" cafe cec1 "$serial"; then
  echo 'corrupt backup was accepted' >&2; exit 1
fi
cp "$fixture/good-backup" "$after/var/lib/cec-injector-mate/runtime-config.backup"
sed -i 's/revision=19/revision=20/' "$after/var/lib/cec-injector-mate/runtime-config.backup"
if "$installed/installation/linux/install.sh" --repair-only "$installed" cafe cec1 "$serial"; then
  echo 'newer backup conflict was accepted' >&2; exit 1
fi
cp "$fixture/good-backup" "$after/var/lib/cec-injector-mate/runtime-config.backup"
rm -f "$after/var/lib/cec-injector-mate/runtime-config.backup"
ln -s "$fixture/good-backup" \
  "$after/var/lib/cec-injector-mate/runtime-config.backup"
if "$installed/installation/linux/install.sh" --repair-only "$installed" cafe cec1 "$serial"; then
  echo 'symlink backup was accepted' >&2; exit 1
fi
rm -f "$after/var/lib/cec-injector-mate/runtime-config.backup"
cp "$fixture/good-backup" "$after/var/lib/cec-injector-mate/runtime-config.backup"

printf '/etc/unrelated.conf\n' > "$after/etc/atomic-update.conf.d/unrelated.conf"
"$installed/installation/linux/uninstall.sh" --desktop-user deck
test -f "$after/etc/cec-injector-mate/runtime/config.conf"
test -f "$after/var/lib/cec-injector-mate/runtime-config.backup"
test -f "$after/etc/atomic-update.conf.d/unrelated.conf"
test "$(sed '/^#/d; /^$/d' "$after/etc/atomic-update.conf.d/cec-injector-mate.conf" | wc -l)" -eq 2
"$packaging/install.sh" --desktop-user deck "$bundle" cafe cec1 "$serial"
cmp "$after/etc/cec-injector-mate/runtime/config.conf" \
  "$after/var/lib/cec-injector-mate/runtime-config.backup"
grep -Fxq 'revision=19' "$after/etc/cec-injector-mate/runtime/config.conf"
test "$(sed '/^#/d; /^$/d' "$after/etc/atomic-update.conf.d/cec-injector-mate.conf" | wc -l)" -eq 7
"$packaging/uninstall.sh" --purge-state
test ! -e "$after/etc/cec-injector-mate"
test ! -e "$after/var/lib/cec-injector-mate"
test ! -e "$after/etc/atomic-update.conf.d/cec-injector-mate.conf"
test -f "$after/etc/atomic-update.conf.d/unrelated.conf"
"$packaging/uninstall.sh" --purge-state
echo 'SteamOS update survivability fixture passed'
