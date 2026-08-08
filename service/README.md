# Mate Service

Long-running composition, Device registry and selection, session ownership,
side-effect serialization, diagnostics, and the local semantic API belong here.
Concrete OS and libCEC mechanisms do not.

`DeviceSessionFactory` is the composition boundary between a selected
`DiscoveredDevice` and its one Service-owned `DeviceSession`. Service carries
the opaque platform locator unchanged; the paired platform/adapter factory
resolves it using its own inventory. Service also supplies the already-resolved
CEC Physical Address in `DeviceSessionConfiguration`; the factory neither
resolves the address nor connects the returned session. A session retains this
construction address across reconnects. A later topology change requires
Service to replace the session rather than mutate it.

`MateService` is the serialized runtime orchestrator. It owns the registry and
single active session, tags Device work with a Service-private generation, and
translates complete external CEC User Control/TV Standby frames into portable
semantic events. Process startup is passive; `BootRequested` has a separate
explicit entry point.
