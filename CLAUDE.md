# Claude project notes

## Project shape

Androtone is a JUCE 8.0.12 audio plugin (synth). CMake is the build system. CPM fetches JUCE at
configure time — no vendored sources.

Targets:

- VST3 — desktop hosts (Windows / macOS / Linux)
- Standalone — desktop
- Standalone — Android (via Gradle + NDK wrapper in `Builds/android/`)

## Layout

- `CMakeLists.txt` — root; CPM bootstrap, JUCE fetch, `juce_add_plugin`
- `source/` — plugin C++ (`PluginProcessor.{h,cpp}`, `PluginEditor.{h,cpp}`)
- `Builds/android/` — Gradle wrapper invoking the root CMake via `externalNativeBuild`
- `docs/` — product requirements, design notes, and any Claude design files. **Read everything in
  `docs/` before making non-trivial changes — it is the canonical source of intent for this
  project.**

## Build (desktop)

```
cmake -S . -B build
cmake --build build --config Release
```

VST3 lands in `build/Androtone_artefacts/Release/VST3/`; the standalone executable in
`build/Androtone_artefacts/Release/Standalone/`.

## Build (Android)

See `Builds/android/README.md`.
