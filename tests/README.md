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
validation, mixed connector/non-connector DRM class layouts, sysfs/native-DRM
EDID source selection and conflict handling, multi-card correlation seams,
filesystem/property failures, unique/ambiguous resolution, and topology-bound
atomic cache fallback. No private display capture or machine-specific PA is
included.

Linux remote-input tests use an in-memory sink to verify generic/profile
mapping boundaries, key/chord ordering, key-plus-sync emission, duplicate
suppression, changed bindings, reset, timeout release, and failure recovery
without opening `/dev/uinput`. They also verify the typed current SteamOS
parity bindings for Guide and Info. Remote Wake tests build a synthetic sysfs and
device-link tree to verify authorization, denial, missing/mismatched devices,
exact selected-ancestor matching through unrelated root hubs, partial identity,
ambiguous matching ancestry, tty renumbering, and read-only behavior.
Packaging tests verify deterministic exact-identity Remote Wake rule generation,
USB-device-level matching, input validation, and the single deterministic
install/removal path without touching the host's udev configuration.

Linux power/session tests use a narrow fake logind seam. They cover ordered
sleep/shutdown lifecycle translation, transition-hold completion,
abandonment, deadline and disconnect release, inhibitor reacquisition without
synthetic Resume, suspend result mapping, fail-closed two-action
authorization, and bounded reconnect backoff. The production sd-bus worker is
compiled only by the opt-in Linux build; tests do not contact the system bus.

Service tests use fake HostPlatform and DeviceSession implementations to cover
selection, PA-gated construction, PA replacement, generation cancellation,
correlated lifecycle/operation diagnostics, Resume path classification,
dependency-skip reporting, unchanged-discovery log suppression,
passive startup, validated CEC User Control and TV Standby translation, Wake
maintenance, lifecycle holds, policy dependencies, and clean shutdown,
including the proven 0x71--0x74 semantic overrides. The
full Linux+libCEC build also tests current-inventory locator validation without
opening hardware. Hardware validation remains distinct from native CTest
results.

Production packaging also simulates early boot with serial/DRM and
uinput/Wake becoming ready independently. Boot readiness succeeds without
uinput, daemon readiness waits for its larger resource set, missing resources
time out, and unit structure preserves Boot/passive-daemon separation. The
controlled Linux composition test constructs the Boot profile with an unusable
uinput endpoint to prove remote-input initialization is absent.
