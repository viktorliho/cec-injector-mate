# Tests

Current native tests cover the Experimental portable value types, Device
selection state, and fake implementations of the DeviceSession and
HostPlatform contracts. Policy tests use a fake executor to verify lifecycle
plans, conditional ordering, and session recovery. The semantic event queue is
tested independently. These tests require no platform or libCEC dependency.

Future Service, adapter, Linux platform, and integration tests will remain
separate. Hardware validation remains distinct from native CTest results.
