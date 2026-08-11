# Architecture contracts

Status: Experimental architecture foundation.

CEC Injector Mate is a reference host implementation. The
[CEC Injector Device repository](https://github.com/viktorliho/cec-injector-device)
is the sole authority for Device Protocol v1, the Pulse-Eight Serial Transport
Binding, the Pulse-Eight Compatibility Profile, Wake Extension v1, and all
numeric and wire-level semantics. Mate may implement codecs required to use a
specified authoritative version, but it does not publish an independent Device
protocol specification. If Mate and the normative Device documents disagree,
the Device documents win.

## Components

### Mate Core

Owns portable semantic state, policy decisions, typed configuration, semantic
remote actions, lifecycle decisions, and typed results. It has no dependency
on an operating system, libCEC, IPC, Device paths, SteamOS, or platform-native
input codes.

### DeviceSession

Represents an operating-system-neutral Device connection and its state,
capabilities, CEC operations, Wake operations, and connection events. Patched
libCEC will be the initial adapter, outside Mate Core. Ordinary CEC traffic and
project extension transactions must share the same session and reader.

### HostPlatform

Provides narrow semantic host mechanisms: Device discovery, physical-address
resolution, host power/session observation, suspend requests, remote-input
emission, and Remote Wake environment checks. Preparing transitions may carry
an opaque, move-only hold which Service completes after executing the Core
plan. Platform APIs do not appear in Core contracts.

### Mate Service

Owns process lifetime, the Device registry, active selection, reconnection,
composition, side-effect serialization, runtime diagnostics, and the local
semantic API. It is the only process that directly opens and owns Device
sessions.

Service passes the selected `DiscoveredDevice`, including its opaque locator,
to a composition-owned `DeviceSessionFactory`. The paired platform/adapter
implementation resolves that locator without exposing its representation to
Core. The factory creates a session but does not become a second connection
owner.

The foreground runtime serializes discovery, DeviceSession callbacks and host
lifecycle notifications on a Service-owned worker. Device-bound policy work is
tagged with the active session generation; replacement or loss invalidates old
work without preventing completion of a host transition hold. Runtime process
startup is passive and does not imply `BootRequested`.

Inbound CEC commands retain the single-reader boundary:

```
TV -> libCEC commandReceived callback
   -> RealLibCecApi copies the complete command into PortableCecFrame
   -> LibCecDeviceSession bounded callback-escape FIFO
   -> DeviceSessionObserver copies bytes and receive-time session generation
   -> serialized MateService task queue
   -> portable validation and semantic translation
   -> ExternalTvStandby -> Core policy -> RequestHostSuspend
```

The adapter FIFO is bounded at 32 events. Ordinary traffic is rejected with a
diagnostic when full. A valid TV broadcast Standby may evict one queued
ordinary frame; it is itself rejected and diagnosed if the queue contains only
connection-loss or Standby events. The serialized Service queue is unbounded
and consequently has no queue-full state.

Generation validity is captured when the callback is received and preserved by
FIFO ordering. A current-generation Standby queued immediately before
SessionLost is therefore handled before the loss. An observer is disabled on
replacement, and any already queued task whose device/generation no longer
matches the active session is rejected as `stale_generation`. This keeps stale
reconnect work from suspending the host without retroactively invalidating a
frame already accepted from the current session.

The R15 investigation found and corrected two independently valid silent-loss
defects: full-FIFO loss of a Standby behind ordinary traffic, and retroactive
generation invalidation after receipt. Four real SteamOS Standby cycles then
completed adapter enqueue, Service acceptance, semantic translation,
`RequestHostSuspend`, and actual suspend end to end. None showed queue pressure
or a generation race near Standby. The historical R14 failure was not
reproduced, so neither corrected defect is claimed as its proven cause.

Discovery and session models support zero to many Devices. Initial product
policy permits at most one selected active Device; ambiguous selection fails
closed.

### CLI and future frontends

The CLI is a normal client of Service semantics. It never opens a Device or
parses Device wire traffic. Future GUIs are optional, unprivileged presentation
components using the same semantic Service functionality. The local Mate API
is an Experimental application API, not another CEC Injector standard.

## Policy and mechanism

Core receives semantic host and TV lifecycle events and decides desired
actions. DeviceSession performs Device and CEC actions. HostPlatform performs
host mechanisms such as suspend and input emission. Core uses semantic remote
actions rather than Linux or other platform key codes.

Configuration separates persisted user or administrator intent from derived
runtime state, ephemeral session state, and platform-internal state. It must
not embed machine serials, test paths, host usernames, or development USB
identity as product policy.

Runtime, configuration, and state path selection belongs to Service/platform
composition. Logging belongs to Service diagnostics and platform integration.
System-service integration belongs to packaging, while
SteamOS persistence and repair belong specifically to `packaging/steamos`.

Core Resume policy retains semantic dependency ordering. Mate Service may hold
one bounded Resume recovery transaction while the same selected Device is
temporarily unavailable, then continue the remaining plan against a newly
validated current session generation. A plain reconnect never creates Resume
semantics, and expiry, selection change, host transition, or shutdown cancels
the transaction before any later TV action.

Service diagnostics correlate lifecycle epoch, selected Device, selection
episode, and concrete session generation. Device operations retain their Core
`OperationResult` while logging start, completion, and elapsed time. In
particular, a command timeout and externally observed TV UX are separate
evidence: Mate has no authoritative TV-source confirmation surface. During the
daily-driver observation period, a correct final UX with a logged timeout is a
diagnostic anomaly, not silently reclassified success. Missing/wrong TV action,
suspend or Wake failure, broken/stuck input, unrecovered session, restart loop,
or reconnect failure remains a hard user-visible failure.

Installed Boot remains an explicit oneshot before the passive daemon. Its
Linux HostPlatform profile includes discovery, PA resolution, the read-only
Remote Wake environment, and DeviceSession construction, but does not create
uinput or start long-running logind lifecycle observation. The existing
30-second DeviceSession readiness budget therefore covers late serial
discovery, PA resolution, and session connection without making remote input a
Boot dependency. The full daemon composition has a separate bounded pre-start
effective-access gate for serial, DRM, uinput, and selected USB Wake state.

Boot failure orders but does not inhibit the daemon: there is intentionally no
`Requires=` edge. This preserves passive Mate availability after a missed Boot
action, while a daemon restart cannot pull in the oneshot or replay Boot.

Live/cache physical-address provenance, richer authorization diagnostics,
cache topology identity, and asynchronous suspend diagnostics remain future
implementation concerns; they do not expand the HostPlatform contract here.

## Architecture invariants

1. The Device repository is the sole authority for Device wire semantics.
2. Mate Core is independent of operating systems, libCEC, IPC, and Device paths.
3. Mate Service is the only process that owns Device sessions.
4. The model supports 0..N discovered Devices; initial policy allows at most one selected active Device.
5. Ambiguous Device selection fails closed.
6. libCEC is an adapter, not part of Mate Core.
7. Ordinary CEC and project extension traffic share one Device session and reader.
8. Core uses semantic remote actions, not platform-native key codes.
9. Core decides policy; DeviceSession and HostPlatform execute mechanisms.
10. The local Mate API is an Experimental application API, not another CEC Injector standard.
11. A GUI is optional, unprivileged, and never owns a Device.
12. Linux integration is immutable-friendly; generic Linux integration is separate from SteamOS persistence.
13. The runtime Service uses minimum privilege.
14. Restart and reconnect do not implicitly replay power or input actions.

## Linux direction

Linux is the first reference platform and SteamOS is the first parity target.
Installed runtime operation must not require a container or modification of
`/usr`. Integration must be reproducible, repairable, and separable into
generic Linux mechanisms and SteamOS-specific persistence.

## Deferred decisions

- IPC technology and schema;
- GUI toolkit, repositories, and implementation;
- Windows and macOS implementations;
- policy for multiple simultaneously active Devices;
- distribution-native package format;
- configuration hot reload;
- protocol code generation or a shared protocol package;
- replacement of libCEC.
