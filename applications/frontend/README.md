# Mate frontend contract

This Qt-free application layer describes the state, configuration, capabilities,
typed action requests/completions, and diagnostic events needed by presentation frontends. It does
not own a Device session and does not expose platform locators or native APIs.

Both an in-process composition and a future daemon/IPC client implement the same
`MateFrontendBackend` contract. IPC is intentionally not specified yet. Backend
observer callbacks may arrive on any thread; a presentation adapter is
responsible for dispatching them onto its event loop.

Diagnostic CEC events are delivered through the observer rather than requiring
every backend to retain journal/history storage. Compatibility-test rows remain
a Lab presentation model until a real optional test capability is designed.

`FakeFrontendBackend` supplies synthetic deterministic development data. It
never discovers or opens hardware.
