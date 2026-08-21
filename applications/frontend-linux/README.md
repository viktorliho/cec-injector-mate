# Linux daemon frontend

The long-running production daemon owns `org.cecinjector.Mate` on the system
bus. It does not use D-Bus activation; the boot oneshot never owns this name.
The object `/org/cecinjector/Mate` implements `org.cecinjector.Mate1` using
sd-bus. The Linux Qt GUI client uses QtDBus, confined to `applications/gui`.

`GetStatus`, `GetCapabilities`, and `GetConfiguration` return versioned
`a{sv}` maps. Status contains service/device/session/address/Wake/EDID-summary
scalars. Configuration contains `schema_version=1`, `revision`, all six policy
booleans, `remote_input_enabled`, and `remote_profile`.

`SetConfiguration(t base_revision, a{sv} configuration)` returns a stable
result name, authoritative revision, and message. `RequestAction(s action)`
accepts only `power_on_tv`, `make_active_source`, or `standby_tv`, returning a
daemon request id. Completion and live CEC traffic are signals; no history is
stored by the daemon.

The bus policy permits local read methods and signal reception. Mutations are
limited to members of `wheel`, and the service independently verifies root or
wheel sender credentials. The GUI never writes `/etc` and never opens the
Injector.
