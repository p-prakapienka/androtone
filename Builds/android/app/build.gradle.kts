import java.util.Properties

plugins {
    id("com.android.application")
}

val localProperties = Properties().apply {
    val f = rootProject.file("local.properties")
    if (f.exists()) f.inputStream().use { stream -> load(stream) }
}

fun localOrEnv(key: String, env: String): String? =
    localProperties.getProperty(key) ?: System.getenv(env)

val releaseStoreFile = localOrEnv("release.storeFile", "RELEASE_STORE_FILE")
val releaseStorePassword = localOrEnv("release.storePassword", "RELEASE_STORE_PASSWORD")
val releaseKeyAlias = localOrEnv("release.keyAlias", "RELEASE_KEY_ALIAS")
val releaseKeyPassword = localOrEnv("release.keyPassword", "RELEASE_KEY_PASSWORD")

val hasReleaseSigning = listOf(
    releaseStoreFile, releaseStorePassword, releaseKeyAlias, releaseKeyPassword,
).all { it != null }

android {
    namespace = "com.restrictedspace.androtone"
    compileSdk = 35
    ndkVersion = "26.1.10909125"

    defaultConfig {
        applicationId = "com.restrictedspace.androtone"
        minSdk = 29
        targetSdk = 35
        versionCode = 1
        versionName = "0.0.1"

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DANDROID_CPP_FEATURES=exceptions rtti",
                    "-DANDROID_ARM_MODE=arm",
                    "-DANDROID_ARM_NEON=TRUE",
                    "-DANDROID_WEAK_API_DEFS=ON"
                )
                targets += "Androtone_Standalone"
            }
        }

        ndk {
            abiFilters += listOf("arm64-v8a")
        }
    }

    signingConfigs {
        create("juceSigning") {
            storeFile = file("${System.getProperty("user.home")}/.android/debug.keystore")
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
            storeType = "jks"
        }
        if (hasReleaseSigning) {
            create("release") {
                storeFile = file(releaseStoreFile!!)
                storePassword = releaseStorePassword
                keyAlias = releaseKeyAlias
                keyPassword = releaseKeyPassword
            }
        }
    }

    buildTypes {
        debug {
            signingConfig = signingConfigs.getByName("juceSigning")
            externalNativeBuild {
                cmake {
                    arguments += "-DCMAKE_BUILD_TYPE=Debug"
                }
            }
        }
        release {
            signingConfig = signingConfigs.getByName(
                if (hasReleaseSigning) "release" else "juceSigning"
            )
            externalNativeBuild {
                cmake {
                    arguments += "-DCMAKE_BUILD_TYPE=Release"
                }
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    sourceSets {
        getByName("main") {
            java.srcDirs("build/generated/juce-java")
        }
    }
}

afterEvaluate {
    listOf("Debug", "Release").forEach { buildType ->
        tasks.matching { it.name == "compile${buildType}JavaWithJavac" }
            .configureEach {
                dependsOn(tasks.matching { it.name == "configureCMake$buildType" })
            }
    }
}
