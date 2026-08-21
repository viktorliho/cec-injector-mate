# Qt Widgets frontend

The optional GUI is one desktop application with a default User Mode and an
explicit Lab Mode that reveals engineering pages. Qt Widgets fits the forms,
tables, tabs, and diagnostic views of a desktop configuration utility while
preserving the existing C++/CMake architecture across Linux, macOS, and
Windows. It requires no language bridge, QML, WebEngine, or custom theme.

The GUI only consumes the Qt-free `MateFrontendBackend`. The current executable
uses `FakeFrontendBackend`; it neither opens hardware nor contacts the daemon.
Future in-process and daemon/IPC implementations will share that contract.
