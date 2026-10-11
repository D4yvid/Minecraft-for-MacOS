package io.github.d4yvid.mcfm

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import java.io.File
import java.io.FileOutputStream
import java.util.concurrent.atomic.AtomicBoolean
import java.util.zip.ZipFile

/**
 * The start screen: imports the user's decrypted Minecraft PE 0.15.10 IPA (the app never
 * includes or downloads the game) and then opens the game. Runs in its own process (":import"),
 * so the game's process can end after a fatal error and a new import starts from scratch.
 */
class ImportActivity : Activity() {
    companion object {
        const val EXTRA_REIMPORT = "reimport"
        /** Debuggable builds only: import the IPA at this path without the picker (tests). */
        const val EXTRA_PATH = "path"
        private const val PICK_IPA = 1
    }

    private lateinit var status: TextView
    private lateinit var choose: Button

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
        GameFiles.recover(this)
        Importer.listener = ::imported
        val debuggable = applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0
        val path = intent.getStringExtra(EXTRA_PATH)
        when {
            Importer.running -> importing()  // this activity was recreated during an import
            path != null && debuggable && savedInstanceState == null -> start(Importer.Source.FilePath(path))
            GameFiles.imported(this) && !intent.getBooleanExtra(EXTRA_REIMPORT, false) -> play()
            else -> status.text = "Minecraft PE (mcfm) runs the iOS version 0.15.10 of Minecraft PE.\n\n" +
                "Choose your decrypted minecraftpe .ipa to import it."
        }
    }

    override fun onDestroy() {
        if (Importer.listener == ::imported) Importer.listener = null
        super.onDestroy()
    }

    private fun pick() {
        startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*"), PICK_IPA)
    }

    @Deprecated("Activity result API of the platform (no AndroidX)")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val uri: Uri = data?.data ?: return
        if (requestCode == PICK_IPA && resultCode == RESULT_OK) start(Importer.Source.Document(uri))
    }

    private fun start(source: Importer.Source) {
        if (Importer.start(applicationContext, source)) importing()
    }

    private fun importing() {
        choose.isEnabled = false
        status.text = "Importing…"
    }

    private fun imported(error: String?) {
        choose.isEnabled = true
        if (error == null) play() else status.text = error
    }

    private fun play() {
        startActivity(Intent(this, GameActivity::class.java))
        finish()
    }
}

/**
 * One import at a time in the process, outliving the activity that started it (a rotation or a
 * theme change recreates the activity; the result goes to whichever one is showing).
 */
object Importer {
    sealed class Source {
        class FilePath(val path: String) : Source()
        class Document(val uri: Uri) : Source()
    }

    private val busy = AtomicBoolean(false)
    private val main = Handler(Looper.getMainLooper())
    private var result: String? = null
    private var done = false

    val running get() = busy.get()

    /** Main thread only. Set: receives a finished import's result (null on success). */
    var listener: ((String?) -> Unit)? = null
        set(value) {
            field = value
            if (value != null && done) {
                done = false
                value(result)
            }
        }

    /** False when an import is already running. */
    fun start(context: Context, source: Source): Boolean {
        if (!busy.compareAndSet(false, true)) return false
        Thread {
            val error = try { importFrom(context, source) } catch (e: Exception) { "The import failed: ${e.message}" }
            main.post {
                busy.set(false)
                val l = listener
                if (l != null) l(error) else { result = error; done = true }
            }
        }.start()
        return true
    }

    /** Null on success, else a message. The previous game stays until the new one is complete. */
    private fun importFrom(context: Context, source: Source): String? {
        val files = context.filesDir
        files.listFiles { f -> f.name.startsWith("import") }?.forEach { it.deleteRecursively() }  // a killed import's
        val staging = File(files, "import-${System.nanoTime()}")
        val copy = File(context.cacheDir, "import.ipa")
        try {
            // ZipFile reads the central directory: a truncated or damaged IPA is refused, not half read.
            val ipa = when (source) {
                is Source.FilePath -> File(source.path)
                is Source.Document -> {
                    val input = context.contentResolver.openInputStream(source.uri) ?: return "Cannot open the file."
                    input.use { i -> FileOutputStream(copy).use { i.copyTo(it) } }
                    copy
                }
            }
            staging.mkdirs()
            var binary: File? = null
            var data = 0
            ZipFile(ipa).use { zip ->
                for (entry in zip.entries()) {
                    // Payload/<name>.app/<rest>: the binary and data/; nothing else is kept.
                    val parts = entry.name.split('/')
                    if (parts.size < 3 || parts[0] != "Payload" || !parts[1].endsWith(".app")) continue
                    val rest = parts.drop(2)
                    if (rest.any { it == ".." }) return "The IPA has an unsafe path: ${entry.name}"
                    val target = when {
                        rest == listOf("minecraftpe2") -> File(staging, "minecraftpe2").also { binary = it }
                        rest.size > 1 && rest[0] == "data" && !entry.isDirectory -> File(staging, rest.joinToString("/")).also { data++ }
                        else -> null
                    } ?: continue
                    target.parentFile?.mkdirs()
                    zip.getInputStream(entry).use { i -> FileOutputStream(target).use { o -> i.copyTo(o); o.fd.sync() } }
                }
            }
            val bin = binary ?: return "This is not a Minecraft PE IPA (no Payload/*.app/minecraftpe2)."
            if (data == 0) return "This IPA has no game data (Payload/*.app/data)."
            Native.nativeImport(bin.path, staging.path)?.let { return it }
            // Swap: the old game is kept until the new one is in place (GameFiles.recover).
            val game = GameFiles.game(context)
            val old = GameFiles.previous(context)
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
            copy.delete()
        }
    }
}
