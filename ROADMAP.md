# Roadmap

Pending work, in order. See `STATUS.md` for the current state and effort estimates.

## 1. Finish the launcher layer

The launcher files `launcher/main.cpp`, `launcher/ui/ActivityManager.*` and
`launcher/ui/NativeBridge.cpp` must use `BytecodeInterpreter` / `Vm` / `NativeRegistry`.

**UI natives (`NativeBridge.cpp`).** The current file targets the old calling convention (it contains no app-specific code anymore), so it is replaced, not patched. Register Android UI classes in `NativeRegistry::instance()` with
`void(NativeCall&)` handlers:

* Views are real `Object`s (`vm.newInstance("Landroid/widget/TextView;")`) whose `nativeHandle` is the
  id in `NativeViewRegistry`. Keep an id -> Object cache so `findViewById` returns stable identities.
* `<init>(Context[, AttributeSet[, int]])` for View, TextView, Button, ImageView, ProgressBar, WebView,
  LinearLayout, FrameLayout, ... A DEX subclass of a framework view must create the native view in the
  parent's `<init>` and store its id in `nativeHandle`.
* `declareClass` for the hierarchy (View -> ViewGroup -> LinearLayout, TextView -> Button, ...).
* Methods: text/color/size, visibility, id, background, padding, layout params, `addView`/`removeView`/
  child access, `setOnClickListener`, `findViewById`, orientation, ProgressBar, WebView basics,
  `Activity.setContentView(int)` (through `LayoutInflater::inflateById`) and `(View)`,
  `Activity.findViewById`, `Activity.startActivity(Intent)`, `Activity.finish`,
  `Resources.getString/getInteger/getBoolean/getColor/getIdentifier` (through `ApkResources`),
  `LayoutInflater.inflate(int, ViewGroup, boolean)`.

**Lifecycle (`ActivityManager`).**

* Before the first Activity: instantiate `Bootstrap.applicationClass` (`interp.newObject`), call
  `frameworkSetAppContext(app)`, `attachBaseContext(Context)` and `onCreate()`; set
  `vm.props["package"]`.
* Create the Activity with `interp.newObject(descriptor)` and call `onCreate(Bundle)`, `onStart`,
  `onResume`, ... on that real object.
* If an Activity sets no content, show an empty window and log it.
* `startActivity(Intent)`: read the target class and extras from the Intent (class under legacy field
  key `0x1A02`, action `0x1A01`, extras in a `MapData` payload) and launch it; `finish()` runs
  onPause/onStop/onDestroy.
* Mark the UI thread and the Looper thread with `frameworkMarkMainThread(vm)` (extend `g_mainThread` in
  `FrameworkNatives.cpp` to a set of threads) and route `vm.setPoster(...)` to the launcher's `Handler`.

**Input.** Add a click listener to `View` and fire it on ACTION_UP; make `ViewGroup::onTouchEvent`
hit-test children and translate coordinates by each child's offset.

**Build.** `main.cpp` should load the APK into `ApkResources::instance()` instead of extracting layout
files; add `core/ApkResources.cpp`, `ui/LayoutInflater.cpp` and `third_party/miniz` to the launcher
target; link the executable with `/STACK:268435456` (the interpreter recurses on the native stack).

## 2. Build with MSVC

Nothing has been compiled with MSVC. Expect compiler errors; verify that the API names used by
`launcher/ui/LayoutInflater.cpp` match `NativeViewRegistry.h` and `Activity.h` (names were checked,
signatures were not).

## 3. First real app

Use a small Java-only APK. Run it, read `logs/native_bridge_missing.log`, implement what it needs.

## Known gaps in the runtime

* Objects are never freed (no GC).
* `TreeMap` / `TreeSet` do not sort; `keySet()` / `values()` / `entrySet()` return copies;
  `Arrays.asList` copies.
* `invoke-polymorphic` / `invoke-custom` throw `UnsupportedOperationException`.
* `check-cast` only fails when the target is a DEX-defined class.
* Missing: `java.io` streams (other than `BufferedReader`/`FileReader`), regex, `java.text`,
  `java.time`, `Charset`, networking, `Class.getAnnotation`, `Parcel`.
* Threads: global interpreter lock with periodic yielding; Java threads get a 64 MB native stack.
* JNI / native libraries are not supported; apps that load them in `<clinit>` or `Application`
  will not start.
* There are no unit tests for the interpreter yet (only for the DEX loader and heap).

## Adding framework methods

Register a native with `NativeRegistry::instance().add(cls, name, proto, fn)`. `fn` receives a
`NativeCall` with `self`, typed arguments (`c.i(0)`, `c.o(1)`, `c.str(2)`, ...) and result setters
(`c.retInt`, `c.retObj`, ...). Call back into Java with `vm.callVirtual(...)`; throw with
`vm.throwNew("Ljava/lang/IllegalStateException;", "message")`. Declare framework class hierarchy with
`declareClass`. Start from `logs/native_bridge_missing.log`.
