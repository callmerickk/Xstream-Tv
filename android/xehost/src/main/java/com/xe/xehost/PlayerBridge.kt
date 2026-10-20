package com.xe.xehost

import android.app.Activity
import android.widget.FrameLayout
import android.view.View

/** Instantiate from P2 Activity after the root FrameLayout and XeView exist. */
object PlayerBridge {
    @Volatile private var instance: XePlayer? = null
    @JvmStatic fun install(activity: Activity, root: FrameLayout, glView: View, nativeHandle: Long) {
        activity.runOnUiThread {
            instance?.close()
            instance = XePlayer(activity, root, glView).also { it.attachNative(nativeHandle) }
        }
    }
    @JvmStatic fun open(url: String, headers: Array<String>, startSec: Double) = instance?.open(url, headers, startSec)
    @JvmStatic fun toggle() = instance?.toggle()
    @JvmStatic fun seek(seconds: Double) = instance?.seek(seconds)
    @JvmStatic fun close() = instance?.close()
    @JvmStatic fun tracks(kind: Int): Array<String> = instance?.trackNames(kind)?.toTypedArray() ?: emptyArray()
    @JvmStatic fun select(kind: Int, index: Int) = instance?.selectTrack(kind, index)
}
