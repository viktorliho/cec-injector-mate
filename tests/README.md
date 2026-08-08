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

Linux remote-input tests use an in-memory sink to verify generic/profile
mapping boundaries, key/chord ordering, key-plus-sync emission, duplicate
suppression, changed bindings, reset, timeout release, and failure recovery
without opening `/dev/uinput`. Remote Wake tests build a synthetic sysfs and
device-link tree to verify authorization, denial, missing/mismatched devices,
ambiguous USB ancestry, tty renumbering, and read-only behavior.

Linux power/session tests use a narrow fake logind seam. They cover ordered
sleep/shutdown lifecycle translation, transition-hold completion,
abandonment, deadline and disconnect release, inhibitor reacquisition without
synthetic Resume, suspend result mapping, fail-closed two-action
authorization, and bounded reconnect backoff. The production sd-bus worker is
compiled only by the opt-in Linux build; tests do not contact the system bus.

Service tests use fake HostPlatform and DeviceSession implementations to cover
selection, PA-gated construction, PA replacement, generation cancellation,
passive startup, validated CEC User Control and TV Standby translation, Wake
maintenance, lifecycle holds, policy dependencies, and clean shutdown. The
full Linux+libCEC build also tests current-inventory locator validation without
opening hardware. Hardware validation remains distinct from native CTest
results.
