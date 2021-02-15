/*
 * vt100.h
 *
 * qodem - Qodem Terminal Emulator
 *
 * Written 2003-2021 by Autumn Lamonte ⚧ Trans Liberation Now
 *
 * To the extent possible under law, the author(s) have dedicated all
 * copyright and related and neighboring rights to this software to the
 * public domain worldwide. This software is distributed without any
 * warranty.
 *
 * You should have received a copy of the CC0 Public Domain Dedication along
 * with this software. If not, see
 * <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#ifndef __VT100_H__
#define __VT100_H__

/* Includes --------------------------------------------------------------- */

#include "emulation.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

/* Globals ---------------------------------------------------------------- */

/**
 * Whether arrow keys send ANSI, VT100, or VT52 sequences.
 */
extern Q_EMULATION q_vt100_arrow_keys;

/**
 * Whether number pad keys send VT100 or VT52, application or numeric
 * sequences.
 */
extern struct q_keypad_mode q_vt100_keypad_mode;

/**
 * When true, VT100 new line mode is set.  Exposed for keyboard.c so that
 * ENTER generates appropriate code.
 */
extern Q_BOOL q_vt100_new_line_mode;

/**
 * The bell frequency in Hz set by ESC [ 10 ; n ] .  Used by qodem_beep().
 */
extern int q_linux_beep_frequency;

/**
 * The bell duration in milliseconds set by ESC [ 10 ; n ] .  Used by
 * qodem_beep().
 */
extern int q_linux_beep_duration;

/**
 * The available mouse tracking protocols.  See handle_mouse() in input.c.
 */
typedef enum {
    XTERM_MOUSE_OFF,
    XTERM_MOUSE_X10,
    XTERM_MOUSE_NORMAL,
    XTERM_MOUSE_BUTTONEVENT,
    XTERM_MOUSE_ANYEVENT
} XTERM_MOUSE_PROTOCOL;

/**
 * The available mouse tracking encodings.  See handle_mouse() in input.c.
 */
typedef enum {
    XTERM_MOUSE_ENCODING_X10,
    XTERM_MOUSE_ENCODING_UTF8,
    XTERM_MOUSE_ENCODING_SGR
} XTERM_MOUSE_ENCODING;

/**
 * The current mouse tracking protocol.  See handle_mouse() in input.c.
 */
extern XTERM_MOUSE_PROTOCOL q_xterm_mouse_protocol;

/**
 * The current mouse tracking encoding.  See handle_mouse() in input.c.
 */
extern XTERM_MOUSE_ENCODING q_xterm_mouse_encoding;

/* Functions -------------------------------------------------------------- */

/**
 * Push one byte through the VT100, VT102, or VT220 emulator.
 *
 * @param from_modem one byte from the remote side.
 * @param to_screen if the return is Q_EMUL_FSM_ONE_CHAR or
 * Q_EMUL_FSM_MANY_CHARS, then to_screen will have a character to display on
 * the screen.
 * @return one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
extern Q_EMULATION_STATUS vt100(const unsigned char from_modem,
                                wchar_t * to_screen);

/**
 * Reset the emulation state.
 */
extern void vt100_reset();

/**
 * Generate a sequence of bytes to send to the remote side that correspond to
 * a keystroke.  Used by VT100, VT102, and VT220.
 *
 * @param keystroke one of the Q_KEY values, OR a Unicode code point.  See
 * input.h.
 * @return a wide string that is appropriate to send to the remote side.
 * Note that these emulations are 7-bit and 8-bit: only the bottom 7/8 bits
 * are transmitted to the remote side.  See post_keystroke().
 */
extern wchar_t * vt100_keystroke(const int keystroke);

/**
 * Generate a sequence of bytes to send to the remote side that correspond to
 * a keystroke.  Used by LINUX and L_UTF8.
 *
 * @param keystroke one of the Q_KEY values, OR a Unicode code point.  See
 * input.h.
 * @return a wide string that is appropriate to send to the remote side.
 * Note that LINUX emulation is an 8-bit emulation: only the bottom 8 bits
 * are transmitted to the remote side.  L_UTF8 emulation sends a true Unicode
 * sequence.  See post_keystroke().
 */
extern wchar_t * linux_keystroke(const int keystroke);

/**
 * Generate a sequence of bytes to send to the remote side that correspond to
 * a keystroke.  Used by XTERM and X_UTF8.
 *
 * @param keystroke one of the Q_KEY values, OR a Unicode code point.  See
 * input.h.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 * @return a wide string that is appropriate to send to the remote side.
 * Note that XTERM emulation is an 8-bit emulation: only the bottom 8 bits
 * are transmitted to the remote side.  X_UTF8 emulation sends a true Unicode
 * sequence.  See post_keystroke().
 */
extern wchar_t * xterm_keystroke(const int keystroke, const int flags);

#ifdef __cplusplus
}
#endif

#endif /* __VT100_H__ */
