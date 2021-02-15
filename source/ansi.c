/*
 * ansi.c
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

/*
 * ANSI music:  ESC [ M <notes> ^N
 *
 * I actually saw a Synchronet BBS using ESC n, which linux calls LS2 (invoke
 * G2 character set).  What's that about?
 *
 * Also saw DECAWM (CSI ? 7 h) - turn line wrap on
 */

#include "common.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "qodem.h"
#include "console.h"
#include "music.h"
#include "states.h"
#include "screen.h"
#include "netclient.h"
#include "ansi.h"

/* Set this to a not-NULL value to enable debug log. */
/* static const char * DLOGNAME = "ansi"; */
static const char * DLOGNAME = NULL;

#define ANSI_RESPONSE_LENGTH    16
#define ANSI_MUSIC_BUFFER_SIZE  1024

/**
 * Scan states for the parser state machine.
 */
typedef enum SCAN_STATES {
    SCAN_NONE,
    SCAN_ESC,
    SCAN_START_SEQUENCE,
    SCAN_COUNT,
    SCAN_COUNT_TWO,
    SCAN_COUNT_MANY,
    SCAN_MUSIC
} SCAN_STATE;

/**
 * Current scanning state.
 */
static SCAN_STATE scan_state;

/**
 * Saved cursor X used by save/restore cursor position.
 */
static int saved_cursor_x;

/**
 * Saved cursor Y used by save/restore cursor position.
 */
static int saved_cursor_y;

/**
 * Repeat character.
 */
static wchar_t rep_character;

/**
 * If true, we saw the ANSI.SYS private mode byte '='.
 */
static Q_BOOL private_mode_flag;

/**
 * If true, we saw the DEC private mode byte '?'.
 */
static Q_BOOL dec_private_mode_flag;

/**
 * If true, we saw a private mode byte that will not be supported.  For now
 * that is only ' '.
 */
static Q_BOOL discard_mode_flag;

/* ANSI music buffer */
static unsigned char music_buffer[ANSI_MUSIC_BUFFER_SIZE];
static int music_buffer_n;

/*
 * For ANSI animation support, if I print a character and the attribute is
 * different from the previous printed character's attribute, then flush the
 * screen.
 */
static attr_t old_character_color;

/**
 * Reset the emulation state.
 */
void ansi_reset() {
    scan_state = SCAN_NONE;
    saved_cursor_x = q_status.cursor_x;
    saved_cursor_y = q_status.cursor_y;
    old_character_color = q_current_color;
    private_mode_flag = Q_FALSE;
    dec_private_mode_flag = Q_FALSE;
    discard_mode_flag = Q_FALSE;
    DLOG(("ansi_reset()\n"));
}

/**
 * Reset the scan state for a new sequence.
 *
 * @param to_screen one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
static void clear_state(wchar_t * to_screen) {
    private_mode_flag = Q_FALSE;
    dec_private_mode_flag = Q_FALSE;
    discard_mode_flag = Q_FALSE;
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
 * Figure out next numbered parameter from buffer.
 *
 * @param count the pointer to the next byte to read
 * @return the CSI parameter's integer value, or -1 on error
 */
static int ansi_ps(unsigned char ** count) {
    /*
     * People are using up to 4 digits in parameters in the wild.  Parse up
     * to 6 digits.
     */
    char ch[7] = "\0\0\0\0\0\0\0";

    /*
     * First digit
     */
    if (q_isdigit(**count)) {
        ch[0] = **count;
        (*count)++;
    } else {
        return -1;
    }

    /*
     * See if the second char is a digit
     */
    if (q_isdigit(**count)) {
        ch[1] = **count;
        (*count)++;
    }

    /*
     * See if the third char is a digit
     */
    if (q_isdigit(**count)) {
        ch[2] = **count;
        (*count)++;
    }

    /*
     * See if the fourth char is a digit
     */
    if (q_isdigit(**count)) {
        ch[3] = **count;
        (*count)++;
    }

    /*
     * See if the fifth char is a digit
     */
    if (q_isdigit(**count)) {
        ch[4] = **count;
        (*count)++;
    }

    /*
     * See if the sixth char is a digit
     */
    if (q_isdigit(**count)) {
        ch[5] = **count;
        (*count)++;
    }

    /*
     * Error out from here on
     */
    if (q_isdigit(**count)) {
        return -1;
    }

    return atoi(ch);
}

/**
 * Parse cursor position command.
 *
 * @param count the pointer to the next byte to read
 * @return true if the parameters were parsed successfully
 */
static Q_BOOL ansi_position(unsigned char ** count) {
    int ps;
    int new_row = -1;
    int new_col = -1;

    while (*count < q_emul_buffer + sizeof(q_emul_buffer)) {
        if ((new_row != -1) && (new_col != -1)) {
            return Q_FALSE;
        }

        /*
         * Check row
         */
        if ((new_row == -1) && (**count == ';')) {
            /*
             * Omitted row means assume 0
             */
            new_row = 0;
            (*count)++;
            continue;
        } else if (new_row == -1) {
            ps = ansi_ps(count);
            if (ps == -1) {
                return Q_FALSE;
            }
            new_row = ps - 1;
            if (new_row < 0) {
                /*
                 * I've seen a live BBS send a "CSI 0;31f" so I have to
                 * support this...
                 */
                new_row = 0;
            }
        }

        /*
         * Check column
         */
        if (strlen((char *) *count) == 0) {
            /*
             * Column wasn't specified, so assume 0
             */
            new_col = 0;
        } else if (**count != ';') {
            /*
             * Error:  code was "CSI [ Pn Xf" where X isn't semicolon
             */
            return Q_FALSE;
        } else {
            /*
             * Push past the semicolon
             */
            (*count)++;

            /*
             * Grab the number argument
             */
            ps = ansi_ps(count);
            if (ps == -1) {
                /*
                 * Number argument is invalid
                 */
                return Q_FALSE;
            }
            new_col = ps - 1;
            if (new_col < 0) {
                /*
                 * I've seen a live BBS send a "CSI 0;31f" so I have * to
                 * support this...
                 */
                new_col = 0;
            }
        }

        /*
         * Push past the string
         */
        (*count)++;
        if (**count == '\0') {
            /*
             * We are at the correct exit point
             */
            break;
        }
    } /* while (*count < q_emul_buffer + sizeof(q_emul_buffer)) */

    cursor_position(new_row, new_col);
    DLOG(("cursor_position(): %d %d\n", new_row, new_col));
    return Q_TRUE;
}

/**
 * Insert blank characters at current position.
 *
 * @param count the pointer to the next byte to read
 * @return true if the parameters were parsed successfully
 */
static Q_BOOL ansi_ich(unsigned char ** count) {
    int ps;
    int insert_count = -1;

    while (*count < q_emul_buffer + sizeof(q_emul_buffer)) {
        /*
         * Technically we can have more than one parameter, but this is
         * ANSI.SYS, not real ANSI.  Error out if we see more than one
         * parameter.
         */
        if (insert_count != -1) {
            return Q_FALSE;
        }

        /*
         * Check insert_count
         */
        if ((insert_count == -1) && (**count == ';')) {
            /*
             * Omitted parameter means assume 1
             */
            insert_count = 1;
            (*count)++;
            continue;
        } else if (insert_count == -1) {
            ps = ansi_ps(count);
            if (ps == -1) {
                return Q_FALSE;
            }
            insert_count = ps;
            if (insert_count < 0) {
                return Q_FALSE;
            }
        }

        /*
         * Check end of string
         */
        if (strlen((char *) *count) == 0) {
            /*
             * The correct exit point
             */
            break;
        } else if (**count != ';') {
            /*
             * Error:  code was "CSI [ Pn X@" where X isn't semicolon
             */
            return Q_FALSE;
        } else {
            /*
             * Push past the semicolon
             */
            (*count)++;
        }

        /*
         * Push past the string
         */
        (*count)++;
        if (**count == '\0') {
            /*
             * We are at the correct exit point
             */
            break;
        }
    } /* while (*count < q_emul_buffer + sizeof(q_emul_buffer)) */

    insert_blanks(insert_count);
    return Q_TRUE;
}

/**
 * Delete characters at current position.
 *
 * @param count the pointer to the next byte to read
 * @return true if the parameters were parsed successfully
 */
static Q_BOOL ansi_dch(unsigned char ** count) {
    int ps;
    int delete_count = -1;

    while (*count < q_emul_buffer + sizeof(q_emul_buffer)) {
        /*
         * Technically we can have more than one parameter, but this is
         * ANSI.SYS, not real ANSI.  Error out if we see more than one
         * parameter.
         */
        if (delete_count != -1) {
            return Q_FALSE;
        }

        /*
         * Check delete_count
         */
        if ((delete_count == -1) && (**count == ';')) {
            /*
             * Omitted parameter means assume 1
             */
            delete_count = 1;
            (*count)++;
            continue;
        } else if (delete_count == -1) {
            ps = ansi_ps(count);
            if (ps == -1) {
                return Q_FALSE;
            }
            delete_count = ps;
            if (delete_count < 0) {
                return Q_FALSE;
            }
        }

        /*
         * Check end of string
         */
        if (strlen((char *) *count) == 0) {
            /*
             * The correct exit point
             */
            break;
        } else if (**count != ';') {
            /*
             * Error:  code was "CSI [ Pn XP" where X isn't semicolon
             */
            return Q_FALSE;
        } else {
            /*
             * Push past the semicolon
             */
            (*count)++;
        }

        /*
         * Push past the string
         */
        (*count)++;
        if (**count == '\0') {
            /*
             * We are at the correct exit point
             */
            break;
        }
    } /* while (*count < q_emul_buffer + sizeof(q_emul_buffer)) */

    delete_character(delete_count);
    return Q_TRUE;
}

/**
 * Insert lines.
 *
 * @param count the pointer to the next byte to read
 * @return true if the parameters were parsed successfully
 */
static Q_BOOL ansi_il(unsigned char ** count) {
    int ps;
    int insert_count = -1;

    while (*count < q_emul_buffer + sizeof(q_emul_buffer)) {
        /*
         * Technically we can have more than one parameter, but this is
         * ANSI.SYS, not real ANSI.  Error out if we see more than one
         * parameter.
         */
        if (insert_count != -1) {
            return Q_FALSE;
        }

        /*
         * Check insert_count
         */
        if ((insert_count == -1) && (**count == ';')) {
            /*
             * Omitted parameter means assume 1
             */
            insert_count = 1;
            (*count)++;
            continue;
        } else if (insert_count == -1) {
            ps = ansi_ps(count);
            if (ps == -1) {
                return Q_FALSE;
            }
            insert_count = ps;
            if (insert_count < 0) {
                return Q_FALSE;
            }
        }

        /*
         * Check end of string
         */
        if (strlen((char *) *count) == 0) {
            /*
             * The correct exit point
             */
            break;
        } else if (**count != ';') {
            /*
             * Error:  code was "CSI [ Pn XL" where X isn't semicolon
             */
            return Q_FALSE;
        } else {
            /*
             * Push past the semicolon
             */
            (*count)++;
        }

        /*
         * Push past the string
         */
        (*count)++;
        if (**count == '\0') {
            /*
             * We are at the correct exit point
             */
            break;
        }
    } /* while (*count < q_emul_buffer + sizeof(q_emul_buffer)) */

    /*
     * I can get the same effect with a scroll-down
     */
    scrolling_region_scroll_down(q_status.cursor_y,
                                 q_status.scroll_region_bottom, insert_count);

    return Q_TRUE;
}

/**
 * Delete lines.
 *
 * @param count the pointer to the next byte to read
 * @return true if the parameters were parsed successfully
 */
static Q_BOOL ansi_dl(unsigned char ** count) {
    int ps;
    int delete_count = -1;

    while (*count < q_emul_buffer + sizeof(q_emul_buffer)) {
        /*
         * Technically we can have more than one parameter, but this is
         * ANSI.SYS, not real ANSI.  Error out if we see more than one
         * parameter.
         */
        if (delete_count != -1) {
            return Q_FALSE;
        }

        /*
         * Check delete_count
         */
        if ((delete_count == -1) && (**count == ';')) {
            /*
             * Omitted parameter means assume 1
             */
            delete_count = 1;
            (*count)++;
            continue;
        } else if (delete_count == -1) {
            ps = ansi_ps(count);
            if (ps == -1) {
                return Q_FALSE;
            }
            delete_count = ps;
            if (delete_count < 0) {
                return Q_FALSE;
            }
        }

        /*
         * Check end of string
         */
        if (strlen((char *) *count) == 0) {
            /*
             * The correct exit point
             */
            break;
        } else if (**count != ';') {
            /*
             * Error:  code was "CSI [ Pn XM" where X isn't semicolon
             */
            return Q_FALSE;
        } else {
            /*
             * Push past the semicolon
             */
            (*count)++;
        }

        /*
         * Push past the string
         */
        (*count)++;
        if (**count == '\0') {
            /*
             * We are at the correct exit point
             */
            break;
        }
    } /* while (*count < q_emul_buffer + sizeof(q_emul_buffer)) */

    /*
     * I can get the same effect with a scroll-up
     */
    scrolling_region_scroll_up(q_status.cursor_y, q_status.scroll_region_bottom,
                               delete_count);

    return Q_TRUE;
}

/**
 * Switch to row #, same column.
 *
 * @param count the pointer to the next byte to read
 * @return true if the parameters were parsed successfully
 */
static Q_BOOL ansi_vpa(unsigned char ** count) {
    int ps;
    int new_row = -1;

    while (*count < q_emul_buffer + sizeof(q_emul_buffer)) {
        /*
         * Technically we can have more than one parameter, but this is
         * ANSI.SYS, not real ANSI.  Error out if we see more than one
         * parameter.
         */
        if (new_row != -1) {
            return Q_FALSE;
        }

        /*
         * Check new_row
         */
        if ((new_row == -1) && (**count == ';')) {
            /*
             * Omitted parameter means assume 0
             */
            new_row = 0;
            (*count)++;
            continue;
        } else if (new_row == -1) {
            ps = ansi_ps(count);
            if (ps == -1) {
                return Q_FALSE;
            }
            new_row = ps - 1;
            if (new_row < 0) {
                return Q_FALSE;
            }
        }

        /*
         * Check end of string
         */
        if (strlen((char *) *count) == 0) {
            /*
             * The correct exit point
             */
            break;
        } else if (**count != ';') {
            /*
             * Error:  code was "CSI [ Pn Xd" where X isn't semicolon
             */
            return Q_FALSE;
        } else {
            /*
             * Push past the semicolon
             */
            (*count)++;
        }

        /*
         * Push past the string
         */
        (*count)++;
        if (**count == '\0') {
            /*
             * We are at the correct exit point
             */
            break;
        }
    } /* while (*count < q_emul_buffer + sizeof(q_emul_buffer)) */

    cursor_position(new_row, q_status.cursor_x);
    DLOG(("cursor_position(): %d %d\n", new_row, q_status.cursor_x));
    return Q_TRUE;
}

/**
 * Horizontal tab.
 *
 * @param count the pointer to the next byte to read
 * @return true if the parameters were parsed successfully
 */
static Q_BOOL ansi_cht(unsigned char ** count) {
    int ps;
    int tab_count = -1;
    int i;

    while (*count < q_emul_buffer + sizeof(q_emul_buffer)) {
        /*
         * Technically we can have more than one parameter, but this is
         * ANSI.SYS, not real ANSI.  Error out if we see more than one
         * parameter.
         */
        if (tab_count != -1) {
            return Q_FALSE;
        }

        /*
         * Check tab_count
         */
        if ((tab_count == -1) && (**count == ';')) {
            /*
             * Omitted parameter means assume 1
             */
            tab_count = 1;
            (*count)++;
            continue;
        } else if (tab_count == -1) {
            ps = ansi_ps(count);
            if (ps == -1) {
                return Q_FALSE;
            }
            tab_count = ps - 1;
            if (tab_count < 0) {
                return Q_FALSE;
            }
        }

        /*
         * Check end of string
         */
        if (strlen((char *) *count) == 0) {
            /*
             * The correct exit point
             */
            break;
        } else if (**count != ';') {
            /*
             * Error:  code was "CSI [ Pn XI" where X isn't semicolon
             */
            return Q_FALSE;
        } else {
            /*
             * Push past the semicolon
             */
            (*count)++;
        }

        /*
         * Push past the string
         */
        (*count)++;
        if (**count == '\0') {
            /*
             * We are at the correct exit point
             */
            break;
        }
    } /* while (*count < q_emul_buffer + sizeof(q_emul_buffer)) */

    DLOG(("CHT: %d\n", tab_count));
    for (i = 0; i < tab_count; i++) {
        while (q_status.cursor_x < 79) {
            print_character(' ');
            if (q_status.cursor_x % 8 == 0) {
                break;
            }
        }
    }

    return Q_TRUE;
}

/**
 * Switch to column #, same row.
 *
 * @param count the pointer to the next byte to read
 * @return true if the parameters were parsed successfully
 */
static Q_BOOL ansi_cha(unsigned char ** count) {
    int ps;
    int new_col = -1;

    while (*count < q_emul_buffer + sizeof(q_emul_buffer)) {
        /*
         * Technically we can have more than one parameter, but this is
         * ANSI.SYS, not real ANSI.  Error out if we see more than one
         * parameter.
         */
        if (new_col != -1) {
            return Q_FALSE;
        }

        /*
         * Check new_col
         */
        if ((new_col == -1) && (**count == ';')) {
            /*
             * Omitted parameter means assume 0
             */
            new_col = 0;
            (*count)++;
            continue;
        } else if (new_col == -1) {
            ps = ansi_ps(count);
            if (ps == -1) {
                return Q_FALSE;
            }
            new_col = ps - 1;
            if (new_col < 0) {
                return Q_FALSE;
            }
        }

        /*
         * Check end of string
         */
        if (strlen((char *) *count) == 0) {
            /*
             * The correct exit point
             */
            break;
        } else if (**count != ';') {
            /*
             * Error:  code was "CSI [ Pn XG" where X isn't semicolon
             */
            return Q_FALSE;
        } else {
            /*
             * Push past the semicolon
             */
            (*count)++;
        }

        /*
         * Push past the string
         */
        (*count)++;
        if (**count == '\0') {
            /*
             * We are at the correct exit point
             */
            break;
        }
    } /* while (*count < q_emul_buffer + sizeof(q_emul_buffer)) */

    cursor_position(q_status.cursor_y, new_col);
    DLOG(("cursor_position(): %d %d\n", q_status.cursor_y, new_col));
    return Q_TRUE;
}

/**
 * Repeat character.
 *
 * @param count the pointer to the next byte to read
 * @return true if the parameters were parsed successfully
 */
static Q_BOOL ansi_rep(unsigned char ** count) {
    int i;
    int ps;
    int rep_count = -1;

    while (*count < q_emul_buffer + sizeof(q_emul_buffer)) {
        /*
         * Technically we can have more than one parameter, but this is
         * ANSI.SYS, not real ANSI.  Error out if we see more than one
         * parameter.
         */
        if (rep_count != -1) {
            return Q_FALSE;
        }

        /*
         * Check rep_count
         */
        if ((rep_count == -1) && (**count == ';')) {
            /*
             * Omitted parameter means assume 1
             */
            rep_count = 1;
            (*count)++;
            continue;
        } else if (rep_count == -1) {
            ps = ansi_ps(count);
            if (ps == -1) {
                return Q_FALSE;
            }
            rep_count = ps;
            if (rep_count < 0) {
                return Q_FALSE;
            }
        }

        /*
         * Check end of string
         */
        if (strlen((char *) *count) == 0) {
            /*
             * The correct exit point
             */
            break;
        } else if (**count != ';') {
            /*
             * Error:  code was "CSI [ Pn Xb" where X isn't semicolon
             */
            return Q_FALSE;
        } else {
            /*
             * Push past the semicolon
             */
            (*count)++;
        }

        /*
         * Push past the string
         */
        (*count)++;
        if (**count == '\0') {
            /*
             * We are at the correct exit point
             */
            break;
        }
    } /* while (*count < q_emul_buffer + sizeof(q_emul_buffer)) */

    DLOG(("ANSI REP: %d\n", rep_count));
    for (i = 0; i < rep_count; i++) {
        print_character(rep_character);
    }

    return Q_TRUE;
}

/**
 * Set an ANSI.SYS mode.
 *
 * @param count the pointer to the next byte to read
 * @param set_mode if true, set ('h'), otherwise reset ('l')
 * @return true if the parameters were parsed successfully
 */
static Q_BOOL ansi_set_mode(unsigned char ** count, Q_BOOL set_mode) {
    int ps;
    int new_mode = -1;

    while (*count < q_emul_buffer + sizeof(q_emul_buffer)) {
        /*
         * Technically we can have more than one parameter, but this is
         * ANSI.SYS, not real ANSI.  Error out if we see more than one
         * parameter.
         */
        if (new_mode != -1) {
            return Q_FALSE;
        }

        /*
         * Check new_mode
         */
        if ((new_mode == -1) && (**count == ';')) {
            /*
             * Omitted parameter means assume 0
             */
            new_mode = 0;
            (*count)++;
            continue;
        } else if (new_mode == -1) {
            ps = ansi_ps(count);
            if (ps == -1) {
                return Q_FALSE;
            }
            new_mode = ps;
            if (new_mode < 0) {
                return Q_FALSE;
            }
        }

        /*
         * Check end of string
         */
        if (strlen((char *) *count) == 0) {
            /*
             * The correct exit point
             */
            break;
        } else if (**count != ';') {
            /*
             * Error: code was "CSI [ Pn Xh" or "CSI [ Pn Xl" where X isn't
             * semicolon
             */
            return Q_FALSE;
        } else {
            /*
             * Push past the semicolon
             */
            (*count)++;
        }

        /*
         * Push past the string
         */
        (*count)++;
        if (**count == '\0') {
            /*
             * We are at the correct exit point
             */
            break;
        }
    } /* while (*count < q_emul_buffer + sizeof(q_emul_buffer)) */

    switch (new_mode) {
    case 7:
        if ((set_mode == Q_TRUE) &&
            ((private_mode_flag == Q_TRUE) || (dec_private_mode_flag == Q_TRUE))
        ) {

            /*
             * Enable line wrap
             */
            DLOG(("SET: %d ENABLE LINE WRAP\n", new_mode));
            q_status.line_wrap = Q_TRUE;
            break;
        } else if ((set_mode == Q_FALSE) &&
            ((private_mode_flag == Q_TRUE) || (dec_private_mode_flag == Q_TRUE))
        ) {

            /*
             * Disable line wrap
             */
            DLOG(("RESET: %d DISABLE LINE WRAP\n", new_mode));
            q_status.line_wrap = Q_FALSE;
            break;
        }

        /*
         * Fall through...
         */

    default:
        if (set_mode == Q_TRUE) {
            DLOG(("SET: %d NOP\n", new_mode));
        } else {
            DLOG(("RESET: %d NOP\n", new_mode));
        }
        break;
    }

    return Q_TRUE;
}

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
Q_BOOL ansi_color(attr_t * output, unsigned char ** count) {
    int ps;
    short foreground, background;
    short curses_color;

    /*
     * Strip the color off *output
     */
    *output &= NO_COLOR_MASK;

    /*
     * Pull the current foreground and background
     */
    curses_color = color_from_attr(q_current_color);
    foreground = (curses_color & 0x38) >> 3;
    background = curses_color & 0x07;

    DLOG(("sgr(): foreground=%02x background=%02x initial *output=%08x\n",
          foreground, background, *output));
    DLOG(("sgr(): Pn...Pn = "));

    while (*count < q_emul_buffer + sizeof(q_emul_buffer)) {
        ps = ansi_ps(count);
        if (ps == -1) {
            return Q_FALSE;
        }
        DLOG2(("%d ", ps));

        switch (ps) {

        case 0:
            *output = Q_A_NORMAL;
            foreground = q_text_colors[Q_COLOR_CONSOLE_TEXT].fg;
            background = q_text_colors[Q_COLOR_CONSOLE_TEXT].bg;
            if (q_text_colors[Q_COLOR_CONSOLE_TEXT].bold == Q_TRUE) {
                *output |= Q_A_BOLD;
            }
            break;
        case 1:
            *output |= Q_A_BOLD;
            break;
        case 2:
            *output |= Q_A_DIM;
            break;
        case 4:
            *output |= Q_A_UNDERLINE;
            break;
        case 5:
            *output |= Q_A_BLINK;
            break;
        case 7:
            *output |= Q_A_REVERSE;
            break;
        case 21:
        case 22:
            *output &= ~(Q_A_DIM | Q_A_BOLD);
            break;
        case 24:
            *output &= ~Q_A_UNDERLINE;
            break;
        case 25:
            *output &= ~Q_A_BLINK;
            break;
        case 27:
            *output &= ~Q_A_REVERSE;
            break;
        case 30:
            foreground = Q_COLOR_BLACK;
            break;
        case 31:
            foreground = Q_COLOR_RED;
            break;
        case 32:
            foreground = Q_COLOR_GREEN;
            break;
        case 33:
            foreground = Q_COLOR_YELLOW;
            break;
        case 34:
            foreground = Q_COLOR_BLUE;
            break;
        case 35:
            foreground = Q_COLOR_MAGENTA;
            break;
        case 36:
            foreground = Q_COLOR_CYAN;
            break;
        case 37:
            foreground = Q_COLOR_WHITE;
            break;
        case 38:
            foreground = q_text_colors[Q_COLOR_CONSOLE_TEXT].fg;
            if (q_text_colors[Q_COLOR_CONSOLE_TEXT].bold == Q_TRUE) {
                *output |= Q_A_BOLD;
            }
            *output |= Q_A_UNDERLINE;
            break;
        case 39:
            foreground = q_text_colors[Q_COLOR_CONSOLE_TEXT].fg;
            if (q_text_colors[Q_COLOR_CONSOLE_TEXT].bold == Q_TRUE) {
                *output |= Q_A_BOLD;
            }
            *output &= ~Q_A_UNDERLINE;
            break;
        case 40:
            background = Q_COLOR_BLACK;
            break;
        case 41:
            background = Q_COLOR_RED;
            break;
        case 42:
            background = Q_COLOR_GREEN;
            break;
        case 43:
            background = Q_COLOR_YELLOW;
            break;
        case 44:
            background = Q_COLOR_BLUE;
            break;
        case 45:
            background = Q_COLOR_MAGENTA;
            break;
        case 46:
            background = Q_COLOR_CYAN;
            break;
        case 47:
            background = Q_COLOR_WHITE;
            break;
        case 49:
            background = q_text_colors[Q_COLOR_CONSOLE_TEXT].bg;
            *output &= ~Q_A_UNDERLINE;
            break;
        default:
            /*
             * Ignore unknown options
             */
            break;
        }

        if (**count != ';') {

            DLOG2(("\n"));

            /*
             * No more processing
             */
            curses_color = (foreground << 3) | background;

            DLOG(("sgr(): new foreground=%02x new background=%02x\n",
                  foreground, background));
            DLOG2(("SGR: old color=%08x ", q_current_color));

            *output |= color_to_attr(curses_color);

            DLOG2(("curses_color=%02x *output=%08x\n", curses_color, *output));
            return Q_TRUE;
        }

        (*count)++;
    } /* while (*count < q_emul_buffer + sizeof(q_emul_buffer)) */

    return Q_FALSE;
}

/**
 * Push one byte through the ANSI emulator.
 *
 * @param from_modem one byte from the remote side.
 * @param to_screen if the return is Q_EMUL_FSM_ONE_CHAR or
 * Q_EMUL_FSM_MANY_CHARS, then to_screen will have a character to display on
 * the screen.
 * @return one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
Q_EMULATION_STATUS ansi(const unsigned char from_modem, wchar_t * to_screen) {
    static unsigned char * count;
    static attr_t attributes;
    int i;
    char response_buffer[ANSI_RESPONSE_LENGTH];

    DLOG(("STATE: %d CHAR: 0x%02x '%c'\n", scan_state, from_modem, from_modem));

    switch (scan_state) {

    case SCAN_NONE:
        /*
         * ESC
         */
        if (from_modem == C_ESC) {
            save_char(from_modem, to_screen);
            scan_state = SCAN_ESC;
            attributes = q_current_color;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * Control characters
         */
        if (iscntrl(from_modem)) {
            DLOG(("generic_handle_control_char(): control_char = 0x%02x\n",
                  from_modem));
            generic_handle_control_char(from_modem);
            *to_screen = 1;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * Print character
         */
        *to_screen = codepage_map_char(from_modem);
        rep_character = codepage_map_char(from_modem);

        /*
         * Special case: for ANSI animation, force the screen to repaint if
         * this character has a different color than the last character.
         */
        if ((q_status.ansi_animate == Q_TRUE) &&
            (old_character_color != q_current_color)
        ) {
            q_screen_dirty = Q_TRUE;
            refresh_handler();
            old_character_color = q_current_color;
        }

        return Q_EMUL_FSM_ONE_CHAR;

    case SCAN_ESC:
        /*
         * Looking for '['
         */
        if (from_modem == '[') {
            save_char(from_modem, to_screen);
            scan_state = SCAN_START_SEQUENCE;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'Z') {
            /*
             * Identify
             */

            /*
             * Send string directly to remote side
             */
            qodem_write(q_child_tty_fd, "\033[?1;2c", 3, Q_TRUE);

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * Believe it or not I actually saw a site use ESC CSI <code>.  So if
         * we see ESC again, stay in SCAN_ESC state.
         */
        if (from_modem == C_ESC) {
            *to_screen = 1;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        break;

    case SCAN_START_SEQUENCE:
        /*
         * Looking for digit or code.
         */
        if (q_isdigit(from_modem)) {
            /*
             * Looks like a counter
             */
            count = q_emul_buffer + q_emul_buffer_n;
            save_char(from_modem, to_screen);
            scan_state = SCAN_COUNT;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }
        if (from_modem == 'K') {
            /*
             * Erase from here to end of line
             */
            erase_line(q_status.cursor_x, q_scrollback_current->length,
                       Q_FALSE);

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }
        if (from_modem == 'J') {
            /*
             * Erase from here to end of screen
             */
            erase_screen(q_status.cursor_y, q_status.cursor_x,
                         HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1, Q_FALSE);

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'm') {
            DLOG(("SGR: reset\n"));
            /*
             * ESC [ m mean ESC [ 0 m, all attributes off
             */
            q_current_color =
                Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'A') {
            cursor_up(1, Q_FALSE);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'B') {
            cursor_down(1, Q_FALSE);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'C') {
            cursor_right(1, Q_FALSE);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'D') {
            cursor_left(1, Q_FALSE);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if ((from_modem == 'H') || (from_modem == 'f')) {
            cursor_position(0, 0);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == ';') {
            /*
             * Continue scanning, more numbers coming.
             */
            count = q_emul_buffer + q_emul_buffer_n;
            save_char(from_modem, to_screen);
            scan_state = SCAN_COUNT_TWO;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'c') {
            /*
             * Identify
             */

            /*
             * Send string directly to remote side
             */
            qodem_write(q_child_tty_fd, "\033[?1;2c", 3, Q_TRUE);

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 's') {
            /*
             * Save cursor position
             */
            saved_cursor_x = q_status.cursor_x;
            saved_cursor_y = q_status.cursor_y;
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'u') {
            /*
             * Restore cursor position
             */
            cursor_position(saved_cursor_y, saved_cursor_x);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'I') {
            /*
             * CHT
             */
            /*
             * No parameter means assume 1
             */
            while (q_status.cursor_x < 79) {
                print_character(' ');
                if (q_status.cursor_x % 8 == 0) {
                    break;
                }
            }

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == '@') {
            /*
             * ICH
             */
            /*
             * No parameter means assume 1
             */
            insert_blanks(1);

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'd') {
            /*
             * VPA
             */
            /*
             * No parameter means assume 0
             */
            cursor_position(0, q_status.cursor_x);

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'G') {
            /*
             * CHA
             */
            /*
             * No parameter means assume 0
             */
            cursor_position(q_status.cursor_y, 0);

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'L') {
            /*
             * IL
             */
            /*
             * No parameter means assume 1
             */
            /*
             * I can get the same effect with a scroll-down
             */
            scrolling_region_scroll_down(q_status.cursor_y,
                                         q_status.scroll_region_bottom, 1);

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'P') {
            /*
             * DCH
             */
            /*
             * No parameter means assume 1
             */
            delete_character(1);

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'b') {
            /*
             * REP
             */
            /*
             * No parameter means assume 1
             */
            print_character(rep_character);

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'M') {

            if (q_status.ansi_music == Q_FALSE) {
                /*
                 * DL
                 */
                /*
                 * No parameter means assume 1
                 */
                /*
                 * I can get the same effect with a scroll-up
                 */
                scrolling_region_scroll_up(q_status.cursor_y,
                                           q_status.scroll_region_bottom, 1);

                clear_state(to_screen);
                return Q_EMUL_FSM_NO_CHAR_YET;
            }

            /*
             * ANSI Music
             */
            memset(music_buffer, 0, sizeof(music_buffer));
            music_buffer[0] = from_modem;
            music_buffer_n = 1;

            scan_state = SCAN_MUSIC;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == '=') {
            /*
             * This will be a DOS private mode change (CSI = Pn { h | l } )
             */
            private_mode_flag = Q_TRUE;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == '?') {
            /*
             * This will be a DEC private mode change (CSI ? Pn { h | l } )
             */
            dec_private_mode_flag = Q_TRUE;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == ' ') {
            /*
             * This is probably a "font selection" command for SyncTERM
             */
            discard_mode_flag = Q_TRUE;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == '!') {
            /*
             * This is a RIPScript query command, discard it
             */
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if ((from_modem == 'D') && (discard_mode_flag == Q_TRUE)) {
            /*
             * This was a "font selection" command, discard it
             */
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        break;

    case SCAN_COUNT:
        /*
         * Looking for digit, ';', or code
         */
        if (q_isdigit(from_modem)) {
            save_char(from_modem, to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'K') {
            i = ansi_ps(&count);
            if (i == -1) {
                break;
            }
            if (i == 0) {
                /*
                 * Erase from here to end of line
                 */
                erase_line(q_status.cursor_x, q_scrollback_current->length,
                           Q_FALSE);
            }
            if (i == 1) {
                /*
                 * Erase from beginning of line to here
                 */
                erase_line(0, q_status.cursor_x, Q_FALSE);
            }
            if (i == 2) {
                /*
                 * Erase entire line
                 */
                erase_line(0, q_scrollback_current->length, Q_FALSE);
            }

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'J') {
            i = ansi_ps(&count);
            if (i == -1) {
                break;
            }
            if (i == 0) {
                /*
                 * Erase from here to end of screen
                 */
                erase_screen(q_status.cursor_y, q_status.cursor_x,
                             HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1, Q_FALSE);
            }
            if (i == 1) {
                /*
                 * Erase from beginning of screen to here
                 */
                erase_screen(0, 0, q_status.cursor_y, q_status.cursor_x,
                             Q_FALSE);
            }
            if (i == 2) {
                /*
                 * Erase entire screen, and move cursor to home position
                 */
                cursor_formfeed();
            }

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'A') {
            i = ansi_ps(&count);
            if (i == -1) {
                break;
            }
            cursor_up(i, Q_FALSE);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'B') {
            i = ansi_ps(&count);
            if (i == -1) {
                break;
            }
            cursor_down(i, Q_FALSE);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'C') {
            i = ansi_ps(&count);
            if (i == -1) {
                break;
            }
            cursor_right(i, Q_FALSE);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'D') {
            i = ansi_ps(&count);
            if (i == -1) {
                break;
            }
            cursor_left(i, Q_FALSE);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == ';') {
            /*
             * Continue scanning, more numbers coming.
             */
            save_char(from_modem, to_screen);
            scan_state = SCAN_COUNT_TWO;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if ((from_modem == 'H') || (from_modem == 'f')) {
            /*
             * Cursor position
             */
            if (ansi_position(&count) == Q_FALSE) {
                break;
            }
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'm') {

            DLOG(("SGR: change text attributes\n"));

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

        /*
         * DSR special case:  it MUST be exactly "<ESC>[6n"
         */
        if (from_modem == 'n') {
            /*
             *
             * Device Status Report
             *
             * It was "common knowledge" circa 1992 that the way to
             * "autodetect" ANSI was to issue DSR 6 (Cursor Position).  I'm
             * adding support for it here only because it was so frequently
             * used by ANSI BBSes.
             *
             * It was also "common knowledge" to return the SIZE of the
             * screen rather than the current cursor position.  Most of the
             * servers/BBSes just look for the return CSI code.  So screw it,
             * I'll actually put the real position in the response.
             *
             * A _real_ ANSI X3.64 terminal would respond to lots more DSR's.
             *
             */
            if ((q_emul_buffer_n == 3) && (q_emul_buffer[2] == '6')) {
                DLOG(("dsr() 6\n"));

                memset(response_buffer, 0, sizeof(response_buffer));
                snprintf(response_buffer, sizeof(response_buffer),
                         "\033[%u;%uR", q_status.cursor_y + 1,
                         q_status.cursor_x + 1);
                /*
                 * Send string directly to remote side
                 */
                qodem_write(q_child_tty_fd, response_buffer,
                            strlen(response_buffer), Q_TRUE);
            } else {
                DLOG(("dsr() UNSUPPORTED: %s\n", q_emul_buffer));
            }

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == '@') {
            /*
             * ICH
             */
            if (ansi_ich(&count) == Q_FALSE) {
                break;
            }
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'I') {
            /*
             * CHT
             */
            if (ansi_cht(&count) == Q_FALSE) {
                break;
            }
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'd') {
            /*
             * VPA
             */
            if (ansi_vpa(&count) == Q_FALSE) {
                break;
            }
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'G') {
            /*
             * CHA
             */
            if (ansi_cha(&count) == Q_FALSE) {
                break;
            }
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'L') {
            /*
             * IL
             */
            if (ansi_il(&count) == Q_FALSE) {
                break;
            }
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'M') {
            /*
             * DL
             */
            if (ansi_dl(&count) == Q_FALSE) {
                break;
            }
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'P') {
            /*
             * DCH
             */
            if (ansi_dch(&count) == Q_FALSE) {
                break;
            }
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'b') {
            /*
             * REP
             */
            if (ansi_rep(&count) == Q_FALSE) {
                break;
            }
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'h') {
            /*
             * Set mode
             */
            if (ansi_set_mode(&count, Q_TRUE) == Q_FALSE) {
                break;
            }
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'l') {
            /*
             * Reset mode
             */
            if (ansi_set_mode(&count, Q_FALSE) == Q_FALSE) {
                break;
            }
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == ' ') {
            /*
             * This is probably a "font selection" command for SyncTERM
             */
            discard_mode_flag = Q_TRUE;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if ((from_modem == 'D') && (discard_mode_flag == Q_TRUE)) {
            /*
             * This was a "font selection" command, discard it
             */
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == '!') {
            /*
             * This is a RIPScript query command, discard it
             */
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        break;

    case SCAN_COUNT_TWO:
        /*
         * We can see ONLY:  digit, ';', H, f, m
         */
        if (q_isdigit(from_modem)) {
            save_char(from_modem, to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == ';') {
            /*
             * Continue scanning, more numbers coming.
             */
            save_char(from_modem, to_screen);
            scan_state = SCAN_COUNT_MANY;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if ((from_modem == 'H') || (from_modem == 'f')) {
            /*
             * Cursor position
             */
            if (ansi_position(&count) == Q_FALSE) {
                break;
            }
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'm') {
            DLOG(("SGR: change text attributes\n"));
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

        if (from_modem == ' ') {
            /*
             * This is probably a "font selection" command for SyncTERM
             */
            discard_mode_flag = Q_TRUE;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if ((from_modem == 'D') && (discard_mode_flag == Q_TRUE)) {
            /*
             * This was a "font selection" command, discard it
             */
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        break;

    case SCAN_COUNT_MANY:
        /*
         * We can see ONLY:  digit, ';', m
         */
        if (q_isdigit(from_modem)) {
            save_char(from_modem, to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == ';') {
            /*
             * Continue scanning, more numbers coming.
             */
            save_char(from_modem, to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'm') {
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

    case SCAN_MUSIC:

        /*
         * Generally we are looking for ^N or CR
         */
        if ((from_modem == 0x0E) || (from_modem == C_CR)) {
            /*
             * Force the screen to refresh before playing the music
             */
            q_screen_dirty = Q_TRUE;
            refresh_handler();

            play_ansi_music(music_buffer, music_buffer_n, Q_TRUE);
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        music_buffer[music_buffer_n] = from_modem;
        music_buffer_n++;

        if (music_buffer_n >= sizeof(music_buffer)) {
            /*
             * Throw it away
             */
            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * Keep collecting characters
         */
        return Q_EMUL_FSM_NO_CHAR_YET;
    }

    /*
     * This point means we got most, but not all, of a sequence.
     */
    q_emul_buffer[q_emul_buffer_n] = from_modem;
    q_emul_buffer_n++;
    DLOG(("UNKNOWN SEQUENCE (length %d): \"%s\"\n", q_emul_buffer_n,
            q_emul_buffer));

    *to_screen = q_emul_buffer[q_emul_buffer_i];
    q_emul_buffer_i++;
    scan_state = SCAN_NONE;

    /*
     * Special case: one character returns Q_EMUL_FSM_ONE_CHAR
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
 * Note that ANSI emulation is an 8-bit emulation: only the bottom 8 bits are
 * transmitted to the remote side.  See post_keystroke().
 */
wchar_t * ansi_keystroke(const int keystroke) {

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
        return L"\033[D";

    case Q_KEY_RIGHT:
        return L"\033[C";

    case Q_KEY_UP:
        return L"\033[A";

    case Q_KEY_DOWN:
        return L"\033[B";

    case Q_KEY_PPAGE:
        return L"\033[V";
    case Q_KEY_NPAGE:
        return L"\033[U";
    case Q_KEY_IC:
        return L"\033[@";
    case Q_KEY_DC:
        return L"\177";
    case Q_KEY_SIC:
    case Q_KEY_SDC:
        return L"";
    case Q_KEY_HOME:
        return L"\033[H";
    case Q_KEY_END:
        return L"\033[K";
    case Q_KEY_F(1):
        return L"\033OP";
    case Q_KEY_F(2):
        return L"\033OQ";
    case Q_KEY_F(3):
        return L"\033OR";
    case Q_KEY_F(4):
        return L"\033OS";
    case Q_KEY_F(5):
        return L"\033Ot";
    case Q_KEY_F(6):
        return L"\033[17~";
    case Q_KEY_F(7):
        return L"\033[18~";
    case Q_KEY_F(8):
        return L"\033[19~";
    case Q_KEY_F(9):
        return L"\033[20~";
    case Q_KEY_F(10):
        return L"\033[21~";
    case Q_KEY_F(11):
        return L"\033[23~";
    case Q_KEY_F(12):
        return L"\033[24~";
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
        return L"";

    case Q_KEY_PAD0:
        return L"0";
    case Q_KEY_C1:
    case Q_KEY_PAD1:
        return L"1";
    case Q_KEY_C2:
    case Q_KEY_PAD2:
        return L"2";
    case Q_KEY_C3:
    case Q_KEY_PAD3:
        return L"3";
    case Q_KEY_B1:
    case Q_KEY_PAD4:
        return L"4";
    case Q_KEY_B2:
    case Q_KEY_PAD5:
        return L"5";
    case Q_KEY_B3:
    case Q_KEY_PAD6:
        return L"6";
    case Q_KEY_A1:
    case Q_KEY_PAD7:
        return L"7";
    case Q_KEY_A2:
    case Q_KEY_PAD8:
        return L"8";
    case Q_KEY_A3:
    case Q_KEY_PAD9:
        return L"9";
    case Q_KEY_PAD_STOP:
        return L".";
    case Q_KEY_PAD_SLASH:
        return L"/";
    case Q_KEY_PAD_STAR:
        return L"*";
    case Q_KEY_PAD_MINUS:
        return L"-";
    case Q_KEY_PAD_PLUS:
        return L"+";
    case Q_KEY_PAD_ENTER:
    case Q_KEY_ENTER:
        return L"\015";

    default:
        break;

    }

    return NULL;
}
