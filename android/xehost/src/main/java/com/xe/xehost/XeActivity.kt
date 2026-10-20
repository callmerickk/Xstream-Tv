package com.xe.xehost

import android.os.Bundle
import android.graphics.Color
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.FrameLayout

open class XeActivity : android.app.Activity() {
    protected lateinit var xeView: XeView
    private lateinit var root: FrameLayout
    private var playerInstalled = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        enterImmersive()
        root = FrameLayout(this).apply { setBackgroundColor(Color.BLACK) }
        xeView = XeView(this)
        root.addView(xeView, FrameLayout.LayoutParams(-1, -1))
        setContentView(root)
        xeView.setFocusableInTouchMode(true); xeView.requestFocus()
        XeBridge.attach(this)
        xeView.queueEvent {
            nativeInit(filesDir.absolutePath, cacheDir.absolutePath, window.decorView.width.coerceAtLeast(1),
                window.decorView.height.coerceAtLeast(1), resources.displayMetrics.density, assets)
            runOnUiThread {
                PlayerBridge.install(this, root, xeView, 1L)
                playerInstalled = true
            }
        }
    }
    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus); if (hasFocus) enterImmersive()
    }
    private fun enterImmersive() {
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            or View.SYSTEM_UI_FLAG_FULLSCREEN or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            or View.SYSTEM_UI_FLAG_LAYOUT_STABLE)
    }
    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        val key = mapKey(keyCode) ?: return super.onKeyDown(keyCode, event)
        xeView.dispatchNativeKey(1, key, event.repeatCount); return true
    }
    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        val key = mapKey(keyCode) ?: return super.onKeyUp(keyCode, event)
        xeView.dispatchNativeKey(2, key, event.repeatCount); return true
    }
    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        if (event.source and InputDevice.SOURCE_CLASS_POINTER != 0 && event.action == MotionEvent.ACTION_SCROLL) {
            xeView.dispatchNativePointer(7, event.getAxisValue(MotionEvent.AXIS_HSCROLL), event.getAxisValue(MotionEvent.AXIS_VSCROLL)); return true
        }
        return super.dispatchGenericMotionEvent(event)
    }
    override fun onTouchEvent(event: MotionEvent): Boolean {
        val type = when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> 4; MotionEvent.ACTION_MOVE -> 5
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> 6
            else -> return super.onTouchEvent(event)
        }
        xeView.dispatchNativePointer(type, event.x, event.y); return true
    }
    override fun onPause() { if (::xeView.isInitialized) { xeView.dispatchNativeLifecycle(9); xeView.onPause() }; super.onPause() }
    override fun onResume() { super.onResume(); if (::xeView.isInitialized) { xeView.onResume(); xeView.dispatchNativeLifecycle(10) }; enterImmersive() }
    override fun onDestroy() {
        if (::xeView.isInitialized) xeView.queueEvent { nativeShutdown() }
        PlayerBridge.close(); XeBridge.detach(); super.onDestroy()
    }
    private fun mapKey(code: Int): Int? = when (code) {
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_BUTTON_THUMBL -> 1
        KeyEvent.KEYCODE_DPAD_DOWN -> 2
        KeyEvent.KEYCODE_DPAD_LEFT -> 3
        KeyEvent.KEYCODE_DPAD_RIGHT -> 4
        KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> 5
        KeyEvent.KEYCODE_BACK, KeyEvent.KEYCODE_ESCAPE -> 6
        KeyEvent.KEYCODE_MENU -> 7
        KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE, KeyEvent.KEYCODE_BUTTON_START -> 8
        KeyEvent.KEYCODE_MEDIA_FAST_FORWARD -> 9
        KeyEvent.KEYCODE_MEDIA_REWIND -> 10
        KeyEvent.KEYCODE_SEARCH -> 11
        KeyEvent.KEYCODE_DEL -> 12
        else -> null
    }
    private external fun nativeInit(dataDir: String, cacheDir: String, width: Int, height: Int, density: Float, assets: android.content.res.AssetManager)
    private external fun nativeShutdown()
}
