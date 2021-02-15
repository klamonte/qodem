/*
 * emulation.c
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
#include "forms.h"
#include "ansi.h"
#include "vt52.h"
#include "vt100.h"
#include "avatar.h"
#include "petscii.h"
#include "atascii.h"
#include "keyboard.h"
#include "states.h"
#include "options.h"
#include "console.h"
#include "help.h"

/**
 * Local buffer for multiple returned characters.
 */
unsigned char q_emul_buffer[128];
int q_emul_buffer_n;
int q_emul_buffer_i;

/**
 * Last returned state.
 */
static Q_EMULATION_STATUS last_state;

/**
 * Some emulations need to wrap at special places.
 */
int q_emulation_right_margin = -1;

/**
 * The total number of bytes received on this connection.
 */
unsigned long q_connection_bytes_received;

/**
 * Avatar has a special command that requires the entire state machine be
 * re-run.  It is the responsibility of an emulation to set these two
 * variables and then return Q_EMUL_FSM_REPEAT_STATE.  terminal_emulator()
 * will free it afterwards.
 */
unsigned char * q_emul_repeat_state_buffer = NULL;
int q_emul_repeat_state_count;

/**
 * Given an emulation string, return a Q_EMULATION enum.
 *
 * @param string "TTY", "VT100", etc.  Note string is case-sensitive.
 * @return Q_EMUL_TTY, Q_EMUL_VT100, etc.
 */
Q_EMULATION emulation_from_string(const char * string) {

    if (strncasecmp(string, "TTY", sizeof("TTY")) == 0) {
        return Q_EMUL_TTY;
    } else if (strncasecmp(string, "ANSI", sizeof("ANSI")) == 0) {
        return Q_EMUL_ANSI;
    } else if (strncasecmp(string, "AVATAR", sizeof("AVATAR")) == 0) {
        return Q_EMUL_AVATAR;
    } else if (strncasecmp(string, "VT52", sizeof("VT52")) == 0) {
        return Q_EMUL_VT52;
    } else if (strncasecmp(string, "VT100", sizeof("VT100")) == 0) {
        return Q_EMUL_VT100;
    } else if (strncasecmp(string, "VT102", sizeof("VT102")) == 0) {
        return Q_EMUL_VT102;
    } else if (strncasecmp(string, "VT220", sizeof("VT220")) == 0) {
        return Q_EMUL_VT220;
    } else if (strncasecmp(string, "LINUX", sizeof("LINUX")) == 0) {
        return Q_EMUL_LINUX;
    } else if (strncasecmp(string, "L_UTF8", sizeof("L_UTF8")) == 0) {
        return Q_EMUL_LINUX_UTF8;
    } else if (strncasecmp(string, "XTERM", sizeof("XTERM")) == 0) {
        return Q_EMUL_XTERM;
    } else if (strncasecmp(string, "X_UTF8", sizeof("X_UTF8")) == 0) {
        return Q_EMUL_XTERM_UTF8;
    } else if (strncasecmp(string, "PETSCII", sizeof("PETSCII")) == 0) {
        return Q_EMUL_PETSCII;
    } else if (strncasecmp(string, "ATASCII", sizeof("ATASCII")) == 0) {
        return Q_EMUL_ATASCII;
    } else if (strncasecmp(string, "DEBUG", sizeof("DEBUG")) == 0) {
        return Q_EMUL_DEBUG;
    }

    return Q_EMUL_VT102;
}

/**
 * Return a string for a Q_EMULATION enum.
 *
 * @param emulation Q_EMUL_TTY etc.
 * @return "TTY" etc.
 */
const char * emulation_string(const Q_EMULATION emulation) {

    switch (emulation) {
    case Q_EMUL_TTY:
        return "TTY";

    case Q_EMUL_ANSI:
        return "ANSI";

    case Q_EMUL_AVATAR:
        return "AVATAR";

    case Q_EMUL_VT52:
        return "VT52";

    case Q_EMUL_VT100:
        return "VT100";

    case Q_EMUL_VT102:
        return "VT102";

    case Q_EMUL_VT220:
        return "VT220";

    case Q_EMUL_LINUX:
        return "LINUX";

    case Q_EMUL_LINUX_UTF8:
        return "L_UTF8";

    case Q_EMUL_XTERM:
        return "XTERM";

    case Q_EMUL_XTERM_UTF8:
        return "X_UTF8";

    case Q_EMUL_PETSCII:
        return "PETSCII";

    case Q_EMUL_ATASCII:
        return "ATASCII";

    case Q_EMUL_DEBUG:
        return "DEBUG";

    }

    /*
     * Should never get here.
     */
    abort();
    return NULL;
}

/**
 * Get the appropriate TERM environment variable value for an emulation.
 *
 * @param emulation the emulation
 * @return "ansi", "xterm", etc.
 */
const char * emulation_term(Q_EMULATION emulation) {
    switch (emulation) {
    case Q_EMUL_ANSI:
        return "ansi";
    case Q_EMUL_AVATAR:
        return "avatar";
    case Q_EMUL_VT52:
        return "vt52";
    case Q_EMUL_VT100:
        return "vt100";
    case Q_EMUL_VT102:
        return "vt102";
    case Q_EMUL_VT220:
        return "vt220";
    case Q_EMUL_TTY:
        return "dumb";
    case Q_EMUL_LINUX:
    case Q_EMUL_LINUX_UTF8:
        return "linux";
    case Q_EMUL_PETSCII:
        return "commodore";
    case Q_EMUL_ATASCII:
        return "atari-old";
    case Q_EMUL_XTERM:
    case Q_EMUL_XTERM_UTF8:
        return "xterm";
    case Q_EMUL_DEBUG:
    default:
        /*
         * No default terminal setting
         */
        return "";
    }
}

/**
 * Get the appropriate LANG environment variable value for an emulation.
 *
 * @param emulation the emulation
 * @return "en", "en_US", etc.
 */
const char * emulation_lang(Q_EMULATION emulation) {
    switch (emulation) {
    case Q_EMUL_XTERM_UTF8:
    case Q_EMUL_LINUX_UTF8:
        return get_option(Q_OPTION_UTF8_LANG);
    case Q_EMUL_PETSCII:
        /* PETSCII is always 8-bit with no codepage translation. */
        return "C";
    case Q_EMUL_ATASCII:
        /* ATASCII is always 8-bit with no codepage translation. */
        return "C";
    default:
        return get_option(Q_OPTION_ISO8859_LANG);
    }
}

/**
 * Process a control character.  This is used by ANSI, AVATAR, and TTY.
 *
 * @param control_char a byte in the C0 or C1 range.
 */
void generic_handle_control_char(const unsigned char control_char) {

    /*
     * Handle control characters
     */
    switch (control_char) {
    case 0x05:
        /*
         * ENQ - transmit the answerback message.
         */
        qodem_write(q_child_tty_fd, get_option(Q_OPTION_ENQ_ANSWERBACK),
                    strlen(get_option(Q_OPTION_ENQ_ANSWERBACK)), Q_TRUE);
        break;

    case 0x07:
        /*
         * BEL
         */
        screen_beep();
        break;

    case 0x08:
        /*
         * BS
         */
        cursor_left(1, Q_FALSE);
        break;

    case 0x09:
        /*
         * HT
         */
        while (q_status.cursor_x < 80) {
            print_character(' ');
            if (q_status.cursor_x % 8 == 0) {
                break;
            }
        }
        break;

    case 0x0A:
        /*
         * LF
         */
        cursor_linefeed(Q_FALSE);
        break;

    case 0x0B:
        /*
         * VT
         */
        cursor_linefeed(Q_FALSE);
        break;

    case 0x0C:
        /*
         * FF
         *
         * In VT100 land form feed is the same as vertical tab.
         *
         * In PC-DOS land form feed clears the screen and homes the cursor.
         */
        cursor_formfeed();
        break;

    case 0x0D:
        /*
         * CR
         */
        cursor_carriage_return();
        break;

    case 0x0E:
        /*
         * SO
         *
         * Fall through...
         */
    case 0x0F:
        /*
         * SI
         *
         * Fall through...
         */
    default:
        /*
         * This is probably a CP437 glyph.
         */
        print_character(cp437_chars[control_char]);
        break;
    }
}

/**
 * Get the default 8-bit codepage for an emulation.  This is usually CP437 or
 * DEC.
 *
 * @param emulation Q_EMUL_TTY, Q_EMUL_ANSI, etc.
 * @return the codepage
 */
Q_CODEPAGE default_codepage(Q_EMULATION emulation) {
    /*
     * Set the right codepage
     */
    switch (emulation) {
    case Q_EMUL_TTY:
        return Q_CODEPAGE_ISO8859_1;
    case Q_EMUL_VT52:
    case Q_EMUL_VT100:
    case Q_EMUL_VT102:
    case Q_EMUL_VT220:
    case Q_EMUL_LINUX_UTF8:
    case Q_EMUL_XTERM_UTF8:
        return Q_CODEPAGE_DEC;
    case Q_EMUL_DEBUG:
    case Q_EMUL_ANSI:
    case Q_EMUL_AVATAR:
    case Q_EMUL_LINUX:
    case Q_EMUL_XTERM:
        return Q_CODEPAGE_CP437;
    case Q_EMUL_PETSCII:
        return Q_CODEPAGE_PETSCII;
    case Q_EMUL_ATASCII:
        return Q_CODEPAGE_ATASCII;
    }

    /*
     * BUG: should never get here
     */
    abort();
    return Q_CODEPAGE_CP437;
}

/* TTY emulation ------------------------------------------------------------ */

/**
 * Process through TTY emulation.
 *
 * @param from_modem one byte from the remote side.
 * @param to_screen if the return is Q_EMUL_FSM_ONE_CHAR or
 * Q_EMUL_FSM_MANY_CHARS, then to_screen will have a character to display on
 * the screen.
 * @return one of the Q_EMULATION_STATUS constants.
 */
static Q_EMULATION_STATUS tty(const unsigned char from_modem,
                              wchar_t * to_screen) {

    /*
     * Handle control characters
     */
    switch (from_modem) {
    case 0x05:
        /*
         * ENQ - transmit the answerback message.
         */
        qodem_write(q_child_tty_fd, get_option(Q_OPTION_ENQ_ANSWERBACK),
                    strlen(get_option(Q_OPTION_ENQ_ANSWERBACK)), Q_TRUE);
        break;

    case 0x07:
        /*
         * BEL
         */
        screen_beep();
        break;

    case 0x08:
        /*
         * BS
         */
        cursor_left(1, Q_FALSE);
        break;

    case 0x09:
        /*
         * HT
         */
        while (q_status.cursor_x < 80) {
            print_character(' ');
            if (q_status.cursor_x % 8 == 0) {
                break;
            }
        }
        break;

    case 0x0A:
        /*
         * LF
         */
        cursor_linefeed(Q_FALSE);
        break;

    case 0x0B:
        /*
         * VT
         */
        cursor_linefeed(Q_FALSE);
        break;

    case 0x0C:
        /*
         * FF
         */
        cursor_linefeed(Q_FALSE);
        break;

    case 0x0D:
        /*
         * CR
         */
        cursor_carriage_return();
        break;

    case 0x0E:
        /*
         * SO
         */
        break;

    case 0x0F:
        /*
         * SI
         */
        break;

    case '_':
        /*
         * One special case: underscores.  TTY emulation will turn on
         * A_UNDERLINE if a character already exists here.
         *
         * Yeah, it's probably overkill.  But it's the closest thing to a
         * "faithful" reproduction of a forty-year-old standard I can manage.
         * :)
         */
        if (q_status.emulation == Q_EMUL_TTY) {
            if (q_scrollback_current->chars[q_status.cursor_x] != ' ') {
                q_scrollback_current->colors[q_status.cursor_x] |=
                    Q_A_UNDERLINE;
                q_status.cursor_x++;
                break;
            }
        }

        /*
         * Else fall through...
         */
    default:
        /*
         * Return to screen
         */
        *to_screen = codepage_map_char(from_modem);
        return Q_EMUL_FSM_ONE_CHAR;
    }

    /*
     * Consume
     */
    *to_screen = 1;
    return Q_EMUL_FSM_NO_CHAR_YET;
}

/* DEBUG emulation ---------------------------------------------------------- */

/**
 * Number of bytes displayed through debug_local_echo().
 */
static int local_echo_count;

/**
 * Advance to a column by printing spaces.
 *
 * @param new_col the column to advance to
 */
static void advance_to(const int new_col) {

    if (new_col < 0) {
        return;
    }

    while (q_status.cursor_x < new_col) {
        print_character(' ');
    }
}

/**
 * Print the current byte offset at the beginning of the line.
 */
static void print_byte_offset() {
    unsigned int i;

    char buffer[16];

    /*
     * Format is " 01234567 | "
     */
    memset(buffer, 0, sizeof(buffer));
    snprintf(buffer, sizeof(buffer) - 1, " %08lx | ",
             (q_connection_bytes_received + local_echo_count));

    /*
     * Print it out
     */
    for (i = 0; i < strlen(buffer); i++) {
        print_character(buffer[i]);
    }

    /*
     * Fix the colors
     */
    for (i = 0; i < WIDTH; i++) {
        q_scrollback_current->colors[i] = q_current_color;
    }

    /*
     * Add the "|" for the characters area.  We cheat and edit the scrollback
     * buffer directly WITHOUT printing it.
     */
    q_scrollback_current->chars[60] = '|';
    q_scrollback_current->colors[60] = q_current_color;
}

/**
 * Print the right-side "printable characters" column.
 */
static void print_printable_chars() {
    int i;

    /*
     * Get out to the right column
     */
    advance_to(60);

    /*
     * Put the vertical bar and space
     */
    print_character('|');
    print_character(' ');

    /*
     * Print out the characters
     */
    for (i = 0; i < 16; i++) {
        q_current_color = q_scrollback_current->colors[i + 62];
        print_character(q_scrollback_current->chars[i + 62]);
        q_current_color =
            Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);
    }

}

/**
 * Reset the emulation state.
 */
static void debug_reset() {

    char header[80];
    unsigned int i;

    /*
     * Explicitly check, since this function alters the scrollback.
     */
    if (q_status.emulation != Q_EMUL_DEBUG) {
        return;
    }

    /*
     * Set default color.
     */
    q_current_color = Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);

    /*
     * Turn off half duplex. console.c has a check to prevent it
     * from being turned back on.
     */
    q_status.full_duplex = Q_TRUE;

    /*
     * Set the header.
     */
    memset(header, 0, sizeof(header));
    snprintf(header, sizeof(header) - 1,
             _(""
               " OFFSET   | BYTES"
               "                                           | CHARACTERS "));

    /*
     * Line feed on CR screws up the hex display.  Always reset it before
     * calling cursor_linefeed() in case the user tried to switch it back.
     * either.
     */
    q_status.line_feed_on_cr = Q_FALSE;

    /*
     * Print out the header line.
     */
    cursor_linefeed(Q_TRUE);
    for (i = 0; i < strlen(header); i++) {
        print_character(header[i]);
    }
    cursor_linefeed(Q_TRUE);

    /*
     * Reset local char count.
     */
    local_echo_count = 0;

    /*
     * Print the current byte offset.
     */
    print_byte_offset();

}

/**
 * Called when switching to another emulation to emit the pending bytes in
 * the hex display.
 */
static void debug_finish() {
    /*
     * Print out the characters on the right-hand side.
     */
    print_printable_chars();
    cursor_linefeed(Q_TRUE);
}

/**
 * Print a character to the scrollback, using a color to distinguish local
 * and remote.
 *
 * @param ch the character (byte) to print
 * @param q_color a Q_COLOR enum
 */
static void debug_print_character(const unsigned char ch,
                                  const Q_COLOR q_color) {

    int offset;
    char buffer[4];

/* Format:

 OFFSET   | BYTES                                           | CHARACTERS
 01234567 | 00 11 22 33 44 55 66 77-00 11 22 33 44 55 66 77 | 0123456701234567

 */

    /*
     * Bytes received start at 1.  Offset needs to start at 0.
     */
    offset = (q_connection_bytes_received + local_echo_count - 1) % 16;

    /*
     * Get to the correct column.
     */
    advance_to((offset * 3) + 12);

    /*
     * Convert the byte to hex.
     */
    memset(buffer, 0, sizeof(buffer));
    snprintf(buffer, sizeof(buffer) - 1, "%02x", ch);

    /*
     * Print the hex characters out.
     */
    q_current_color = scrollback_full_attr(q_color);
    print_character(buffer[0]);
    print_character(buffer[1]);

    /*
     * Drop the codepage printable character directly into the scrollback
     * buffer.
     */
    q_scrollback_current->chars[62 + offset] = codepage_map_char(ch);
    q_scrollback_current->colors[62 + offset] = q_current_color;
    q_scrollback_current->length = 62 + offset + 1;
    q_current_color = Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);

    /*
     * Check if it's time to wrap the line.
     */
    if (offset == 15) {
        /*
         * It's time to wrap the line.  Print out the characters on the
         * right-hand side.
         */
        print_printable_chars();
        cursor_linefeed(Q_TRUE);

        /*
         * Print out the byte offset for the new line.
         */
        print_byte_offset();
    } else if (offset == 7) {
        /*
         * Halfway there, put in the dash separating the two columns of hex
         * digits.
         */
        print_character('-');
    } else {
        /*
         * Inside one of the hex columns, just insert a space to the next hex
         * value.
         */
        print_character(' ');
    }
}

/**
 * Echo local transmitted bytes to DEBUG emulation the hex display in a
 * distinct color.
 *
 * @param ch the byte that was sent to the remote side
 */
void debug_local_echo(const unsigned char ch) {
    local_echo_count++;
    debug_print_character(ch, Q_COLOR_DEBUG_ECHO);
}

/**
 * Push one byte through the DEBUG emulator.
 *
 * @param from_modem one byte from the remote side.
 * @param to_screen if the return is Q_EMUL_FSM_ONE_CHAR or
 * Q_EMUL_FSM_MANY_CHARS, then to_screen will have a character to display on
 * the screen.
 * @return one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
static Q_EMULATION_STATUS debug_emulator(const unsigned char from_modem,
                                         wchar_t * to_screen) {

    debug_print_character(from_modem, Q_COLOR_CONSOLE_TEXT);

    /*
     * Every character is consumed, and none are printed directly.
     */
    *to_screen = 1;
    return Q_EMUL_FSM_NO_CHAR_YET;
}

/* The main entry point for all terminal emulation -------------------------- */

/**
 * All the emulations use the same top-level function.  Q_EMULATION_STATE is
 * returned.
 *
 * If Q_EMUL_FSM_NO_CHAR_YET, then to_screen contains the number of
 * characters that can be safely discarded from the data stream, usually 1.
 *
 * If Q_EMUL_FSM_ONE_CHAR, then to_screen contains one character that can be
 * rendered.
 *
 * If Q_EMUL_FSM_MANY_CHARS, then to_screen contains one character that can
 * be rendered, AND more characters are ready.  Continue calling
 * terminal_emulator() until Q_EMUL_FSM_NO_CHAR_YET is returned.
 *
 * The emulator is expected to modify the following globals:
 *        q_current_color
 *        q_status.cursor_x
 *        q_status.cursor_y
 *        q_status.scroll_region_top
 *        q_status.scroll_region_bottom
 *
 * Also the emulator may modify data in the scrollback buffer.
 *
 * @param from_modem one byte from the remote side.
 * @param to_screen if the return is Q_EMUL_FSM_ONE_CHAR or
 * Q_EMUL_FSM_MANY_CHARS, then to_screen will have a character to display on
 * the screen.
 * @return one of the Q_EMULATION_STATUS constants.
 */
Q_EMULATION_STATUS terminal_emulator(const unsigned char from_modem,
                                     wchar_t * to_screen) {

    int i;

    /*
     * Junk extraneous data
     */
    if (q_emul_buffer_n >= sizeof(q_emul_buffer) - 1) {
        q_emul_buffer_n = 0;
        q_emul_buffer_i = 0;
        memset(q_emul_buffer, 0, sizeof(q_emul_buffer));
        last_state = Q_EMUL_FSM_NO_CHAR_YET;
    }

    if (last_state == Q_EMUL_FSM_MANY_CHARS) {

        if (q_status.emulation == Q_EMUL_AVATAR) {
            /*
             * Avatar has its own logic that needs to handle RLE strings.  It
             * will also dump unknown sequences.
             */
            last_state = avatar(from_modem, to_screen);
            return last_state;
        } else if (q_status.emulation == Q_EMUL_PETSCII) {
            /*
             * PETSCII needs to dump unknown sequences.
             */
            last_state = petscii(from_modem, to_screen);
            return last_state;
        } else if (q_status.emulation == Q_EMUL_ATASCII) {
            /*
             * ATASCII needs to dump unknown sequences.
             */
            last_state = atascii(from_modem, to_screen);
            return last_state;
        } else {
            /*
             * Everybody else just dumps the string in q_emul_buffer
             */
            if (q_emul_buffer_n == 0) {
                /*
                 * We just emitted the last character
                 */
                last_state = Q_EMUL_FSM_NO_CHAR_YET;
                *to_screen = 0;
                return last_state;
            }

            *to_screen = codepage_map_char(q_emul_buffer[q_emul_buffer_i]);
            q_emul_buffer_i++;
            if (q_emul_buffer_i == q_emul_buffer_n) {
                /*
                 * This is the last character
                 */
                q_emul_buffer_n = 0;
                q_emul_buffer_i = 0;
                memset(q_emul_buffer, 0, sizeof(q_emul_buffer));
            }
        }
        return Q_EMUL_FSM_MANY_CHARS;
    }

    /*
     * A new character has arrived.  Increase the byte counter.
     */
    q_connection_bytes_received++;

    /*
     * VT100 scrolling regions require that the vt100() function sees these
     * characters.
     *
     * DEBUG emulation also performs its own CR/LF handling.
     *
     * AVATAR uses the other control characters as its codes, so we can't
     * process them here.
     *
     * PETSCII processes every byte on its own.
     *
     * ATASCII processes every byte on its own.
     */
    if ((q_status.emulation != Q_EMUL_VT100) &&
        (q_status.emulation != Q_EMUL_VT102) &&
        (q_status.emulation != Q_EMUL_VT220) &&
        (q_status.emulation != Q_EMUL_LINUX) &&
        (q_status.emulation != Q_EMUL_LINUX_UTF8) &&
        (q_status.emulation != Q_EMUL_XTERM) &&
        (q_status.emulation != Q_EMUL_XTERM_UTF8) &&
        (q_status.emulation != Q_EMUL_AVATAR) &&
        (q_status.emulation != Q_EMUL_PETSCII) &&
        (q_status.emulation != Q_EMUL_ATASCII) &&
        (q_status.emulation != Q_EMUL_DEBUG)
    ) {
        if (from_modem == C_CR) {
            cursor_carriage_return();
            *to_screen = 1;
            return Q_EMUL_FSM_NO_CHAR_YET;
        } else if (from_modem == C_LF) {
            cursor_linefeed(Q_FALSE);
            *to_screen = 1;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }
    }

    /*
     * Dispatch to the specific emulation function.
     */
    switch (q_status.emulation) {
    case Q_EMUL_ANSI:
        last_state = ansi(from_modem, to_screen);
        break;
    case Q_EMUL_VT52:
        last_state = vt52(from_modem, to_screen);
        break;
    case Q_EMUL_AVATAR:
        last_state = avatar(from_modem, to_screen);
        break;
    case Q_EMUL_PETSCII:
        last_state = petscii(from_modem, to_screen);
        break;
    case Q_EMUL_ATASCII:
        last_state = atascii(from_modem, to_screen);
        break;
    case Q_EMUL_VT100:
    case Q_EMUL_VT102:
    case Q_EMUL_VT220:
    case Q_EMUL_LINUX:
    case Q_EMUL_LINUX_UTF8:
    case Q_EMUL_XTERM:
    case Q_EMUL_XTERM_UTF8:
        last_state = vt100(from_modem, to_screen);
        break;
    case Q_EMUL_TTY:
        last_state = tty(from_modem, to_screen);
        break;
    case Q_EMUL_DEBUG:
        last_state = debug_emulator(from_modem, to_screen);
        break;
    }

    if (last_state == Q_EMUL_FSM_REPEAT_STATE) {

        for (i = 0; i < q_emul_repeat_state_count; i++) {

            switch (q_status.emulation) {
            case Q_EMUL_ANSI:
                last_state = ansi(q_emul_repeat_state_buffer[i], to_screen);
                break;
            case Q_EMUL_VT52:
                last_state = vt52(q_emul_repeat_state_buffer[i], to_screen);
                break;
            case Q_EMUL_AVATAR:
                last_state = avatar(q_emul_repeat_state_buffer[i], to_screen);
                break;
            case Q_EMUL_PETSCII:
                last_state = petscii(q_emul_repeat_state_buffer[i], to_screen);
                break;
            case Q_EMUL_ATASCII:
                last_state = atascii(q_emul_repeat_state_buffer[i], to_screen);
                break;
            case Q_EMUL_VT100:
            case Q_EMUL_VT102:
            case Q_EMUL_VT220:
            case Q_EMUL_LINUX:
            case Q_EMUL_LINUX_UTF8:
            case Q_EMUL_XTERM:
            case Q_EMUL_XTERM_UTF8:
                last_state = vt100(q_emul_repeat_state_buffer[i], to_screen);
                break;
            case Q_EMUL_TTY:
                last_state = tty(q_emul_repeat_state_buffer[i], to_screen);
                break;
            case Q_EMUL_DEBUG:
                last_state =
                    debug_emulator(q_emul_repeat_state_buffer[i], to_screen);
                break;
            }

            /*
             * Ugly hack, this should be console
             */
            if (last_state == Q_EMUL_FSM_ONE_CHAR) {
                /*
                 * Print this character
                 */
                print_character(codepage_map_char((unsigned char) *to_screen));
            }

        }

        Xfree(q_emul_repeat_state_buffer, __FILE__, __LINE__);
        q_emul_repeat_state_buffer = NULL;

        *to_screen = 1;
        last_state = Q_EMUL_FSM_NO_CHAR_YET;
    }

    return last_state;
}

/**
 * Reset the emulation state.
 */
void reset_emulation() {
    q_emul_buffer_n = 0;
    q_emul_buffer_i = 0;
    memset(q_emul_buffer, 0, sizeof(q_emul_buffer));
    last_state = Q_EMUL_FSM_NO_CHAR_YET;

    if (q_emul_repeat_state_buffer != NULL) {
        Xfree(q_emul_repeat_state_buffer, __FILE__, __LINE__);
        q_emul_repeat_state_buffer = NULL;
    }

    q_current_color = Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);

    /*
     * Reset ALL of the emulators
     */
    ansi_reset();
    vt52_reset();
    avatar_reset();
    petscii_reset();
    atascii_reset();
    vt100_reset();
    debug_reset();
    q_emulation_right_margin = -1;
    q_status.scroll_region_top = 0;
    q_status.scroll_region_bottom = HEIGHT - STATUS_HEIGHT - 1;
    q_status.reverse_video = Q_FALSE;
    q_status.origin_mode = Q_FALSE;

    switch (q_status.emulation) {
    case Q_EMUL_LINUX:
    case Q_EMUL_LINUX_UTF8:
        /*
         * LINUX emulation specifies that backspace is DEL.
         */
        q_status.hard_backspace = Q_FALSE;
        break;

    case Q_EMUL_VT220:
    case Q_EMUL_XTERM:
    case Q_EMUL_XTERM_UTF8:
        /*
         * VT220 style emulations tend to use DEL
         */
        q_status.hard_backspace = Q_FALSE;
        break;

    case Q_EMUL_ANSI:
    case Q_EMUL_AVATAR:
    case Q_EMUL_PETSCII:
    case Q_EMUL_ATASCII:
    case Q_EMUL_VT52:
    case Q_EMUL_VT100:
    case Q_EMUL_VT102:
    case Q_EMUL_TTY:
    case Q_EMUL_DEBUG:
        q_status.hard_backspace = Q_TRUE;
        break;
    }

    if (q_status.emulation == Q_EMUL_PETSCII) {
        /* Commodore starts as bright-white on black. */
        q_current_color = color_to_attr((Q_COLOR_WHITE << 3) | Q_COLOR_BLACK);
        q_current_color |= Q_A_BOLD;
    }

    /*
     * If xterm mouse reporting is enabled, enable the mouse.  Do not resolve
     * double and triple clicks.
     */
    if ((q_status.xterm_mouse_reporting == Q_TRUE) &&
        ((q_status.emulation == Q_EMUL_XTERM) ||
         (q_status.emulation == Q_EMUL_XTERM_UTF8))
    ) {
        /*
         * xterm emulations: listen for the mouse.
         */
        enable_mouse_listener();
    } else {
        /*
         * Non-xterm or mouse disabled, do not listen for the mouse.
         */
        disable_mouse_listener();
    }

    /*
     * Reset bracketed paste mode to global flag.
     */
    q_status.bracketed_paste_mode = Q_FALSE;
    if (strcasecmp(get_option(Q_OPTION_BRACKETED_PASTE), "true") == 0) {
        q_status.bracketed_paste_mode = Q_TRUE;
    }

}

/**
 * Draw screen for the emulation selection dialog.
 */
void emulation_menu_refresh() {
    char * status_string;
    int status_left_stop;
    char * message;
    int message_left;
    int window_left;
    int window_top;
    int window_height = 20;
    int window_length;

    if (q_screen_dirty == Q_FALSE) {
        return;
    }

    /*
     * Clear screen for when it resizes
     */
    console_refresh(Q_FALSE);

    /*
     * Put up the status line
     */
    screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH,
                              Q_COLOR_STATUS);

    status_string = _(" LETTER-Select an Emulation   ESC/`-Exit ");
    status_left_stop = WIDTH - strlen(status_string);
    if (status_left_stop <= 0) {
        status_left_stop = 0;
    } else {
        status_left_stop /= 2;
    }
    screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string,
                            Q_COLOR_STATUS);

    window_length = 27;

    /*
     * Add room for border + 1 space on each side
     */
    window_length += 4;

    /*
     * Window will be centered on the screen
     */
    window_left = WIDTH - 1 - window_length;
    if (window_left < 0) {
        window_left = 0;
    } else {
        window_left /= 2;
    }
    window_top = HEIGHT - 1 - window_height;
    if (window_top < 0) {
        window_top = 0;
    } else {
        window_top /= 10;
    }

    /*
     * Draw the sub-window
     */
    screen_draw_box(window_left, window_top, window_left + window_length,
                    window_top + window_height);

    /*
     * Place the title
     */
    message = _("Set Emulation");
    message_left = window_length - (strlen(message) + 2);
    if (message_left < 0) {
        message_left = 0;
    } else {
        message_left /= 2;
    }
    screen_put_color_printf_yx(window_top + 0, window_left + message_left,
                               Q_COLOR_WINDOW_BORDER, " %s ", message);

    /*
     * Add the "F1 Help" part
     */
    screen_put_color_str_yx(window_top + window_height - 1,
                            window_left + window_length - 10, _("F1 Help"),
                            Q_COLOR_WINDOW_BORDER);

    screen_put_color_str_yx(window_top + 1, window_left + 2, _("Emulation is "),
                            Q_COLOR_MENU_TEXT);
    screen_put_color_printf(Q_COLOR_MENU_COMMAND, "%s",
                            emulation_string(q_status.emulation));

    screen_put_color_str_yx(window_top + 3, window_left + 7, "A",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, "  TTY");
    screen_put_color_str_yx(window_top + 4, window_left + 7, "B",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, "  ANSI");
    screen_put_color_str_yx(window_top + 5, window_left + 7, "C",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, "  AVATAR");
    screen_put_color_str_yx(window_top + 6, window_left + 7, "D",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, "  VT52");
    screen_put_color_str_yx(window_top + 7, window_left + 7, "E",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, "  VT100");
    screen_put_color_str_yx(window_top + 8, window_left + 7, "F",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, "  VT102");
    screen_put_color_str_yx(window_top + 9, window_left + 7, "G",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, "  VT220");
    screen_put_color_str_yx(window_top + 10, window_left + 7, "L",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, "  LINUX");
    screen_put_color_str_yx(window_top + 11, window_left + 7, "T",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, "  LINUX UTF-8");
    screen_put_color_str_yx(window_top + 12, window_left + 7, "S",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, "  ATASCII (Atari)");
    screen_put_color_str_yx(window_top + 13, window_left + 7, "P",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, "  PETSCII (Commodore)");
    screen_put_color_str_yx(window_top + 14, window_left + 7, "X",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, "  XTERM");
    screen_put_color_str_yx(window_top + 15, window_left + 7, "8",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, "  XTERM UTF-8");
    screen_put_color_str_yx(window_top + 16, window_left + 7, "U",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, "  DEBUG");

    /*
     * Prompt
     */
    screen_put_color_str_yx(window_top + 18, window_left + 2,
                            _("Your Choice ? "), Q_COLOR_MENU_COMMAND);

    screen_flush();
    q_screen_dirty = Q_FALSE;
}

/**
 * Keyboard handler for the emulation selection dialog.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
void emulation_menu_keyboard_handler(const int keystroke, const int flags) {
    int new_keystroke;

    /*
     * Default to ANSI
     */
    Q_EMULATION new_emulation = Q_EMUL_ANSI;

    switch (keystroke) {
    case 'A':
    case 'a':
        new_emulation = Q_EMUL_TTY;
        break;

    case 'B':
    case 'b':
        new_emulation = Q_EMUL_ANSI;
        break;

    case 'C':
    case 'c':
        new_emulation = Q_EMUL_AVATAR;
        break;

    case 'D':
    case 'd':
        new_emulation = Q_EMUL_VT52;
        break;

    case 'E':
    case 'e':
        new_emulation = Q_EMUL_VT100;
        break;

    case 'F':
    case 'f':
        new_emulation = Q_EMUL_VT102;
        break;

    case 'G':
    case 'g':
        new_emulation = Q_EMUL_VT220;
        break;

    case 'L':
    case 'l':
        new_emulation = Q_EMUL_LINUX;
        break;

    case 'P':
    case 'p':
        new_emulation = Q_EMUL_PETSCII;
        break;

    case 'S':
    case 's':
        new_emulation = Q_EMUL_ATASCII;
        break;

    case 'T':
    case 't':
        new_emulation = Q_EMUL_LINUX_UTF8;
        break;

    case 'X':
    case 'x':
        new_emulation = Q_EMUL_XTERM;
        break;

    case '8':
        new_emulation = Q_EMUL_XTERM_UTF8;
        break;

    case 'U':
    case 'u':
        new_emulation = Q_EMUL_DEBUG;
        break;

    case Q_KEY_F(1):
        launch_help(Q_HELP_EMULATION_MENU);

        /*
         * Refresh the whole screen.
         */
        console_refresh(Q_FALSE);
        q_screen_dirty = Q_TRUE;
        return;

    case '`':
        /*
         * Backtick works too
         */
    case Q_KEY_ESCAPE:
        /*
         * ESC return to TERMINAL mode
         */
        switch_state(Q_STATE_CONSOLE);

        /*
         * The ABORT exit point
         */
        return;

    default:
        /*
         * Ignore keystroke
         */
        return;
    }

    if (new_emulation == q_status.emulation) {
        /*
         * Ask for emulation reset
         */

        /*
         * Weird.  I had the last parameter set to "0" and GCC didn't turn it
         * into a double that equaled 0.  I have to explicitly pass 0.0 here.
         * But for some reason I didn't have this behavior in console.c ?
         */
        new_keystroke = notify_prompt_form(_("Emulation"),
                                           _("Reset Current Emulation? [y/N] "),
                                           _(" Y-Reset Emulation   N-Exit "),
                                           Q_TRUE, 0.0, "YyNn\r");
        new_keystroke = q_tolower(new_keystroke);

        /*
         * Reset only if the user said so
         */
        if (new_keystroke == 'y') {
            /*
             * If we're finishing off DEBUG emulation, call debug_finish() to
             * get the printable characters in the capture file.
             */
            if (q_status.emulation == Q_EMUL_DEBUG) {
                debug_finish();
            }
            reset_emulation();
        }
    } else {
        /*
         * If we're finishing off DEBUG emulation, call debug_finish() to get
         * the printable characters in the capture file.
         */
        if (q_status.emulation == Q_EMUL_DEBUG) {
            debug_finish();
        }
        q_status.emulation = new_emulation;
        reset_emulation();

        /*
         * Switch the keyboard to the current emulation keyboard
         */
        switch_current_keyboard("");
    }

    /*
     * Set the right codepage
     */
    q_status.codepage = default_codepage(q_status.emulation);

    /*
     * The OK exit point
     */
    switch_state(Q_STATE_CONSOLE);
}
