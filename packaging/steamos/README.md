# SteamOS atomic update survival

The installer owns `/etc/atomic-update.conf.d/cec-injector-mate.conf`. SteamOS
3.9.5 reads this directory through `/usr/lib/holo/holo-sync-var` and preserves
the drop-in itself through `/usr/lib/rauc/atomic-update-keep.conf`. The list
contains seven exact Mate files. Parent directories are traversed by rsync's
`--include="*/"`; systemd service units are already in SteamOS's built-in list.
No Valve file or unrelated `/etc` path is changed.

The authoritative configuration is
`/etc/cec-injector-mate/runtime/config.conf`. The daemon and installer maintain
a byte-identical protected backup at
`/var/lib/cec-injector-mate/runtime-config.backup`. A successful GUI Apply
atomically replaces and syncs both files at the same monotonic revision. A
backup write failure rolls the live policy and `/etc` back to the prior
revision; an incomplete rollback is reported as an error. If a crash occurs
between replacements, `/etc` remains authoritative when its revision is
newer; the next installer run refreshes the stale backup. A newer backup or
same-revision mismatch fails closed. Missing `/etc` is restored from a valid
backup, never from defaults when an installed release exists.

For an installed M15A release, run its canonical installer with
`--repair-only` and its own bundle and identity arguments to reconstruct
derived integration without starting either Mate unit. The installed release
contains its installer and templates under `installation/`. Verify the
installed manifest and Device identity first. Activation remains an explicit
operator step after repair. Normal uninstall keeps configuration, backup, and
a two-path preservation fragment; `--purge-state` removes those Mate-owned
files and the fragment. Neither mode touches other applications' drop-ins.

An isolated two-update and broken-update simulation is registered as
`mate-steamos-update-survivability`. Native validation may use
`sudo /usr/lib/holo/holo-sync-var --dry-run all`; this does not install Mate or
perform an atomic update.
