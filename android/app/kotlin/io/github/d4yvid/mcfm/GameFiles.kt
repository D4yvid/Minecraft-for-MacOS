package io.github.d4yvid.mcfm

import android.content.Context
import java.io.File

/**
 * The imported game in the app's internal storage: files/game holds minecraftpe2 (the binary from
 * the IPA, kept to convert it again when an update changes the launcher's hooks),
 * minecraftpe.dylib (the converted image), minecraftpe.hooks and data/. An import is staged next
 * to it and swapped in by renames; worlds and options are in files/home.
 */
object GameFiles {
    fun game(context: Context) = File(context.filesDir, "game")
    fun previous(context: Context) = File(context.filesDir, "game.old")
    fun home(context: Context) = File(context.filesDir, "home")

    /** A swap cut short between its two renames leaves game.old and no game: put it back. */
    fun recover(context: Context) {
        val game = game(context)
        val old = previous(context)
        if (!game.exists() && old.isDirectory) old.renameTo(game)
    }

    fun imported(context: Context): Boolean {
        val game = game(context)
        return File(game, "minecraftpe.dylib").isFile && File(game, "data").isDirectory
    }
}
