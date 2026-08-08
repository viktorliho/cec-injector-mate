# Mate Service

Long-running composition, Device registry and selection, session ownership,
side-effect serialization, diagnostics, and the local semantic API belong here.
Concrete OS and libCEC mechanisms do not.

`DeviceSessionFactory` is the composition boundary between a selected
`DiscoveredDevice` and its one Service-owned `DeviceSession`. Service carries
the opaque platform locator unchanged; the paired platform/adapter factory
resolves it using its own inventory.
