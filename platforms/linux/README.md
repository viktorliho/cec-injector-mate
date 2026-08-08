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

Power/session integration, physical-address resolution, input emission,
storage, and Remote Wake environment mechanisms remain future Linux work.
Mate Core policy and SteamOS-specific persistence do not belong here.
