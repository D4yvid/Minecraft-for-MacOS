package io.github.d4yvid.mcfm

import android.app.Activity
import android.view.KeyEvent
import android.window.OnBackInvokedCallback
import android.window.OnBackInvokedDispatcher

/**
 * Android 13+ (API 33): back is a gesture with a callback (from Android 16 it never reaches
 * onKeyDown); here it is the game's Escape. Its own class: on older Android the
 * OnBackInvokedCallback type does not exist, and a class naming it fails verification.
 */
class BackGesture(private val activity: Activity) {
    private val callback = OnBackInvokedCallback {
        Native.nativeKey(KeyEvent.KEYCODE_BACK, true)
        Native.nativeKey(KeyEvent.KEYCODE_BACK, false)
    }

    init {
        activity.onBackInvokedDispatcher.registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT, callback)
    }

    fun unregister() = activity.onBackInvokedDispatcher.unregisterOnBackInvokedCallback(callback)
}
