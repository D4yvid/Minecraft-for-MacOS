package io.github.d4yvid.mcfm

import android.content.Context
import android.text.InputType
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection

/**
 * The game's window: a SurfaceView the native render thread draws into, the source of touch,
 * mouse and keyboard input, and the soft keyboard's target (text goes to the engine's text
 * queue, as iOS's keyboard view feeds it).
 */
class GameView(context: Context) : SurfaceView(context), SurfaceHolder.Callback {
    init {
        holder.addCallback(this)
        isFocusable = true
        isFocusableInTouchMode = true
    }

    override fun surfaceCreated(holder: SurfaceHolder) {}

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        Native.nativeSurface(holder.surface, width, height)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        Native.nativeSurface(null, 0, 0)  // returns once the game has let go of the window
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (event.isFromSource(InputDevice.SOURCE_MOUSE)) return mouse(event)
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = event.actionIndex
                Native.nativeTouch(0, event.getPointerId(i), event.getX(i), event.getY(i))
            }
            MotionEvent.ACTION_MOVE ->
                for (i in 0 until event.pointerCount) Native.nativeTouch(1, event.getPointerId(i), event.getX(i), event.getY(i))
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val i = event.actionIndex
                Native.nativeTouch(2, event.getPointerId(i), event.getX(i), event.getY(i))
            }
            MotionEvent.ACTION_CANCEL ->
                for (i in 0 until event.pointerCount) Native.nativeTouch(3, event.getPointerId(i), event.getX(i), event.getY(i))
        }
        return true
    }

    override fun onGenericMotionEvent(event: MotionEvent): Boolean =
        if (event.isFromSource(InputDevice.SOURCE_MOUSE)) mouse(event) else super.onGenericMotionEvent(event)

    // A physical mouse: buttons 1 left / 2 right / 3 middle, moves, the wheel.
    private fun mouse(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_BUTTON_PRESS, MotionEvent.ACTION_BUTTON_RELEASE -> {
                val button = when (event.actionButton) {
                    MotionEvent.BUTTON_SECONDARY -> 2
                    MotionEvent.BUTTON_TERTIARY -> 3
                    else -> 1
                }
                Native.nativeMouse(0, button, if (event.actionMasked == MotionEvent.ACTION_BUTTON_PRESS) 1 else 0, event.x, event.y)
            }
            MotionEvent.ACTION_HOVER_MOVE, MotionEvent.ACTION_MOVE -> Native.nativeMouse(1, 0, 0, event.x, event.y)
            MotionEvent.ACTION_SCROLL -> {
                val notches = Math.round(event.getAxisValue(MotionEvent.AXIS_VSCROLL))
                if (notches != 0) Native.nativeMouse(2, notches, 0, event.x, event.y)
            }
        }
        return true
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean =
        Native.nativeKey(keyCode, true) || super.onKeyDown(keyCode, event)

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean =
        Native.nativeKey(keyCode, false) || super.onKeyUp(keyCode, event)

    override fun onCheckIsTextEditor(): Boolean = true

    override fun onCreateInputConnection(outAttrs: EditorInfo): InputConnection {
        outAttrs.inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
        outAttrs.imeOptions = EditorInfo.IME_ACTION_DONE or EditorInfo.IME_FLAG_NO_EXTRACT_UI or EditorInfo.IME_FLAG_NO_FULLSCREEN
        return object : BaseInputConnection(this, false) {
            override fun commitText(text: CharSequence, newCursorPosition: Int): Boolean {
                if (text.isNotEmpty()) Native.nativeText(text.toString())
                return true
            }

            override fun deleteSurroundingText(beforeLength: Int, afterLength: Int): Boolean {
                repeat(beforeLength) { Native.nativeBackspace() }
                return true
            }

            override fun sendKeyEvent(event: KeyEvent): Boolean {
                if (event.action == KeyEvent.ACTION_DOWN) {
                    when (event.keyCode) {
                        KeyEvent.KEYCODE_DEL -> Native.nativeBackspace()
                        KeyEvent.KEYCODE_ENTER -> Native.nativeReturn()
                        else -> event.unicodeChar.takeIf { it != 0 }?.let { Native.nativeText(String(Character.toChars(it))) }
                    }
                }
                return true
            }

            override fun performEditorAction(editorAction: Int): Boolean {
                Native.nativeReturn()
                return true
            }
        }
    }
}
