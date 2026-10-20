# P3 renderer notes

`render_gl.c` is a self-contained batched quad renderer for the P3 API. It uses a common vertex format, premultiplied-alpha blending, an SDF rounded-rectangle fragment shader, a 4096-quad CPU batch, and texture switches as batch boundaries. The shader sources are separate ES 2.0 and desktop GL 3.3 strings with the same shader logic and `XE_DESKTOP_GL` prelude define.

Important implementation status:

- Rounded fills and images use SDF edge coverage; GLES 2.0 enables `GL_OES_standard_derivatives` for `fwidth`.
- Gradient colors are vertex interpolated.
- Clips currently use nested rectangular GL scissor intersections; the radius argument is reserved and does not yet produce rounded clip masks.
- `xe_r_border` and `xe_r_border_progress` are composed from batched rectangles rather than a dedicated SDF border primitive.
- `xe_r_glow` uses several translucent expanded shells, not a blur pass.
- `xe_r_image_blurred` currently falls back to the unblurred image. The cached downsample + dual-Kawase implementation is a known follow-up, not silently claimed as implemented.
- `xe_r_line` currently emits an axis-aligned approximation; arbitrary-angle line geometry is a follow-up.
- Renderer and texture APIs must be called on the thread where the current GL context is current (Android GLSurfaceView render thread).
- Desktop GL function loading is currently implemented for Windows/WGL; other desktop targets use system GL prototypes when their headers provide them.

The stress test draws 200 cards with gradients, shell glows, borders, and an animated progress border; once per second it prints the measured frame rate.
