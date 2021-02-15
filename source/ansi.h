/*
 * ansi.h
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

#ifndef __ANSI_H__
#define __ANSI_H__

/* Includes --------------------------------------------------------------- */

#include "emulation.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

/**
 * Push one byte through the ANSI emulator.
 *
 * @param from_modem one byte from the remote side.
 * @param to_screen if the return is Q_EMUL_FSM_ONE_CHAR or
 * Q_EMUL_FSM_MANY_CHARS, then to_screen will have a character to display on
 * the screen.
 * @return one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
extern Q_EMULATION_STATUS ansi(const unsigned char from_modem,
                               wchar_t * to_screen);

/**
 * Reset the emulation state.
 */
extern void ansi_reset();

/**
 * Generate a sequence of bytes to send to the remote side that correspond to
 * a keystroke.
 *
 * @param keystroke one of the Q_KEY values, OR a Unicode code point.  See
 * input.h.
 * @return a wide string that is appropriate to send to the remote side.
 * Note that ANSI emulation is an 8-bit emulation: only the bottom 8 bits are
 * transmitted to the remote side.  See post_keystroke().
 */
extern wchar_t * ansi_keystroke(const int keystroke);

/**
 * Determine if *count points to a "XX;YY" sequence (the first two parameters
 * of an SGR sequence), and if so set the color portion of output to the
 * selected colors.
 *
 * @param output a pointer to a curses attr_t.
 * @param count a pointer to a buffer containing the parameters of an SGR.
 * sequence.  If parsing was successful, count will point to the next byte of
 * unprocessed data.
 * @return Q_TRUE if parsing was successful.
 */
extern Q_BOOL ansi_color(attr_t * output, unsigned char ** count);

#ifdef __cplusplus
}
#endif

#endif /* __ANSI_H__ */
