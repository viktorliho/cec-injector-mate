# Linux platform

The first Linux slice provides deterministic 0..N Device discovery. Its
current development/reference matcher requires USB `CAFE:CEC1`, `cdc_acm`,
interface class `0202`, a non-empty USB serial, and a
`/dev/serial/by-id` link. Those USB values identify the proven reference
hardware only; they are not a Device Protocol or product-wide identity.

Linux derives an implementation-private `DeviceId` solely from the USB serial,
so identity survives tty number changes and reconnects. The stable by-id path
is carried as an opaque `PlatformDeviceLocator`. Discovery returns a complete,
deterministically ordered inventory and never selects or opens a Device.
Duplicate serial-derived identities invalidate the snapshot and fail closed.

Pure matching and snapshot code is part of the portable default build and has
no libudev dependency. Production enumeration and monitoring are enabled with
`CEC_INJECTOR_MATE_BUILD_LINUX=ON`; this requires system libudev and performs a
complete re-enumeration after relevant tty add, remove, or change events.

Physical addresses are resolved from checksum-valid EDID on currently
connected DRM connectors; no HDMI port or PA is hardcoded. Zero unique PAs is
`Unavailable`, one is `Available`, and multiple different PAs is `Ambiguous`.
Identical PA values are deduplicated because the HostPlatform result requires
the numeric source address, while different values fail closed.
One known PA alongside an unreadable connected output is also Ambiguous rather
than allowing cache fallback across an unresolved topology.

The optional cache path is supplied by later composition. Cache v2 stores only
a live Available PA plus a non-security `topology-v1` key derived from sorted
connector names and validated EDID base blocks. It is fallback-only for
Unavailable, never overrides Ambiguous, and is ignored when current topology
identity cannot be proven or has changed. This permits conservative recovery
when a validated base block remains visible but the CTA extension is
temporarily unavailable.

Linux remote input maps generic semantic navigation and media actions to a
private Linux key vocabulary. The uinput backend advertises only configured
keys, emits a synchronization event after every key transition, tracks one
active binding, and applies a 900 ms safety release. Chords are supported for
later profiles; `Guide` is profile-required, while `Home` and `Menu` are
currently unmapped. In particular, the private SteamOS `Guide` to Ctrl+1
choice is not a generic mapping.

Remote Wake support is runtime inspection only. Using the current discovery
metadata, it resolves the stable locator to the current tty, validates the
matching USB ancestor, and reads `power/wakeup` without writing sysfs.
Privileged wake configuration and repair remain packaging responsibilities;
machine-specific serial rules are not part of the runtime.

`LinuxHostPlatform` composes discovery, DRM/EDID PA resolution, uinput,
Remote Wake inspection, and power/session mechanisms behind the portable
HostPlatform contract. Its paired session factory validates a selected record
against the current private inventory and supplies the explicit by-id port to
libCEC without auto-discovery or auto-connect. Mate Core policy and
SteamOS-specific persistence do not belong here.

Linux power/session support observes logind `PrepareForSleep` and
`PrepareForShutdown` signals on the system bus. Prepare-true notifications
carry an opaque delay-transition hold when an inhibitor was acquired; Service
completes it after executing the Core plan, while the platform also enforces a
three-second release deadline. Prepare-false maps to `Resumed` or
`ShutdownCancelled`. Startup and reconnect reacquire inhibitors without
inventing lifecycle events.

The opt-in Linux runtime uses sd-bus, requests suspend with `Suspend(false)`,
and fail-closed checks both normal and multiple-session suspend authorization.
It only observes and holds host transitions: Service/Core remain responsible
for input reset, Wake preparation, TV power, active-source, Device refresh,
and Wake Reason operations. Service units, polkit installation, and other
deployment configuration are not part of this platform slice.

The optional construction-time input profile can supply the SteamOS parity
binding `Guide` to Ctrl+1 without changing generic Linux mappings.
