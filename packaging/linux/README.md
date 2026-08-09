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

`install.sh BUNDLE VID PID SERIAL` installs a pinned public bundle, a dedicated
unprivileged `cec-injector-mate` account, exact-device ACL and Wake rules,
uinput/DRM ACLs, narrow suspend authorization, and two systemd units. Program
files live below `/opt/cec-injector-mate/releases`; configuration is in
`/etc/cec-injector-mate`; persistent PA state is in
`/var/lib/cec-injector-mate`; `/run/cec-injector-mate` is ephemeral.

The long-running service always starts passively. A distinct boot oneshot owns
the DeviceSession first, waits for it, submits the existing typed
`BootRequested` exactly once, exits, and only then permits the passive service
to start. The installer starts only the passive service; the oneshot first runs
on the next ordinary boot.

`/usr/libexec/cec-injector-mate/health-check` correlates diagnostics with the
current systemd invocation and checks the selected serial, PA, session, Wake,
input permission, and lifecycle inhibitors. `uninstall.sh` removes only public
integration and program files and preserves `/etc/cec-injector-mate` and
`/var/lib/cec-injector-mate` by default. `--purge-state` removes persistent
state explicitly.

The pinned Debian 12 build recipe is under `debian12/`. SteamOS atomic-update
and RAUC persistence remain deliberately deferred.
