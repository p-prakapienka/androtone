# Android build

This directory is a Gradle wrapper that drives the root `CMakeLists.txt` through Android Studio's
`externalNativeBuild`.

> Scope: this scaffold gets you to a configured Gradle project that compiles Androtone's native code
> into an `.so` per ABI. Producing a runnable APK additionally requires JUCE's generated Java
> activity sources and a matching `<activity>` entry in `AndroidManifest.xml` — see "First build"
> below for the manual steps.

## Prerequisites

- Android Studio (Hedgehog or newer recommended)
- Android NDK r26+ (the `ndkVersion` in `app/build.gradle.kts` pins r26.1.10909125)
- CMake 3.22.1+ available to Gradle (Android Studio's SDK Manager → SDK Tools → CMake)
- JDK 17

## First build

1. From this directory, run `gradle wrapper` once to generate `gradlew` / `gradlew.bat`. (Not
   committed — bootstrap on your machine.)
2. Open `platform/android` in Android Studio. Let it sync; this triggers the CMake configure that
   downloads JUCE 8.0.12 via CPM and emits the Java sources for the Standalone activity into the
   build tree.
3. Add the JUCE-generated activity class to `AndroidManifest.xml` (Android Studio's lint will name
   it after the first configure). The activity typically lives under
   `com.rmsl.juce` or `com.restrictedspace.androtone` depending on JUCE's output naming.
4. Build → Make Project, then Run on a connected device.

## Notes

- The native target name is `Androtone_Standalone` (set in `defaultConfig.externalNativeBuild`).
- ABIs default to `arm64-v8a`, `armeabi-v7a`, `x86_64`. Trim the list in `app/build.gradle.kts` if
  you only need one.
- The root `CMakeLists.txt` excludes VST3 from the format list when `CMAKE_SYSTEM_NAME=Android`.
