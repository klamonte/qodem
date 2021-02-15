/*
 * input.h
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

#ifndef __INPUT_H__
#define __INPUT_H__

/* Curses ----------------------------------------------------------------- */

#include "qcurses.h"

#define Q_COLOR_BLACK   COLOR_BLACK
#define Q_COLOR_RED     COLOR_RED
#define Q_COLOR_GREEN   COLOR_GREEN
#define Q_COLOR_BLUE    COLOR_BLUE
#define Q_COLOR_CYAN    COLOR_CYAN
#define Q_COLOR_MAGENTA COLOR_MAGENTA
#define Q_COLOR_YELLOW  COLOR_YELLOW
#define Q_COLOR_WHITE   COLOR_WHITE

#define Q_KEY_BREAK     KEY_BREAK
#define Q_KEY_DOWN      KEY_DOWN
#define Q_KEY_UP        KEY_UP
#define Q_KEY_LEFT      KEY_LEFT
#define Q_KEY_RIGHT     KEY_RIGHT
#define Q_KEY_HOME      KEY_HOME
#define Q_KEY_BACKSPACE KEY_BACKSPACE
#define Q_KEY_F0        KEY_F0
#define Q_KEY_DL        KEY_DL
#define Q_KEY_IL        KEY_IL
#define Q_KEY_DC        KEY_DC
#define Q_KEY_IC        KEY_IC
#define Q_KEY_EIC       KEY_EIC
#define Q_KEY_CLEAR     KEY_CLEAR
#define Q_KEY_EOS       KEY_EOS
#define Q_KEY_EOL       KEY_EOL
#define Q_KEY_SF        KEY_SF
#define Q_KEY_SR        KEY_SR
#define Q_KEY_NPAGE     KEY_NPAGE
#define Q_KEY_PPAGE     KEY_PPAGE
#define Q_KEY_STAB      KEY_STAB
#define Q_KEY_CTAB      KEY_CTAB
#define Q_KEY_CATAB     KEY_CATAB
#define Q_KEY_ENTER     KEY_ENTER
#define Q_KEY_SRESET    KEY_SRESET
#define Q_KEY_RESET     KEY_RESET
#define Q_KEY_PRINT     KEY_PRINT
#define Q_KEY_LL        KEY_LL
#define Q_KEY_ABORT     KEY_ABORT
#define Q_KEY_SHELP     KEY_SHELP
#define Q_KEY_LHELP     KEY_LHELP
#define Q_KEY_BTAB      KEY_BTAB
#define Q_KEY_BEG       KEY_BEG
#define Q_KEY_CANCEL    KEY_CANCEL
#define Q_KEY_CLOSE     KEY_CLOSE
#define Q_KEY_COMMAND   KEY_COMMAND
#define Q_KEY_COPY      KEY_COPY
#define Q_KEY_CREATE    KEY_CREATE
#define Q_KEY_END       KEY_END
#define Q_KEY_EXIT      KEY_EXIT
#define Q_KEY_FIND      KEY_FIND
#define Q_KEY_HELP      KEY_HELP
#define Q_KEY_MARK      KEY_MARK
#define Q_KEY_MESSAGE   KEY_MESSAGE
#define Q_KEY_MOVE      KEY_MOVE
#define Q_KEY_NEXT      KEY_NEXT
#define Q_KEY_OPEN      KEY_OPEN
#define Q_KEY_OPTIONS   KEY_OPTIONS
#define Q_KEY_PREVIOUS  KEY_PREVIOUS
#define Q_KEY_REDO      KEY_REDO
#define Q_KEY_REFERENCE KEY_REFERENCE
#define Q_KEY_REFRESH   KEY_REFRESH
#define Q_KEY_REPLACE   KEY_REPLACE
#define Q_KEY_RESTART   KEY_RESTART
#define Q_KEY_RESUME    KEY_RESUME
#define Q_KEY_SAVE      KEY_SAVE
#define Q_KEY_SBEG      KEY_SBEG
#define Q_KEY_SCANCEL   KEY_SCANCEL
#define Q_KEY_SCOMMAND  KEY_SCOMMAND
#define Q_KEY_SCOPY     KEY_SCOPY
#define Q_KEY_SCREATE   KEY_SCREATE
#define Q_KEY_SDC       KEY_SDC
#define Q_KEY_SDL       KEY_SDL
#define Q_KEY_SELECT    KEY_SELECT
#define Q_KEY_SEND      KEY_SEND
#define Q_KEY_SEOL      KEY_SEOL
#define Q_KEY_SEXIT     KEY_SEXIT
#define Q_KEY_SFIND     KEY_SFIND
#define Q_KEY_SHOME     KEY_SHOME
#define Q_KEY_SIC       KEY_SIC
#define Q_KEY_MOUSE     KEY_MOUSE
#define Q_KEY_SLEFT     KEY_SLEFT
#define Q_KEY_SRIGHT    KEY_SRIGHT
#define Q_KEY_SR        KEY_SR
#define Q_KEY_SF        KEY_SF

#define Q_KEY_SUSPEND   KEY_SUSPEND

#define Q_KEY_F(n)      (KEY_F0 + (n))

/* Number pad constants */
#define Q_KEY_PAD_MIN   Q_KEY_PAD0
#define Q_KEY_PAD0      0x1DEAD000
#define Q_KEY_PAD1      0x1DEAD001
#define Q_KEY_PAD2      0x1DEAD002
#define Q_KEY_PAD3      0x1DEAD003
#define Q_KEY_PAD4      0x1DEAD004
#define Q_KEY_PAD5      0x1DEAD005
#define Q_KEY_PAD6      0x1DEAD006
#define Q_KEY_PAD7      0x1DEAD007
#define Q_KEY_PAD8      0x1DEAD008
#define Q_KEY_PAD9      0x1DEAD009
#define Q_KEY_PAD_ENTER 0x1DEAD010
#define Q_KEY_PAD_SLASH 0x1DEAD011
#define Q_KEY_PAD_PLUS  0x1DEAD012
#define Q_KEY_PAD_MINUS 0x1DEAD013
#define Q_KEY_PAD_STAR  0x1DEAD014
#define Q_KEY_PAD_STOP  0x1DEAD015

/* Ncurses only defines these */
#define Q_KEY_A1        KEY_A1
#define Q_KEY_A3        KEY_A3
#define Q_KEY_B2        KEY_B2
#define Q_KEY_C1        KEY_C1
#define Q_KEY_C3        KEY_C3

#ifdef Q_PDCURSES
#define Q_KEY_A2        KEY_A2
#define Q_KEY_B1        KEY_B1
#define Q_KEY_B3        KEY_B3
#define Q_KEY_C2        KEY_C2
#define Q_KEY_PAD_MAX   Q_KEY_PAD_STOP
#else
#define Q_KEY_A2        0x1DEAD020
#define Q_KEY_B1        0x1DEAD021
#define Q_KEY_B3        0x1DEAD022
#define Q_KEY_C2        0x1DEAD023
#define Q_KEY_PAD_MAX   Q_KEY_C2
#endif

#define Q_A_NORMAL      A_NORMAL
#define Q_A_UNDERLINE   A_UNDERLINE
#define Q_A_REVERSE     A_REVERSE
#define Q_A_BLINK       A_BLINK
#define Q_A_BOLD        A_BOLD
#define Q_A_PROTECT     A_PROTECT
#define Q_A_DIM         A_DIM
#define Q_A_INVIS       A_INVIS
#define Q_A_ATTRIBUTES  A_ATTRIBUTES
#define Q_A_COLOR       A_COLOR

#define Q_ERR           ERR

#define KEY_FLAG_ALT            0x0001  /* ALT (META) key was pressed */
#define KEY_FLAG_CTRL           0x0002  /* CTRL key was pressed */
#define KEY_FLAG_SHIFT          0x0004  /* SHIFT key was pressed */
#define KEY_FLAG_UNICODE        0x0008  /* Key contains a Unicode character */

#define Q_KEY_ESCAPE            0x00E5CA9E
#define Q_KEY_TAB               0x007AB000
#define Q_KEY_BRACKET_ON        0x00B4ACE1
#define Q_KEY_BRACKET_OFF       0x00B4ACE0

#define Q_KEYBOARD_DELAY        10000

/* Includes --------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

/* Globals ---------------------------------------------------------------- */

/**
 * The current rendering color.
 */
extern attr_t q_current_color;

#if defined(__linux) && defined(Q_ENABLE_GPM)
#include <gpm.h>
/**
 * The GPM handler.
 */
extern int gpm_handle_mouse(Gpm_Event * event, void * data);
#endif

/**
 * If true, we are currently inside handle_resize().
 */
extern Q_BOOL q_in_handle_resize;

/* Functions -------------------------------------------------------------- */

/**
 * Obtain a keyboard input event from a window.
 *
 * @param window the curses WINDOW to query
 * @param keystroke the output keystroke, or ERR if nothing came in
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.
 * @param usleep_time the number of MICROseconds to wait for input before
 * returning ERR
 */
extern void qodem_win_getch(void * window, int * keystroke, int * flags,
                            const unsigned int usleep_time);

/**
 * Obtain a keyboard input event from stdscr.  This is the main keyboard and
 * mouse input function, called by keyboard_handler().
 *
 * @param keystroke the output keystroke, or ERR if nothing came in
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.
 * @param usleep_time the number of MICROseconds to wait for input before
 * returning ERR
 */
extern void qodem_getch(int * keystroke, int * flags,
                        const unsigned int usleep_time);

/**
 * Read data from the keyboard/mouse and throw it away.
 */
extern void discarding_getch();

/**
 * Tell curses whether or not we want calls to getch() to block.
 *
 * @param block if true, block on input.  If false, when no input is
 * available then return ERR.
 */
extern void set_blocking_input(Q_BOOL block);

/**
 * Make the cursor invisible.
 *
 * @return the previous cursor state
 */
extern int q_cursor_off();

/**
 * Make the cursor visible.
 *
 * @return the previous cursor state
 */
extern int q_cursor_on();

/**
 * Make the cursor visible or invisible.
 *
 * @param cursor if 0, make the cursor invisible.  If 1, make it visible.  If
 * 2, make it "very visible".
 * @return the previous cursor state
 */
extern int q_cursor(const int cursor);

/**
 * Determine if a keystroke is a "special key" like a function key, arrow
 * key, or number pad key, etc.
 *
 * @param keystroke key to check
 * @return 1 if this is a special key, 0 otherwise
 */
extern int q_key_code_yes(int keystroke);

/**
 * Send the current screen dimensions to the remote side.
 */
extern void send_screen_size();

#ifdef __cplusplus
}
#endif

#endif /* __INPUT_H__ */
