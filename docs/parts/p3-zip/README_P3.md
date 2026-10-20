# XE P3 — 2D renderer

P3 adds `include/xe/render.h`, `src/render/render_gl.c`, and `tests/p3_test.c` to the P2 repository.

## Build the Windows stress test

Use an SDL2 development package discoverable by CMake, then run from the repository root:

```powershell
cmake -S . -B build -DXE_BUILD_P3_TEST=ON -DXE_BUILD_P1_TEST=OFF
cmake --build build --config Release
```

Run `build/Release/xe_win.exe`. The test draws 200 gradient cards with shell glows and border segments; it animates a progress border and logs measured FPS once per second. Arrow keys change the palette.

## API and threading

The public API is `include/xe/render.h`. Initialize and call the renderer only while a current GL context exists. On Android, all renderer calls should run on the `GLSurfaceView` render thread. The desktop path uses GL 3.3 core shader source; Android uses GLES 2.0 shader source. Both shader paths use the same SDF rounded-rectangle logic, with a small platform-specific shader prelude.

## Implementation scope

The renderer batches up to 4096 quads per flush and flushes on texture changes. It uses a common vertex format and premultiplied-alpha blending. It is a foundation implementation, not yet the fully optimized final StreamX renderer: `xe_r_image_blurred` is currently an unblurred fallback, rounded clipping currently uses rectangular scissor intersections, glow is made from translucent shells rather than a blur pass, and arbitrary-angle lines are not yet supported accurately. `xe_r_border` and progress borders are batched edge rectangles. These gaps are called out rather than represented as completed features.

The default non-Windows/non-Android build uses a no-GL stub so common CI environments without OpenGL development headers can still build `libxe.a`; that stub is not a rendering backend. Android Gradle should pass `-DXE_GLES=ON`, as configured in the P2 module.
