package io.github.d4yvid.mcfm

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.net.wifi.WifiManager
import android.os.Build
import android.os.Bundle
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.view.inputmethod.InputMethodManager
import java.io.File

/** The game, full screen: the native render thread draws into [GameView]. */
class GameActivity : Activity() {
    private lateinit var view: GameView
    /** Wi-Fi drops broadcasts without it: LAN games would not show on the Play screen. */
    private val lanLock by lazy {
        (applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager?)
            ?.createMulticastLock("mcfm-lan")?.apply { setReferenceCounted(false) }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        view = GameView(this)
        setContentView(view)
        view.requestFocus()
        Native.game = this
        val game = File(filesDir, "game")
        // Worlds and options: internal storage (Android/data is out of reach of file managers and
        // adb on Android 11+ anyway); `adb shell run-as io.github.d4yvid.mcfm` reaches it.
        val home = File(filesDir, "home")
        Native.nativeStart(File(game, "minecraftpe.dylib").path, File(game, "data").path + "/", home.path)
    }

    override fun onResume() {
        super.onResume()
        hideSystemBars()
        lanLock?.acquire()
        Native.nativePause(false)
    }

    override fun onPause() {
        Native.nativePause(true)  // returns after the game saved
        lanLock?.release()
        super.onPause()
    }

    override fun onDestroy() {
        if (Native.game === this) Native.game = null
        super.onDestroy()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars()
        Native.nativeFocus(hasFocus)
    }

    @Suppress("DEPRECATION")
    private fun hideSystemBars() {
        if (Build.VERSION.SDK_INT >= 30) {
            window.insetsController?.let {
                it.hide(WindowInsets.Type.systemBars())
                it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or View.SYSTEM_UI_FLAG_FULLSCREEN or
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_LAYOUT_STABLE or
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
        }
    }

    fun showKeyboard() {
        view.requestFocus()
        (getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).showSoftInput(view, 0)
    }

    fun hideKeyboard() {
        (getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).hideSoftInputFromWindow(view.windowToken, 0)
    }

    /** The game cannot run (not imported, a bad image, no GLES 3): say why, offer a new import. */
    fun showFatal(message: String) {
        AlertDialog.Builder(this)
            .setTitle("Minecraft PE cannot start")
            .setMessage(message)
            .setPositiveButton("Import again") { _, _ ->
                startActivity(Intent(this, ImportActivity::class.java).putExtra(ImportActivity.EXTRA_REIMPORT, true))
                finish()
            }
            .setNegativeButton("Close") { _, _ -> finish() }
            .setCancelable(false)
            .show()
    }
}
