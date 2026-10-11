package io.github.d4yvid.mcfm

/**
 * libmcfm_launcher.so (android/launcher/app.cpp): the loader, the Darwin layer and the engine
 * boot of the iOS game image. JNI calls come from the UI thread; the game runs on its own
 * render thread, which calls back [showKeyboard], [hideKeyboard], [pickImage] and [fatal].
 */
object Native {
    init {
        System.loadLibrary("mcfm_launcher")
    }

    /** Converts the extracted game binary into `dir`/minecraftpe.dylib. Null, or a message. */
    @JvmStatic external fun nativeImport(binary: String, dir: String): String?

    @JvmStatic external fun nativeStart(image: String, data: String, home: String)
    @JvmStatic external fun nativeSurface(surface: android.view.Surface?, width: Int, height: Int)
    @JvmStatic external fun nativePause(paused: Boolean)
    @JvmStatic external fun nativeFocus(focused: Boolean)
    @JvmStatic external fun nativeTouch(action: Int, pointer: Int, x: Float, y: Float)
    @JvmStatic external fun nativeKey(keyCode: Int, down: Boolean): Boolean
    @JvmStatic external fun nativeMouse(kind: Int, a: Int, b: Int, x: Float, y: Float)
    @JvmStatic external fun nativeText(text: String)
    @JvmStatic external fun nativeBackspace()
    /** pressEnter: the soft keyboard's return, which also presses Enter (ends editing, as on iOS). */
    @JvmStatic external fun nativeReturn(pressEnter: Boolean)
    /** The image picker's answer: the path of a PNG, or null (cancelled). */
    @JvmStatic external fun nativeImagePicked(pngPath: String?)

    @Volatile var game: GameActivity? = null

    @JvmStatic fun showKeyboard(text: String) {
        game?.let { it.runOnUiThread { it.showKeyboard() } }
    }

    @JvmStatic fun hideKeyboard() {
        game?.let { it.runOnUiThread { it.hideKeyboard() } }
    }

    @JvmStatic fun pickImage() {
        val g = game
        if (g == null) nativeImagePicked(null) else g.runOnUiThread { g.pickImage() }
    }

    @JvmStatic fun fatal(message: String) {
        game?.let { it.runOnUiThread { it.showFatal(message) } }
    }
}
