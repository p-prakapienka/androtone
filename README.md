# Riffbook

An Android sketchpad for musical ideas: a grid of clips and scenes, drum and melodic pads,
capture of whatever you just played, a touch MIDI editor, synths, a drum kit, a sampler with
microphone recording, effects, and audio export.

It reimplements the workflow of Ableton Note from its public documentation (clean room; no
Ableton code, assets or presets). See `docs/spec.md` for the feature list.

Built with JUCE 8 in C++.

## Building

Desktop (development and tests):

```sh
git clone --depth 1 --branch 8.0.12 https://github.com/juce-framework/JUCE.git JUCE
cmake -S . -B build -G Ninja -DRB_JUCE_DIR=JUCE
cmake --build build --target Riffbook RiffbookTests
./build/RiffbookTests_artefacts/Debug/RiffbookTests
```

Android: the GitHub Actions workflow builds a debug APK on every push (download it from the run's
artifacts). Locally: build Projucer from `JUCE/extras/Projucer`, run
`Projucer --resave Riffbook.jucer`, then open `Builds/Android` in Android Studio or run
`./gradlew assembleDebug` there.

## Licence note

JUCE is dual-licensed (AGPLv3 or commercial). Distributing Riffbook requires either releasing it
under the AGPLv3 or holding a JUCE licence.
