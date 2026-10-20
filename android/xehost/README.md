# xehost (P2 Android host)

Android library module providing a GLES 2.0 `XeView`, immersive `XeActivity`, JNI bridge, APK asset reader, and Kotlin callbacks. The manifest includes `XeDemoActivity` as a launchable smoke test; in a consuming app, remove that launcher filter if another activity should be the entry point.

## Requirements
- Android Gradle Plugin 8.x, Kotlin Android plugin, Android SDK platform 35, NDK, and CMake 3.22.1.
- Add `include(":xehost")` to the consuming project's `settings.gradle.kts` and use `implementation(project(":xehost"))`.
- The module points externalNativeBuild at the repository root `CMakeLists.txt` and requests `armeabi-v7a` and `arm64-v8a`.

## Demo
`XeDemoActivity` uses the native demo callbacks in `xe_demo.c`. D-pad left/up and right/down change the clear colour. For a real app, set `-DXE_ANDROID_DEMO=OFF` and provide the `xe_app_init`, `xe_app_frame`, and `xe_app_shutdown` symbols from the application target/source. The Android JNI library still needs to link the app implementation.

## Native APIs
`xe_asset_read(path, &data, &len)` returns 1 on success. Returned memory is allocated by `malloc`; caller must `free(data)`. `xe_android_call_void(name, "(Ljava/lang/String;)V", text)` calls a static `XeBridge` method with one string argument. `xe_platform_wants_frames()` controls whether `XeView` queues another frame after each native frame.
