# Contributing

CEC Injector Mate is Experimental. Before changing architecture, read
[docs/architecture.md](docs/architecture.md).

- Preserve the Mate Core, Service, DeviceSession adapter, HostPlatform, and CLI
  boundaries.
- Device wire semantics originate from the authoritative
  [CEC Injector Device repository](https://github.com/viktorliho/cec-injector-device).
  Do not silently redefine them in Mate.
- Add focused tests for behavior changes where practical. Hardware-sensitive
  changes should include relevant hardware evidence.
- Preserve third-party license and provenance information described in
  [THIRD_PARTY.md](THIRD_PARTY.md).
- Do not commit generated build artifacts.

Run the native validation before submitting changes:

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
git diff --check
```
