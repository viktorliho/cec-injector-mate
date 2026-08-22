# Application icon

`cec-injector-mate.png` is the canonical, lossless application artwork supplied
by the project owner. The PNG files under `hicolor/` are deterministic Lanczos
downsamples for Linux packaging and the compiled Qt resource. macOS packaging
generates `cec-injector-mate.icns` from the canonical PNG with `sips` and
`iconutil`; a future Windows package can generate its `.ico` from the same file.
