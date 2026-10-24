#!/bin/sh
set -eu

action=${1:-}
root=${CEC_MATE_ROOT:-}
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../steamos" && pwd)
registration="${root}/etc/atomic-update.conf.d/cec-injector-mate.conf"
base=${CEC_MATE_STEAMOS_KEEP_CONF:-/usr/lib/rauc/atomic-update-keep.conf}
sync_script=${CEC_MATE_STEAMOS_SYNC_SCRIPT:-/usr/lib/holo/holo-sync-var}

is_steamos() {
  test "${CEC_MATE_STEAMOS_TEST:-0}" = 1 && return 0
  test -z "$root" && test -r /etc/os-release &&
    grep -Eq '^ID="?steamos"?$' /etc/os-release
}

validate_paths() {
  file=$1
  mode=$2
  test -s "$file"
  # Keep lists are rsync include patterns. Only exact project paths are allowed.
  while IFS= read -r line || test -n "$line"; do
    case "$line" in
      ''|'#'*) continue;;
      /etc/cec-injector-mate/installation.conf|\
      /etc/cec-injector-mate/runtime/config.conf) :;;
      /etc/udev/rules.d/71-cec-injector-mate-wakeup.rules|\
      /etc/udev/rules.d/80-cec-injector-mate-device.rules|\
      /etc/udev/rules.d/80-cec-injector-mate-platform.rules|\
      /etc/dbus-1/system.d/org.cecinjector.Mate.conf|\
      /etc/polkit-1/rules.d/49-cec-injector-mate-suspend.rules)
        test "$mode" = install || {
          echo "non-configuration path in retained uninstall list: $line" >&2
          exit 1
        };;
      *) echo "invalid SteamOS keep path: $line" >&2; exit 1;;
    esac
  done < "$file"
  entries=$(sed '/^#/d; /^$/d' "$file")
  count=$(printf '%s\n' "$entries" | wc -l)
  unique=$(printf '%s\n' "$entries" | sort -u | wc -l)
  test "$count" -eq "$unique" || { echo 'duplicate SteamOS keep entry' >&2; exit 1; }
  if test "$mode" = install; then expected=7; else expected=2; fi
  test "$count" -eq "$expected" || {
    echo 'SteamOS keep list omits required Mate paths' >&2; exit 1;
  }
}

validate_native() {
  test -r "$base" && grep -Fxq '/etc/atomic-update.conf.d/*.conf' "$base" || {
    echo 'SteamOS does not preserve user keep-list fragments' >&2; exit 1;
  }
  test -r "$sync_script" && grep -Fq 'ATOMIC_UPDATE_CONF_D=/etc/atomic-update.conf.d' "$sync_script" &&
    grep -Fq -- '--include-from="${config}"' "$sync_script" &&
    grep -Fq -- '--include="*/"' "$sync_script" || {
    echo 'unsupported SteamOS preservation implementation' >&2; exit 1;
  }
}

case "$action" in
  install|config-only)
    is_steamos || exit 0
    validate_native
    if test "$action" = install; then source_file=$source_dir/cec-injector-mate.conf
    else source_file=$source_dir/cec-injector-mate-config-only.conf; fi
    validate_paths "$source_file" "$action"
    install -d -m 0755 "$(dirname "$registration")"
    install -m 0644 "$source_file" "$registration"
    validate_paths "$registration" "$action"
    if test -z "$root"; then
      # The installed implementation performs a read-only rsync simulation.
      report=$("$sync_script" --dry-run all 2>&1) || {
        echo "$report" >&2; exit 1;
      }
      if printf '%s\n' "$report" | grep -Eq '^/etc/(cec-injector-mate/|udev/rules.d/(71|80)-cec-injector-mate|dbus-1/system.d/org.cecinjector.Mate|polkit-1/rules.d/49-cec-injector-mate)'; then
        echo 'SteamOS would discard a Mate path' >&2; exit 1;
      fi
    fi;;
  purge)
    is_steamos || exit 0
    rm -f -- "$registration";;
  verify)
    is_steamos || exit 0
    validate_native
    validate_paths "$registration" install;;
  *) echo 'usage: steamos-preservation.sh install|config-only|purge|verify' >&2; exit 2;;
esac
