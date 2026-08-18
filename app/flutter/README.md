# ODPAR Territorial Domain — Flutter/Dart FFI

This is the real Flutter app for the authoritative C11 engine. It presents the
native RGBA framebuffer directly, and does not embed the browser preview or
duplicate gameplay in Dart.

Dart FFI queries ABI v1/API 14 before initialization, then drives the fixed
120 Hz simulation and copies coherent RGBA/stats snapshots for Flutter. The
APK packaging compiles the same `engine/` sources with CMake/NDK; Kotlin exists
only as Flutter's minimal launch activity.

See `../../docs/FLUTTER_APP.md` for architecture, controls, validation and APK
packaging details.
