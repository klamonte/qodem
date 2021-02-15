/*
 * avatar.c
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
#include "avatar.h"

/* Set this to a not-NULL value to enable debug log. */
/* static const char * DLOGNAME = "avatar"; */
static const char * DLOGNAME = NULL;

/**
 * Scan states for the parser state machine.
 */
typedef enum SCAN_STATES {
    SCAN_NONE,
    SCAN_V,
    SCAN_H_1,
    SCAN_H_2,
    SCAN_A_1,
    SCAN_Y_1,
    SCAN_Y_2,
    SCAN_Y_EMIT,
    SCAN_V_Y_1,
    SCAN_V_Y_2,
    SCAN_V_Y_3,
    SCAN_V_Y_EMIT,
    SCAN_V_M_1,
    SCAN_V_M_2,
    SCAN_V_M_3,
    SCAN_V_M_4,
    SCAN_V_JK_1,
    SCAN_V_JK_2,
    SCAN_V_JK_3,
    SCAN_V_JK_4,
    SCAN_V_JK_5,
    SCAN_ESC,
    SCAN_CSI,
    SCAN_CSI_PARAM,
    SCAN_ANSI_FALLBACK,
    DUMP_UNKNOWN_SEQUENCE
} SCAN_STATE;

/* Current scanning state. */
static SCAN_STATE scan_state;

/* For the ^Y and ^V^Y sequences. */
static unsigned char y_char;
static int y_count;
static unsigned char * v_y_chars = NULL;
static int v_y_chars_i;
static int v_y_chars_n;

/* For the ^V^J and ^V^K sequences. */
static Q_BOOL v_jk_scrollup;
static int v_jk_numlines;
static int v_jk_upper;
static int v_jk_left;
static int v_jk_lower;
static int v_jk_right;

/**
 * ANSI fallback: the unknown escape sequence is copied here and then run
 * through the ANSI emulator.
 */
static unsigned char ansi_buffer[sizeof(q_emul_buffer)];
static int ansi_buffer_n;
static int ansi_buffer_i;

/**
 * Reset the emulation state.
 */
void avatar_reset() {
    scan_state = SCAN_NONE;
    if (v_y_chars != NULL) {
        Xfree(v_y_chars, __FILE__, __LINE__);
        v_y_chars = NULL;
    }
    v_y_chars_i = 0;
    v_y_chars_n = 0;
    DLOG(("avatar_reset()\n"));
}

/**
 * Reset the scan state for a new sequence.
 *
 * @param to_screen one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
static void clear_state(wchar_t * to_screen) {
    q_status.insert_mode = Q_FALSE;
    q_emul_buffer_n = 0;
    q_emul_buffer_i = 0;
    memset(q_emul_buffer, 0, sizeof(q_emul_buffer));
    scan_state = SCAN_NONE;
    *to_screen = 1;

    if (v_y_chars != NULL) {
        Xfree(v_y_chars, __FILE__, __LINE__);
        v_y_chars = NULL;
        v_y_chars_i = 0;
        v_y_chars_n = 0;
    }
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

/*
 * The AVATAR specification defines colors in terms of the CGA bitmask.  This
 * maps those bits to a curses color number.
 */
static short pc_to_curses_map[] = {
    Q_COLOR_BLACK,
    Q_COLOR_BLUE,
    Q_COLOR_GREEN,
    Q_COLOR_CYAN,
    Q_COLOR_RED,
    Q_COLOR_MAGENTA,

    /*
     * This is really brown
     */
    Q_COLOR_YELLOW,

    /*
     * Really light gray
     */
    Q_COLOR_WHITE

    /*
     * The bold colors are:
     *
     * dark gray
     * light blue
     * light green
     * light cyan
     * light red
     * light magenta
     * yellow
     * white
     */
};

/**
 * Set the current drawing color based on PC attribute.
 *
 * @param from_modem one byte from the remote side
 */
static void avatar_set_color(const unsigned char from_modem) {
    short fg;
    short bg;
    attr_t attr;

    /*
     * Set color
     */
    attr = Q_A_NORMAL;
    fg = from_modem & 0x07;
    bg = (from_modem >> 4) & 0x07;
    fg = pc_to_curses_map[fg];
    bg = pc_to_curses_map[bg];
    if ((from_modem & 0x08) != 0) {
        attr |= Q_A_BOLD;
    }
    if ((from_modem & 0x80) != 0) {
        attr |= Q_A_BLINK;
    }
    q_current_color = attr | color_to_attr((short) ((fg << 3) | bg));

    DLOG(("new color: %04x\n", (unsigned int) q_current_color));
}

/**
 * Push one byte through the AVATAR emulator.
 *
 * @param from_modem one byte from the remote side.
 * @param to_screen if the return is Q_EMUL_FSM_ONE_CHAR or
 * Q_EMUL_FSM_MANY_CHARS, then to_screen will have a character to display on
 * the screen.
 * @return one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
Q_EMULATION_STATUS avatar(const unsigned char from_modem, wchar_t * to_screen) {
    static unsigned char * count;
    static attr_t attributes;
    Q_EMULATION_STATUS rc;
    attr_t old_color;
    int old_x;
    int old_y;
    int i;

    DLOG(("STATE: %d CHAR: 0x%02x '%c'\n", scan_state, from_modem, from_modem));

avatar_start:

    switch (scan_state) {

    case SCAN_ANSI_FALLBACK:

        /*
         * From here on out we pass through ANSI until we don't get
         * Q_EMUL_FSM_NO_CHAR_YET.
         */

        DLOG(("ANSI FALLBACK ansi_buffer_i %d ansi_buffer_n %d\n",
                ansi_buffer_i, ansi_buffer_n));
        DLOG(("              q_emul_buffer_i %d q_emul_buffer_n %d\n",
                q_emul_buffer_i, q_emul_buffer_n));

        if (ansi_buffer_n == 0) {
            assert(ansi_buffer_i == 0);
            /*
             * We have already cleared the old buffer, now push one byte at a
             * time through ansi until it is finished with its state machine.
             */
            ansi_buffer[ansi_buffer_n] = from_modem;
            ansi_buffer_n++;
        }

        DLOG(("ANSI FALLBACK ansi()\n"));

        rc = Q_EMUL_FSM_NO_CHAR_YET;
        while (rc == Q_EMUL_FSM_NO_CHAR_YET) {
            rc = ansi(ansi_buffer[ansi_buffer_i], to_screen);

            DLOG(("ANSI FALLBACK ansi() RC %d\n", rc));

            if (rc != Q_EMUL_FSM_NO_CHAR_YET) {
                /*
                 * We can be ourselves again now.
                 */
                DLOG(("ANSI FALLBACK END\n"));
                scan_state = SCAN_NONE;
            }

            ansi_buffer_i++;
            if (ansi_buffer_i == ansi_buffer_n) {
                /*
                 * No more characters to send through ANSI.
                 */
                ansi_buffer_n = 0;
                ansi_buffer_i = 0;
                break;
            }
        }

        if (rc == Q_EMUL_FSM_MANY_CHARS) {
            /*
             * ANSI is dumping q_emul_buffer.  Finish the job.
             */
            scan_state = DUMP_UNKNOWN_SEQUENCE;
        }

        return rc;

    case SCAN_NONE:
        /*
         * ESC
         */
        if (from_modem == C_ESC) {
            save_char(from_modem, to_screen);
            scan_state = SCAN_ESC;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * ^V
         */
        if (from_modem == 0x16) {
            save_char(from_modem, to_screen);
            scan_state = SCAN_V;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * ^L
         */
        if (from_modem == 0x0C) {

            DLOG(("clear screen, home cursor\n"));

            /*
             * Cursor position to (0,0) and erase entire screen.
             */
            cursor_formfeed();
            q_current_color =
                Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * ^Y
         */
        if (from_modem == 0x19) {
            save_char(from_modem, to_screen);
            scan_state = SCAN_Y_1;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * Other control characters
         */
        if (iscntrl(from_modem)) {

            DLOG(("generic_handle_control_char(): control_char = 0x%02x\n",
                 from_modem));

            generic_handle_control_char(from_modem);
            *to_screen = 1;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        *to_screen = codepage_map_char(from_modem);
        return Q_EMUL_FSM_ONE_CHAR;

    case SCAN_A_1:
        /*
         * from_modem has new color attribute.
         */
        avatar_set_color(from_modem);

        clear_state(to_screen);
        return Q_EMUL_FSM_NO_CHAR_YET;

    case SCAN_H_1:
        save_char(from_modem, to_screen);
        scan_state = SCAN_H_2;
        return Q_EMUL_FSM_NO_CHAR_YET;

    case SCAN_H_2:
        /*
         * from_modem has new column value.
         */

        /*
         * Cursor position
         */

        DLOG(("cursor_position() %d %d\n", q_emul_buffer[2] - 1,
             from_modem - 1));

        old_y = q_emul_buffer[2] - 1;
        old_x = from_modem - 1;
        if (old_x < 0) {
            old_x = 0;
        }
        if (old_y < 0) {
            old_y = 0;
        }
        cursor_position(old_y, old_x);

        clear_state(to_screen);
        return Q_EMUL_FSM_NO_CHAR_YET;

    case SCAN_Y_1:
        save_char(from_modem, to_screen);
        y_char = from_modem;
        scan_state = SCAN_Y_2;
        return Q_EMUL_FSM_NO_CHAR_YET;

    case SCAN_Y_2:
        y_count = from_modem;

        DLOG(("RLE char '%c' count=%d\n", y_char, y_count));

        scan_state = SCAN_Y_EMIT;
        /*
         * Fall through ...
         */
repeat_loop:

    case SCAN_Y_EMIT:
        if (y_count == 0) {
            if (q_status.insert_mode == Q_TRUE) {
                /*
                 * Since clear_state() resets insert mode, we have to change
                 * it back after.
                 */
                clear_state(to_screen);
                q_status.insert_mode = Q_TRUE;
            } else {
                clear_state(to_screen);
            }

            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        y_count--;

        /*
         * Avatar allows repeated single characters to be control characters
         * too.  They have to be handled but not displayed.
         */
        if (iscntrl(y_char)) {

            DLOG(("REPEAT generic_handle_control_char(): control_char = 0x%02x\n",
                 y_char));

            generic_handle_control_char(y_char);
            goto repeat_loop;
        }

        *to_screen = codepage_map_char(y_char);
        return Q_EMUL_FSM_MANY_CHARS;

    case SCAN_V_JK_1:
        v_jk_numlines = from_modem;
        save_char(from_modem, to_screen);
        scan_state = SCAN_V_JK_2;
        return Q_EMUL_FSM_NO_CHAR_YET;

    case SCAN_V_JK_2:
        v_jk_upper = from_modem;
        save_char(from_modem, to_screen);
        scan_state = SCAN_V_JK_3;
        return Q_EMUL_FSM_NO_CHAR_YET;

    case SCAN_V_JK_3:
        v_jk_left = from_modem;
        save_char(from_modem, to_screen);
        scan_state = SCAN_V_JK_4;
        return Q_EMUL_FSM_NO_CHAR_YET;

    case SCAN_V_JK_4:
        v_jk_lower = from_modem;
        save_char(from_modem, to_screen);
        scan_state = SCAN_V_JK_5;
        return Q_EMUL_FSM_NO_CHAR_YET;

    case SCAN_V_JK_5:
        v_jk_right = from_modem;

        /*
         * Scroll a rectangular region.
         */
        DLOG(("scroll_rectangle() %s %d %d %d %d %d\n",
             (v_jk_scrollup == Q_TRUE ? "true" : "false"), v_jk_numlines,
             v_jk_upper, v_jk_left, v_jk_lower, v_jk_right));

        if ((v_jk_numlines == 0) || (v_jk_numlines > HEIGHT - STATUS_HEIGHT)) {
            erase_screen(0, 0, HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1, Q_FALSE);
        } else {
            if (v_jk_upper > 0) {
                v_jk_upper--;
            }
            if (v_jk_left > 0) {
                v_jk_left--;
            }
            if (v_jk_lower > 0) {
                v_jk_lower--;
            }
            if (v_jk_right > 0) {
                v_jk_right--;
            }

            if (v_jk_scrollup == Q_TRUE) {
                rectangle_scroll_up(v_jk_upper, v_jk_left, v_jk_lower,
                                    v_jk_right, v_jk_numlines);
            } else {
                rectangle_scroll_down(v_jk_upper, v_jk_left, v_jk_lower,
                                      v_jk_right, v_jk_numlines);
            }
        }

        clear_state(to_screen);
        return Q_EMUL_FSM_NO_CHAR_YET;

    case SCAN_V_Y_1:
        save_char(from_modem, to_screen);
        v_y_chars_n = from_modem;
        v_y_chars =
            (unsigned char *) Xmalloc(sizeof(unsigned char) * (v_y_chars_n + 1),
                                      __FILE__, __LINE__);
        memset(v_y_chars, 0, sizeof(unsigned char) * (v_y_chars_n + 1));
        v_y_chars_i = 0;

        scan_state = SCAN_V_Y_2;
        return Q_EMUL_FSM_NO_CHAR_YET;

    case SCAN_V_Y_2:
        save_char(from_modem, to_screen);
        v_y_chars[v_y_chars_i] = from_modem;
        v_y_chars_i++;
        if (v_y_chars_i == v_y_chars_n) {
            scan_state = SCAN_V_Y_3;
        }
        return Q_EMUL_FSM_NO_CHAR_YET;

    case SCAN_V_Y_3:
        y_count = from_modem;
        v_y_chars_i = 0;

        DLOG(("RLE pattern '%s' count=%d\n", v_y_chars, y_count));
        scan_state = SCAN_V_Y_EMIT;

        /*
         * Fall through ...
         */
    case SCAN_V_Y_EMIT:

        /*
         * It's possible to repeat the entire state machine...ick.
         */
        q_emul_repeat_state_count = v_y_chars_n * y_count;
        q_emul_repeat_state_buffer =
            (unsigned char *) Xmalloc(sizeof(unsigned char) *
                                      q_emul_repeat_state_count, __FILE__,
                                      __LINE__);
        while (y_count > 0) {
            memcpy(q_emul_repeat_state_buffer + (v_y_chars_n * (y_count - 1)),
                   v_y_chars, v_y_chars_n);
            y_count--;
        }
        Xfree(v_y_chars, __FILE__, __LINE__);
        v_y_chars = NULL;

        if (q_status.insert_mode == Q_TRUE) {
            /*
             * Since clear_state() resets insert mode, we have to change it
             * back after.
             */
            clear_state(to_screen);
            q_status.insert_mode = Q_TRUE;
        } else {
            clear_state(to_screen);
        }
        return Q_EMUL_FSM_REPEAT_STATE;

    case SCAN_V_M_1:
        save_char(from_modem, to_screen);
        scan_state = SCAN_V_M_2;
        return Q_EMUL_FSM_NO_CHAR_YET;

    case SCAN_V_M_2:
        save_char(from_modem, to_screen);
        scan_state = SCAN_V_M_3;
        return Q_EMUL_FSM_NO_CHAR_YET;

    case SCAN_V_M_3:
        save_char(from_modem, to_screen);
        scan_state = SCAN_V_M_4;
        return Q_EMUL_FSM_NO_CHAR_YET;

    case SCAN_V_M_4:
        /*
         * q_emul_buffer contains all the parameters:
         *
         * ^M
         * attr
         * lines
         * cols
         */
        assert(q_emul_buffer_n >= 5);
        DLOG(("clear area char='%c' attr=%02x lines=%d cols=%d\n",
             q_emul_buffer[2], q_emul_buffer[3], q_emul_buffer[4],
             from_modem));

        old_color = q_current_color;
        old_x = q_status.cursor_x;
        old_y = q_status.cursor_y;

        for (i = 0; i < from_modem; i++) {
            avatar_set_color(q_emul_buffer[3]);
            fill_line_with_character(q_status.cursor_x,
                                     q_status.cursor_x + q_emul_buffer[4],
                                     q_emul_buffer[2], Q_FALSE);
            q_current_color = old_color;
            if (q_status.cursor_y <= HEIGHT - STATUS_HEIGHT - 1) {
                cursor_down(1, Q_FALSE);
            }
        }

        q_current_color = old_color;
        cursor_position(old_y, old_x);

        clear_state(to_screen);
        return Q_EMUL_FSM_NO_CHAR_YET;

    case SCAN_V:

        /*
         * ^A
         */
        if (from_modem == 0x01) {
            save_char(from_modem, to_screen);
            scan_state = SCAN_A_1;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * ^B
         */
        if (from_modem == 0x02) {
            q_current_color |= Q_A_BLINK;
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * ^C
         */
        if (from_modem == 0x03) {
            cursor_up(1, Q_FALSE);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * ^D
         */
        if (from_modem == 0x04) {
            cursor_down(1, Q_FALSE);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * ^E
         */
        if (from_modem == 0x05) {
            cursor_left(1, Q_FALSE);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * ^F
         */
        if (from_modem == 0x06) {
            cursor_right(1, Q_FALSE);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * ^G
         */
        if (from_modem == 0x07) {
            /*
             * Erase from here to end of line.
             */
            erase_line(q_status.cursor_x, WIDTH - 1, Q_FALSE);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * ^H
         */
        if (from_modem == 0x08) {
            /*
             * First byte of a cursor position command.
             */
            save_char(from_modem, to_screen);
            scan_state = SCAN_H_1;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * ^I
         */
        if (from_modem == 0x09) {
            /*
             * Enable insert mode.  Call clear_state() first, then overwrite
             * the insert_mode flag.
             */
            clear_state(to_screen);
            q_status.insert_mode = Q_TRUE;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * ^J or ^K
         */
        if ((from_modem == 0x0a) || (from_modem == 0x0b)) {
            if (from_modem == 0x0a) {
                v_jk_scrollup = Q_TRUE;
            } else {
                v_jk_scrollup = Q_FALSE;
            }
            save_char(from_modem, to_screen);
            save_char(' ', to_screen);
            scan_state = SCAN_V_JK_1;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * ^L
         */
        if (from_modem == 0x0c) {
            save_char(from_modem, to_screen);
            save_char(' ', to_screen);
            scan_state = SCAN_V_M_2;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * ^M
         */
        if (from_modem == 0x0d) {
            save_char(from_modem, to_screen);
            scan_state = SCAN_V_M_1;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * ^N
         */
        if (from_modem == 0x0e) {
            delete_character(1);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * ^P
         */
        if (from_modem == 0x10) {
            /*
             * Disable insert mode.  We actually don't need to do anything
             * because clear_state() disables it anyway.
             */
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * ^Y
         */
        if (from_modem == 0x19) {
            save_char(from_modem, to_screen);
            scan_state = SCAN_V_Y_1;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        break;

    case SCAN_ESC:
        save_char(from_modem, to_screen);

        if (from_modem == '[') {
            if (q_status.avatar_color == Q_TRUE) {
                /*
                 * Fall into SCAN_CSI only if AVATAR_COLOR is enabled.
                 */
                scan_state = SCAN_CSI;
                return Q_EMUL_FSM_NO_CHAR_YET;
            }
        }
        break;

    case SCAN_CSI:
        save_char(from_modem, to_screen);

        /*
         * We are only going to support CSI Pn [ ; Pn ... ] m a.k.a. ANSI
         * Select Graphics Rendition.  We can see only a digit or 'm'.
         */
        if (q_isdigit(from_modem)) {
            /*
             * Save the position for the counter.
             */
            count = q_emul_buffer + q_emul_buffer_n - 1;
            scan_state = SCAN_CSI_PARAM;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'm') {
            /*
             * ESC [ m mean ESC [ 0 m, all attributes off.
             */
            q_current_color =
                Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }
        break;

    case SCAN_CSI_PARAM:
        save_char(from_modem, to_screen);
        /*
         * Following through on the SGR code, we are now looking only for a
         * digit, semicolon, or 'm'.
         */
        if ((q_isdigit(from_modem)) || (from_modem == ';')) {
            scan_state = SCAN_CSI_PARAM;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'm') {

            DLOG(("ANSI SGR: change text attributes\n"));

            /*
             * Text attributes
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

    case DUMP_UNKNOWN_SEQUENCE:

        DLOG(("DUMP_UNKNOWN_SEQUENCE q_emul_buffer_i %d q_emul_buffer_n %d\n",
                q_emul_buffer_i, q_emul_buffer_n));

        /*
         * Dump the string in q_emul_buffer
         */
        assert(q_emul_buffer_n > 0);

        *to_screen = codepage_map_char(q_emul_buffer[q_emul_buffer_i]);
        q_emul_buffer_i++;
        if (q_emul_buffer_i == q_emul_buffer_n) {
            /*
             * This was the last character.
             */
            q_emul_buffer_n = 0;
            q_emul_buffer_i = 0;
            memset(q_emul_buffer, 0, sizeof(q_emul_buffer));
            scan_state = SCAN_NONE;
            return Q_EMUL_FSM_ONE_CHAR;

        } else {
            return Q_EMUL_FSM_MANY_CHARS;
        }

    } /* switch (scan_state) */

    if (q_status.avatar_ansi_fallback == Q_TRUE) {
        /*
         * Process through ANSI fallback code.
         *
         * This is UGLY AS HELL, but lots of BBSes assume that Avatar
         * emulators will "fallback" to ANSI for sequences they don't
         * understand.
         */
        scan_state = SCAN_ANSI_FALLBACK;
        DLOG(("ANSI FALLBACK BEGIN\n"));

        /*
         * From here on out we pass through ANSI until we don't get
         * Q_EMUL_FSM_NO_CHAR_YET.
         */
        memcpy(ansi_buffer, q_emul_buffer, q_emul_buffer_n);
        ansi_buffer_i = 0;
        ansi_buffer_n = q_emul_buffer_n;
        q_emul_buffer_i = 0;
        q_emul_buffer_n = 0;

        DLOG(("ANSI FALLBACK ansi()\n"));

        /*
         * Run through the emulator again
         */
        assert(ansi_buffer_n > 0);
        goto avatar_start;

    } else {

        DLOG(("Unknown sequence, and no ANSI fallback\n"));
        scan_state = DUMP_UNKNOWN_SEQUENCE;

        /*
         * This point means we got most, but not all, of a sequence.
         */
        *to_screen = codepage_map_char(q_emul_buffer[q_emul_buffer_i]);
        q_emul_buffer_i++;

        /*
         * Special case: one character returns Q_EMUL_FSM_ONE_CHAR.
         */
        if (q_emul_buffer_n == 1) {
            q_emul_buffer_i = 0;
            q_emul_buffer_n = 0;
            return Q_EMUL_FSM_ONE_CHAR;
        }

        /*
         * Tell the emulator layer that I need to be called many more times
         * to dump the string in q_emul_buffer.
         */
        return Q_EMUL_FSM_MANY_CHARS;
    }

    /*
     * Should never get here.
     */
    abort();
    return Q_EMUL_FSM_NO_CHAR_YET;
}
