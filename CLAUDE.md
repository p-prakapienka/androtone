# Claude project notes

Riffbook: an Android music sketchpad, a clean-room reimplementation of the Ableton Note workflow,
built with JUCE 8.0.12 (C++17). This branch (`note-clone`) is unrelated to the androtone project
that lives on other branches of the same repository.

- `docs/spec.md` — what the app does, with requirement IDs. Canonical source of intent.
- `docs/architecture.md` — threads, modules, data flow, how to add files.

## Rules

- Clean room: never use Ableton names, assets, presets or text in code or UI. Factory sounds are
  synthesised in code.
- The audio thread never allocates, locks or frees: everything it reads comes from an
  `EngineSnapshot`; communication is through lock-free FIFOs and atomics.
- New source files go into the module amalgamation `.cpp` files (see architecture doc); never
  edit the `.jucer` file list or CMake for them.

## Build and test (Linux/macOS)

```
git clone --depth 1 --branch 8.0.12 https://github.com/juce-framework/JUCE.git JUCE
cmake -S . -B build -G Ninja -DRB_JUCE_DIR=JUCE
cmake --build build --target RiffbookTests Riffbook
./build/RiffbookTests_artefacts/Debug/RiffbookTests          # all tests
./build/RiffbookTests_artefacts/Debug/RiffbookTests engine   # one category
```

Android APKs are built by `.github/workflows/build.yml` (Projucer `--resave` + Gradle
`assembleRelease`). Never ship or test audio with the debug APK: it is built with `-O0`.
