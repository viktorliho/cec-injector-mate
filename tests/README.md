# Tests

Current native tests cover the Experimental portable value types, Device
selection state, and fake implementations of the DeviceSession and
HostPlatform contracts. The boundary harness also covers opaque-locator
session construction, ordered host lifecycle delivery, and explicit or
destructor-driven transition-hold completion. A source scan rejects Linux,
systemd, libCEC, Device-path, and sysfs dependencies in Mate Core. Adapter
tests exercise the libCEC-facing seam, Wake
codec, callback copying, and repeated session ownership without requiring
libCEC or hardware. Policy tests use a fake executor to verify lifecycle
plans, conditional ordering, and session recovery. The semantic event queue is
tested independently. These tests require no platform or libCEC dependency.

Linux discovery tests exercise the pure reference matcher, serial-derived
identity, opaque stable locator, deterministic 0..N snapshots, duplicate
identity failure, and the add/remove/change re-enumeration seam without USB
hardware. The opt-in Linux build also compiles the production libudev source.

Linux physical-address tests construct public synthetic checksum-valid EDID
blocks in memory. They cover structural and CTA HDMI VSDB parsing, PA topology
validation, deterministic DRM connector observations, filesystem read
failures, unique/ambiguous resolution, and topology-bound atomic cache
fallback. No private display capture or machine-specific PA is included.

Future Service, other Linux platform slices, and external libCEC integration
tests remain separate. Hardware validation remains distinct from native CTest
results.
