package io.github.d4yvid.mcfm

import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.view.Gravity
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView

/**
 * Shown while the game loads (preparing the bundled game on first start, then the image, the
 * engine and the first frame): the game's name and a spinner on black, faded out once the
 * first frame is on screen ([Native.firstFrame]).
 */
class SplashView(context: Context) : LinearLayout(context) {
    private val status: TextView

    init {
        orientation = VERTICAL
        gravity = Gravity.CENTER
        setBackgroundColor(Color.BLACK)
        isClickable = true  // touches wait for the game
        addView(TextView(context).apply {
            text = "Minecraft PE"
            textSize = 30f
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(Color.WHITE)
            gravity = Gravity.CENTER
        })
        addView(ProgressBar(context).apply { isIndeterminate = true; setPadding(0, 32, 0, 32) })
        status = TextView(context).apply {
            text = "Loading…"
            textSize = 16f
            setTextColor(Color.rgb(180, 180, 180))
            gravity = Gravity.CENTER
        }
        addView(status)
    }

    fun setStatus(text: String) { status.text = text }

    fun dismiss() {
        animate().alpha(0f).setDuration(250).withEndAction { visibility = GONE }.start()
    }
}
