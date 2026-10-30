plugins {
    id("com.android.application")
}

// The showcase on Android. The native code (the library and the showcase's sources, built by the root
// CMakeLists.txt as libmain.so), Kompoz's Java side (platform/android/java) and the bundled fonts
// come from the repository root, shared with the desktop build. Run scripts/fetch-deps.sh once before
// building.
val kompozRoot: File = rootProject.file("../../..")
val exampleDir: File = rootProject.file("..")

android {
    namespace = "dev.kompoz.showcase"
    compileSdk {
        version = release(36)
    }
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "dev.kompoz.showcase"
        minSdk = 24
        targetSdk = 36
        versionCode = 2
        versionName = "0.1.1"
        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")  // phones + emulator
        }
        externalNativeBuild {
            cmake {
                // Optimized native code in debug builds too: -O0 makes every frame several times
                // slower (scrolling stutters); RelWithDebInfo keeps the symbols for debugging.
                arguments += listOf("-DANDROID_STL=c++_static", "-DCMAKE_BUILD_TYPE=RelWithDebInfo")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = File(kompozRoot, "CMakeLists.txt")
            version = "4.1.2"
        }
    }

    sourceSets["main"].java.srcDir(File(kompozRoot, "platform/android/java"))
    sourceSets["main"].assets.srcDir(File(kompozRoot, "assets"))  // the library's fonts
    sourceSets["main"].assets.srcDir(File(exampleDir, "assets"))  // the showcase's images

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
