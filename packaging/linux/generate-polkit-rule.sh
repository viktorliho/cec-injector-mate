#!/bin/sh
set -eu

service_user=${1:-cec-injector-mate}
case "$service_user" in
  *[!A-Za-z0-9._-]*|'') echo "unsupported service user" >&2; exit 2 ;;
esac

cat <<EOF
polkit.addRule(function(action, subject) {
    if (subject.user == "$service_user" &&
        (action.id == "org.freedesktop.login1.suspend" ||
         action.id == "org.freedesktop.login1.suspend-multiple-sessions")) {
        return polkit.Result.YES;
    }
});
EOF
