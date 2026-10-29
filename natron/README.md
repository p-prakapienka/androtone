# Natron

_Natron is a natural soda-ash mineral — a nod to Caustic without borrowing its name._

A clean-room recreation of Caustic 3 — a rack-based synth and sequencer — for Android, built with
JUCE. Natron is separate from Androtone, the other project in this repository.

Status: Phase 3 done — song timeline, piano roll / step editor, insert effects, WAV export. See `docs/roadmap.md`.

## Build (desktop)

Needs CMake 3.22+, a C++17 compiler, and network access on first configure (JUCE and Catch2 are
fetched by CPM).

```sh
cmake -S natron -B natron/build
cmake --build natron/build --target Natron
```

On Linux, configuring needs `libasound2-dev libx11-dev libxrandr-dev libxinerama-dev
libxcursor-dev libxext-dev libfreetype-dev libfontconfig1-dev libgl-dev` (JUCE builds its juceaide
helper at configure time), even if you only build the tests.

## Tests

```sh
cmake --build natron/build --target NatronTests
ctest --test-dir natron/build --output-on-failure
```

## Build (Android)

Configure the desktop build once (it fetches JUCE into `natron/external/JUCE`, which the Android
project references), then open `natron/Builds/Android` in Android Studio or run
`./gradlew assembleDebug` there. The Android project is generated from `Natron.jucer` by Projucer.
