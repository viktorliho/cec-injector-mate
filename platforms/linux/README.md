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

Power/session integration, input emission, and Remote Wake environment
mechanisms remain future Linux work. Mate Core policy and SteamOS-specific
persistence do not belong here.
