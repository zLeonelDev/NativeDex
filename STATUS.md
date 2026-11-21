# NativeDex - Status

Numbers below were measured unless marked "estimate".

## Summary

* The **execution engine works**: on Linux it runs a large production APK's `Application` and `Activity`
  code (about 18,900 method invocations, 1,680 distinct methods, 1,188 of them app code) until it needs
  native JNI libraries.
* The **Windows launcher / UI is not finished**: the project does not build yet and no app shows a UI.
* The first realistic milestone is a **small Java-only app** (one `Activity`, a `TextView` and a `Button`).
  See "What is left".

## Measured facts

| Metric | Value |
| ------ | ----- |
| Runtime code (interpreter, VM, class library) | ~5,800 lines |
| Registered native methods (java.* / android.* without UI) | 1,428 |
| Real DEX loaded in the harness | 15 files, ~1 s to parse |
| Invocations executed from a real app before stopping | 18,867 (1,680 distinct methods) |
| Framework calls without an implementation (stubbed) in that run | 153 unresolved invocations, ~110 distinct methods (`logs/native_bridge_missing.log`) |
| Resource table parsed from the same APK | 78,997 entries; layouts resolved through obfuscated paths |
| Dalvik opcodes covered | All, except `invoke-polymorphic/custom` (throw `UnsupportedOperationException`) |
| Verified on Windows / MSVC | **Nothing yet**: the code has only been built with GCC on Linux |

## Status by component

| Component | State | Notes |
| --------- | ----- | ----- |
| DEX loader (multi-dex) | Done | |
| Bytecode interpreter | Done | Every opcode format; exceptions, `<clinit>`, monitors, threads |
| Class model / dispatch | Done | virtual/interface/super/static/direct by (class, name, proto); inline caches |
| Native registry + stub-on-miss | Done | Extensible, no per-app code |
| java.lang / java.util / concurrency / reflection | Core done | Gaps listed in `ROADMAP.md` |
| Android non-visual APIs | Basic | Context, Application, Activity (lifecycle no-ops), Intent, Bundle, Handler/Looper, SharedPreferences, Log |
| `resources.arsc` + binary XML | Done | Layout ids resolve; strings/colors/dimensions decoded |
| Layout inflater | Written, **not compiled** | |
| Android UI natives (View, TextView, Button, ...) | **Not written yet** | |
| Activity manager / lifecycle in launcher | **In progress** | Must create the real `Application` and Activity objects |
| Input (click, hit-testing) | **Missing** | |
| Garbage collector | Not implemented | Objects are never freed |
| JNI / native libraries | Not supported | Out of scope for now |

## What is left to run a simple app

Target: a Java-only app with one plain `Activity` (no AppCompat/Material), a `TextView`, a `Button`
and a click listener that changes the text.

| Step | Work | Size (estimate) | Risk |
| ---- | ---- | --------------- | ---- |
| 1. Finish the launcher layer | UI natives (~1,000 lines), `ActivityManager` lifecycle (~300), `main.cpp`/CMake (~100) | 1-2 focused sessions | Medium |
| 2. Build on MSVC | Nothing has been compiled with MSVC; expect several rounds of compiler errors | 2-5 rounds | **High** |
| 3. Input | click listener on `View`, hit-testing and coordinate translation in `ViewGroup` (~100 lines) | < 1 session | Low |
| 4. First real app | Run a tiny APK, read `native_bridge_missing.log`, implement what it asks for | 1-3 sessions | Medium (unknown APIs) |

Overall estimate for "a simple Java app shows its window and reacts to a click": **roughly 4-8 focused
work sessions plus the MSVC feedback loop**. It is a range, not a promise; steps 2 and 4 are the unknowns.

### What is not close

* **AppCompat / Material / Jetpack apps** (themes, styles, `obtainStyledAttributes`, `RecyclerView`,
  vector drawables, fonts, text layout): a large UI-toolkit effort, weeks rather than days.
* **Apps with native libraries** (large social apps, games, Flutter, React Native): need JNI plus ARM
  emulation or Windows ports of those libraries. Not planned.
* **Performance**: the interpreter is a plain switch loop, not profiled; with no GC, memory grows.

## Reproducing the numbers

```
tools/linux_harness/build_and_run.sh <Activity-descriptor> [<Application-descriptor>|-] [debug]
```

See `BUILD.md`.
