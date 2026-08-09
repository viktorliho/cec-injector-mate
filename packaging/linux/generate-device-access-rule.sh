#!/bin/sh
set -eu

vendor=${1:?usage: generate-device-access-rule.sh VID PID SERIAL [USER]}
product=${2:?usage: generate-device-access-rule.sh VID PID SERIAL [USER]}
serial=${3:?usage: generate-device-access-rule.sh VID PID SERIAL [USER]}
service_user=${4:-cec-injector-mate}

case "$vendor$product" in
  *[!0-9A-Fa-f]*|'') echo "VID/PID must be hexadecimal" >&2; exit 2 ;;
esac
test "${#vendor}" -eq 4 && test "${#product}" -eq 4 || exit 2
case "$serial$service_user" in
  *[!A-Za-z0-9._:-]*|'') echo "unsupported identity character" >&2; exit 2 ;;
esac

vendor=$(printf '%s' "$vendor" | tr 'A-F' 'a-f')
product=$(printf '%s' "$product" | tr 'A-F' 'a-f')
printf '%s\n' "ACTION==\"add|change\", SUBSYSTEM==\"tty\", ATTRS{idVendor}==\"$vendor\", ATTRS{idProduct}==\"$product\", ATTRS{serial}==\"$serial\", GROUP=\"$service_user\", MODE=\"0660\", TAG-=\"uaccess\""
