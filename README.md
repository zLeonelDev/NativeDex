# NativeDex

NativeDex is an experimental Android runtime for Windows that executes Android applications by interpreting DEX bytecode directly on the host system.

Unlike traditional Android emulators, NativeDex does not run a complete Android operating system or emulate a mobile device. It provides a Dalvik-compatible execution environment and implements Android framework APIs on top of the Windows platform.

Inspired by projects like Wine, NativeDex aims to be an Android compatibility layer where applications run against native Windows APIs.

## Project Status

> ⚠️ **Experimental. It does not run real-world apps yet.**
>
> The bytecode interpreter and the Java/Android class library run real DEX files (tested with a
> Linux harness). The Windows launcher/UI layer is **still being adapted to the runtime API and
> does not build yet** (see [STATUS.md](STATUS.md)).

| Area | State |
| ---- | ----- |
| DEX parsing (multi-dex), resource table (`resources.arsc`), binary XML | Working (tested on a 15-DEX, 79k-resource APK) |
| Bytecode interpreter (all Dalvik opcodes, exceptions, `<clinit>`, threads) | Working, validated on real app code |
| `java.lang` / `java.util` / reflection / concurrency class library | Working core (≈1400 native methods), many gaps |
| Android framework **without UI** (Context, Intent, Bundle, Handler, SharedPreferences…) | Basic |
| Android UI (views, layouts, input, Activity lifecycle in the launcher) | **In progress, does not build yet** |
| JNI / native `.so` libraries | **Not supported** (apps that depend on them cannot start) |
| Linux / macOS | Runtime core builds on Linux (test harness only); no UI |

### What "works" means today

On Linux, `tools/linux_harness` loads all DEX files of an APK and runs its `Application` and an
`Activity` through real method calls (a large commercial app reaches ~19,000 invocations before it
needs native JNI libraries). A small Java-only app is the intended first end-to-end target and has
**not** been run yet. Details, numbers and the remaining work: [STATUS.md](STATUS.md) and
[ROADMAP.md](ROADMAP.md).

## Goals

- Execute Android DEX bytecode directly on Windows.
- Provide a modular Android runtime architecture.
- Implement Android framework APIs using native Windows technologies.
- Avoid dependency on a full Android operating system or emulator.
- Support incremental expansion of Android API compatibility (no per-app hardcoding).
- Maintain a clean separation between runtime, framework, and platform layers.

## How it works

```
APK ──► APK/DEX loader ──► Vm (classes, objects, threads)
                              │
                              ├── Interpreter (Dalvik opcodes)
                              │       │ invoke
                              │       ▼
                              │   Method resolution ──► DEX method (interpreted)
                              │                    └──► NativeRegistry (C++ implementation)
                              ▼
                  Android framework natives (launcher/ui) ──► Win32 / Direct2D
```

* Framework classes (`java.*`, `android.*`) are **not** loaded from DEX: they are implemented in C++ and
  registered in `NativeRegistry` by `(class, method, prototype)`.
* A framework method that is not implemented does not crash the app: it returns a default value
  (or a harmless stub object for `android.*` types) and is recorded in `logs/native_bridge_missing.log`.
  That log is the to-do list for compatibility work.

## Repository layout

```
runtime/
  dex_loader/            DEX/multi-dex parsing
  object_model/          Object, ClassObject, StringObject, ArrayObject, InstanceObject
  bytecode_interpreter/  Vm, Interpreter, NativeRegistry and the portable class library
                         (CoreNatives, CollectionNatives, FrameworkNatives, ReflectNatives)
  class_loader/, heap/   unused by the interpreter (objects are allocated directly)
android/                 Android API layer skeletons (android.os Looper/Handler, ...)
platform/windows/        Win32 window, Direct2D graphics, input, threads
launcher/                Launcher, Activity/view classes, layout inflater, resource table, manifest
tools/linux_harness/     Build and run the runtime on Linux against real DEX files
tools/*.py               DEX disassembly/inspection helpers
tests/                   Unit tests
```

## Platform Support

| Platform | Status |
| -------- | ------ |
| Windows  | Primary target (launcher in progress) |
| Linux    | Runtime core + test harness only |
| macOS    | Planned |

## Documentation

- [`STATUS.md`](STATUS.md) - Progress report, metrics, and what is left to run a simple app.
- [`ROADMAP.md`](ROADMAP.md) - Pending work, design notes for the launcher layer, known gaps.
- [`ARCHITECTURE.md`](ARCHITECTURE.md) - Runtime architecture and design decisions.
- [`BUILD.md`](BUILD.md) - Build instructions, Linux harness, current build state.
- [`CHANGELOG.md`](CHANGELOG.md) - Release notes.

## License

Open-source project. License information will be added as the project matures.
