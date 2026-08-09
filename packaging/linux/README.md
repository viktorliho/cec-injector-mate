# Generic Linux packaging

The foreground runtime only inspects USB Remote Wake authorization. Privileged
integration can generate and manage one exact udev rule for the selected USB
Device's VID, PID, and serial:

```sh
./generate-remote-wake-rule.sh VID PID SERIAL
sudo ./manage-remote-wake-rule.sh install VID PID SERIAL
sudo ./manage-remote-wake-rule.sh remove
```

The install operation atomically replaces the single project rule, reloads
udev, and retriggers only the supplied identity. Running it repeatedly is
idempotent; selecting a new serial replaces rather than accumulates rules. The
rule acts on USB `add` and `change`, so a physical reconnect restores the
selected Device's `power/wakeup=enabled` without relying on a ttyACM number.
The serial is deployment input and is not stored in public source.

## Production-style installation candidate

`install.sh BUNDLE VID PID SERIAL` installs a pinned public bundle and a
dedicated unprivileged `cec-injector-mate` account/group. Exact-device serial
access and `/dev/uinput` use native udev ownership with mode `0600`;
read-only DRM EDID queries use the ordinary primary-node `video` group without
granting render-node access. The installer also provides the exact-device Wake
rule, narrow suspend authorization, and two systemd units. Program files live
below `/opt/cec-injector-mate/releases`; configuration is in
`/etc/cec-injector-mate`; persistent PA state is in
`/var/lib/cec-injector-mate`; `/run/cec-injector-mate` is ephemeral.

The project rules run after the normal seat rules and remove the `uaccess` tag
for their selected devices. Service access relies on the owner `user::rw-` ACL
entry, not the owning-group entry that SteamOS seat handling can rewrite. Tag
removal does not erase a named ACL that an earlier seat builtin already applied
during the same event. Such a pre-existing desktop-user ACL is informational;
installation never uses an external udev helper to rewrite it.

The long-running service always starts passively. A distinct boot oneshot owns
the DeviceSession first, waits for it, submits the existing typed
`BootRequested` exactly once, exits, and only then permits the passive service
to start. The installer starts only the passive service; the oneshot first runs
on the next ordinary boot.

`/opt/cec-injector-mate/current/libexec/health-check` correlates diagnostics with the
current systemd invocation and checks the selected serial, PA, session, Wake,
input permission, and lifecycle inhibitors. `uninstall.sh` removes only public
integration and program files and preserves `/etc/cec-injector-mate` and
`/var/lib/cec-injector-mate` by default. `--purge-state` removes persistent
state explicitly.

The pinned Debian 12 build recipe is under `debian12/`. SteamOS atomic-update
and RAUC persistence remain deliberately deferred.
