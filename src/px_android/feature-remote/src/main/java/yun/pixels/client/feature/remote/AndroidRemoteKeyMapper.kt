package yun.pixels.client.feature.remote

import android.view.KeyEvent
import yun.pixels.client.core.domain.session.RemoteKey

internal fun Int.toRemoteKey(): RemoteKey? = when (this) {
    KeyEvent.KEYCODE_DEL -> RemoteKey.Backspace
    KeyEvent.KEYCODE_TAB -> RemoteKey.Tab
    KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> RemoteKey.Enter
    KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SHIFT_RIGHT -> RemoteKey.Shift
    KeyEvent.KEYCODE_CTRL_LEFT, KeyEvent.KEYCODE_CTRL_RIGHT -> RemoteKey.Control
    KeyEvent.KEYCODE_ALT_LEFT, KeyEvent.KEYCODE_ALT_RIGHT -> RemoteKey.Alt
    KeyEvent.KEYCODE_BREAK -> RemoteKey.Pause
    KeyEvent.KEYCODE_CAPS_LOCK -> RemoteKey.CapsLock
    KeyEvent.KEYCODE_META_LEFT, KeyEvent.KEYCODE_META_RIGHT -> RemoteKey.Meta
    KeyEvent.KEYCODE_ESCAPE -> RemoteKey.Escape
    KeyEvent.KEYCODE_SPACE -> RemoteKey.Space
    KeyEvent.KEYCODE_PAGE_UP -> RemoteKey.PageUp
    KeyEvent.KEYCODE_PAGE_DOWN -> RemoteKey.PageDown
    KeyEvent.KEYCODE_MOVE_END -> RemoteKey.End
    KeyEvent.KEYCODE_MOVE_HOME -> RemoteKey.Home
    KeyEvent.KEYCODE_DPAD_LEFT -> RemoteKey.ArrowLeft
    KeyEvent.KEYCODE_DPAD_UP -> RemoteKey.ArrowUp
    KeyEvent.KEYCODE_DPAD_RIGHT -> RemoteKey.ArrowRight
    KeyEvent.KEYCODE_DPAD_DOWN -> RemoteKey.ArrowDown
    KeyEvent.KEYCODE_SYSRQ -> RemoteKey.PrintScreen
    KeyEvent.KEYCODE_INSERT -> RemoteKey.Insert
    KeyEvent.KEYCODE_FORWARD_DEL -> RemoteKey.Delete
    KeyEvent.KEYCODE_NUM_LOCK -> RemoteKey.NumLock
    KeyEvent.KEYCODE_SCROLL_LOCK -> RemoteKey.ScrollLock
    KeyEvent.KEYCODE_SEMICOLON -> RemoteKey.Semicolon
    KeyEvent.KEYCODE_EQUALS, KeyEvent.KEYCODE_PLUS -> RemoteKey.Equals
    KeyEvent.KEYCODE_COMMA -> RemoteKey.Comma
    KeyEvent.KEYCODE_MINUS -> RemoteKey.Minus
    KeyEvent.KEYCODE_PERIOD -> RemoteKey.Period
    KeyEvent.KEYCODE_SLASH -> RemoteKey.Slash
    KeyEvent.KEYCODE_GRAVE -> RemoteKey.Grave
    KeyEvent.KEYCODE_LEFT_BRACKET -> RemoteKey.LeftBracket
    KeyEvent.KEYCODE_BACKSLASH -> RemoteKey.Backslash
    KeyEvent.KEYCODE_RIGHT_BRACKET -> RemoteKey.RightBracket
    KeyEvent.KEYCODE_APOSTROPHE -> RemoteKey.Apostrophe
    in KeyEvent.KEYCODE_0..KeyEvent.KEYCODE_9 -> RemoteKey.entries[this - KeyEvent.KEYCODE_0 + RemoteKey.Digit0.ordinal]
    in KeyEvent.KEYCODE_A..KeyEvent.KEYCODE_Z -> RemoteKey.entries[this - KeyEvent.KEYCODE_A + RemoteKey.A.ordinal]
    in KeyEvent.KEYCODE_F1..KeyEvent.KEYCODE_F12 -> RemoteKey.entries[this - KeyEvent.KEYCODE_F1 + RemoteKey.F1.ordinal]
    else -> null
}
