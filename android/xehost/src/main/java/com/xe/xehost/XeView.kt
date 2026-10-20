package com.xe.xehost

import android.content.Context
import android.opengl.GLSurfaceView
import android.util.AttributeSet
import android.view.SurfaceHolder
import android.view.MotionEvent
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

/** GLES 2.0 view. Continuous rendering is enabled only while native code requests frames. */
class XeView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null
) : GLSurfaceView(context, attrs), GLSurfaceView.Renderer {
    private var lastFrameNanos = 0L

    init {
        setEGLContextClientVersion(2)
        setEGLConfigChooser(8, 8, 8, 8, 0, 0)
        preserveEGLContextOnPause = true
        setRenderer(this)
        renderMode = RENDERMODE_WHEN_DIRTY
    }

    fun requestRenderFromApp() = requestRender()

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        lastFrameNanos = System.nanoTime()
        nativeResize(width, height, resources.displayMetrics.density)
        requestRender()
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        nativeResize(width, height, resources.displayMetrics.density)
        requestRender()
    }

    override fun onDrawFrame(gl: GL10?) {
        val now = System.nanoTime()
        val dt = if (lastFrameNanos == 0L) 0.0 else (now - lastFrameNanos) / 1_000_000_000.0
        lastFrameNanos = now
        nativeFrame(dt)
        if (nativeWantsFrames()) {
            // Queue the next frame from the GL thread; no extra worker or busy UI loop.
            requestRender()
        }
    }

    fun dispatchNativeKey(type: Int, key: Int, repeat: Int) {
        queueEvent { nativeKey(type, key, repeat) }
        requestRender()
    }

    fun dispatchNativePointer(type: Int, x: Float, y: Float) {
        queueEvent { nativePointer(type, x, y) }
        requestRender()
    }

    fun dispatchNativeLifecycle(type: Int) {
        queueEvent { nativeKey(type, 0, 0) }
        requestRender()
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val type = when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> 4
            MotionEvent.ACTION_MOVE -> 5
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> 6
            else -> return super.onTouchEvent(event)
        }
        dispatchNativePointer(type, event.x, event.y)
        return true
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        super.surfaceDestroyed(holder)
    }

    companion object { init { System.loadLibrary("xe_android") } }

    private external fun nativeFrame(dt: Double)
    private external fun nativeResize(width: Int, height: Int, density: Float)
    private external fun nativeKey(type: Int, key: Int, repeat: Int)
    private external fun nativePointer(type: Int, x: Float, y: Float)
    private external fun nativeWantsFrames(): Boolean
}
