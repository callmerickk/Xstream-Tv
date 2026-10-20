package com.xe.xehost

import android.app.Activity
import android.os.Handler
import android.os.Looper
import android.view.View
import android.widget.FrameLayout

object PlayerBridge {
    private val main = Handler(Looper.getMainLooper())
    @Volatile private var instance: XePlayer? = null

    @JvmStatic fun install(activity: Activity, root: FrameLayout, glView: View, nativeHandle: Long) {
        main.post {
            instance?.close()
            instance = XePlayer(activity.applicationContext, root, glView).also { it.attachNative(nativeHandle) }
        }
    }
    @JvmStatic fun open(url: String, headers: Array<String>, startSec: Double) {
        main.post { instance?.open(url, headers, startSec) }
    }
    @JvmStatic fun toggle() { main.post { instance?.toggle() } }
    @JvmStatic fun seek(seconds: Double) { main.post { instance?.seek(seconds) } }
    @JvmStatic fun close() { main.post { instance?.close() } }
    @JvmStatic fun select(kind: Int, index: Int) { main.post { instance?.selectTrack(kind, index) } }
    @JvmStatic fun requestPreview(seconds: Double, bucket: Int, gen: Int) { main.post { instance?.requestPreview(seconds, bucket, gen) } }
    @JvmStatic fun tracks(kind: Int): Array<String> = instance?.trackNames(kind)?.toTypedArray() ?: emptyArray()
}
