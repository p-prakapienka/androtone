# Androtone

A cross-platform JUCE 8 synthesizer plugin. Built with CMake; JUCE is fetched via CPM at configure
time.

## Targets

| Platform | Format     | Notes                                       |
| -------- | ---------- | ------------------------------------------- |
| Desktop  | VST3       | Windows / macOS / Linux                     |
| Desktop  | Standalone | `.exe` / `.app` / binary                    |
| Android  | Standalone | APK, built via Gradle wrapper in `platform/android/` |

## Prerequisites

- CMake 3.22+
- A C++17 toolchain (MSVC / Clang / GCC)
- Network access on first configure (CPM downloads JUCE)

Android also needs:

- Android Studio + Android NDK r26+
- See [`platform/android/README.md`](platform/android/README.md)

## Build (desktop)

```sh
cmake -S . -B build
cmake --build build --config Release
```

Artifacts:

- `build/Androtone_artefacts/Release/VST3/Androtone.vst3`
- `build/Androtone_artefacts/Release/Standalone/Androtone[.exe|.app]`

## Build (Android)

See [`platform/android/README.md`](platform/android/README.md).

## Repo layout

```
.
├── CMakeLists.txt
├── CLAUDE.md
├── docs/                       # requirements & design notes
├── source/                     # plugin C++
│   ├── PluginEditor.{h,cpp}
│   └── PluginProcessor.{h,cpp}
└── platform/android/           # Gradle wrapper for Android NDK build
```

## Notes on JUCE version

`CMakeLists.txt` pins JUCE 8.0.12 via CPM. If that tag is not available on
`juce-framework/JUCE`, bump `GIT_TAG` / `VERSION` to the current 8.0.x patch.

## Plugin identity

| Field                    | Value             |
| ------------------------ | ----------------- |
| Product name             | Androtone         |
| Company                  | Restricted Space  |
| Plugin code              | Antn              |
| Manufacturer code        | RstS              |
| Bundle ID                | com.restrictedspace.androtone |
