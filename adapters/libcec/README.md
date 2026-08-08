# libCEC adapter

This optional adapter is the initial production binding for an explicitly
selected CEC Injector Device. It owns one patched-libCEC connection: ordinary
CEC and Wake Extension transactions use that connection and its single reader.
No direct serial side channel exists.

The codec references the authoritative [Device Wake Extension](https://github.com/viktorliho/cec-injector-device/blob/main/protocol/wake-extension-v1.md);
it is implementation code, not a second specification. Core-facing headers
contain no libCEC types or transport locator.

The portable test seam builds with `BUILD_TESTING`. The real binding is opt-in:

```sh
cmake -S . -B build/libcec -DCEC_INJECTOR_MATE_BUILD_LIBCEC=ON
```

Configuration requires libCEC 7.1.1 with
`cec_project_adapter_transaction_v1`. Unpatched upstream libCEC is rejected.
