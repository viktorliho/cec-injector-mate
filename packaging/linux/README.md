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

Production service units, a complete installer, and distribution persistence
remain deferred.
