# Lye

A clean-room recreation of Caustic 3 — a rack-based synth and sequencer — for Android, built with
JUCE. Lye is separate from Androtone, the other project in this repository.

Status: Phase 0 (engine skeleton, test harness, Android export). See `docs/roadmap.md`.

## Build (desktop)

Needs CMake 3.22+, a C++17 compiler, and network access on first configure (JUCE and Catch2 are
fetched by CPM).

```sh
cmake -S lye -B lye/build
cmake --build lye/build --target Lye
```

On Linux the app needs `libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev
libxext-dev libfreetype-dev libfontconfig1-dev libgl-dev`. The tests need none of these.

## Tests

```sh
cmake --build lye/build --target LyeTests
ctest --test-dir lye/build --output-on-failure
```

## Build (Android)

Configure the desktop build once (it fetches JUCE into `lye/external/JUCE`, which the Android
project references), then open `lye/Builds/Android` in Android Studio or run
`./gradlew assembleDebug` there. The Android project is generated from `Lye.jucer` by Projucer.
