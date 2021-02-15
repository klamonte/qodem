/*
 * vt52.h
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

#ifndef __VT52_H__
#define __VT52_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes --------------------------------------------------------------- */

#include "emulation.h"

/* Defines ---------------------------------------------------------------- */

/* Globals ---------------------------------------------------------------- */

/**
 * True means alternate keypad mode, false means numeric keypad mode.
 */
extern Q_BOOL q_vt52_alternate_keypad_mode;

/* Functions -------------------------------------------------------------- */

/**
 * Push one byte through the VT52 emulator.
 *
 * @param from_modem one byte from the remote side.
 * @param to_screen if the return is Q_EMUL_FSM_ONE_CHAR or
 * Q_EMUL_FSM_MANY_CHARS, then to_screen will have a character to display on
 * the screen.
 * @return one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
extern Q_EMULATION_STATUS vt52(const unsigned char from_modem,
                               wchar_t * to_screen);

/**
 * Reset the emulation state.
 */
extern void vt52_reset();

/**
 * Generate a sequence of bytes to send to the remote side that correspond to
 * a keystroke.
 *
 * @param keystroke one of the Q_KEY values, OR a Unicode code point.  See
 * input.h.
 * @return a wide string that is appropriate to send to the remote side.
 * Note that VT52 emulation is a 7-bit emulation: only the bottom 7 bits are
 * transmitted to the remote side.  See post_keystroke().
 */
extern wchar_t * vt52_keystroke(const int keystroke);

#ifdef __cplusplus
}
#endif

#endif /* __VT52_H__ */
