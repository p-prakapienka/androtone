# Androtone agent guide

## Project

Androtone is a C++17 synthesizer built with JUCE 8.0.12. It produces VST3 and
Standalone desktop targets and an Android Standalone app.

Before making non-trivial changes, read `README.md` and every relevant file in
`docs/`. Preserve existing behaviour unless the task explicitly changes it.

## Repository layout

- `source/` contains the application and plugin code.
- `source/Engine/SoundSources/` contains synth voices and sounds.
- `source/Engine/Processors/` contains audio effects implementing `Processor`.
- `source/Sequencer/`, `source/Mixer/`, `source/Project/`, and `source/Ui/`
  contain their respective subsystems.
- `Builds/desktop/CMakeLists.txt` is the hand-maintained desktop build.
- `androtone.jucer` defines the Projucer project and Android export.
- `Builds/android/` is Projucer-generated. Do not hand-edit generated files
  unless the task specifically requires it; prefer changing `androtone.jucer`
  and regenerating them.
- `JuceLibraryCode/` is generated. Do not edit it manually.

When adding source files, keep `androtone.jucer` and
`Builds/desktop/CMakeLists.txt` in sync so desktop and Android builds include
the same code.

## Build and validation

Desktop release build:

```sh
cmake -S Builds/desktop -B Builds/desktop/cmake-build-release-visual-studio
cmake --build Builds/desktop/cmake-build-release-visual-studio --config Release
```

Android release build:

```sh
Builds/android/gradlew assembleRelease
```

The Android export currently references JUCE inside the desktop debug build
tree, so configure that desktop build first when required. Run the checks most
relevant to the changed area. If a required build or test cannot run, report
the exact command, error, and unvalidated scope.

## C++ and audio rules

- Follow the surrounding naming, formatting, ownership, and header-only style.
- Keep changes focused; do not refactor unrelated code.
- Treat `processBlock()` and every `Processor::process()` call as real-time
  code: do not allocate, lock, perform file or network I/O, or call UI code.
- Allocate buffers and initialize DSP state in `prepare()` or
  `prepareToPlay()`; clear state in `reset()` or `releaseResources()`.
- Handle sample-rate changes explicitly. Clamp parameter ranges and avoid
  clicks for parameters that need smoothing.
- Parameters shared between UI and audio threads must be safe to access
  without blocking the audio thread.
- Preserve channel bounds, avoid denormals, and verify DSP output remains
  finite for silence and extreme parameter values.
- UI state must not become the source of truth for engine state.

## Git workflow

- Start work from the requested base branch, normally `master`.
- Use `feature/<name>` for features, `fix/<name>` for fixes, and
  `chore/<name>` for maintenance unless the user requests another name.
- Match the concise, imperative commit-message style in the repository.
- Do not stage, overwrite, or revert unrelated user changes.

## Definition of done

- The requested behaviour is implemented without unrelated changes.
- Desktop and Android project definitions remain consistent when files change.
- Relevant builds or tests pass, or any validation gap is reported clearly.
- The final summary lists changed behaviour, validation performed, and known
  limitations.
