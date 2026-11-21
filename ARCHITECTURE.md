# NativeDex Architecture - Android Runtime on Windows

> **Implementation status:** sections marked _(implemented)_ describe what exists in code today; the
> rest is design intent. See [STATUS.md](STATUS.md). The `gc/`, `thread_manager/`, `api_registry/`,
> `method_resolver/` and `cache_manager/` directories are placeholders; their roles are currently
> filled by `bytecode_interpreter/Vm`.


## Overview

NativeDex is an Android runtime for Windows that executes DEX bytecode directly, without emulating an Android device or CPU.

Unlike traditional Android emulators, NativeDex does not virtualize hardware or run a complete Android operating system. Instead, it provides implementations of the Android Runtime (Dalvik/ART execution model) and Android Framework APIs on top of the Win32 platform.

Inspired by the philosophy of Wine, NativeDex implements Android APIs natively for Windows, allowing applications to interact with the host operating system through a compatibility layer rather than an emulated environment.

Applications execute DEX bytecode through NativeDex's virtual machine while using Android framework classes and services implemented for Windows.

## Project Goals

The long-term goal of NativeDex is to execute Android applications on Windows without requiring a traditional emulator or a complete Android operating system.

Development is incremental. The project focuses on building the runtime layer first—DEX loading, class resolution, bytecode execution, memory management, and framework APIs—before expanding compatibility with more Android features.

The architecture described in this document represents the intended design of the runtime. Individual components may be incomplete or evolve as the project matures.

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              NATIVE DEX RUNTIME                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌─────────────────┐   ┌──────────────────┐   ┌──────────────────────────┐  │
│  │   APK Loader    │   │  DEX Loader      │   │   Class Loader           │  │
│  │  (resources,    │──▶│  (DEX parsing,  │──▶│  (class resolution,      │  │
│  │   manifest,     │   │   type parsing)  │   │   method resolution)     │  │
│  │   DEX files)    │   │                  │   │                          │  │
│  └─────────────────┘   └──────────────────┘   └──────────────────────────┘  │
│                                                                      │      │
│                                                                      ▼      │
│  ┌────────────────────────────────────────────────────────────────────────┐ │
│  │                     BYTECODE INTERPRETER                               │ │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌──────────────┐   │ │
│  │  │ Interpreter │  │  Method     │  │  Register   │  │  Exception   │   │ │
│  │  │ Loop        │──│  Resolver   │──│  Frame      │──│  Handler     │   │ │
│  │  └─────────────┘  └─────────────┘  └─────────────┘  └──────────────┘   │ │
│  └────────────────────────────────────────────────────────────────────────┘ │
│                                 │                   │                       │
│                    ┌────────────┴────────┐ ┌───────┴────────┐               │
│                    ▼                     ▼ ▼                ▼               │
│         ┌──────────────────┐   ┌──────────────────┐ ┌───────────────┐       │
│         │  Object Model    │   │   Heap Manager   │ │     GC        │       │
│         │  (Objects,       │   │  (bump-pointer,  │ │  (mark-sweep, │       │
│         │   Classes,       │   │   free lists)    │ │  generational)│       │
│         │   Fields)        │   │                  │ │               │       │
│         └──────────────────┘   └──────────────────┘ └───────────────┘       │
│                                 │                   │                       │
│                                 └─────────┬─────────┘                       │
│                                           ▼                                 │
│                    ┌─────────────────────────────────────────┐              │
│                    │         API REGISTRY                    │              │
│                    │  (java.lang, android.*, java.*)         │              │
│                    └─────────────────────────────────────────┘              │
│                                           │                                 │
│                    ┌──────────────────────┼──────────────────────┐          │
│                    ▼                      ▼                      ▼          │
│         ┌──────────────────┐    ┌──────────────────┐  ┌──────────────────┐  │
│         │  Platform Layer  │    │  Activity/Window │  │  Thread Manager  │  │
│         │    (Win32)       │    │   Manager        │  │  (Thread pool,   │  │
│         │  Window, Input,  │    │  (Activity       │  │   Looper,        │  │
│         │  Graphics (GDI/  │    │   lifecycle,     │  │   Handler)       │  │
│         │  Direct2D),      │    │   Window mgmt)   │  │                  │  │
│         │  File I/O        │    │                  │  │                  │  │
│         └──────────────────┘    └──────────────────┘  └──────────────────┘  │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

       APK
        │
        ▼

NativeDex Runtime
│

Android Framework APIs
│
▼
Windows Platform
(Win32 / Direct2D /
WASAPI / WinHTTP)

## Core Components

### 1. APK Loader (`apk_loader/`)

- Parses APK (ZIP) structure
- Reads `AndroidManifest.xml` (binary XML)
- Extracts `classes.dex` (and `classes2.dex`, etc.)
- Extracts resources (`resources.arsc`, `res/`)
- Parses manifest: package name, permissions, activities, services, receivers, providers

### 2. DEX Loader (`dex_loader/`)

- Parses DEX file format (header, string_ids, type_ids, proto_ids, field_ids, method_ids, class_defs)
- DEX bytecode instruction decoding
- String/type/proto/field/method resolution
- Multi-DEX support

### 3. Class Loader (`class_loader/`)

- Class resolution from DEX type indices
- Method/field resolution
- Superclass/interface resolution
- Class initialization (`<clinit>`)
- Bootstrap class loader (boot classpath) + app class loader

### 4. Bytecode Interpreter (`bytecode_interpreter/`) _(implemented)_

* `Vm.*` - the virtual machine: class registry built over all DEX files, class loading and linking
  (instance slot layout, static storage with `encoded_array` initial values), `<clinit>` handling,
  interned strings, arrays, Java exceptions (`JavaThrow`), monitors with `wait/notify`, Java threads,
  a global interpreter lock with periodic yielding, and a main-thread message queue.
* `Interpreter.cpp` - the Dalvik interpreter loop (all formats and opcodes), invocation dispatch
  (virtual/interface/super/static/direct) with per-reference inline caches.
* `Frame.h` - register frames. A register holds either a 32-bit primitive or an object reference so
  `if-eqz` and reference comparison work on the same file the bytecode uses.
* `NativeRegistry.*` - C++ implementations of framework methods keyed by (class, name, prototype) plus
  declarations of the framework class hierarchy (for dispatch, `instanceof`, catch matching).
* `CoreNatives.cpp`, `CollectionNatives.cpp`, `FrameworkNatives.cpp`, `ReflectNatives.cpp` - the portable
  class library. The launcher registers the UI part on top of the same registry.
* `BytecodeInterpreter.*` - small facade used by the launcher (`newObject`, `callMethod`, `findDexForClass`).

**Native method contract.** A native receives a `NativeCall` (`self`, typed arguments indexed by
parameter, result setters) and may call back into Java with `vm.callVirtual(...)`. It may throw a Java
exception with `vm.throwNew("Ljava/lang/IllegalStateException;", "msg")`.

**Missing framework methods.** When no DEX or native implementation exists the call returns a default
value (a stub instance for `Landroid/...` return types) and the signature is appended to
`logs/native_bridge_missing.log`.

### 5. Object Model (`object_model/`) _(implemented)_

Every Java instance is an `InstanceObject` with `klass`, `fieldSlots` (one slot per instance field, laid
out superclass-first), an optional `nativeHandle` (for example the id of a native view, or the bits of a
boxed number) and an optional native payload (`Object::native`) used by framework classes such as
`ArrayList` or `StringBuilder`. Strings are `StringObject`, arrays are `ArrayObject`. `RuntimeClass`
extends `ClassObject` with DEX linkage (fields, methods, statics, initialization state).

### 6. Heap & GC (`heap/`, `gc/`) _(not in use)_

The interpreter allocates objects directly and never frees them; there is no garbage collector yet.
`heap/HeapManager` is not used by the interpreter. A collector would need roots
from thread frames, statics, interned strings and native payloads.

### 7. API Registry (`api_registry/`) _(implemented as `NativeRegistry`)_

See section 4. The registry is process-wide, keyed by (class descriptor, method name, prototype), and
shared by the portable class library and the launcher's Android UI natives.

### 8. Android Framework Implementation (`android/`)

Each package maps to native C++ implementations:

| Package            | Key Classes                                         | Win32 Mapping                |
| ------------------ | --------------------------------------------------- | ---------------------------- |
| `java.lang`        | Object, String, Class, Thread, System, Runtime      | CRT, Win32 threads, Heap     |
| `java.util`        | ArrayList, HashMap, HashSet, Locale, UUID           | STL containers               |
| `java.io`          | File, InputStream, OutputStream, FileDescriptor     | Win32 File API               |
| `android.app`      | Activity, Application, Service, Intent, Context     | Win32 Window, Message Loop   |
| `android.content`  | Context, ContentResolver, Intent, SharedPreferences | Registry, Files              |
| `android.view`     | View, ViewGroup, SurfaceView, WindowManager         | Win32 HWND, GDI/Direct2D     |
| `android.widget`   | TextView, Button, LinearLayout, ListView            | Win32 Controls / Custom Draw |
| `android.graphics` | Canvas, Paint, Bitmap, Matrix, Path                 | Direct2D / GDI+              |
| `android.os`       | Handler, Looper, Message, Bundle, Binder            | Win32 Message Queue          |

### 9. Platform Layer (`platform/windows/`)

- **Window**: Win32 `HWND`, message pump (`GetMessage`/`DispatchMessage`)
- **Input**: `WM_KEYDOWN`, `WM_MOUSEMOVE`, `WM_TOUCH` → Android `KeyEvent`/`MotionEvent`
- **Graphics**: Direct2D for `Canvas`/`Paint`/`Bitmap`; fallback to GDI
- **Threading**: `CreateThread`/`CreateThreadpool` → `java.lang.Thread`, `Handler`
- **File I/O**: `CreateFile`/`ReadFile`/`WriteFile` → `java.io.*`
- **Assets**: APK asset extraction → `AssetManager`
- **Time**: `QueryPerformanceCounter` → `System.currentTimeMillis`/`nanoTime`

### 10. Activity/Window Manager (`launcher/ui/`)

- `ActivityManager`: Activity lifecycle (`onCreate`, `onStart`, `onResume`, `onPause`, `onStop`, `onDestroy`)
- `WindowManager`: Win32 window creation, layout, focus
- `View` hierarchy: Layout, measure, draw pass
- `SurfaceView`/`TextureView`: Direct2D swap chain

### 11. Thread Manager / Looper (`thread_manager/`)

- `Looper`: Win32 message queue (`MsgWaitForMultipleObjects`)
- `Handler`: Post messages/runnables to looper
- `Thread`: Wrapper around Win32 thread + TLS for `Thread.currentThread()`
- Main thread = UI thread = Looper loop

### 12. Launcher (`launcher/`)

- Entry point (`main`)
- Win32 window creation
- APK path argument parsing
- Bootstrap → APK load → DEX load → Class load → `Application.onCreate()` → `Activity.onCreate()`

## Project Structure

```
NativeDex/
├── CMakeLists.txt
├── ARCHITECTURE.md
├── BUILD.md
├── src/
│   ├── runtime/
│   │   ├── apk_loader/          # APK parsing, manifest, resources
│   │   ├── dex_loader/          # DEX file format parser
│   │   ├── class_loader/        # Class resolution & initialization
│   │   ├── bytecode_interpreter/ # DEX bytecode interpreter
│   │   ├── method_resolver/     # Method/field resolution
│   │   ├── api_registry/        # Native API registration
│   │   ├── object_model/        # Object, ClassObject, Array, String
│   │   ├── heap/                # Bump-pointer allocator
│   │   ├── gc/                  # Mark-sweep GC
│   │   ├── thread_manager/      # Thread, Looper, Handler
│   │   ├── module_manager/      # Module/DEX loading
│   │   ├── cache_manager/       # DEX/OAT cache
│   │   └── logger/              # Logging
│   ├── android/                 # Android framework implementations
│   │   ├── java.lang/           # java.lang.* native impl
│   │   ├── java.util/           # java.util.* native impl
│   │   ├── java.io/             # java.io.* native impl
│   │   ├── android.app/         # Activity, Application, Service
│   │   ├── android.content/     # Context, Intent, ContentResolver
│   │   ├── android.view/        # View, ViewGroup, WindowManager
│   │   ├── android.widget/      # Widgets (TextView, Button, etc.)
│   │   ├── android.graphics/    # Canvas, Paint, Bitmap, Matrix
│   │   ├── android.os/          # Handler, Looper, Message, Bundle
│   │   └── android.util/        # Log, SparseArray, Pair
│   ├── platform/
│   │   └── windows/
│   │       ├── window/          # Win32 window management
│   │       ├── input/           # Input event translation
│   │       ├── graphics/        # Direct2D/GDI graphics
│   │       ├── filesystem/      # Win32 file I/O
│   │       ├── threading/       # Win32 thread wrappers
│   │       └── time/            # High-res timers
│   ├── launcher/
│   │   ├── core/                # Bootstrap, APK loading
│   │   ├── ui/                  # ActivityManager, WindowManager
│   │   └── main.cpp             # Entry point
│   └── tools/
│       ├── inspector/           # DEX/APK inspector tool
│       └── tracer/              # Bytecode tracer
├── tests/
│   ├── unit/
│   └── integration/
└── third_party/
    └── (miniz, rapidxml, etc.)
```

## Execution Flow

```
main()
  └─ WinMain / wmain
       └─ Launcher::initialize()
            ├─ Parse command line (APK path)
            ├─ Create Win32 window (hidden initially)
            ├─ Bootstrap.loadAPK(apkPath)
            │    ├─ APKLoader.open(apkPath)
            │    ├─ Parse AndroidManifest.xml → package, launcher activity
            │    ├─ Extract classes.dex (and classes2.dex...)
            │    └─ DEXLoader.load(dexData)
            ├─ ClassLoader.loadClass(launcherActivity)
            ├─ ClassLoader.initializeClass(launcherActivity)  // <clinit>
            ├─ ActivityManager.launchActivity(launcherActivity)
            │    ├─ Activity.onCreate()
            │    ├─ WindowManager.createWindow()
            │    └─ ViewRootImpl.performTraversals()  // measure/layout/draw
            ├─ ThreadManager.startMainLooper()
            └─ MessageLoop.run()  // Win32 GetMessage/DispatchMessage
```

## DEX Bytecode Interpreter Design _(implemented)_

### Register Frame

A frame has `registersSize` slots; the last `insSize` slots receive the arguments (receiver first for
instance methods; `long`/`double` take two slots). Each slot is `{uint32 v; Object* o;}` and is written
as either a primitive or a reference, never both.

### Instruction Dispatch

A single `switch` over the opcode with the pc and frame in locals. Formats follow the Dalvik bytecode
specification exactly (10x ... 51l, 35c/3rc). The loop is wrapped in one `try/catch (JavaThrow&)`:
on a Java exception the frame's try/catch table is consulted using the pc of the faulting instruction;
if no handler matches the exception propagates to the calling frame through normal C++ unwinding.

### Invocation

`invoke-*` resolves a `MethodRef` (class, name, prototype) to a `Target`: either an interpreted `RMethod`
or a native function. Virtual/interface calls start the search at the receiver's runtime class, then
superclasses, then interfaces (default methods); framework classes are searched in the native registry
at the same points. Results are cached per reference.

### Concurrency

Bytecode runs under a global interpreter lock. It is released around blocking natives (`sleep`,
`wait`, `join`, ...) and periodically on back-edges and calls so other Java threads progress.
`synchronized` and `monitor-enter/exit` use real owner-tracked monitors.

## Android Framework → Win32 Mapping

| Android API              | Win32 Implementation                              |
| ------------------------ | ------------------------------------------------- |
| `Activity.onCreate()`    | `CreateWindowEx` + `ShowWindow`                   |
| `View.onDraw(Canvas)`    | `BeginPaint`/`EndPaint` + Direct2D draw calls     |
| `Canvas.drawRect()`      | `ID2D1RenderTarget::FillRectangle`                |
| `Canvas.drawText()`      | `IDWriteTextLayout` + `DrawTextLayout`            |
| `Bitmap`                 | `IWICBitmap` / `ID2D1Bitmap`                      |
| `Handler.post(Runnable)` | `PostThreadMessage` to UI thread                  |
| `Looper.loop()`          | `GetMessage`/`DispatchMessage` loop               |
| `Thread.start()`         | `CreateThread` + TLS for `Thread.currentThread()` |
| `FileInputStream`        | `CreateFile` + `ReadFile`                         |
| `SharedPreferences`      | Registry (`HKEY_CURRENT_USER\Software\<pkg>`)     |
| `Intent.startActivity()` | `ActivityManager.launchActivity()`                |
| `SurfaceView`            | Direct2D swap chain on child HWND                 |
| `AudioTrack`             | WASAPI / XAudio2                                  |
| `Camera`                 | Media Foundation                                  |
| `Bluetooth`              | Windows Bluetooth API                             |
| `Location`               | Windows Location API                              |

## Build System (CMake)

### Targets

| Target               | Type       | Description                                                    |
| -------------------- | ---------- | -------------------------------------------------------------- |
| `native_dex_runtime` | Static lib | Core runtime (interpreter, GC, heap, class loader, DEX loader) |
| `android_framework`  | Static lib | All android._ and java._ implementations                       |
| `platform_windows`   | Static lib | Win32 platform abstraction                                     |
| `launcher`           | Executable | Main entry point, WinMain, window                              |
| `inspector`          | Executable | DEX/APK inspector tool                                         |
| `tracer`             | Executable | Bytecode execution tracer                                      |
| `tests`              | Executable | Unit/integration tests                                         |

### Dependencies

```
launcher
  ├─ native_dex_runtime
  ├─ android_framework
  └─ platform_windows

android_framework
  └─ native_dex_runtime

platform_windows
  └─ (Windows SDK only)

native_dex_runtime
  └─ (C++20 STL only, optionally miniz for APK)
```

## Testing Strategy

### Unit Tests (per module)

- DEX parser: parse test DEX files, verify string/type/method tables
- Interpreter: execute hand-crafted DEX bytecode, verify register state
- GC: allocate, collect, verify no leaks, finalizers run
- Class loader: resolve classes, methods, fields

### Integration Tests

- Hello World APK → shows "Hello World" in Win32 window
- Button click → onClick handler called
- ListView scrolling → measure/layout/draw works
- Activity lifecycle → onCreate/Start/Resume/Pause/Stop/Destroy called
- Thread/Handler → post to UI thread works

### Test APKs

- Minimal: empty activity
- UI: buttons, text, layouts
- Lifecycle: activity transitions
- Threading: background thread + handler
- Graphics: custom View.onDraw

## Getting Started (Build)

```bash
# Requirements
# - Windows 10/11 SDK (10.0.19041+)
# - MSVC v143 (Visual Studio 2022 Build Tools or newer)
# - CMake 3.20+
# - Ninja (optional, for faster builds)

# Configure
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build
cmake --build build --config Release

# Run
.\build\launcher\Release\NativeDex.exe path\to\app.apk
```

## References

- [DEX Format](https://source.android.com/docs/core/runtime/dex-format)
- [Android Runtime (ART)](https://source.android.com/docs/core/runtime)
- [AOSP Source](https://android.googlesource.com/)
- [Win32 API](https://learn.microsoft.com/en-us/windows/win32/)
- [Direct2D](https://learn.microsoft.com/en-us/windows/win32/direct2d/direct2d-portal)
- [DirectWrite](https://learn.microsoft.com/en-us/windows/win32/directwrite/directwrite-portal)
