/*
 * atascii.c
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

#include "common.h"
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "qodem.h"
#include "screen.h"
#include "scrollback.h"
#include "options.h"
#include "ansi.h"
#include "atascii.h"

/* Set this to a not-NULL value to enable debug log. */
/* static const char * DLOGNAME = "atascii"; */
static const char * DLOGNAME = NULL;

/**
 * State change flags for the Commodore keyboard/screen.
 */
struct atari_state {
    /**
     * If true, reverse video is enabled.
     */
    Q_BOOL reverse;

    /**
     * ESC is used to toggle between printing and interpreting control
     * characters.
     */
    Q_BOOL print_control_char;

    /**
     * tab_stops_n is the number of elements in tab_stops, so it begins as 0.
     */
    int tab_stops_n;

    /**
     * The list of defined tab stops.
     */
    int * tab_stops;
};

/**
 * The current keyboard/screen state.
 */
static struct atari_state state = {
    Q_FALSE,
    Q_FALSE,
    0,
    NULL
};

/**
 * ATASCII (Atari) to Unicode map.
 */
wchar_t atascii_chars[128] = {
    0x2665, 0x251C, 0x23B9, 0x2518, 0x2524, 0x2510, 0x2571, 0x2572,
    0x25E2, 0x2597, 0x25E3, 0x259D, 0x2598, 0x23BA, 0x23BD, 0x2596,
    0x2663, 0x250C, 0x2500, 0x253C, 0x25CF, 0x2584, 0x23B8, 0x252C,
    0x2534, 0x258C, 0x2514, 0x241B, 0x2191, 0x2193, 0x2190, 0x2192,
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
    0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
    0x2666, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
    0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
    0x0078, 0x0079, 0x007A, 0x2660, 0x007C, 0x2196, 0x25C0, 0x25B6
};

/**
 * Advance the cursor to the next tab stop.
 */
static void advance_to_next_tab_stop() {
    int i;
    if (state.tab_stops == NULL) {
        /* Go to the rightmost column */
        cursor_right(WIDTH - 1 - q_status.cursor_x, Q_FALSE);
        return;
    }
    for (i = 0; i < state.tab_stops_n; i++) {
        if (state.tab_stops[i] > q_status.cursor_x) {
            cursor_right(state.tab_stops[i] - q_status.cursor_x, Q_FALSE);
            return;
        }
    }
    /*
     * If we got here then there isn't a tab stop beyond the current cursor
     * position.  Place the cursor of the right-most edge of the screen.
     */
    cursor_right(WIDTH - 1 - q_status.cursor_x, Q_FALSE);
}

/**
 * Reset the tab stops list.
 */
static void reset_tab_stops() {
    int i;
    if (state.tab_stops != NULL) {
        Xfree(state.tab_stops, __FILE__, __LINE__);
        state.tab_stops = NULL;
        state.tab_stops_n = 0;
    }
    for (i = 0; (i * 8) < WIDTH; i++) {
        state.tab_stops = (int *) Xrealloc(state.tab_stops,
            (state.tab_stops_n + 1) * sizeof(int), __FILE__, __LINE__);
        state.tab_stops[i] = i * 8;
        state.tab_stops_n++;
    }
}

/**
 * Set a tab stop at the current position.
 */
static void set_tab_stop() {
    int i;

    for (i = 0; i < state.tab_stops_n; i++) {
        if (state.tab_stops[i] == q_status.cursor_x) {
            /* Already have a tab stop here */
            return;
        }
        if (state.tab_stops[i] > q_status.cursor_x) {
            /* Insert a tab stop */
            state.tab_stops = (int *) Xrealloc(state.tab_stops,
                (state.tab_stops_n + 1) * sizeof(int), __FILE__, __LINE__);
            memmove(&state.tab_stops[i + 1], &state.tab_stops[i],
                (state.tab_stops_n - i) * sizeof(int));
            state.tab_stops_n++;
            state.tab_stops[i] = q_status.cursor_x;
            return;
        }
    }

    /* If we get here, we need to append a tab stop to the end of the array */
    state.tab_stops = (int *) Xrealloc(state.tab_stops,
        (state.tab_stops_n + 1) * sizeof(int), __FILE__, __LINE__);
    state.tab_stops[state.tab_stops_n] = q_status.cursor_x;
    state.tab_stops_n++;
}

/**
 * Remove a tab stop at the current position.
 */
static void clear_tab_stop() {
    int i;

    /* Clear the tab stop at this position */
    for (i = 0; i < state.tab_stops_n; i++) {
        if (state.tab_stops[i] > q_status.cursor_x) {
            /* No tab stop here */
            return;
        }
        if (state.tab_stops[i] == q_status.cursor_x) {
            /* Remove this tab stop */
            memmove(&state.tab_stops[i], &state.tab_stops[i + 1],
                (state.tab_stops_n - i - 1) * sizeof(int));
            state.tab_stops = (int *) Xrealloc(state.tab_stops,
                (state.tab_stops_n - 1) * sizeof(int), __FILE__, __LINE__);
            state.tab_stops_n--;
            return;
        }
    }

    /* If we get here, the array ended before we found a tab stop. */
    /* NOP */
}

/**
 * Reset the emulation state.
 */
void atascii_reset() {
    DLOG(("atascii_reset()\n"));

    state.reverse = Q_FALSE;
    state.print_control_char = Q_FALSE;
    reset_tab_stops();
}

/**
 * Process a special ATASCII control character.
 *
 * @param control_char a byte that could be interpreted as a control byte
 * @return true if it was consumed and should not be printed
 */
static Q_BOOL atascii_handle_control_char(const unsigned char control_char) {

    switch (control_char) {
    case 0x1C:
        /*
         * Cursor up (CTRL + -)
         */
        cursor_up(1, Q_FALSE);
        return Q_TRUE;
    case 0x1D:
        /*
         * Cursor down (CTRL + =)
         */
        cursor_down(1, Q_FALSE);
        return Q_TRUE;
    case 0x1E:
        /*
         * Cursor left (CTRL + +)
         */
        cursor_left(1, Q_FALSE);
        return Q_TRUE;
    case 0x1F:
        /*
         * Cursor right (CTRL + *)
         */
        cursor_right(1, Q_FALSE);
        return Q_TRUE;
    case 0x7D:
        /*
         * Clear screen (CTRL + < or SHIFT + <)
         */
        erase_screen(0, 0, HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1, Q_FALSE);
        cursor_position(0, 0);
        return Q_TRUE;
    case 0x7E:
        /*
         * Backspace
         */
        cursor_left(1, Q_FALSE);
        delete_character(1);
        return Q_TRUE;
    case 0x7F:
        /*
         * Tab
         */
        advance_to_next_tab_stop();
        return Q_TRUE;
    case 0x9B:
        /*
         * Return
         */
        cursor_linefeed(Q_TRUE);
        return Q_TRUE;
    case 0x9C:
        /*
         * Delete line (SHIFT + Backspace)
         */
        erase_line(q_status.cursor_x, WIDTH - 1, Q_FALSE);
        return Q_TRUE;
    case 0x9D:
        /*
         * Insert line (SHIFT + >)
         */
        scrolling_region_scroll_down(q_status.cursor_y,
            HEIGHT - STATUS_HEIGHT - 1, 1);
        return Q_TRUE;
    case 0x9E:
        /*
         * Clear tabstop (CTRL + Tab)
         */
        clear_tab_stop();
        return Q_TRUE;
    case 0x9F:
        /*
         * Set Tabstop (SHIFT + Tab)
         */
        set_tab_stop();
        return Q_TRUE;
    case 0xFD:
        /*
         * Bell (CTRL + 2)
         */
        screen_beep();
        return Q_TRUE;
    case 0xFE:
        /*
         * Delete (CTRL + Backspace)
         */
        delete_character(1);
        return Q_TRUE;
    case 0xFF:
        /*
         * Insert (CTRL + >)
         */
        insert_blanks(1);
        return Q_TRUE;
    default:
        break;
    }

    /*
     * We did not consume it, let it be printed.
     */
    return Q_FALSE;
}

/**
 * Push one byte through the ATASCII emulator.
 *
 * @param from_modem one byte from the remote side.
 * @param to_screen if the return is Q_EMUL_FSM_ONE_CHAR or
 * Q_EMUL_FSM_MANY_CHARS, then to_screen will have a character to display on
 * the screen.
 * @return one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
Q_EMULATION_STATUS atascii(const unsigned char from_modem,
                           wchar_t * to_screen) {

    DLOG(("ESC: %s REVERSE %s CHAR: 0x%02x '%c'\n",
            (state.print_control_char == Q_TRUE ? "true" : "false"),
            (state.reverse == Q_TRUE ? "true" : "false"),
            from_modem, from_modem));

    if (q_status.atascii_has_wide_font == Q_FALSE) {
        /*
         * We don't think our font is double-width, so ask xterm/X11 to make
         * it bigger for us.
         */
        set_double_width(Q_TRUE);
    }

    /*
     * Determine if a control char needs to be printed or handled.
     */
    if ((state.print_control_char == Q_TRUE) &&
        (((from_modem & 0x7F) < 0x20) || ((from_modem & 0x7F) == 0x7F))
    ) {
        /*
         * This is a control character, and we are printing them.  Fall
         * through to the print at the end.
         */
        DLOG(("Control character, PRINT\n"));
    } else {
        if (state.print_control_char == Q_FALSE) {
            if (from_modem == C_ESC) {
                DLOG(("Start PRINTING control characters\n"));
                state.print_control_char = Q_TRUE;
                return Q_EMUL_FSM_NO_CHAR_YET;
            }

            if (atascii_handle_control_char(from_modem) == Q_TRUE) {
                /*
                 * This byte was consumed, it does not need to be printed.
                 */
                return Q_EMUL_FSM_NO_CHAR_YET;
            }
            if (((from_modem & 0x7F) < 0x20) || ((from_modem & 0x7F) == 0x7F)) {
                /*
                 * This is a control character, but we are not printing them.
                 * Make it vanish.
                 */
                DLOG(("Unhandled control character, IGNORE\n"));
                return Q_EMUL_FSM_NO_CHAR_YET;
            }
        } /* if (state.print_control_char == Q_FALSE) */
    }

    /* Reset print control char flag. */
    state.print_control_char = Q_FALSE;

    /* This is a printable character, send it out. */
    if ((from_modem & 0x80) != 0) {
        /* Reverse for this character */
        q_current_color |= Q_A_REVERSE;
    } else {
        /* Normal for this character */
        q_current_color &= ~Q_A_REVERSE;
    }
    *to_screen = atascii_chars[from_modem & 0x7F];
    DLOG(("Send to screen: '%c'\n", *to_screen));
    return Q_EMUL_FSM_ONE_CHAR;
}

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
wchar_t * atascii_keystroke(const int keystroke) {

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
    case Q_KEY_TAB:
        return L"\177";
    case Q_KEY_ESCAPE:
        return L"\033";

    case Q_KEY_PPAGE:
    case Q_KEY_NPAGE:
    case Q_KEY_SIC:
    case Q_KEY_SDC:
    case Q_KEY_HOME:
    case Q_KEY_END:
    case Q_KEY_F(1):
    case Q_KEY_F(2):
    case Q_KEY_F(3):
    case Q_KEY_F(4):
    case Q_KEY_F(5):
    case Q_KEY_F(6):
    case Q_KEY_F(7):
    case Q_KEY_F(8):
    case Q_KEY_F(9):
    case Q_KEY_F(10):
    case Q_KEY_F(11):
    case Q_KEY_F(12):
    case Q_KEY_F(13):
    case Q_KEY_F(14):
    case Q_KEY_F(15):
    case Q_KEY_F(16):
    case Q_KEY_F(17):
    case Q_KEY_F(18):
    case Q_KEY_F(19):
    case Q_KEY_F(20):
    case Q_KEY_F(21):
    case Q_KEY_F(22):
    case Q_KEY_F(23):
    case Q_KEY_F(24):
    case Q_KEY_F(25):
    case Q_KEY_F(26):
    case Q_KEY_F(27):
    case Q_KEY_F(28):
    case Q_KEY_F(29):
    case Q_KEY_F(30):
    case Q_KEY_F(31):
    case Q_KEY_F(32):
    case Q_KEY_F(33):
    case Q_KEY_F(34):
    case Q_KEY_F(35):
    case Q_KEY_F(36):
    case Q_KEY_PAD0:
    case Q_KEY_C1:
    case Q_KEY_PAD1:
    case Q_KEY_C2:
    case Q_KEY_PAD2:
    case Q_KEY_C3:
    case Q_KEY_PAD3:
    case Q_KEY_B1:
    case Q_KEY_PAD4:
    case Q_KEY_B2:
    case Q_KEY_PAD5:
    case Q_KEY_B3:
    case Q_KEY_PAD6:
    case Q_KEY_A1:
    case Q_KEY_PAD7:
    case Q_KEY_A2:
    case Q_KEY_PAD8:
    case Q_KEY_A3:
    case Q_KEY_PAD9:
    case Q_KEY_PAD_STOP:
    case Q_KEY_PAD_SLASH:
    case Q_KEY_PAD_STAR:
    case Q_KEY_PAD_MINUS:
    case Q_KEY_PAD_PLUS:
        return L"";

    default:
        break;

    }

    return NULL;
}
