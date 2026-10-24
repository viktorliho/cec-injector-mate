#!/bin/sh
set -eu

# This validator follows the version-1 typed fields accepted by Mate. Unknown
# future fields are retained byte-for-byte, not interpreted by the installer.
validate() {
  test -f "$1" && test ! -L "$1" || {
    echo "runtime configuration is missing or is not a regular file: $1" >&2
    return 1
  }
  LC_ALL=C awk -F= '
    BEGIN {
      split("version revision power_on_boot power_on_resume standby_on_suspend standby_on_shutdown suspend_on_tv_standby wake_host_on_tv_source_request remote_input_enabled remote_profile", names, " ")
      for (i in names) required[names[i]] = names[i] != "revision"
    }
    /^[[:space:]]*$/ || /^#/ { next }
    index($0, "=") < 2 { bad=1; next }
    {
      key=substr($0, 1, index($0, "=")-1)
      value=substr($0, index($0, "=")+1)
      if (seen[key]++) bad=1
      field[key]=value
    }
    END {
      for (key in required) if (required[key] && !seen[key]) bad=1
      if (field["version"] != "1" || field["remote_profile"] != "steamos-parity") bad=1
      split("power_on_boot power_on_resume standby_on_suspend standby_on_shutdown suspend_on_tv_standby wake_host_on_tv_source_request remote_input_enabled", booleans, " ")
      for (i in booleans) {
        value=field[booleans[i]]
        if (value != "true" && value != "false") bad=1
      }
      revision=seen["revision"] ? field["revision"] : "1"
      if (revision !~ /^[1-9][0-9]*$/ || length(revision) > 18) bad=1
      if (bad) exit 1
      print revision
    }
  ' "$1"
}

atomic_copy() {
  source_file=$1
  destination=$2
  owner=$3
  directory=$(dirname "$destination")
  install -d -m 0700 "$directory"
  temporary=$(mktemp "$directory/.runtime-config.XXXXXX")
  trap 'rm -f -- "$temporary"' EXIT HUP INT TERM
  cat "$source_file" > "$temporary"
  chmod 0600 "$temporary"
  if test -n "$owner"; then chown "$owner:$owner" "$temporary" "$directory"; fi
  sync -f "$temporary"
  mv -f -- "$temporary" "$destination"
  sync -f "$directory"
  trap - EXIT HUP INT TERM
}

case ${1:-} in
  validate)
    test "$#" -eq 2
    validate "$2";;
  copy)
    test "$#" -eq 4
    atomic_copy "$2" "$3" "$4";;
  check)
    test "$#" -eq 3
    authority=$2
    backup=$3
    test ! -L "$authority" && test ! -L "$backup" || {
      echo 'runtime configuration symlink is unsupported' >&2; exit 1;
    }
    if test -e "$authority"; then
      current_revision=$(validate "$authority") || {
        echo "invalid authoritative runtime configuration" >&2; exit 1;
      }
      if test -e "$backup"; then
        backup_revision=$(validate "$backup") || {
          echo "invalid runtime configuration backup" >&2; exit 1;
        }
        if test "${#backup_revision}" -gt "${#current_revision}" ||
           { test "${#backup_revision}" -eq "${#current_revision}" &&
             test "$backup_revision" \> "$current_revision"; }; then
          echo "backup revision is newer than authoritative configuration" >&2
          exit 1
        fi
        if test "$backup_revision" = "$current_revision" &&
           ! cmp -s "$authority" "$backup"; then
          echo "same-revision runtime configuration conflict" >&2
          exit 1
        fi
      fi
    elif test -e "$backup"; then
      validate "$backup" >/dev/null || {
        echo "invalid runtime configuration backup" >&2; exit 1;
      }
    fi;;
  reconcile)
    test "$#" -eq 4
    authority=$2
    backup=$3
    owner=$4
    "$0" check "$authority" "$backup"
    if test -e "$authority"; then
      if test ! -e "$backup" || ! cmp -s "$authority" "$backup"; then
        atomic_copy "$authority" "$backup" "$owner"
      fi
    elif test -e "$backup"; then
      validate "$backup" >/dev/null || {
        echo "invalid runtime configuration backup" >&2; exit 1;
      }
      atomic_copy "$backup" "$authority" "$owner"
    else
      echo "no runtime configuration or backup to reconcile" >&2
      exit 1
    fi;;
  *) echo "usage: runtime-config-file.sh validate FILE | reconcile AUTHORITY BACKUP OWNER" >&2; exit 2;;
esac
