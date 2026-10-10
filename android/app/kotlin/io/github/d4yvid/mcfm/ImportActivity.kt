package io.github.d4yvid.mcfm

import android.app.Activity
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.net.Uri
import android.os.Bundle
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import java.io.File
import java.io.FileInputStream
import java.io.InputStream
import java.util.zip.ZipInputStream

/**
 * The start screen: imports the user's decrypted Minecraft PE 0.15.10 IPA (the app never
 * includes or downloads the game) and then opens the game. An IPA is a zip: Payload/<name>.app/
 * holds the game binary (minecraftpe2) and its data/ directory, which is all we keep.
 */
class ImportActivity : Activity() {
    companion object {
        const val EXTRA_REIMPORT = "reimport"
        /** Debug builds only: import the IPA at this path without the picker (tests). */
        const val EXTRA_PATH = "path"
        private const val PICK_IPA = 1
    }

    private lateinit var status: TextView
    private lateinit var choose: Button
    private val game by lazy { File(filesDir, "game") }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        status = TextView(this).apply { textSize = 16f; gravity = Gravity.CENTER; setPadding(48, 48, 48, 24) }
        choose = Button(this).apply { text = "Choose the IPA…"; setOnClickListener { pick() } }
        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            addView(status)
            addView(choose)
        })
        val debuggable = applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0
        val path = intent.getStringExtra(EXTRA_PATH)
        when {
            path != null && debuggable -> import { FileInputStream(path) }
            imported() && !intent.getBooleanExtra(EXTRA_REIMPORT, false) -> play()
            else -> status.text = "Minecraft PE (mcfm) runs the iOS version 0.15.10 of Minecraft PE.\n\n" +
                "Choose your decrypted minecraftpe .ipa to import it."
        }
    }

    private fun imported() = File(game, "minecraftpe.dylib").isFile && File(game, "data").isDirectory

    private fun pick() {
        startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*"), PICK_IPA)
    }

    @Deprecated("Activity result API of the platform (no AndroidX)")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val uri: Uri = data?.data ?: return
        if (requestCode == PICK_IPA && resultCode == RESULT_OK) import { contentResolver.openInputStream(uri) ?: throw java.io.IOException("cannot open the file") }
    }

    private fun import(open: () -> InputStream) {
        choose.isEnabled = false
        status.text = "Importing…"
        Thread {
            val error = try { importFrom(open) } catch (e: Exception) { "The import failed: ${e.message}" }
            runOnUiThread {
                choose.isEnabled = true
                if (error == null) {
                    status.text = "imported"
                    play()
                } else {
                    status.text = error
                }
            }
        }.start()
    }

    /** Null on success, else a message. The previous game stays until the new one is complete. */
    private fun importFrom(open: () -> InputStream): String? {
        val staging = File(filesDir, "import")
        staging.deleteRecursively()
        staging.mkdirs()
        try {
            var binary: File? = null
            var files = 0
            ZipInputStream(open().buffered()).use { zip ->
                while (true) {
                    val entry = zip.nextEntry ?: break
                    // Payload/<name>.app/<rest>: the binary and data/; nothing else is kept.
                    val parts = entry.name.split('/')
                    if (parts.size < 3 || parts[0] != "Payload" || !parts[1].endsWith(".app")) continue
                    val rest = parts.drop(2)
                    if (rest.any { it == ".." }) return "The IPA has an unsafe path: ${entry.name}"
                    val target = when {
                        rest == listOf("minecraftpe2") -> File(staging, "minecraftpe2").also { binary = it }
                        rest.size > 1 && rest[0] == "data" && !entry.isDirectory -> File(staging, rest.joinToString("/"))
                        else -> null
                    } ?: continue
                    target.parentFile?.mkdirs()
                    target.outputStream().use { zip.copyTo(it) }
                    files++
                }
            }
            val bin = binary ?: return "This is not a Minecraft PE IPA (no Payload/*.app/minecraftpe2)."
            if (!File(staging, "data").isDirectory) return "This IPA has no game data (Payload/*.app/data)."
            Native.nativeImport(bin.path, staging.path)?.let { return it }
            bin.delete()
            // Swap: the old game is kept until the new one is in place.
            val old = File(filesDir, "game.old")
            old.deleteRecursively()
            if (game.exists() && !game.renameTo(old)) return "Cannot replace the previous game."
            if (!staging.renameTo(game)) {
                old.renameTo(game)
                return "Cannot install the imported game."
            }
            old.deleteRecursively()
            return null
        } finally {
            staging.deleteRecursively()  // after a success it was renamed away: nothing left
        }
    }

    private fun play() {
        startActivity(Intent(this, GameActivity::class.java))
        finish()
    }
}
