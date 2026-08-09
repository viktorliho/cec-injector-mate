#!/bin/sh
set -eu

vendor=${1:?usage: generate-remote-wake-rule.sh VID PID SERIAL}
product=${2:?usage: generate-remote-wake-rule.sh VID PID SERIAL}
serial=${3:?usage: generate-remote-wake-rule.sh VID PID SERIAL}

case "$vendor" in
  [0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]) ;;
  *) echo "VID must contain exactly four hexadecimal digits" >&2; exit 2 ;;
esac
case "$product" in
  [0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]) ;;
  *) echo "PID must contain exactly four hexadecimal digits" >&2; exit 2 ;;
esac
case "$serial" in
  *[!A-Za-z0-9._:-]*|'')
    echo "serial contains unsupported characters" >&2
    exit 2
    ;;
esac

vendor=$(printf '%s' "$vendor" | tr 'A-F' 'a-f')
product=$(printf '%s' "$product" | tr 'A-F' 'a-f')

printf '%s\n' "ACTION==\"add|change\", SUBSYSTEM==\"usb\", ENV{DEVTYPE}==\"usb_device\", ATTR{idVendor}==\"$vendor\", ATTR{idProduct}==\"$product\", ATTR{serial}==\"$serial\", TEST==\"power/wakeup\", ATTR{power/wakeup}=\"enabled\""
