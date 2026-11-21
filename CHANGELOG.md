# Changelog

## Unreleased

Initial state of the project.

### Included
- DEX loader (multi-dex) and APK access (zip, `AndroidManifest.xml`, `resources.arsc`, binary XML layouts).
- Dalvik interpreter with every opcode format, real Java exceptions, static initializers, monitors
  (`synchronized`, `wait/notify`), Java threads, a global interpreter lock and a main-thread message queue.
- Class model with virtual/interface/super/static/direct dispatch and per-reference inline caches.
- `NativeRegistry`: framework methods implemented in C++ and keyed by (class, method, prototype); unknown
  framework methods return a default value and are logged to `logs/native_bridge_missing.log`.
- Portable class library: `java.lang`, `java.util`, concurrency, reflection, `java.io.File`, enums,
  references, atomics, plus non-visual Android APIs (Context, Intent, Bundle, Handler/Looper,
  SharedPreferences, Log).
- Layout inflater that resolves layouts by resource id.
- Linux test harness (`tools/linux_harness`) that runs real DEX files.

### Known limitations
- The Windows launcher (`main.cpp`, `ActivityManager`, `NativeBridge.cpp`) is not finished and the
  project does not build yet.
- Nothing has been built with MSVC.
- No garbage collector. No JNI / native libraries.
- See `ROADMAP.md`.
