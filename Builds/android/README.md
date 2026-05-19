# Android build

This directory is a Gradle wrapper that drives the root `CMakeLists.txt` through Android Studio's
`externalNativeBuild`. The root CMake copies the JUCE Java sources (`JuceApp`, `JuceActivity`,
`Receiver`, …) into `app/build/generated/juce-java/`, which is wired into the Gradle `main`
source set so the manifest's `com.rmsl.juce.*` class references resolve at install time.

## Prerequisites

- Android Studio (Hedgehog or newer recommended)
- Android NDK 26.1.10909125 (pinned by `ndkVersion` in `app/build.gradle.kts`)
- CMake 3.22.1+ available to Gradle (Android Studio's SDK Manager → SDK Tools → CMake)
- JDK 17
- A `~/.android/debug.keystore` (Android Studio creates this on first run; required by the
  `juceSigning` signingConfig in `app/build.gradle.kts`)
- A host C/C++ compiler on PATH (Visual Studio on Windows). JUCE cross-compiles a host helper
  (`juceaide`) during CMake configure; without it, configure aborts with
  `No CMAKE_C_COMPILER could be found`. Android Studio sets this up automatically — for CLI
  builds, run `gradlew.bat` from a Visual Studio Developer PowerShell.

## First build

1. Open `Builds/android` in Android Studio. Let it sync — Gradle invokes the root CMake, which
   uses CPM to fetch JUCE 8.0.12 into the per-ABI build tree and populates the generated Java
   source dir referenced above.
2. Build → Make Project, then Run on a connected device.

## Notes

- The native target is `Androtone_Standalone` (set in `defaultConfig.externalNativeBuild`).
- Builds target `arm64-v8a` only — add `armeabi-v7a` / `x86_64` to `abiFilters` in
  `app/build.gradle.kts` if you need broader coverage.
- `minSdk` is 29; `compileSdk` / `targetSdk` are 35.
- The root `CMakeLists.txt` excludes VST3 from the format list when `CMAKE_SYSTEM_NAME=Android`.
- NDK-toolchain flags (`ANDROID_CPP_FEATURES`, `ANDROID_ARM_MODE`, `ANDROID_ARM_NEON`,
  `ANDROID_WEAK_API_DEFS`) are passed through `externalNativeBuild.cmake.arguments` —
  JUCE's `juce_recommended_*` helpers don't set these.

## Release signing

`app/build.gradle.kts` reads release-signing credentials from `local.properties` (gitignored) or
matching environment variables (for CI). If none are set, **release builds fall back to the debug
keystore** so the APK is installable for testing — but it must NOT be published to Google Play.

Generate a release keystore once:

```sh
keytool -genkey -keystore ~/.android/androtone-release.jks -alias androtone \
        -keyalg RSA -keysize 2048 -validity 10000
```

Then add to `Builds/android/local.properties` (gitignored — never commit this):

```properties
release.storeFile=C\:/Users/you/.android/androtone-release.jks
release.storePassword=...
release.keyAlias=androtone
release.keyPassword=...
```

CI alternative — set these env vars instead:
`RELEASE_STORE_FILE`, `RELEASE_STORE_PASSWORD`, `RELEASE_KEY_ALIAS`, `RELEASE_KEY_PASSWORD`.

Build with `gradlew.bat :app:assembleRelease`. The APK lands in
`app/build/outputs/apk/release/app-release.apk`.
