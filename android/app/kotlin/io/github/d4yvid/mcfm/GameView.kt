package io.github.d4yvid.mcfm

import android.content.Context
import android.text.Editable
import android.text.InputType
import android.text.Selection
import android.text.SpannableStringBuilder
import android.view.InputDevice
import android.view.KeyCharacterMap
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import android.view.inputmethod.TextAttribute

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

    // A hardware keyboard: the key (VK) for the game's controls and, as on the Mac, its text for
    // text boxes.
    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        val key = Native.nativeKey(keyCode, true)
        val text = when (keyCode) {
            KeyEvent.KEYCODE_DEL -> { Native.nativeBackspace(); true }
            KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> { Native.nativeReturn(false); true }
            else -> typed(event)?.let { Native.nativeText(it); true } ?: false
        }
        return key || text || super.onKeyDown(keyCode, event)
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean =
        Native.nativeKey(keyCode, false) || super.onKeyUp(keyCode, event)

    private fun typed(event: KeyEvent): String? {
        val c = event.getUnicodeChar(event.metaState)
        if (c < 0x20 || c == 0x7F || c and KeyCharacterMap.COMBINING_ACCENT != 0) return null
        return String(Character.toChars(c))
    }

    override fun onCheckIsTextEditor(): Boolean = true

    override fun onCreateInputConnection(outAttrs: EditorInfo): InputConnection {
        outAttrs.inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
        outAttrs.imeOptions = EditorInfo.IME_ACTION_DONE or EditorInfo.IME_FLAG_NO_EXTRACT_UI or EditorInfo.IME_FLAG_NO_FULLSCREEN
        return GameInputConnection(this)
    }
}

/**
 * The soft keyboard's edits as the engine's text events. The engine's text box only takes
 * characters, backspaces and return (its cursor stays at the end), while keyboards edit text in
 * many ways (composing, suggestions replacing a word, voice input). So the keyboard edits a real
 * Editable of what this session typed, and after each edit the difference is typed into the game:
 * backspaces back to the common prefix, then the new characters.
 */
private class GameInputConnection(view: GameView) : BaseInputConnection(view, true) {
    private val text = SpannableStringBuilder()
    private var typed = ""  // what the game's text box has from this session

    init {
        Selection.setSelection(text, 0)
    }

    override fun getEditable(): Editable = text

    private fun sync(): Boolean {
        val now = text.toString()
        var common = 0
        while (common < typed.length && common < now.length && typed[common] == now[common]) common++
        if (common > 0 && Character.isHighSurrogate(typed[common - 1])) common--
        repeat(typed.codePointCount(common, typed.length)) { Native.nativeBackspace() }
        if (common < now.length) Native.nativeText(now.substring(common))
        typed = now
        return true
    }

    override fun commitText(text: CharSequence, newCursorPosition: Int) = super.commitText(text, newCursorPosition).also { sync() }
    override fun setComposingText(text: CharSequence, newCursorPosition: Int) = super.setComposingText(text, newCursorPosition).also { sync() }
    override fun setComposingRegion(start: Int, end: Int) = super.setComposingRegion(start, end).also { sync() }
    override fun finishComposingText() = super.finishComposingText().also { sync() }
    override fun replaceText(start: Int, end: Int, text: CharSequence, newCursorPosition: Int, textAttribute: TextAttribute?) =
        super.replaceText(start, end, text, newCursorPosition, textAttribute).also { sync() }

    // Text from before this session (the box's initial text) is not in the Editable: deleting
    // past its start becomes backspaces in the game.
    override fun deleteSurroundingText(beforeLength: Int, afterLength: Int): Boolean {
        val past = beforeLength - Selection.getSelectionStart(text).coerceAtLeast(0)
        super.deleteSurroundingText(beforeLength, afterLength)
        sync()
        repeat(past.coerceAtLeast(0)) { Native.nativeBackspace() }
        return true
    }

    override fun deleteSurroundingTextInCodePoints(beforeLength: Int, afterLength: Int): Boolean {
        val cursor = Selection.getSelectionStart(text).coerceAtLeast(0)
        val past = beforeLength - text.toString().codePointCount(0, cursor)
        super.deleteSurroundingTextInCodePoints(beforeLength, afterLength)
        sync()
        repeat(past.coerceAtLeast(0)) { Native.nativeBackspace() }
        return true
    }

    override fun sendKeyEvent(event: KeyEvent): Boolean {
        if (event.action != KeyEvent.ACTION_DOWN) return true
        when (event.keyCode) {
            KeyEvent.KEYCODE_DEL -> if (text.isEmpty()) Native.nativeBackspace() else {
                text.delete(text.length - Character.charCount(Character.codePointBefore(text, text.length)), text.length)
                sync()
            }
            KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> Native.nativeReturn(true)
            else -> event.unicodeChar.takeIf { it >= 0x20 && it != 0x7F }?.let {
                text.append(String(Character.toChars(it)))
                sync()
            }
        }
        return true
    }

    override fun performEditorAction(editorAction: Int): Boolean {
        Native.nativeReturn(true)
        return true
    }
}
