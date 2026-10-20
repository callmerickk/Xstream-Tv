package com.xe.xehost

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.view.inputmethod.InputMethodManager
import android.content.Context
import android.widget.Toast
import android.view.View
import android.content.ClipData

/** Java-callable callbacks used by native code. */
object XeBridge {
    @Volatile private var activity: Activity? = null
    @Volatile private var focusedView: View? = null

    @JvmStatic fun attach(host: Activity) { activity = host; focusedView = host.currentFocus }
    @JvmStatic fun detach() { activity = null; focusedView = null }

    @JvmStatic fun openUrlInBrowser(url: String) {
        activity?.runOnUiThread { runCatching { activity?.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url))) } }
    }

    @JvmStatic fun showKeyboard(initialText: String) {
        activity?.runOnUiThread {
            val host = activity ?: return@runOnUiThread
            val view = host.currentFocus ?: host.window.decorView
            focusedView = view
            view.requestFocus()
            (host.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).showSoftInput(view, InputMethodManager.SHOW_IMPLICIT)
        }
    }

    @JvmStatic fun hideKeyboard() {
        activity?.runOnUiThread {
            val host = activity ?: return@runOnUiThread
            val view = host.currentFocus ?: host.window.decorView
            (host.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).hideSoftInputFromWindow(view.windowToken, 0)
        }
    }

    @JvmStatic fun toast(message: String) {
        activity?.runOnUiThread { Toast.makeText(activity, message, Toast.LENGTH_SHORT).show() }
    }
}
