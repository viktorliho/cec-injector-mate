#!/bin/sh
set -eu
output=$(dbus-daemon --session --fork --print-address=1 --print-pid=1)
address=$(printf '%s\n' "$output" | sed -n '1p')
pid=$(printf '%s\n' "$output" | sed -n '2p')
trap 'kill "$pid" 2>/dev/null || true' EXIT INT TERM
MATE_TEST_BUS_ADDRESS=$address QT_QPA_PLATFORM=offscreen exec "$1"
