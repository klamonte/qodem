/*
 * atari.c
 *
 * qodem - Qodem Terminal Emulator
 *
 * Written 2003-2016 by Kevin Lamonte
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

#include "common.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "qodem.h"
#include "scrollback.h"
#include "screen.h"
#include "atari.h"

/* Set this to a not-NULL value to enable debug log. */
/* static const char * DLOGNAME = "atari"; */
static const char *DLOGNAME = NULL;
extern FILE *dlogfile;

static Q_BOOL last_char_esc = Q_FALSE;
static int *tab_stops = NULL;
static unsigned int n_tab_stops = 0;

/**
 * Advance the cursor to the next tab stop.
 */
static void advance_to_next_tab_stop(void) {
    int i;
    if (!tab_stops) goto no_tab;

    for (i = 0; i < n_tab_stops; i++) {
        if (tab_stops[i] > q_status.cursor_x) {
            cursor_right(tab_stops[i] - q_status.cursor_x, Q_FALSE);
            return;
        }
    }

    /*
     * If we got here then there isn't a tab stop beyond the current cursor
     * position.  Place the cursor of the right-most edge of the screen.
     */
no_tab:
    cursor_right(WIDTH - 1 - q_status.cursor_x, Q_FALSE);
}

/**
 * Set a tab stop at the current position
 */
static void set_tab_stop(void)
{
    int i;

    for (i = 0; i < n_tab_stops; i++) {
        if (tab_stops[i] == q_status.cursor_x) {
            /* Already have a tab stop here */
            return;
        }

        if (tab_stops[i] > q_status.cursor_x) {
            /* Insert a tab stop */
            tab_stops = (int *)Xrealloc(tab_stops,
                (n_tab_stops + 1) * sizeof(int), __FILE__, __LINE__);
            memmove(&tab_stops[i + 1], &tab_stops[i],
                (n_tab_stops - i) * sizeof(int));
            n_tab_stops++;
            tab_stops[i] = q_status.cursor_x;
            return;
        }
    }

    /* If we get here, we need to append a tab stop to the end of the array */
    tab_stops = (int *)Xrealloc(tab_stops,
        (n_tab_stops + 1) * sizeof(int), __FILE__, __LINE__);
    tab_stops[n_tab_stops] = q_status.cursor_x;
    n_tab_stops++;
}

/**
 * Remove a tab stop at the current position
 */
static void clear_tab_stop(void)
{
    int i;

    for (i = 0; i < n_tab_stops; i++) {
        if (tab_stops[i] != q_status.cursor_x) continue;

        /* Clear this tab stop */
        memmove(tab_stops + i, tab_stops + i + 1,
                (n_tab_stops - i) * sizeof(int));
        n_tab_stops--;
        return;
    }
}

/**
 * Push one byte through the Atari emulator.
 *
 * @param from_modem one byte from the remote side.
 * @param to_screen if the return is Q_EMUL_FSM_ONE_CHAR or
 * Q_EMUL_FSM_MANY_CHARS, then to_screen will have a character to display on
 * the screen.
 * @return one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
Q_EMULATION_STATUS atari(const unsigned char from_modem, wchar_t * to_screen) {
    Q_BOOL handled = Q_FALSE;

    /**
     * ESC is used to toggle between printing and interpreting control
     * characters.
     */
    if (!last_char_esc) {
        handled = Q_TRUE;
        switch (from_modem) {
            case 27: /* ESC */
                last_char_esc = Q_TRUE;
                break;
            case 28: /* Cursor Up    (CTRL + -) */
                cursor_up(1, Q_TRUE);
                break;
            case 29: /* Cursor Down  (CTRL + =) */
                cursor_down(1, Q_TRUE);
                break;
            case 30: /* Cursor Left  (CTRL + +) */
                cursor_left(1, Q_TRUE);
                break;
            case 31: /* Cursor Right (CTRL + *) */
                cursor_right(1, Q_TRUE);
                break;
            case 125: /* Clear Screen (CTRL + < or SHIFT + <) */
                erase_screen(0, 0, HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1, Q_FALSE);
                cursor_position(0, 0);
                break;
            case 126: /* Backspace */
                cursor_left(1, Q_TRUE);
                delete_character(1);
                break;
            case 127: /* Tab */
                advance_to_next_tab_stop();
                break;
            case 155: /* Return */
                cursor_carriage_return();
                if (!q_status.line_feed_on_cr)
                    cursor_linefeed(Q_FALSE);
                break;
            case 156: /* Delete Line (SHIFT + Backspace) */
                erase_line(q_status.cursor_x, WIDTH - 1, Q_FALSE);
                break;
            case 157: /* Insert Line (SHIFT + >) */
                if ((q_status.cursor_y >= q_status.scroll_region_top) &&
                    (q_status.cursor_y <= q_status.scroll_region_bottom)) {
                    scrolling_region_scroll_down(q_status.cursor_y,
                                                 q_status.scroll_region_bottom, 1);
                }
                break;
            case 158: /* Clear Tabstop (CTRL + Tab) */
                clear_tab_stop();
                break;
            case 159: /* Set Tabstop (SHIFT + Tab) */
                set_tab_stop();
                break;
            case 253: /* Bell (CTRL + 2) */
                screen_beep();
                break;
            case 254: /* Delete (CTRL + Backspace) */
                delete_character(1);
                break;
            case 255: /* Insert (CTRL + >) */
                insert_blanks(1);
                break;
            default:
                handled = Q_FALSE;
        }
    }

    /* If it was a control char, we've handled it */
    if (handled) return Q_EMUL_FSM_NO_CHAR_YET;

    /* Otherwise we can draw a char */
    q_current_color &= ~Q_A_REVERSE;
    if (from_modem & 0x80) q_current_color |= Q_A_REVERSE;
    *to_screen = codepage_map_char(from_modem);
    last_char_esc = Q_FALSE;
    return Q_EMUL_FSM_ONE_CHAR;
}

/**
 * Reset the emulation state.
 */
void atari_reset(void) {
    int i;

    if (tab_stops) {
        Xfree(tab_stops, __FILE__, __LINE__);
        tab_stops = NULL;
        n_tab_stops = 0;
    }

    for (i = 0; (i * 8) < WIDTH; i++) {
        tab_stops = (int *) Xrealloc(tab_stops,
            (n_tab_stops + 1) * sizeof(int), __FILE__, __LINE__);
        tab_stops[i] = i * 8;
        n_tab_stops++;
    }
}

/**
 * Generate a sequence of bytes to send to the remote side that correspond to
 * a keystroke.
 *
 * @param keystroke one of the Q_KEY values, OR a Unicode code point.  See
 * input.h.
 * @return a wide string that is appropriate to send to the remote side.
 * Note that this emulation is 7-bit: only the bottom 7/8 bits are
 * transmitted to the remote side.  See post_keystroke().
 */
wchar_t * atari_keystroke(const int keystroke) {
    switch (keystroke) {
        case Q_KEY_BACKSPACE:
            return L"\176";
        case Q_KEY_UP:
            return L"\034";
        case Q_KEY_DOWN:
            return L"\035";
        case Q_KEY_LEFT:
            return L"\036";
        case Q_KEY_RIGHT:
            return L"\037";
        case Q_KEY_DC:
            return L"\376";
        case Q_KEY_IC:
            return L"\377";
        case Q_KEY_DL:
            return L"\234";
        case Q_KEY_IL:
            return L"\235";
        case Q_KEY_PAD_ENTER:
        case Q_KEY_ENTER:
            return L"\233";
        case Q_KEY_CTAB:
            return L"\236";
        case Q_KEY_STAB:
            return L"\237";
        case Q_KEY_CLEAR:
            return L"\175";
        case '\t':
            return L"\177";
    default:
            break;
    }

    return NULL;
}

