package io.github.d4yvid.mcfm

import android.content.Context
import android.os.Handler
import android.os.Looper
import java.io.File
import java.io.FileOutputStream
import java.util.concurrent.atomic.AtomicBoolean
import java.util.zip.ZipFile

/**
 * The game bundled in the APK (assets/game: the iOS binary minecraftpe2, data/, bundle.id) and its
 * prepared copy in internal storage, files/game: minecraftpe2 (kept to convert it again when an
 * update changes the launcher's hooks), minecraftpe.dylib (the converted image), minecraftpe.hooks,
 * data/ and bundle.id (which bundle it came from). Prepared on first start and after an update
 * that brings another game: staged next to files/game and swapped in by renames. Worlds and
 * options are in files/home.
 */
object GameFiles {
    fun game(context: Context) = File(context.filesDir, "game")
    fun home(context: Context) = File(context.filesDir, "home")
    private fun previous(context: Context) = File(context.filesDir, "game.old")

    /** The bundle in this APK (assets/game/bundle.id), or null when the APK has no game. */
    private fun bundleId(context: Context): String? =
        try { context.assets.open("game/bundle.id").bufferedReader().use { it.readText().trim() } } catch (e: Exception) { null }

    /** files/game holds this APK's game (else [prepare] it). */
    fun ready(context: Context): Boolean {
        recover(context)
        val game = game(context)
        val id = bundleId(context) ?: return false
        return File(game, "minecraftpe.dylib").isFile && File(game, "minecraftpe2").isFile && File(game, "data").isDirectory &&
            File(game, "bundle.id").takeIf { it.isFile }?.readText()?.trim() == id
    }

    /** After a fatal error: prepare the game again on the next start. */
    fun forget(context: Context) {
        File(game(context), "bundle.id").delete()
    }

    /** A swap cut short between its two renames leaves game.old and no game: put it back. */
    private fun recover(context: Context) {
        val game = game(context)
        val old = previous(context)
        if (!game.exists() && old.isDirectory) old.renameTo(game)
    }

    private val busy = AtomicBoolean(false)
    private val main = Handler(Looper.getMainLooper())
    private val waiting = mutableListOf<(String?) -> Unit>()  // main thread only

    /** Prepares the bundled game on a background thread; `done` gets null, or a message (main thread). */
    fun prepare(context: Context, done: (String?) -> Unit) {
        waiting += done
        if (!busy.compareAndSet(false, true)) return  // already running: answered with the others
        val app = context.applicationContext
        Thread {
            val error = try { extract(app) } catch (e: Exception) { "The game could not be prepared: ${e.message}" }
            main.post {
                busy.set(false)
                val all = waiting.toList()
                waiting.clear()
                all.forEach { it(error) }
            }
        }.start()
    }

    private fun extract(context: Context): String? {
        val id = bundleId(context) ?: return "This APK has no game in it (build it with make android-app IPA=…)."
        val files = context.filesDir
        files.listFiles { f -> f.name.startsWith("import") }?.forEach { it.deleteRecursively() }  // a killed preparation's
        val staging = File(files, "import-${System.nanoTime()}")
        try {
            staging.mkdirs()
            ZipFile(context.applicationInfo.sourceDir).use { apk ->
                for (entry in apk.entries()) {
                    if (entry.isDirectory || !entry.name.startsWith("assets/game/")) continue
                    val rest = entry.name.removePrefix("assets/game/").split('/')
                    if (rest.any { it == ".." || it.isEmpty() }) return "The APK has an unsafe path: ${entry.name}"
                    val target = File(staging, rest.joinToString("/"))
                    target.parentFile?.mkdirs()
                    apk.getInputStream(entry).use { i -> FileOutputStream(target).use { o -> i.copyTo(o); o.fd.sync() } }
                }
            }
            val binary = File(staging, "minecraftpe2")
            if (!binary.isFile || !File(staging, "data").isDirectory) return "The APK's game is incomplete."
            Native.nativeImport(binary.path, staging.path)?.let { return it }
            File(staging, "bundle.id").writeText(id + "\n")
            // Swap: the old copy is kept until the new one is in place (recover).
            val game = game(context)
            val old = previous(context)
            old.deleteRecursively()
            if (game.exists() && !game.renameTo(old)) return "Cannot replace the previous copy of the game."
            if (!staging.renameTo(game)) {
                old.renameTo(game)
                return "Cannot install the game."
            }
            old.deleteRecursively()
            return null
        } finally {
            staging.deleteRecursively()  // after a success it was renamed away: nothing left
        }
    }
}
