package io.github.d4yvid.mcfm

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.wifi.WifiManager
import android.os.Build
import android.os.Bundle
import android.os.Process
import android.provider.MediaStore
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.view.inputmethod.InputMethodManager
import java.io.File

/**
 * The game, full screen: the native render thread draws into [GameView]. One per process
 * (singleTask): the native side runs one game, so after a fatal error the process ends.
 */
class GameActivity : Activity() {
    private companion object {
        const val PICK_IMAGE = 1
    }

    private lateinit var view: GameView
    /** Wi-Fi drops broadcasts without it: LAN games would not show on the Play screen. */
    private val lanLock by lazy {
        (applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager?)
            ?.createMulticastLock("mcfm-lan")?.apply { setReferenceCounted(false) }
    }
    private var back: BackGesture? = null  // API 33+

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        view = GameView(this)
        setContentView(view)
        view.requestFocus()
        Native.game = this
        GameFiles.recover(this)
        val game = GameFiles.game(this)
        // Worlds and options: internal storage (Android/data is out of reach of file managers and
        // adb on Android 11+ anyway); `adb shell run-as io.github.d4yvid.mcfm` reaches it.
        val home = GameFiles.home(this)
        Native.nativeStart(File(game, "minecraftpe.dylib").path, File(game, "data").path + "/", home.path)
        if (Build.VERSION.SDK_INT >= 33) back = BackGesture(this)  // older: KEYCODE_BACK in GameView
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
        back?.unregister()
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
        val imm = getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager
        imm.restartInput(view)  // a new text box: nothing composing
        imm.showSoftInput(view, 0)
    }

    /**
     * The skin screen's "Browse": a picture from the photo picker (Android 13+) or any app that
     * provides images, handed to the game as a PNG in its temp directory (as iOS's picker does).
     */
    fun pickImage() {
        val intent = if (Build.VERSION.SDK_INT >= 33) Intent(MediaStore.ACTION_PICK_IMAGES)
        else Intent(Intent.ACTION_GET_CONTENT).addCategory(Intent.CATEGORY_OPENABLE).setType("image/*")
        try {
            startActivityForResult(intent, PICK_IMAGE)
        } catch (e: android.content.ActivityNotFoundException) {
            Native.nativeImagePicked(null)
        }
    }

    @Deprecated("Activity result API of the platform (no AndroidX)")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != PICK_IMAGE) return
        val uri = data?.data.takeIf { resultCode == RESULT_OK }
        if (uri == null) return Native.nativeImagePicked(null)
        val png = File(GameFiles.home(this), "tmp/newSkin.png")
        Thread {
            val ok = try {
                val bitmap = contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it) }
                png.parentFile?.mkdirs()
                bitmap != null && png.outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
            } catch (e: Exception) {
                false
            }
            Native.nativeImagePicked(if (ok) png.path else null)
        }.start()
    }

    fun hideKeyboard() {
        (getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).hideSoftInputFromWindow(view.windowToken, 0)
    }

    /**
     * The game cannot run (not imported, a bad image, no GLES 3): say why, offer a new import. The
     * process ends either way: its one game is gone, and the import runs in its own process.
     */
    fun showFatal(message: String) {
        if (isFinishing) return
        AlertDialog.Builder(this)
            .setTitle("Minecraft PE cannot start")
            .setMessage(message)
            .setPositiveButton("Import again") { _, _ ->
                startActivity(Intent(this, ImportActivity::class.java).putExtra(ImportActivity.EXTRA_REIMPORT, true))
                end(false)
            }
            .setNegativeButton("Close") { _, _ -> end(true) }
            .setCancelable(false)
            .show()
    }

    private fun end(removeTask: Boolean) {
        if (removeTask) finishAndRemoveTask() else finish()
        view.post { Process.killProcess(Process.myPid()) }
    }
}
