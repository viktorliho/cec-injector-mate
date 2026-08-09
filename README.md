# CEC Injector Mate

CEC Injector Mate is the reference host implementation for CEC Injector. It is
intended to turn Device capabilities into host-side policy and integration
without making those policies part of the Device firmware.

The [CEC Injector Device project](https://github.com/viktorliho/cec-injector-device)
is the sole authority for the Device Protocol, transport binding, compatibility
profile, extensions, and all wire-level semantics. Mate consumes those
contracts; it does not redefine them.

## Architecture direction

Mate is organized around four layers:

- **Mate Core** owns portable semantic state and policy decisions.
- **Mate Service** owns Device sessions and composes Core with adapters and the
  host platform.
- **Platform implementations** provide operating-system mechanisms.
- **CLI** is a client of the Service's semantic functionality.

Linux is the first reference platform and SteamOS is the first parity target.
The boundaries are intended to permit later Windows and macOS implementations
without rewriting Mate Core. A GUI is optional future work and will not own a
Device connection.

## Current status

Experimental. The repository contains the portable Core and policy, the
patched-libCEC DeviceSession adapter, Linux HostPlatform mechanisms, the
hardware-validated foreground runtime, and a first production-style generic
Linux installation candidate. These APIs are not stable. SteamOS OS-update
persistence, IPC, and a GUI remain future work.

See the [architecture contracts](docs/architecture.md),
[contribution guidelines](CONTRIBUTING.md), and
[third-party policy](THIRD_PARTY.md).

## Build the foundation

Requirements: CMake 3.25 or newer and a native C++20 compiler.

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

Project-owned code is licensed under the [Apache License 2.0](LICENSE).
