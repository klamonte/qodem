/*
 * vt52.c
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
#include <ctype.h>
#include <string.h>
#include "qodem.h"
#include "options.h"
#include "screen.h"
#include "ansi.h"
#include "netclient.h"
#include "vt52.h"

/**
 * Scan states for the parser state machine.
 */
typedef enum SCAN_STATES {
    SCAN_NONE,
    SCAN_ESC,
    SCAN_Y_1,
    SCAN_Y_2,
    SCAN_CSI,
    SCAN_CSI_PARAM
} SCAN_STATE;

/* Current scanning state. */
static SCAN_STATE scan_state;

/* The only real VT52 state is whether it is in graphics mode. */
static Q_BOOL graphics_mode;

/**
 * True means alternate keypad mode, false means numeric keypad mode.
 */
Q_BOOL q_vt52_alternate_keypad_mode;

/* Set this to a not-NULL value to enable debug log. */
/* static const char * DLOGNAME = "vt52"; */
static const char * DLOGNAME = NULL;

/**
 * Reset the emulation state.
 */
void vt52_reset() {
    scan_state = SCAN_NONE;
    graphics_mode = Q_FALSE;
    q_vt52_alternate_keypad_mode = Q_FALSE;

    DLOG(("vt52_reset()\n"));
}

/**
 * Reset the scan state for a new sequence.
 *
 * @param to_screen one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
static void clear_state(wchar_t * to_screen) {
    q_emul_buffer_n = 0;
    q_emul_buffer_i = 0;
    memset(q_emul_buffer, 0, sizeof(q_emul_buffer));
    scan_state = SCAN_NONE;
    *to_screen = 1;
}

/**
 * Hang onto one character in the buffer.
 *
 * @param keep_char the character to save into q_emul_buffer
 * @param to_screen one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
static void save_char(unsigned char keep_char, wchar_t * to_screen) {
    q_emul_buffer[q_emul_buffer_n] = keep_char;
    q_emul_buffer_n++;
    *to_screen = 1;
}

/**
 * Translate VT52 graphics chars to a Unicode code point.
 *
 * @param vt52_char the 7-bit character in the VT52 graphics character set
 * @return the code point
 */
static wchar_t map_character(const unsigned char vt52_char) {
    if (graphics_mode == Q_TRUE) {
        switch (vt52_char) {
        case '^':
            return cp437_chars[BLANK];
        case '_':
            return cp437_chars[BLANK];
        case '`':
            return cp437_chars[BLANK];  /* Reserved */
        case 'a':
            return cp437_chars[BOX];
        case 'b':
            return 0x215F;      /* 1/ */

            /*
             * The following characters can be made in Unicode using two
             * combining characters.
             */
        case 'c':
            return cp437_chars[HATCH];  /* 3/ */
        case 'd':
            return cp437_chars[HATCH];  /* 5/ */
        case 'e':
            return cp437_chars[HATCH];  /* 7/ */

        case 'f':
            return cp437_chars[DEGREE];
        case 'g':
            return cp437_chars[PLUSMINUS];
        case 'h':
            return cp437_chars[RIGHTARROW];
        case 'i':
            return 0x2026;      /* Ellipsis */
        case 'j':
            return 0x00F7;      /* Divide by */
        case 'k':
            return cp437_chars[DOWNARROW];
        case 'l':
            return 0x23BA;      /* Scan 0 */
        case 'm':
            return 0x23BA;      /* Scan 1 */
        case 'n':
            return 0x23BB;      /* Scan 2 */
        case 'o':
            return 0x23BB;      /* Scan 3 */
        case 'p':
            return cp437_chars[SINGLE_BAR];     /* Scan 4 */
        case 'q':
            return cp437_chars[SINGLE_BAR];     /* Scan 5 */
        case 'r':
            return 0x23BC;      /* Scan 6 */
        case 's':
            return 0x23BC;      /* Scan 7 */
        case 't':
            return 0x2080;      /* Subscript 0 */
        case 'u':
            return 0x2081;      /* Subscript 1 */
        case 'v':
            return 0x2082;      /* Subscript 2 */
        case 'w':
            return 0x2083;      /* Subscript 3 */
        case 'x':
            return 0x2084;      /* Subscript 4 */
        case 'y':
            return 0x2085;      /* Subscript 5 */
        case 'z':
            return 0x2086;      /* Subscript 6 */
        case '{':
            return 0x2087;      /* Subscript 7 */
        case '|':
            return 0x2088;      /* Subscript 8 */
        case '}':
            return 0x2089;      /* Subscript 9 */
        case '~':
            return 0x00B6;      /* Paragraph */
        }
    }
    return vt52_char;
}

/**
 * Push one byte through the VT52 emulator.
 *
 * @param from_modem one byte from the remote side.
 * @param to_screen if the return is Q_EMUL_FSM_ONE_CHAR or
 * Q_EMUL_FSM_MANY_CHARS, then to_screen will have a character to display on
 * the screen.
 * @return one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
Q_EMULATION_STATUS vt52(const unsigned char from_modem, wchar_t * to_screen) {
    static unsigned char *count;
    static attr_t attributes;
    int new_row;
    int new_col;
    unsigned char from_modem2;

    /*
     * The VT52 spec only supports 7-bit ASCII. Strip the high bit off every
     * character.
     */
    from_modem2 = from_modem & 0x7F;

    DLOG(("STATE: %d CHAR: 0x%02x '%c'\n", scan_state, from_modem2,
          from_modem2));

vt52_start:

    switch (scan_state) {

    case SCAN_NONE:
        /*
         * ESC
         */
        if (from_modem2 == C_ESC) {
            save_char(from_modem2, to_screen);
            scan_state = SCAN_ESC;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * Only a few control chars to handle here.  CR and LF are in
         * emulation.c .
         */
        if (from_modem2 == 0x05) {

            DLOG(("Enquire\n"));

            /*
             * ENQ - transmit the answerback message.
             */
            qodem_write(q_child_tty_fd, get_option(Q_OPTION_ENQ_ANSWERBACK),
                        strlen(get_option(Q_OPTION_ENQ_ANSWERBACK)), Q_TRUE);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem2 == 0x08) {

            DLOG(("Backspace\n"));

            /*
             * Backspace.
             */
            cursor_left(1, Q_FALSE);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem2 == 0x09) {

            DLOG(("Tab\n"));

            /*
             * Tab.
             */
            while (q_status.cursor_x < 80) {
                cursor_right(1, Q_FALSE);
                if (q_status.cursor_x % 8 == 0) {
                    break;
                }
            }

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem2 == 0x7F) {

            DLOG(("Del\n"));

            /*
             * Del - consume but do nothing.
             */
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * Any other control characters.
         */
        if (iscntrl(from_modem2)) {
            /*
             * Consume but do nothing.
             */
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * This is a printable character.
         */
        *to_screen = map_character(from_modem2);
        return Q_EMUL_FSM_ONE_CHAR;

    case SCAN_Y_1:
        save_char(from_modem2, to_screen);
        scan_state = SCAN_Y_2;
        return Q_EMUL_FSM_NO_CHAR_YET;

    case SCAN_Y_2:
        /*
         * q_emul_buffer[0] = ESC
         * q_emul_buffer[1] = 'Y'
         */
        new_row = q_emul_buffer[2] - 32;
        new_col = from_modem2 - 32;
        if (new_row < 0) {
            new_row = 0;
        }
        if (new_col < 0) {
            new_col = 0;
        }

        DLOG(("Cursor position: %d %d\n", new_row, new_col));

        cursor_position(new_row, new_col);
        clear_state(to_screen);
        return Q_EMUL_FSM_NO_CHAR_YET;

    case SCAN_ESC:

        if (from_modem2 == 'A') {

            DLOG(("Cursor up\n"));

            cursor_up(1, Q_FALSE);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem2 == 'B') {

            DLOG(("Cursor down\n"));

            cursor_down(1, Q_FALSE);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem2 == 'C') {

            DLOG(("Cursor right\n"));

            cursor_right(1, Q_FALSE);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem2 == 'D') {

            DLOG(("Cursor left\n"));

            cursor_left(1, Q_FALSE);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem2 == 'E') {

            DLOG(("Erase entire screen\n"));

            /*
             * Cursor position to (0,0) and erase entire screen.
             */
            cursor_formfeed();
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem2 == 'F') {

            DLOG(("Graphics mode ON\n"));

            graphics_mode = Q_TRUE;
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem2 == 'G') {

            DLOG(("Graphics mode OFF\n"));

            graphics_mode = Q_FALSE;
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem2 == 'H') {

            DLOG(("Cursor home\n"));

            cursor_position(0, 0);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem2 == 'I') {

            DLOG(("Reverse linefeed\n"));

            /*
             * Move up one column, inserting a line if already at the top.
             */
            if (q_status.cursor_y == 0) {
                scroll_down(1);
            } else {
                cursor_up(1, Q_FALSE);
            }

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem2 == 'J') {

            DLOG(("Erase to end of screen\n"));

            /*
             * Erase from here to end of screen.
             */
            erase_screen(q_status.cursor_y, q_status.cursor_x,
                         HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1, Q_FALSE);

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem2 == 'K') {

            DLOG(("Erase to end of line\n"));

            /*
             * Erase from here to end of line.
             */
            erase_line(q_status.cursor_x, q_scrollback_current->length,
                       Q_FALSE);

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem2 == 'Y') {
            /*
             * Cursor position.
             */
            save_char(from_modem2, to_screen);
            scan_state = SCAN_Y_1;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem2 == 'Z') {

            DLOG(("DECID\n"));

            /*
             * Identify
             */

            /*
             * Note the VT100 and above will send <ESC>/Z, but the DECScope
             * manual claims the VT52 will send <ESC>/K if it does not have
             * an "integral electrolytic copier" (an internal printer that
             * used wet paper).
             */
            qodem_write(q_child_tty_fd, "\033/K", 3, Q_TRUE);

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem2 == '=') {

            DLOG(("Alternate keypad ON\n"));

            q_vt52_alternate_keypad_mode = Q_TRUE;
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem2 == '>') {

            DLOG(("Alternate keypad OFF\n"));

            q_vt52_alternate_keypad_mode = Q_FALSE;
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem2 == '[') {
            if (q_status.vt52_color == Q_TRUE) {
                /*
                 * Fall into SCAN_CSI only if VT52_COLOR is enabled.
                 */
                save_char(from_modem2, to_screen);
                scan_state = SCAN_CSI;
                return Q_EMUL_FSM_NO_CHAR_YET;
            }

            DLOG(("Hold screen mode ON\n"));

            q_status.hold_screen_mode = Q_TRUE;
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem2 == '\\') {

            DLOG(("Hold screen mode OFF\n"));

            q_status.hold_screen_mode = Q_FALSE;
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        break;

    case SCAN_CSI:
        /*
         * We are only going to support CSI Pn [ ; Pn ... ] m a.k.a. ANSI
         * Select Graphics Rendition.  We can see only a digit or 'm'.
         */
        if (q_isdigit(from_modem2)) {
            /*
             * Save the position for the counter.
             */
            count = q_emul_buffer + q_emul_buffer_n;
            save_char(from_modem2, to_screen);
            scan_state = SCAN_CSI_PARAM;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem2 == 'm') {
            /*
             * ESC [ m mean ESC [ 0 m, all attributes off.
             */

            DLOG(("ANSI SGR: reset\n"));

            q_current_color =
                Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * This means we entered HOLD SCREEN mode.
         */

        DLOG(("Hold screen mode ON\n"));

        q_status.hold_screen_mode = Q_TRUE;

        /*
         * Reprocess the character from the top.
         */
        clear_state(to_screen);
        goto vt52_start;

    case SCAN_CSI_PARAM:
        /*
         * Following through on the SGR code, we are now looking only for a
         * digit, semicolon, or 'm'
         *
         */
        if ((q_isdigit(from_modem2)) || (from_modem2 == ';')) {
            save_char(from_modem2, to_screen);
            scan_state = SCAN_CSI_PARAM;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem2 == 'm') {

            DLOG(("ANSI SGR: change text attributes\n"));

            /*
             * Text attributes.
             */
            if (ansi_color(&attributes, &count) == Q_TRUE) {
                q_current_color = attributes;
            } else {
                break;
            }

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        break;
    }

    /*
     * This point means we got most, but not all, of a sequence.
     */
    q_emul_buffer[q_emul_buffer_n] = from_modem2;
    q_emul_buffer_n++;
    *to_screen = q_emul_buffer[q_emul_buffer_i];
    q_emul_buffer_i++;
    scan_state = SCAN_NONE;

    /*
     * Special case: one character returns Q_EMUL_FSM_ONE_CHAR.
     */
    if (q_emul_buffer_n == 1) {
        q_emul_buffer_i = 0;
        q_emul_buffer_n = 0;
        return Q_EMUL_FSM_ONE_CHAR;
    }

    return Q_EMUL_FSM_MANY_CHARS;
}

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
wchar_t * vt52_keystroke(const int keystroke) {

    switch (keystroke) {

    case Q_KEY_ESCAPE:
        return L"\033";

    case Q_KEY_TAB:
        return L"\011";

    case Q_KEY_BACKSPACE:
        if (q_status.hard_backspace == Q_TRUE) {
            return L"\010";
        } else {
            return L"\177";
        }

    case Q_KEY_LEFT:
        return L"\033D";

    case Q_KEY_RIGHT:
        return L"\033C";

    case Q_KEY_UP:
        return L"\033A";

    case Q_KEY_DOWN:
        return L"\033B";

    case Q_KEY_HOME:
        return L"\033H";

    case Q_KEY_F(1):
        /*
         * PF1
         */
        return L"\033P";
    case Q_KEY_F(2):
        /*
         * PF2
         */
        return L"\033Q";
    case Q_KEY_F(3):
        /*
         * PF3
         */
        return L"\033R";
    case Q_KEY_F(4):
        /*
         * PF4
         */
        return L"\033S";
    case Q_KEY_F(5):
    case Q_KEY_F(6):
    case Q_KEY_F(7):
    case Q_KEY_F(8):
    case Q_KEY_F(9):
    case Q_KEY_F(10):
    case Q_KEY_F(11):
    case Q_KEY_F(12):
        return L"";
    case Q_KEY_F(13):
        /*
         * Shifted PF1
         */
        return L"\033P";
    case Q_KEY_F(14):
        /*
         * Shifted PF2
         */
        return L"\033Q";
    case Q_KEY_F(15):
        /*
         * Shifted PF3
         */
        return L"\033R";
    case Q_KEY_F(16):
        /*
         * Shifted PF4
         */
        return L"\033S";
    case Q_KEY_F(17):
    case Q_KEY_F(18):
    case Q_KEY_F(19):
    case Q_KEY_F(20):
    case Q_KEY_F(21):
    case Q_KEY_F(22):
    case Q_KEY_F(23):
    case Q_KEY_F(24):
        return L"";
    case Q_KEY_F(25):
        /*
         * Control PF1
         */
        return L"\033P";
    case Q_KEY_F(26):
        /*
         * Control PF2
         */
        return L"\033Q";
    case Q_KEY_F(27):
        /*
         * Control PF3
         */
        return L"\033R";
    case Q_KEY_F(28):
        /*
         * Control PF4
         */
        return L"\033S";
    case Q_KEY_F(29):
    case Q_KEY_F(30):
    case Q_KEY_F(31):
    case Q_KEY_F(32):
    case Q_KEY_F(33):
    case Q_KEY_F(34):
    case Q_KEY_F(35):
    case Q_KEY_F(36):
    case Q_KEY_PPAGE:
    case Q_KEY_NPAGE:
    case Q_KEY_IC:
    case Q_KEY_SIC:
        return L"";
    case Q_KEY_DC:
    case Q_KEY_SDC:
        return L"\177";
    case Q_KEY_END:
        return L"";

    case Q_KEY_PAD0:
        if (q_vt52_alternate_keypad_mode == Q_TRUE) {
            /*
             * Number pad 0
             */
            return L"\033?p";
        }
        return L"0";

    case Q_KEY_C1:
    case Q_KEY_PAD1:
        if (q_vt52_alternate_keypad_mode == Q_TRUE) {
            /*
             * Number pad 1
             */
            return L"\033?q";
        }
        return L"1";

    case Q_KEY_C2:
    case Q_KEY_PAD2:
        if (q_vt52_alternate_keypad_mode == Q_TRUE) {
            /*
             * Number pad 2
             */
            return L"\033?r";
        }
        return L"2";

    case Q_KEY_C3:
    case Q_KEY_PAD3:
        if (q_vt52_alternate_keypad_mode == Q_TRUE) {
            /*
             * Number pad 3
             */
            return L"\033?s";
        }
        return L"3";

    case Q_KEY_B1:
    case Q_KEY_PAD4:
        if (q_vt52_alternate_keypad_mode == Q_TRUE) {
            /*
             * Number pad 4
             */
            return L"\033?t";
        }
        return L"4";

    case Q_KEY_B2:
    case Q_KEY_PAD5:
        if (q_vt52_alternate_keypad_mode == Q_TRUE) {
            /*
             * Number pad 5
             */
            return L"\033?u";
        }
        return L"5";

    case Q_KEY_B3:
    case Q_KEY_PAD6:
        if (q_vt52_alternate_keypad_mode == Q_TRUE) {
            /*
             * Number pad 6
             */
            return L"\033?v";
        }
        return L"6";

    case Q_KEY_A1:
    case Q_KEY_PAD7:
        if (q_vt52_alternate_keypad_mode == Q_TRUE) {
            /*
             * Number pad 7
             */
            return L"\033?w";
        }
        return L"7";

    case Q_KEY_A2:
    case Q_KEY_PAD8:
        if (q_vt52_alternate_keypad_mode == Q_TRUE) {
            /*
             * Number pad 8
             */
            return L"\033?x";
        }
        return L"8";

    case Q_KEY_A3:
    case Q_KEY_PAD9:
        if (q_vt52_alternate_keypad_mode == Q_TRUE) {
            /*
             * Number pad 9
             */
            return L"\033?y";
        }
        return L"9";

    case Q_KEY_PAD_STOP:
        if (q_vt52_alternate_keypad_mode == Q_TRUE) {
            /*
             * Number pad .
             */
            return L"\033?n";
        }
        return L".";

    case Q_KEY_PAD_SLASH:
        /*
         * Number pad /
         */
        return L"/";

    case Q_KEY_PAD_STAR:
        /*
         * Number pad *
         */
        return L"*";

    case Q_KEY_PAD_MINUS:
        return L"-";

    case Q_KEY_PAD_PLUS:
        /*
         * Number pad +
         */
        return L"+";

    case Q_KEY_PAD_ENTER:
        /* Number pad Enter, fall through */
    case Q_KEY_ENTER:
        return L"\015";

    default:
        break;
    }

    return NULL;
}
