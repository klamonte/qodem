/*
 * input.c
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

#include "qcurses.h"
#include "common.h"

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#ifdef Q_PDCURSES_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/ioctl.h>
#endif
#include "qodem.h"
#include "console.h"
#include "states.h"
#include "scrollback.h"
#include "netclient.h"
#include "vt100.h"
#include "input.h"

/* Set this to a not-NULL value to enable debug log. */
/* static const char * DLOGNAME = "input"; */
static const char * DLOGNAME = NULL;

/**
 * The current rendering color.
 */
attr_t q_current_color = COLOR_PAIR(0);

/**
 * If true, we are currently inside handle_resize().
 */
Q_BOOL q_in_handle_resize = Q_FALSE;

/**
 * How long it's been since user input came in.
 */
time_t screensaver_time;

/**
 * A table of mappings between ncurses (well, xterm) keyname's OR raw string
 * sequences and qodem keystroke/flags.  This is used by
 * ncurses_extended_keycode() and ncurses_match_keystring().
 */
struct string_to_qodem_keystroke {
    /*
     * The value returned from keyname().
     */
    const char * name;

    /*
     * The KEY_* value that this corresponds to.
     */
    int keystroke;

    /*
     * Shift flag, either 0 or KEY_FLAG_SHIFT.
     */
    int shift;

    /*
     * Ctrl flag, either 0 or KEY_FLAG_CTRL.
     */
    int ctrl;

    /*
     * Alt flag, either 0 or KEY_FLAG_ALT.
     */
    int alt;
};

/**
 * The mappings used by ncurses_match_keystring().
 */
static struct string_to_qodem_keystroke terminfo_keystrings[] = {

    /* name       , key      ,          shift,          ctrl,          alt */
    { "\033[3;2~" , KEY_DC   , KEY_FLAG_SHIFT,             0,            0 },
    { "\033[3;3~" , KEY_DC   ,              0,             0, KEY_FLAG_ALT },
    { "\033[3;4~" , KEY_DC   , KEY_FLAG_SHIFT,             0, KEY_FLAG_ALT },
    { "\033[3;5~" , KEY_DC   ,              0, KEY_FLAG_CTRL,            0 },
    { "\033[3;6~" , KEY_DC   , KEY_FLAG_SHIFT, KEY_FLAG_CTRL,            0 },
    { "\033[3;7~" , KEY_DC   ,              0, KEY_FLAG_CTRL, KEY_FLAG_ALT },
    { "\033[3;8~" , KEY_DC   , KEY_FLAG_SHIFT, KEY_FLAG_CTRL, KEY_FLAG_ALT },

    { "\033[1;2B" , KEY_DOWN , KEY_FLAG_SHIFT,             0,            0 },
    { "\033[1;3B" , KEY_DOWN ,              0,             0, KEY_FLAG_ALT },
    { "\033[1;4B" , KEY_DOWN , KEY_FLAG_SHIFT,             0, KEY_FLAG_ALT },
    { "\033[1;5B" , KEY_DOWN ,              0, KEY_FLAG_CTRL,            0 },
    { "\033[1;6B" , KEY_DOWN , KEY_FLAG_SHIFT, KEY_FLAG_CTRL,            0 },
    { "\033[1;7B" , KEY_DOWN ,              0, KEY_FLAG_CTRL, KEY_FLAG_ALT },
    { "\033[1;8B" , KEY_DOWN , KEY_FLAG_SHIFT, KEY_FLAG_CTRL, KEY_FLAG_ALT },

    { "\033[K"    , KEY_END  ,              0,             0,            0 },
    { "\033[F"    , KEY_END  ,              0,             0,            0 },
    { "\033[1;2F" , KEY_END  , KEY_FLAG_SHIFT,             0,            0 },
    { "\033[1;3F" , KEY_END  ,              0,             0, KEY_FLAG_ALT },
    { "\033[1;4F" , KEY_END  , KEY_FLAG_SHIFT,             0, KEY_FLAG_ALT },
    { "\033[1;5F" , KEY_END  ,              0, KEY_FLAG_CTRL,            0 },
    { "\033[1;6F" , KEY_END  , KEY_FLAG_SHIFT, KEY_FLAG_CTRL,            0 },
    { "\033[1;7F" , KEY_END  ,              0, KEY_FLAG_CTRL, KEY_FLAG_ALT },
    { "\033[1;8F" , KEY_END  , KEY_FLAG_SHIFT, KEY_FLAG_CTRL, KEY_FLAG_ALT },

    { "\033[L"    , KEY_HOME ,              0,             0,            0 },
    { "\033[H"    , KEY_HOME ,              0,             0,            0 },
    { "\033[1;2H" , KEY_HOME , KEY_FLAG_SHIFT,             0,            0 },
    { "\033[1;3H" , KEY_HOME ,              0,             0, KEY_FLAG_ALT },
    { "\033[1;4H" , KEY_HOME , KEY_FLAG_SHIFT,             0, KEY_FLAG_ALT },
    { "\033[1;5H" , KEY_HOME ,              0, KEY_FLAG_CTRL,            0 },
    { "\033[1;6H" , KEY_HOME , KEY_FLAG_SHIFT, KEY_FLAG_CTRL,            0 },
    { "\033[1;7H" , KEY_HOME ,              0, KEY_FLAG_CTRL, KEY_FLAG_ALT },
    { "\033[1;8H" , KEY_HOME , KEY_FLAG_SHIFT, KEY_FLAG_CTRL, KEY_FLAG_ALT },

    { "\033[@"    , KEY_IC   ,              0,             0,            0 },
    { "\033[2;2~" , KEY_IC   , KEY_FLAG_SHIFT,             0,            0 },
    { "\033[2;3~" , KEY_IC   ,              0,             0, KEY_FLAG_ALT },
    { "\033[2;4~" , KEY_IC   , KEY_FLAG_SHIFT,             0, KEY_FLAG_ALT },
    { "\033[2;5~" , KEY_IC   ,              0, KEY_FLAG_CTRL,            0 },
    { "\033[2;6~" , KEY_IC   , KEY_FLAG_SHIFT, KEY_FLAG_CTRL,            0 },
    { "\033[2;7~" , KEY_IC   ,              0, KEY_FLAG_CTRL, KEY_FLAG_ALT },
    { "\033[2;8~" , KEY_IC   , KEY_FLAG_SHIFT, KEY_FLAG_CTRL, KEY_FLAG_ALT },

    { "\033[D"    , KEY_LEFT ,              0,             0,            0 },
    { "\033[1;2D" , KEY_LEFT , KEY_FLAG_SHIFT,             0,            0 },
    { "\033[1;3D" , KEY_LEFT ,              0,             0, KEY_FLAG_ALT },
    { "\033[1;4D" , KEY_LEFT , KEY_FLAG_SHIFT,             0, KEY_FLAG_ALT },
    { "\033[1;5D" , KEY_LEFT ,              0, KEY_FLAG_CTRL,            0 },
    { "\033[1;6D" , KEY_LEFT , KEY_FLAG_SHIFT, KEY_FLAG_CTRL,            0 },
    { "\033[1;7D" , KEY_LEFT ,              0, KEY_FLAG_CTRL, KEY_FLAG_ALT },
    { "\033[1;8D" , KEY_LEFT , KEY_FLAG_SHIFT, KEY_FLAG_CTRL, KEY_FLAG_ALT },

    { "\033[U"    , KEY_NPAGE,              0,             0,            0 },
    { "\033[6;2~" , KEY_NPAGE, KEY_FLAG_SHIFT,             0,            0 },
    { "\033[6;3~" , KEY_NPAGE,              0,             0, KEY_FLAG_ALT },
    { "\033[6;4~" , KEY_NPAGE, KEY_FLAG_SHIFT,             0, KEY_FLAG_ALT },
    { "\033[6;5~" , KEY_NPAGE,              0, KEY_FLAG_CTRL,            0 },
    { "\033[6;6~" , KEY_NPAGE, KEY_FLAG_SHIFT, KEY_FLAG_CTRL,            0 },
    { "\033[6;7~" , KEY_NPAGE,              0, KEY_FLAG_CTRL, KEY_FLAG_ALT },
    { "\033[6;8~" , KEY_NPAGE, KEY_FLAG_SHIFT, KEY_FLAG_CTRL, KEY_FLAG_ALT },

    { "\033[M"    , KEY_PPAGE,              0,             0,            0 },
    { "\033[V"    , KEY_PPAGE,              0,             0,            0 },
    { "\033[5;2~" , KEY_PPAGE, KEY_FLAG_SHIFT,             0,            0 },
    { "\033[5;3~" , KEY_PPAGE,              0,             0, KEY_FLAG_ALT },
    { "\033[5;4~" , KEY_PPAGE, KEY_FLAG_SHIFT,             0, KEY_FLAG_ALT },
    { "\033[5;5~" , KEY_PPAGE,              0, KEY_FLAG_CTRL,            0 },
    { "\033[5;6~" , KEY_PPAGE, KEY_FLAG_SHIFT, KEY_FLAG_CTRL,            0 },
    { "\033[5;7~" , KEY_PPAGE,              0, KEY_FLAG_CTRL, KEY_FLAG_ALT },
    { "\033[5;8~" , KEY_PPAGE, KEY_FLAG_SHIFT, KEY_FLAG_CTRL, KEY_FLAG_ALT },

    { "\033[C"    , KEY_RIGHT,              0,             0,            0 },
    { "\033[1;2C" , KEY_RIGHT, KEY_FLAG_SHIFT,             0,            0 },
    { "\033[1;3C" , KEY_RIGHT,              0,             0, KEY_FLAG_ALT },
    { "\033[1;4C" , KEY_RIGHT, KEY_FLAG_SHIFT,             0, KEY_FLAG_ALT },
    { "\033[1;5C" , KEY_RIGHT,              0, KEY_FLAG_CTRL,            0 },
    { "\033[1;6C" , KEY_RIGHT, KEY_FLAG_SHIFT, KEY_FLAG_CTRL,            0 },
    { "\033[1;7C" , KEY_RIGHT,              0, KEY_FLAG_CTRL, KEY_FLAG_ALT },
    { "\033[1;8C" , KEY_RIGHT, KEY_FLAG_SHIFT, KEY_FLAG_CTRL, KEY_FLAG_ALT },

    { "\033[A"    , KEY_UP   ,              0,             0,            0 },
    { "\033[1;2A" , KEY_UP   , KEY_FLAG_SHIFT,             0,            0 },
    { "\033[1;3A" , KEY_UP   ,              0,             0, KEY_FLAG_ALT },
    { "\033[1;4A" , KEY_UP   , KEY_FLAG_SHIFT,             0, KEY_FLAG_ALT },
    { "\033[1;5A" , KEY_UP   ,              0, KEY_FLAG_CTRL,            0 },
    { "\033[1;6A" , KEY_UP   , KEY_FLAG_SHIFT, KEY_FLAG_CTRL,            0 },
    { "\033[1;7A" , KEY_UP   ,              0, KEY_FLAG_CTRL, KEY_FLAG_ALT },
    { "\033[1;8A" , KEY_UP   , KEY_FLAG_SHIFT, KEY_FLAG_CTRL, KEY_FLAG_ALT },

    { "\033[200~" , Q_KEY_BRACKET_ON,       0,             0,            0 },
    { "\033[201~" , Q_KEY_BRACKET_OFF,      0,             0,            0 },

    /* Terminating entry */
    { ""      , ERR      ,              0,             0,            0 },
};

/**
 * If true, don't call win_wch() in qodem_getch(), instead call
 * curses_match_string() keys_in_queue is false again.
 */
static Q_BOOL keys_in_queue = Q_FALSE;

/**
 * curses_match_string() parse states:
 *   0 - no characters received
 *   1 - reading more bytes into a sequence
 *   2 - draining the bytes in queue (keys_in_queue is true)
 */
static int curses_match_state = 0;

/**
 * A buffer to match sequences to a function key number.
 */
static char curses_match_buffer[16];
static unsigned int curses_match_buffer_i = 0;
static unsigned int curses_match_buffer_n = 0;

/**
 * Reset the curses function key recognizer.
 */
static void curses_match_reset() {
    DLOG(("curses_match_reset()\n"));

    curses_match_buffer_i = 0;
    curses_match_buffer_n = 0;
    memset(curses_match_buffer, 0, sizeof(curses_match_buffer));
    keys_in_queue = Q_FALSE;
    curses_match_state = 0;
}

/**
 * Parse terminal keystroke sequences into a keystroke and flags.
 *
 * @param ch the next byte of the sequence
 * @param key the ncurses key
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.
 */
static void curses_match_keystring(int ch, int * key, int * flags) {

    int i;

    DLOG(("curses_match_keystring() %d in: ch '%c' 0x%04x %d %o curses_match_buffer_n %d\n",
            curses_match_state, ch, ch, ch, ch, curses_match_buffer_n));

drain_queue:

    if (curses_match_state == 2) {
        /*
         * We are draining the queue.
         */

        if (ch != ERR) {
            /*
             * Special case: we got ESC {something} ESC {something}, and that
             * second {something} is being passed in right here.  Allow it to
             * be appended, it will be drained out later.
             */
            DLOG(("curses_match_keystring() %d DRAIN APPEND '%c'\n",
                    curses_match_state, ch));
            if (curses_match_buffer_n < sizeof(curses_match_buffer) - 1) {
                curses_match_buffer[curses_match_buffer_n] = (char) (ch & 0xFF);
                curses_match_buffer_n++;
            }
        }

        if ((curses_match_buffer_n - curses_match_buffer_i >= 2) &&
            (curses_match_buffer[curses_match_buffer_i] == '\033')) {
            /*
             * Special case: the last two bytes in the buffer represent
             * Alt-{comething}.  Return it as {something} with KEY_FLAG_ALT.
             */
            DLOG(("curses_match_keystring() %d DRAIN ALT-'%c'\n",
                    curses_match_state,
                    curses_match_buffer[curses_match_buffer_i] + 1));
            if (flags != NULL) {
                *flags |= KEY_FLAG_ALT;
            }
            *key = curses_match_buffer[curses_match_buffer_i + 1];
            curses_match_buffer_i += 2;
            if (curses_match_buffer_i == curses_match_buffer_n) {
                curses_match_reset();
            }
            if (flags == NULL) {
                DLOG(("curses_match_keystring() DRAIN ALT out: key '%c' %d flags NULL\n",
                        *key, *key));
            }
            else {
                DLOG(("curses_match_keystring() DRAIN ALT out: key '%c' %d flags %d\n",
                        *key, *key, *flags));
            }
            return;
        }

        *key = curses_match_buffer[curses_match_buffer_i];
        curses_match_buffer_i++;
        if (ch > 0xFF) {
            /*
             * Unicode character
             */
            if (flags != NULL) {
                *flags |= KEY_FLAG_UNICODE;
            }
        }
        if ((curses_match_buffer_i == curses_match_buffer_n - 1) && (curses_match_buffer[curses_match_buffer_i] == '\033')) {
            /*
             * The last byte in curses_match_buffer is the first byte for a
             * new sequence.  Switch to match state 1.
             */
            curses_match_reset();
            curses_match_state = 1;
            curses_match_buffer_n = 1;
            curses_match_buffer[0] = '\033';
            keys_in_queue = Q_TRUE;
        } else if (curses_match_buffer_i == curses_match_buffer_n) {
            curses_match_reset();
        }
        if (flags == NULL) {
            DLOG(("curses_match_keystring() DRAIN out: key '%c' %d flags NULL\n",
                    *key, *key));
        }
        else {
            DLOG(("curses_match_keystring() DRAIN out: key '%c' %d flags %d\n",
                    *key, *key, *flags));
        }

        return;
    }

    assert(ch != ERR);
    assert(curses_match_state != 2);
    assert(keys_in_queue == Q_FALSE);
    assert(curses_match_buffer_i == 0);

    if (curses_match_buffer_n < sizeof(curses_match_buffer) - 1) {
        /*
         * We still have room for another byte.
         */
        curses_match_buffer[curses_match_buffer_n] = (char) (ch & 0xFF);
        curses_match_buffer_n++;
    } else {
        /*
         * A sequence is too long.
         */
        curses_match_state = 2;
        keys_in_queue = Q_TRUE;
        ch = ERR;
        goto drain_queue;
    }

    switch (curses_match_state) {
    case 0:
        /*
         * Waiting to begin a new sequence.
         */
        if (ch != '\033') {
            /*
             * ch is either a byte or a Unicode wchar_t.
             */
            *key = ch;
            if (ch > 0xFF) {
                /*
                 * Unicode character
                 */
                if (flags != NULL) {
                    *flags |= KEY_FLAG_UNICODE;
                }
            }
            assert(curses_match_buffer_n == 1);
            curses_match_buffer_n = 0;
            return;
        }
        /* We have seen the beginning of a sequence. */
        curses_match_state = 1;
        *key = ERR;
        if (flags != NULL) {
            *flags = 0;
        }
        return;

    case 1:
        if (curses_match_buffer_n == 2) {
            /*
             * This is where we differentiate between Alt-x and something
             * else.
             */
            if (ch != '[') {
                /*
                 * This was Alt-x.
                 */
                *key = ch;
                if (flags != NULL) {
                    *flags = KEY_FLAG_ALT;
                }
                curses_match_reset();
                return;
            }
            /*
             * We have at least CSI in the buffer from this line forward.
             */
        }

        /*
         * Collecting more bytes to match a sequence.
         */
        switch (ch) {
        case '~':
        case 'A':
        case 'B':
        case 'C':
        case 'D':
        case 'F':
        case 'H':
        case 'K':
        case 'V':
        case 'U':
        case '@':
            /* A sequence is completed, and should match. */
            curses_match_state = 0;
            break;

        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case ';':
        case '[':
            /* We are collecting more values to match a sequence. */
            *key = ERR;
            if (flags != NULL) {
                *flags = 0;
            }
            return;

        default:
            /* An invalid sequence came in, switch to drain the queue. */
            curses_match_state = 2;
            keys_in_queue = Q_TRUE;
            ch = ERR;
            goto drain_queue;
        }

        break;

    default:
        /*
         * BUG.
         */
        abort();
        return;
    }
    DLOG(("curses_match_keystring() searching for sequence: '%s'\n",
        curses_match_buffer));

    /*
     * The state machine is partially reset.  It is set to scan for a new
     * sequence, but the buffer contains the last sequence.  We scan our list
     * and either return something or not, but we must trash the buffer
     * before we exit.
     */
    assert(curses_match_state == 0);
    assert(keys_in_queue == Q_FALSE);

    for (i = 0; ; i++) {
        if (strlen(terminfo_keystrings[i].name) == 0) {
            /*
             * Not found, and not looking for a potential match.
             */
            DLOG(("curses_match_keystring() out: NOT FOUND, return ERR\n"));
            *key = ERR;
            if (flags != NULL) {
                *flags = 0;
            }
            curses_match_reset();
            return;
        }

        if (strcmp(terminfo_keystrings[i].name, curses_match_buffer) == 0) {
            /*
             * Match found.
             */
            DLOG(("curses_match_keystring() out: ** FOUND** %o %d 0x%x\n",
                    terminfo_keystrings[i].keystroke,
                    terminfo_keystrings[i].keystroke,
                    terminfo_keystrings[i].keystroke));
            *key = terminfo_keystrings[i].keystroke;
            if (flags != NULL) {
                *flags = terminfo_keystrings[i].shift |
                         terminfo_keystrings[i].ctrl |
                         terminfo_keystrings[i].alt;
            }
            curses_match_reset();
            break;
        }
    }

    if (flags == NULL) {
        DLOG(("curses_match_keystring() out: key '%c' %d flags NULL\n",
                *key, *key));
    } else {
        DLOG(("curses_match_keystring() out: key '%c' %d flags %d\n",
                *key, *key, *flags));
    }

}

#if defined(Q_PDCURSES) || defined(Q_PDCURSES_WIN32)

/**
 * Convert a PDCurses key to a ncurses key + flags.
 *
 * @param key the PDCurses key
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.
 */
static void pdcurses_key(int * key, int * flags) {
    DLOG(("pdcurses_key() in: key '%c' %d 0x%x %o flags %d\n",
            *key, *key, *key, *key, *flags));

    switch (*key) {
    case ALT_0:
        *key = '0';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_1:
        *key = '1';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_2:
        *key = '2';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_3:
        *key = '3';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_4:
        *key = '4';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_5:
        *key = '5';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_6:
        *key = '6';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_7:
        *key = '7';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_8:
        *key = '8';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_9:
        *key = '9';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_A:
        *key = 'a';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_B:
        *key = 'b';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_C:
        *key = 'c';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_D:
        *key = 'd';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_E:
        *key = 'e';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_F:
        *key = 'f';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_G:
        *key = 'g';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_H:
        *key = 'h';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_I:
        *key = 'i';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_J:
        *key = 'j';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_K:
        *key = 'k';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_L:
        *key = 'l';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_M:
        *key = 'm';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_N:
        *key = 'n';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_O:
        *key = 'o';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_P:
        *key = 'p';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_Q:
        *key = 'q';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_R:
        *key = 'r';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_S:
        *key = 's';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_T:
        *key = 't';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_U:
        *key = 'u';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_V:
        *key = 'v';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_W:
        *key = 'w';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_X:
        *key = 'x';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_Y:
        *key = 'y';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_Z:
        *key = 'z';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case CTL_LEFT:
        *key = KEY_LEFT;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case CTL_RIGHT:
        *key = KEY_RIGHT;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case CTL_PGUP:
        *key = KEY_PPAGE;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case CTL_PGDN:
        *key = KEY_NPAGE;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case CTL_HOME:
        *key = KEY_HOME;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case CTL_END:
        *key = KEY_END;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case ALT_MINUS:
        *key = '-';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_EQUAL:
        *key = '=';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case CTL_UP:
        *key = KEY_UP;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case CTL_DOWN:
        *key = KEY_DOWN;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case CTL_TAB:
        *key = Q_KEY_TAB;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case ALT_PGUP:
        *key = KEY_PPAGE;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_PGDN:
        *key = KEY_NPAGE;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_HOME:
        *key = KEY_HOME;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_END:
        *key = KEY_END;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case SHF_UP:
        *key = KEY_UP;
        break;
    case ALT_UP:
        *key = KEY_UP;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case SHF_DOWN:
        *key = KEY_DOWN;
        break;
    case ALT_DOWN:
        *key = KEY_DOWN;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_LEFT:
        *key = KEY_LEFT;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_RIGHT:
        *key = KEY_RIGHT;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_TAB:
        *key = Q_KEY_TAB;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case SHF_IC:
        *key = KEY_IC;
        break;
    case ALT_INS:
        *key = KEY_IC;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case CTL_INS:
        *key = KEY_IC;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case SHF_DC:
        *key = KEY_DC;
        break;
    case ALT_DEL:
        *key = KEY_DC;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case CTL_DEL:
        *key = KEY_DC;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case CTL_BKSP:
        *key = KEY_BACKSPACE;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case ALT_BKSP:
        *key = KEY_BACKSPACE;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_ENTER:
        *key = KEY_ENTER;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_ESC:
        *key = Q_KEY_ESCAPE;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_BQUOTE:
        *key = '`';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_FQUOTE:
        *key = '\'';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_LBRACKET:
        *key = '[';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_RBRACKET:
        *key = ']';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_SEMICOLON:
        *key = ';';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_COMMA:
        *key = ',';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_FSLASH:
        *key = '/';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_BSLASH:
        *key = '\\';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case CTL_ENTER:
        *key = KEY_ENTER;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;

    case PADSLASH:
        *key = Q_KEY_PAD_SLASH;
        break;
    case SHF_PADSLASH:
        *key = Q_KEY_PAD_SLASH;
        break;
    case CTL_PADSLASH:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD_SLASH;
        break;
    case ALT_PADSLASH:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD_SLASH;
        break;

    case PADENTER:
    case SHF_PADENTER:
        *key = Q_KEY_PAD_ENTER;
        break;
    case CTL_PADENTER:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD_ENTER;
        break;
    case ALT_PADENTER:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD_ENTER;
        break;

    /*
     * case SHF_PADSTOP - seems an omission in the API
     */

    case PADSTOP:
        *key = Q_KEY_PAD_STOP;
        break;
    case CTL_PADSTOP:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD_STOP;
        break;
    case ALT_PADSTOP:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD_STOP;
        break;
    case ALT_STOP:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = '.';
        break;

    case PADSTAR:
        *key = Q_KEY_PAD_STAR;
        break;
    case SHF_PADSTAR:
        *key = Q_KEY_PAD_STAR;
        break;
    case CTL_PADSTAR:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD_STAR;
        break;
    case ALT_PADSTAR:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD_STAR;
        break;

    case PADMINUS:
        *key = Q_KEY_PAD_MINUS;
        break;
    case SHF_PADMINUS:
        *key = Q_KEY_PAD_MINUS;
        break;
    case CTL_PADMINUS:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD_MINUS;
        break;
    case ALT_PADMINUS:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD_MINUS;
        break;

    case PADPLUS:
        *key = Q_KEY_PAD_PLUS;
        break;
    case SHF_PADPLUS:
        *key = Q_KEY_PAD_PLUS;
        break;
    case CTL_PADPLUS:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD_PLUS;
        break;
    case ALT_PADPLUS:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD_PLUS;
        break;

    case PAD0:
        *key = Q_KEY_PAD0;
        break;
    case CTL_PAD0:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD0;
        break;
    case ALT_PAD0:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD0;
        break;

    case CTL_PAD1:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD1;
        break;
    case ALT_PAD1:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD1;
        break;

    case CTL_PAD2:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD2;
        break;
    case ALT_PAD2:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD2;
        break;

    case CTL_PAD3:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD3;
        break;
    case ALT_PAD3:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD3;
        break;

    case CTL_PAD4:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD4;
        break;
    case ALT_PAD4:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD4;
        break;

    case CTL_PADCENTER:
        *key = Q_KEY_PAD5;
        break;
    case CTL_PAD5:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD5;
        break;
    case ALT_PAD5:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD5;
        break;

    case CTL_PAD6:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD6;
        break;
    case ALT_PAD6:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD6;
        break;

    case CTL_PAD7:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD7;
        break;
    case ALT_PAD7:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD7;
        break;

    case CTL_PAD8:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD8;
        break;
    case ALT_PAD8:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD8;
        break;

    case CTL_PAD9:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD9;
        break;
    case ALT_PAD9:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD9;
        break;

    case KEY_SPREVIOUS:
        /*
         * Shift-PgUp
         */
        if (flags != NULL) {
            *flags = KEY_FLAG_SHIFT;
        }
        *key = Q_KEY_PPAGE;
        break;

    case KEY_SNEXT:
        /*
         * Shift-PgDn
         */
        if (flags != NULL) {
            *flags = KEY_FLAG_SHIFT;
        }
        *key = Q_KEY_NPAGE;
        break;
    }

    DLOG(("pdcurses_key() out: key '%c' %d 0x%x %o flags %d\n",
            *key, *key, *key, *key, *flags));

}

#else

/**
 * The mappings used by ncurses_extended_keycode().
 */
static struct string_to_qodem_keystroke keynames[] = {

    /* name   , key      ,          shift,          ctrl,          alt */
    { "kDC"   , KEY_DC   ,              0,             0,            0 },
    { "kDC3"  , KEY_DC   ,              0,             0, KEY_FLAG_ALT },
    { "kDC4"  , KEY_DC   , KEY_FLAG_SHIFT,             0, KEY_FLAG_ALT },
    { "kDC5"  , KEY_DC   ,              0, KEY_FLAG_CTRL,            0 },
    { "kDC6"  , KEY_DC   , KEY_FLAG_SHIFT, KEY_FLAG_CTRL,            0 },
    { "kDC7"  , KEY_DC   ,              0, KEY_FLAG_CTRL, KEY_FLAG_ALT },

    { "kDN"   , KEY_DOWN ,              0,             0,            0 },
    { "kDN3"  , KEY_DOWN ,              0,             0, KEY_FLAG_ALT },
    { "kDN4"  , KEY_DOWN , KEY_FLAG_SHIFT,             0, KEY_FLAG_ALT },
    { "kDN5"  , KEY_DOWN ,              0, KEY_FLAG_CTRL,            0 },
    { "kDN6"  , KEY_DOWN , KEY_FLAG_SHIFT, KEY_FLAG_CTRL,            0 },
    { "kDN7"  , KEY_DOWN ,              0, KEY_FLAG_CTRL, KEY_FLAG_ALT },

    { "kEND"  , KEY_END  ,              0,             0,            0 },
    { "kEND3" , KEY_END  ,              0,             0, KEY_FLAG_ALT },
    { "kEND4" , KEY_END  , KEY_FLAG_SHIFT,             0, KEY_FLAG_ALT },
    { "kEND5" , KEY_END  ,              0, KEY_FLAG_CTRL,            0 },
    { "kEND6" , KEY_END  , KEY_FLAG_SHIFT, KEY_FLAG_CTRL,            0 },
    { "kEND7" , KEY_END  ,              0, KEY_FLAG_CTRL, KEY_FLAG_ALT },

    { "kHOM"  , KEY_HOME ,              0,             0,            0 },
    { "kHOM3" , KEY_HOME ,              0,             0, KEY_FLAG_ALT },
    { "kHOM4" , KEY_HOME , KEY_FLAG_SHIFT,             0, KEY_FLAG_ALT },
    { "kHOM5" , KEY_HOME ,              0, KEY_FLAG_CTRL,            0 },
    { "kHOM6" , KEY_HOME , KEY_FLAG_SHIFT, KEY_FLAG_CTRL,            0 },
    { "kHOM7" , KEY_HOME ,              0, KEY_FLAG_CTRL, KEY_FLAG_ALT },

    { "kIC"   , KEY_IC   ,              0,             0,            0 },
    { "kIC3"  , KEY_IC   ,              0,             0, KEY_FLAG_ALT },
    { "kIC4"  , KEY_IC   , KEY_FLAG_SHIFT,             0, KEY_FLAG_ALT },
    { "kIC5"  , KEY_IC   ,              0, KEY_FLAG_CTRL,            0 },
    { "kIC6"  , KEY_IC   , KEY_FLAG_SHIFT, KEY_FLAG_CTRL,            0 },
    { "kIC7"  , KEY_IC   ,              0, KEY_FLAG_CTRL, KEY_FLAG_ALT },

    { "kLFT"  , KEY_LEFT ,              0,             0,            0 },
    { "kLFT3" , KEY_LEFT ,              0,             0, KEY_FLAG_ALT },
    { "kLFT4" , KEY_LEFT , KEY_FLAG_SHIFT,             0, KEY_FLAG_ALT },
    { "kLFT5" , KEY_LEFT ,              0, KEY_FLAG_CTRL,            0 },
    { "kLFT6" , KEY_LEFT , KEY_FLAG_SHIFT, KEY_FLAG_CTRL,            0 },
    { "kLFT7" , KEY_LEFT ,              0, KEY_FLAG_CTRL, KEY_FLAG_ALT },

    { "kNXT"  , KEY_NPAGE,              0,             0,            0 },
    { "kNXT3" , KEY_NPAGE,              0,             0, KEY_FLAG_ALT },
    { "kNXT4" , KEY_NPAGE, KEY_FLAG_SHIFT,             0, KEY_FLAG_ALT },
    { "kNXT5" , KEY_NPAGE,              0, KEY_FLAG_CTRL,            0 },
    { "kNXT6" , KEY_NPAGE, KEY_FLAG_SHIFT, KEY_FLAG_CTRL,            0 },
    { "kNXT7" , KEY_NPAGE,              0, KEY_FLAG_CTRL, KEY_FLAG_ALT },

    { "kPRV"  , KEY_PPAGE,              0,             0,            0 },
    { "kPRV3" , KEY_PPAGE,              0,             0, KEY_FLAG_ALT },
    { "kPRV4" , KEY_PPAGE, KEY_FLAG_SHIFT,             0, KEY_FLAG_ALT },
    { "kPRV5" , KEY_PPAGE,              0, KEY_FLAG_CTRL,            0 },
    { "kPRV6" , KEY_PPAGE, KEY_FLAG_SHIFT, KEY_FLAG_CTRL,            0 },
    { "kPRV7" , KEY_PPAGE,              0, KEY_FLAG_CTRL, KEY_FLAG_ALT },

    { "kRIT"  , KEY_RIGHT,              0,             0,            0 },
    { "kRIT3" , KEY_RIGHT,              0,             0, KEY_FLAG_ALT },
    { "kRIT4" , KEY_RIGHT, KEY_FLAG_SHIFT,             0, KEY_FLAG_ALT },
    { "kRIT5" , KEY_RIGHT,              0, KEY_FLAG_CTRL,            0 },
    { "kRIT6" , KEY_RIGHT, KEY_FLAG_SHIFT, KEY_FLAG_CTRL,            0 },
    { "kRIT7" , KEY_RIGHT,              0, KEY_FLAG_CTRL, KEY_FLAG_ALT },

    { "kUP"   , KEY_UP   ,              0,             0,            0 },
    { "kUP3"  , KEY_UP   ,              0,             0, KEY_FLAG_ALT },
    { "kUP4"  , KEY_UP   , KEY_FLAG_SHIFT,             0, KEY_FLAG_ALT },
    { "kUP5"  , KEY_UP   ,              0, KEY_FLAG_CTRL,            0 },
    { "kUP6"  , KEY_UP   , KEY_FLAG_SHIFT, KEY_FLAG_CTRL,            0 },
    { "kUP7"  , KEY_UP   ,              0, KEY_FLAG_CTRL, KEY_FLAG_ALT },

    /* Terminating entry */
    { ""      , ERR      ,              0,             0,            0 },

};

/**
 * Convert out-of-range key codes (extended_keycodes) into a keystroke and
 * flags.
 *
 * @param key the ncurses key
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.
 */
static void ncurses_extended_keycode(int * key, int * flags) {
    int i;

    if (flags == NULL) {
        DLOG(("ncurses_extended_keycode() in: key '%c' %d %x %o flags NULL\n",
                *key, *key, *key, *key));
    } else {
        DLOG(("ncurses_extended_keycode() in: key '%c' %d %x %o flags %d\n",
                *key, *key, *key, *key, *flags));
    }
    DLOG(("ncurses_extended_keycode()    keyname '%s'\n", keyname(*key)));

    for (i = 0; ; i++) {
        if (strlen(keynames[i].name) == 0) {
            /*
             * Not found.
             */
            DLOG(("ncurses_extended_keycode() out: NOT FOUND, return ERR\n"));
            *key = ERR;
            if (flags != NULL) {
                *flags = 0;
            }
            return;
        }

        if (strcmp(keynames[i].name, keyname(*key)) == 0) {
            DLOG(("ncurses_extended_keycode() out: ** FOUND **\n"));
            *key = keynames[i].keystroke;
            if (flags != NULL) {
                *flags = keynames[i].shift | keynames[i].ctrl |
                         keynames[i].alt;
            }
            break;
        }
    }

    if (flags == NULL) {
        DLOG(("ncurses_extended_keycode() out: key '%c' %d flags NULL\n",
                *key, *key));
    } else {
        DLOG(("ncurses_extended_keycode() out: key '%c' %d flags %d\n",
                *key, *key, *flags));
    }

}

#endif /* Q_PDCURSES || Q_PDCURSES_WIN32 */

/**
 * Send the current screen dimensions to the remote side.
 */
void send_screen_size() {
#ifndef Q_PDCURSES_WIN32
    struct winsize console_size;
#endif

    /*
     * Pass the new dimensions to the remote side
     */
    if (q_status.online == Q_TRUE) {
        if (q_status.dial_method == Q_DIAL_METHOD_TELNET) {
            telnet_resize_screen(HEIGHT - STATUS_HEIGHT, WIDTH);
        }
        if (q_status.dial_method == Q_DIAL_METHOD_RLOGIN) {
            rlogin_resize_screen(HEIGHT - STATUS_HEIGHT, WIDTH);
        }
#ifdef Q_SSH_CRYPTLIB
        if (q_status.dial_method == Q_DIAL_METHOD_SSH) {
            ssh_resize_screen(HEIGHT - STATUS_HEIGHT, WIDTH);
        }
#endif

#ifndef Q_PDCURSES_WIN32
        if ((q_status.dial_method == Q_DIAL_METHOD_SHELL) ||
            (q_status.dial_method == Q_DIAL_METHOD_COMMANDLINE)) {

            /*
             * Set the TTY cols and rows.
             */
            if (ioctl(q_child_tty_fd, TIOCGWINSZ, &console_size) < 0) {
                /* perror("ioctl(TIOCGWINSZ)"); */
            } else {
                console_size.ws_row = HEIGHT - STATUS_HEIGHT;
                console_size.ws_col = WIDTH;
                if (ioctl(q_child_tty_fd, TIOCSWINSZ, &console_size) < 0) {
                    /* perror("ioctl(TIOCSWINSZ)"); */
                }
            }
        }
#endif
    } /* if (q_status.online == Q_TRUE) */
}

/**
 * Perform the necessary screen resizing if a KEY_RESIZE comes in.
 */
static void handle_resize() {
    int new_height, new_width;

#if defined(Q_PDCURSES) || defined(Q_PDCURSES_WIN32)
    /*
     * Update the internal window state with the new size the user selected.
     */
    resize_term(0, 0);
#endif

    /*
     * Special case: KEY_RESIZE
     */
    getmaxyx(stdscr, new_height, new_width);

    /*
     * At this point, the display is hosed.
     */
    q_in_handle_resize = Q_TRUE;

    /*
     * Increase the # of lines to match the new screen size if necessary.
     *
     * Note this check is (new_height - STATUS_HEIGHT), not
     * (new_height - STATUS_HEIGHT - 1)
     */
    if (q_status.scrollback_lines < new_height - STATUS_HEIGHT) {
        while (q_status.scrollback_lines < new_height - STATUS_HEIGHT) {
            new_scrollback_line();
        }
        q_scrollback_position = q_scrollback_last;
        q_scrollback_current = q_scrollback_last;

        /*
         * Reset scrolling regions
         */
        q_status.scroll_region_top = 0;
        q_status.scroll_region_bottom = new_height - STATUS_HEIGHT - 1;

        /*
         * q_status.cursor_x and cursor_y are almost certainly in the wrong
         * place.  Let's get them in the bottom left corner.
         */
        q_status.cursor_x = 0;
        q_status.cursor_y = new_height - STATUS_HEIGHT - 1;

    } else {
        /*
         * We have enough scrollback to cover the new size, we just need to
         * bring q_status.cursor_y down or up.
         */
        q_status.cursor_y += (new_height - HEIGHT);
        q_status.scroll_region_bottom += (new_height - HEIGHT);
    }
    WIDTH = new_width;
    HEIGHT = new_height;

    if (q_status.cursor_x > WIDTH - 1) {
        q_status.cursor_x = WIDTH - 1;
    }

    /*
     * Fix the phonebook display
     */
    phonebook_reset();

    /*
     * Pass the new dimensions to the remote side
     */
    send_screen_size();

    q_screen_dirty = Q_TRUE;
    q_in_handle_resize = Q_FALSE;
}

/**
 * Sends mouse tracking sequences to the other side if KEY_MOUSE comes in.
 */
void handle_mouse() {

    wchar_t raw_buffer[6];
    char utf8_buffer[6 * 2 + 4 + 1];
    char sgr_buffer[32];
    MEVENT mouse;
    int rc;
    unsigned int i;
    /*
     * ncurses appears to be providing a PRESS followed by MOUSE_POSITION
     * rather than PRESS/RELEASE/MOUSE_POSITION.  This looks like a bug in
     * that it is forgetting button state.
     *
     * Hang onto the prior states and convert MOTION into RELEASE events as
     * needed.
     */
    static Q_BOOL old_mouse1 = Q_FALSE;
    static Q_BOOL old_mouse2 = Q_FALSE;
    static Q_BOOL old_mouse3 = Q_FALSE;
    Q_BOOL mouse1 = Q_FALSE;
    Q_BOOL mouse2 = Q_FALSE;
    Q_BOOL mouse3 = Q_FALSE;
    Q_BOOL mouse4 = Q_FALSE;
    /*
     * ncurses doesn't always support button 5, so put all references to
     * button 5 inside an ifdef check.
     */
#ifdef BUTTON5_PRESSED
    Q_BOOL mouse5 = Q_FALSE;
#endif
    static int old_x = -1;
    static int old_y = -1;
    Q_BOOL press = Q_FALSE;
    Q_BOOL release = Q_FALSE;
    Q_BOOL motion = Q_FALSE;
    Q_BOOL real_motion = Q_FALSE;

#if defined(Q_PDCURSES) || defined(Q_PDCURSES_WIN32)
    if ((rc = nc_getmouse(&mouse)) == OK) {
#else
    if ((rc = getmouse(&mouse)) == OK) {
#endif /* Q_PDCURSES */

        DLOG(("MOUSE mode %s %s\t",
                q_xterm_mouse_protocol == XTERM_MOUSE_OFF ? "XTERM_MOUSE_OFF" :
                q_xterm_mouse_protocol == XTERM_MOUSE_X10 ? "XTERM_MOUSE_X10" :
                q_xterm_mouse_protocol ==
                XTERM_MOUSE_NORMAL ? "XTERM_MOUSE_NORMAL" :
                q_xterm_mouse_protocol ==
                XTERM_MOUSE_BUTTONEVENT ? "XTERM_MOUSE_BUTTONEVENT" :
                q_xterm_mouse_protocol ==
                XTERM_MOUSE_ANYEVENT ? "XTERM_MOUSE_ANYEVENT" : "UNKOWN",
                q_xterm_mouse_encoding ==
                XTERM_MOUSE_ENCODING_X10 ? "XTERM_MOUSE_ENCODING_X10" :
                q_xterm_mouse_encoding ==
                XTERM_MOUSE_ENCODING_UTF8 ? "XTERM_MOUSE_ENCODING_UTF8" :
                "XTERM_MOUSE_ENCODING_SGR"));
        DLOG2(("raw: %d %d %d %08lx\t", mouse.x, mouse.y, mouse.z,
                mouse.bstate));

        /*
         * Mouse event is parsed, now decide what to do with it.
         */
        if (q_program_state != Q_STATE_CONSOLE) {
            DLOG2((" DISCARD not in console\n"));

            /*
             * Discard.  We only care about the mouse when connected and in
             * the console.
             */
            return;
        }
        if ((q_status.online == Q_FALSE) && !Q_SERIAL_OPEN) {
            DLOG2((" DISCARD not online\n"));
            return;
        }

        /*
         * Analyze the MOUSE structure to figure out if this is a press,
         * release, or motion event.
         */

        if (mouse.bstate & REPORT_MOUSE_POSITION) {
            motion = Q_TRUE;
        }
        if ((old_x != mouse.x) || (old_y != mouse.y)) {
            real_motion = Q_TRUE;
        }
        old_x = mouse.x;
        old_y = mouse.y;

        if ((mouse.bstate & BUTTON1_PRESSED) && (old_mouse1 == Q_FALSE)) {
            /*
             * This is a fresh press on mouse1.
             */
            mouse1 = Q_TRUE;
            old_mouse1 = Q_TRUE;
        } else if ((old_mouse1 == Q_TRUE) && (real_motion == Q_TRUE)) {
            /*
             * This is a movement with button 1 down.
             */
            mouse1 = Q_TRUE;
        } else if ((old_mouse1 == Q_TRUE) && (real_motion == Q_FALSE)) {
            /*
             * Convert this motion into a RELEASE.  Same logic for the other
             * buttons.
             */
            mouse1 = Q_TRUE;
            release = Q_TRUE;
            motion = Q_FALSE;
        }
        if ((mouse.bstate & BUTTON2_PRESSED) && (old_mouse2 == Q_FALSE)) {
            mouse2 = Q_TRUE;
            old_mouse2 = Q_TRUE;
        } else if ((old_mouse2 == Q_TRUE) && (real_motion == Q_TRUE)) {
            mouse2 = Q_TRUE;
        } else if ((old_mouse2 == Q_TRUE) && (real_motion == Q_FALSE)) {
            mouse2 = Q_TRUE;
            release = Q_TRUE;
            motion = Q_FALSE;
        }
        if ((mouse.bstate & BUTTON3_PRESSED) && (old_mouse3 == Q_FALSE)) {
            mouse3 = Q_TRUE;
            old_mouse3 = Q_TRUE;
        } else if ((old_mouse3 == Q_TRUE) && (real_motion == Q_TRUE)) {
            mouse3 = Q_TRUE;
        } else if ((old_mouse3 == Q_TRUE) && (real_motion == Q_FALSE)) {
            mouse3 = Q_TRUE;
            release = Q_TRUE;
            motion = Q_FALSE;
        }

        if ((mouse.bstate & BUTTON4_PRESSED) &&
            ((old_mouse1 == Q_TRUE) ||
                (old_mouse2 == Q_TRUE) ||
                (old_mouse3 == Q_TRUE))
        ) {
            /*
             * This is actually a motion event with another mouse button
             * down.
             */
            motion = Q_TRUE;
            mouse1 = old_mouse1;
            mouse2 = old_mouse2;
            mouse3 = old_mouse3;
        } else if (mouse.bstate & BUTTON4_PRESSED) {
            mouse4 = Q_TRUE;
        }

#ifdef BUTTON5_PRESSED
        if (mouse.bstate & BUTTON5_PRESSED) {
            mouse5 = Q_TRUE;
        }
#endif

        if (mouse.bstate & BUTTON1_RELEASED) {
            mouse1 = Q_TRUE;
            old_mouse1 = Q_FALSE;
            release = Q_TRUE;
            motion = Q_FALSE;
        }
        if (mouse.bstate & BUTTON2_RELEASED) {
            mouse2 = Q_TRUE;
            old_mouse2 = Q_FALSE;
            release = Q_TRUE;
            motion = Q_FALSE;
        }
        if (mouse.bstate & BUTTON3_RELEASED) {
            mouse3 = Q_TRUE;
            old_mouse3 = Q_FALSE;
            release = Q_TRUE;
            motion = Q_FALSE;
        }
        if ((release == Q_FALSE) && (motion == Q_FALSE)) {
            press = Q_TRUE;
        }

#ifdef BUTTON5_RELEASED
        DLOG2(("buttons: %s %s %s %s %s %s %s %s\n",
                (release == Q_TRUE ? "RELEASE" : "       "),
                (press == Q_TRUE ? "PRESS" : "     "),
                (motion == Q_TRUE ? "MOTION" : "      "),
                (mouse1 == Q_TRUE ? "1" : " "),
                (mouse2 == Q_TRUE ? "2" : " "),
                (mouse3 == Q_TRUE ? "3" : " "),
                (mouse4 == Q_TRUE ? "4" : " "),
                (mouse5 == Q_TRUE ? "5" : " ")));
#else
        DLOG2(("buttons: %s %s %s %s %s %s %s\n",
                (release == Q_TRUE ? "RELEASE" : "       "),
                (press == Q_TRUE ? "PRESS" : "     "),
                (motion == Q_TRUE ? "MOTION" : "      "),
                (mouse1 == Q_TRUE ? "1" : " "),
                (mouse2 == Q_TRUE ? "2" : " "),
                (mouse3 == Q_TRUE ? "3" : " "),
                (mouse4 == Q_TRUE ? "4" : " ")));
#endif

        /*
         * See if we need to report this event based on the requested
         * protocol.
         */
        switch (q_xterm_mouse_protocol) {
        case XTERM_MOUSE_OFF:
            /*
             * Do nothing
             */
            return;

        case XTERM_MOUSE_X10:
            /*
             * Only report button presses
             */
            if ((release == Q_TRUE) || (motion == Q_TRUE)) {
                return;
            }
            break;

        case XTERM_MOUSE_NORMAL:
            /*
             * Only report button presses and releases
             */
            if ((press == Q_FALSE) && (release == Q_FALSE)) {
                return;
            }
            break;

        case XTERM_MOUSE_BUTTONEVENT:
            /*
             * Only report button presses, button releases, and motions that
             * have a button down (i.e. drag-and-drop).
             */
            if (motion == Q_TRUE) {
                if ((mouse1 == Q_FALSE) &&
                    (mouse2 == Q_FALSE) &&
                    (mouse3 == Q_FALSE) &&
                    (mouse4 == Q_FALSE)
#ifdef BUTTON5_RELEASED
                    && (mouse5 == Q_FALSE)
#endif
                ) {
                    return;
                }
            }
            break;

        case XTERM_MOUSE_ANYEVENT:
            /*
             * Report everything
             */
            break;
        }

        DLOG(("   -- DO ENCODE --\n"));

        /*
         * At this point, if motion == true and release == false, then this
         * was a mouse motion event without a button down.  Otherwise there
         * is a button and release is either true or false.
         */
        if (!((motion == Q_TRUE) && (release == Q_FALSE))) {
            DLOG(("   button press or button release or motion while button down\n"));
        } else if (!((release == Q_TRUE) || (real_motion == Q_TRUE))) {
            DLOG(("   button press only\n"));
        } else if (real_motion == Q_FALSE) {
            DLOG(("   button press or button release only\n"));
        } else {
            DLOG(("   motion with no button down: motion %s real_motion %s\n",
                    (motion == Q_TRUE ? "TRUE" : "FALSE"),
                    (real_motion == Q_TRUE ? "TRUE" : "")));
        }

#ifdef BUTTON5_RELEASED
        if ((mouse4 == Q_TRUE) || (mouse5 == Q_TRUE)) {
#else
        if (mouse4 == Q_TRUE) {
#endif
            DLOG(("   mouse wheel\n"));
        }

        if (motion == Q_TRUE) {
            assert(release == Q_FALSE);
            DLOG(("   -motion only\n"));
        }

        if (q_xterm_mouse_encoding != XTERM_MOUSE_ENCODING_SGR) {

            raw_buffer[0] = C_ESC;
            raw_buffer[1] = '[';
            raw_buffer[2] = 'M';
            if (release == Q_TRUE) {
                raw_buffer[3] = 3 + 32;
            } else if (mouse1 == Q_TRUE) {
                if (motion == Q_TRUE) {
                    raw_buffer[3] = 0 + 32 + 32;
                } else {
                    raw_buffer[3] = 0 + 32;
                }
            } else if (mouse2 == Q_TRUE) {
                if (motion == Q_TRUE) {
                    raw_buffer[3] = 1 + 32 + 32;
                } else {
                    raw_buffer[3] = 1 + 32;
                }
            } else if (mouse3 == Q_TRUE) {
                if (motion == Q_TRUE) {
                    raw_buffer[3] = 2 + 32 + 32;
                } else {
                    raw_buffer[3] = 2 + 32;
                }
            } else if (mouse4 == Q_TRUE) {
                /*
                 * Mouse wheel up.
                 */
                raw_buffer[3] = 4 + 64;
#ifdef BUTTON5_RELEASED
            } else if (mouse5 == Q_TRUE) {
                /*
                 * Mouse wheel down.
                 */
                raw_buffer[3] = 5 + 64;
#endif
            } else {
                /*
                 * This is motion with no buttons down.
                 */
                raw_buffer[3] = 3 + 32;
            }
            raw_buffer[4] = mouse.x + 33;
            raw_buffer[5] = mouse.y + 33;

            memset(utf8_buffer, 0, sizeof(utf8_buffer));
            switch (q_xterm_mouse_encoding) {
            case XTERM_MOUSE_ENCODING_X10:
                for (i = 0; i < 6; i++) {
                    utf8_buffer[i] = (unsigned char) raw_buffer[i];
                }
                break;
            case XTERM_MOUSE_ENCODING_UTF8:
                rc = 0;
                for (i = 0; i < 6; i++) {
                    rc += utf8_encode(raw_buffer[i], utf8_buffer + rc);
                }
                break;
            case XTERM_MOUSE_ENCODING_SGR:
                /*
                 * BUG: should never get here
                 */
                abort();
            }

            DLOG((" * WRITE %ld bytes: ", (long) strlen(utf8_buffer)));
            for (i = 0; i < strlen(utf8_buffer); i++) {
                DLOG2(("%02x ", utf8_buffer[i]));
            }
            DLOG2(("\n"));
            qodem_write(q_child_tty_fd, utf8_buffer, strlen(utf8_buffer),
                        Q_TRUE);
            return;

        } else {
            /*
             * SGR encoding.
             */
            memset(sgr_buffer, 0, sizeof(sgr_buffer));
            sgr_buffer[0] = 0x1B;
            sgr_buffer[1] = '[';
            sgr_buffer[2] = '<';

            if (mouse1 == Q_TRUE) {
                if (motion == Q_TRUE) {
                    snprintf(sgr_buffer + strlen(sgr_buffer),
                             sizeof(sgr_buffer) - strlen(sgr_buffer) - 1, "%s",
                             "32;");
                } else {
                    snprintf(sgr_buffer + strlen(sgr_buffer),
                             sizeof(sgr_buffer) - strlen(sgr_buffer) - 1, "%s",
                             "0;");
                }
            } else if (mouse2 == Q_TRUE) {
                if (motion == Q_TRUE) {
                    snprintf(sgr_buffer + strlen(sgr_buffer),
                             sizeof(sgr_buffer) - strlen(sgr_buffer) - 1, "%s",
                             "33;");
                } else {
                    snprintf(sgr_buffer + strlen(sgr_buffer),
                             sizeof(sgr_buffer) - strlen(sgr_buffer) - 1, "%s",
                             "1;");
                }
            } else if (mouse3 == Q_TRUE) {
                if (motion == Q_TRUE) {
                    snprintf(sgr_buffer + strlen(sgr_buffer),
                             sizeof(sgr_buffer) - strlen(sgr_buffer) - 1, "%s",
                             "34;");
                } else {
                    snprintf(sgr_buffer + strlen(sgr_buffer),
                             sizeof(sgr_buffer) - strlen(sgr_buffer) - 1, "%s",
                             "2;");
                }
            } else if (mouse4 == Q_TRUE) {
                snprintf(sgr_buffer + strlen(sgr_buffer),
                         sizeof(sgr_buffer) - strlen(sgr_buffer) - 1, "%s",
                         "64;");
#ifdef BUTTON5_RELEASED
            } else if (mouse5 == Q_TRUE) {
                snprintf(sgr_buffer + strlen(sgr_buffer),
                         sizeof(sgr_buffer) - strlen(sgr_buffer) - 1, "%s",
                         "65;");
#endif
            } else {
                /*
                 * This is motion with no buttons down.
                 */
                snprintf(sgr_buffer + strlen(sgr_buffer),
                         sizeof(sgr_buffer) - strlen(sgr_buffer) - 1, "%s",
                         "35;");
            }

            snprintf(sgr_buffer + strlen(sgr_buffer),
                     sizeof(sgr_buffer) - strlen(sgr_buffer) - 1, "%d;%d",
                     mouse.x + 1, mouse.y + 1);

            if (release == Q_TRUE) {
                sgr_buffer[strlen(sgr_buffer)] = 'm';
            } else {
                sgr_buffer[strlen(sgr_buffer)] = 'M';
            }

            DLOG((" * WRITE %ld bytes: ", (long) strlen(sgr_buffer)));
            for (i = 0; i < strlen(sgr_buffer); i++) {
                DLOG2(("%c ", sgr_buffer[i]));
            }
            DLOG2(("\n"));
            qodem_write(q_child_tty_fd, sgr_buffer, strlen(sgr_buffer), Q_TRUE);

        } /* if (q_xterm_mouse_encoding != XTERM_MOUSE_ENCODING_SGR) */

    } /* if ((rc = getmouse(&mouse)) == OK) */

    /*
     * All done
     */
    return;
}

/**
 * Obtain a keyboard input event from a window.
 *
 * @param window the curses WINDOW to query
 * @param keystroke the output keystroke, or ERR if nothing came in
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.
 * @param usleep_time the number of MICROseconds to wait for input before
 * returning ERR
 */
void qodem_win_getch(void * window, int * keystroke, int * flags,
                     const unsigned int usleep_time) {
    time_t current_time;
    wint_t utf_keystroke;
    int res;

    /*
     * Check for screensaver
     */
    if ((q_screensaver_timeout > 0) &&
        (q_program_state != Q_STATE_SCREENSAVER) &&
        (q_program_state != Q_STATE_DOWNLOAD) &&
        (q_program_state != Q_STATE_UPLOAD) &&
        (q_program_state != Q_STATE_UPLOAD_BATCH) &&
        (q_program_state != Q_STATE_SCRIPT_EXECUTE) &&
        (q_program_state != Q_STATE_HOST) &&
        (q_program_state != Q_STATE_DIALER)
    ) {

        time(&current_time);
        if (difftime(current_time, screensaver_time) > q_screensaver_timeout) {
            qlog(_("SCREENSAVER activating...\n"));
            switch_state(Q_STATE_SCREENSAVER);
            *flags = 0;
            *keystroke = ERR;
            return;
        }
    }

    if (keys_in_queue == Q_TRUE) {
        curses_match_keystring(ERR, keystroke, flags);
        assert(*keystroke != ERR);
        return;
    }

    /*
     * Assume no KEY_FLAG_ALT or KEY_FLAG_CTRL
     */
    if (flags != NULL) {
        *flags = 0;
    }

    /*
     * Switch to non-block
     */
    if (q_keyboard_blocks == Q_TRUE) {
        nodelay((WINDOW *) window, FALSE);
    } else {
        nodelay((WINDOW *) window, TRUE);
    }

#if defined(Q_PDCURSES) || defined(Q_PDCURSES_WIN32)
    PDC_save_key_modifiers(TRUE);
#endif

    /*
     * Grab keystroke
     */
    res = wget_wch((WINDOW *) window, &utf_keystroke);
    *keystroke = utf_keystroke;

    if (res != ERR) {
        DLOG(("wget_wch() res %04x utf8_keystroke: 0x%04x %d %o '%c'\n",
                res, utf_keystroke, utf_keystroke, utf_keystroke,
                utf_keystroke));
    }

    if (res == ERR) {
        *keystroke = ERR;
    }

    if (*keystroke != ERR) {
#if defined(Q_PDCURSES) || defined(Q_PDCURSES_WIN32)
        unsigned long modifiers = PDC_get_key_modifiers();
        if ((modifiers & PDC_KEY_MODIFIER_CONTROL) != 0) {

            DLOG(("PDC: CTRL\n"));

            if (flags != NULL) {
                *flags |= KEY_FLAG_CTRL;
            }
        }
        if ((modifiers & PDC_KEY_MODIFIER_SHIFT) != 0) {

            DLOG(("PDC: SHIFT\n"));

            if (flags != NULL) {
                *flags |= KEY_FLAG_SHIFT;
            }
        }
        if ((modifiers & PDC_KEY_MODIFIER_ALT) != 0) {

            DLOG(("PDC: ALT\n"));

            if (flags != NULL) {
                *flags |= KEY_FLAG_ALT;
            }
        }
        if ((modifiers & PDC_KEY_MODIFIER_NUMLOCK) != 0) {

            DLOG(("PDC: NUMLOCK\n"));

            if (*keystroke == PADENTER) {
                *keystroke = KEY_ENTER;
            }
        }
#endif /* Q_PDCURSES */

        time(&screensaver_time);
    }

    if (*keystroke == ERR) {
        if ((usleep_time > 0) && (q_keyboard_blocks == Q_FALSE)) {
#ifdef Q_PDCURSES_WIN32
            Sleep(usleep_time / 1000);
#else
            usleep(usleep_time);
#endif
        }
    } else if ((*keystroke == KEY_RESIZE) && (res == KEY_CODE_YES)) {
        handle_resize();
        *keystroke = ERR;
    } else if ((*keystroke == KEY_MOUSE) && (res == KEY_CODE_YES)) {
        handle_mouse();
        *keystroke = ERR;
    } else if ((*keystroke == KEY_SUSPEND) && (res == KEY_CODE_YES)) {
        /*
         * Special case: KEY_SUSPEND (Ctrl-Z usually)
         *
         * For now, map it to Ctrl-Z (decimal 26, hex 1A, ASCII [SUB])
         */
        *keystroke = 0x1A;

    } else if ((*keystroke == 0x7F) && (res == OK)) {
        /*
         * Special case: map DEL to KEY_BACKSPACE
         */
        *keystroke = KEY_BACKSPACE;

    } else if ((*keystroke == 0x08) && (res == OK)) {
        /*
         * Special case: map ^H to KEY_BACKSPACE, but only if CTRL is not
         * set.
         */
        if (flags != NULL) {
            if ((*flags & KEY_FLAG_CTRL) == 0) {
                *keystroke = KEY_BACKSPACE;
                *flags = 0;
            }
        } else {
            /*
             * Flags not looked for, just make this backspace.
             */
            *keystroke = KEY_BACKSPACE;
        }
#if defined(Q_PDCURSES) || defined(Q_PDCURSES_WIN32)
    } else if (res == KEY_CODE_YES) {
        /*
         * Handle PDCurses alternate keystrokes
         */
        pdcurses_key(keystroke, flags);
#else
    } else if (res == KEY_CODE_YES) {
        /*
         * Handle (n)curses alternate keystrokes
         */
        if ((*keystroke < KEY_MIN) || (*keystroke > KEY_MAX)) {
            /*
             * This is an "extended" key code.  The integer value of
             * keystroke in non-deterministic.  keyname() can be used to try
             * to figure out what it is.
             */
            ncurses_extended_keycode(keystroke, flags);
        }
#endif

    } else if (*keystroke == C_ESC) {
        /*
         * We have some complex ESC handling here due to the multiple ways
         * ESC is used: as Alt-{X}, as a sequence initializer for a function
         * key, and as bare ESC.
         */
        if (flags != NULL) {
            *flags |= KEY_FLAG_ALT;
        }
        /*
         * Grab the next keystroke
         */
        nodelay((WINDOW *) window, TRUE);
        res = wget_wch((WINDOW *) window, &utf_keystroke);

        DLOG(("wget_wch() ESC 1 res %04x utf8_keystroke: 0x%04x %d %o '%c'\n",
                res, utf_keystroke, utf_keystroke, utf_keystroke,
                utf_keystroke));

        if (q_keyboard_blocks == Q_TRUE) {
            nodelay((WINDOW *) window, FALSE);
        } else {
            nodelay((WINDOW *) window, TRUE);
        }
        *keystroke = utf_keystroke;
        if (res == ERR) {
            *keystroke = ERR;
        }
        if (*keystroke == ERR) {
            /*
             * This is actually ESCAPE, not ALT-x
             */
            if (flags != NULL) {
                *flags &= ~KEY_FLAG_ALT;
            }
            *keystroke = Q_KEY_ESCAPE;
        } else if (res == KEY_CODE_YES) {
            /*
             * This was Alt-{some function key}.  Set ALT.
             */
            curses_match_reset();
            if (flags != NULL) {
                *flags &= ~KEY_FLAG_ALT;
            }
        } else {
            assert(res == OK);

            /*
             * A more complex keyboard sequence has come in that curses
             * doesn't know about.  Use curses_match_keystring().  We have
             * the first TWO bytes in the sequence: ESCAPE and utf_keystroke.
             */
            if (flags != NULL) {
                *flags = 0;
            }
            assert(keys_in_queue == Q_FALSE);
            curses_match_keystring(C_ESC, keystroke, flags);
            curses_match_keystring(utf_keystroke, keystroke, flags);

            while ((curses_match_state == 1) && (res == OK)) {
                res = wget_wch((WINDOW *) window, &utf_keystroke);

                DLOG(("wget_wch() ESC 2 res %04x utf8_keystroke: 0x%04x %d %o '%c'\n",
                        res, utf_keystroke, utf_keystroke, utf_keystroke,
                        utf_keystroke));
                if (res != ERR) {
                    /*
                     * Normal key, keep processing in match buffer.
                     */
                    curses_match_keystring(utf_keystroke, keystroke, flags);
                } else if (res == KEY_CODE_YES) {
                    /*
                     * OK, this one is weird.  We got part of a matching
                     * sequence, but then ended with a function key.  Ditch
                     * what we had before.
                     */
                    curses_match_reset();
                }
            }
        }
    } else if ((*keystroke != ERR) && (res == OK)) {
        assert(curses_match_state != 2);
        assert(keys_in_queue == Q_FALSE);
        if (curses_match_state != 0) {
            /*
             * Still collecting a sequence.
             */
            assert(curses_match_state == 1);
            curses_match_keystring(utf_keystroke, keystroke, flags);
        }
    }

    /*
     * Restore the normal keyboard
     */
    if (q_keyboard_blocks == Q_TRUE) {
        nodelay((WINDOW *) window, FALSE);
    } else {
        nodelay((WINDOW *) window, TRUE);
    }

    /*
     * Special case: remap ASCII carriage return to KEY_ENTER.  Note do this
     * before the CTRL check.
     */
    if ((*keystroke == C_CR) && (res == OK)) {
        *keystroke = KEY_ENTER;
    }

    /*
     * Special case: remap ASCII tab to Q_KEY_TAB.  Note do this before the
     * CTRL check.
     */
    if ((*keystroke == C_TAB) && (res == OK)) {
        *keystroke = Q_KEY_TAB;
    }

    /*
     * Set CTRL for normal keystrokes
     */
    if ((*keystroke < 0x20) && (res == OK)) {
        if (flags != NULL) {
            *flags |= KEY_FLAG_CTRL;
        }
    }

    /*
     * Special case: remap KEY_FIND to KEY_HOME and KEY_SELECT to KEY_END
     */
    if (*keystroke == KEY_FIND) {
        *keystroke = KEY_HOME;
    }
    if (*keystroke == KEY_SFIND) {
        *keystroke = KEY_HOME;
        if (flags != NULL) {
            *flags = KEY_FLAG_SHIFT;
        }
    }
    if (*keystroke == KEY_SELECT) {
        *keystroke = KEY_END;
    }
    if (*keystroke == KEY_SHOME) {
        *keystroke = KEY_HOME;
        if (flags != NULL) {
            *flags = KEY_FLAG_SHIFT;
        }
    }
    if (*keystroke == KEY_SEND) {
        *keystroke = KEY_END;
        if (flags != NULL) {
            *flags = KEY_FLAG_SHIFT;
        }
    }

    if (*keystroke != -1) {
        if (flags != NULL) {
            DLOG(("Keystroke: 0x%04x %d %o '%c' FLAGS: %02x\n",
                    *keystroke, *keystroke, *keystroke, *keystroke, *flags));
        } else {
            DLOG(("Keystroke: 0x%04x %d %o '%c' FLAGS: NULL\n",
                    *keystroke, *keystroke, *keystroke, *keystroke));
        }
    }

    return;
}

/**
 * Obtain a keyboard input event from stdscr.  This is the main keyboard and
 * mouse input function, called by keyboard_handler().
 *
 * @param keystroke the output keystroke, or ERR if nothing came in
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.
 * @param usleep_time the number of MICROseconds to wait for input before
 * returning ERR
 */
void qodem_getch(int * keystroke, int * flags, const unsigned int usleep_time) {
    qodem_win_getch(stdscr, keystroke, flags, usleep_time);
}

/**
 * Read data from the keyboard/mouse and throw it away.
 */
void discarding_getch() {
    wgetch(stdscr);
}

/**
 * Tell curses whether or not we want calls to getch() to block.
 *
 * @param block if true, block on input.  If false, when no input is
 * available then return ERR.
 */
void set_blocking_input(Q_BOOL block) {
    if (block == Q_TRUE) {
        nodelay(stdscr, FALSE);
    } else {
        nodelay(stdscr, TRUE);
    }
}

/**
 * Make the cursor invisible.
 *
 * @return the previous cursor state
 */
int q_cursor_off() {
    return curs_set(0);
}

/**
 * Make the cursor visible.
 *
 * @return the previous cursor state
 */
int q_cursor_on() {
#ifdef Q_PDCURSES
    /*
     * Use the block cursor for PDCurses.
     */
    return curs_set(2);
#else
    /*
     * Use the normal cursor (whatever the user defined for their terminal)
     * for ncurses.
     */
    return curs_set(1);
#endif
}

/**
 * Make the cursor visible or invisible.
 *
 * @param cursor if 0, make the cursor invisible.  If 1, make it visible.  If
 * 2, make it "very visible".
 * @return the previous cursor state
 */
int q_cursor(const int cursor) {
    return curs_set(cursor);
}

/**
 * Determine if a keystroke is a "special key" like a function key, arrow
 * key, or number pad key, etc.
 *
 * @param keystroke key to check
 * @return 1 if this is a special key, 0 otherwise
 */
int q_key_code_yes(int keystroke) {

    /*
     * Special cases: we use our own keys for control characters that are
     * outside the normal curses function key range.
     */
    if ((keystroke == Q_KEY_ENTER) ||
        (keystroke == Q_KEY_ESCAPE) ||
        (keystroke == Q_KEY_TAB)
    ) {
        return 1;
    }
    if ((keystroke >= Q_KEY_PAD_MIN) && (keystroke <= Q_KEY_PAD_MAX)) {
        return 1;
    }
    if ((keystroke >= KEY_MIN) && (keystroke <= KEY_MAX)) {
        return 1;
    }
    return 0;
}

#if defined(__linux) && defined(Q_ENABLE_GPM)
#include <gpm.h>
/**
 * The GPM handler.
 */
int gpm_handle_mouse(Gpm_Event * event, void * data) {
    DLOG(("gpm_handle_mouse()\n"));

    /*
     * Clamp data to the screen dimensions, and draw the pointer.
     */
    Gpm_FitEvent(event);
    GPM_DRAWPOINTER(event);

    wchar_t raw_buffer[6];
    char utf8_buffer[6 * 2 + 4 + 1];
    char sgr_buffer[32];
    int rc;
    unsigned int i;
    /*
     * ncurses appears to be providing a PRESS followed by MOUSE_POSITION
     * rather than PRESS/RELEASE/MOUSE_POSITION.  This looks like a bug in
     * that it is forgetting button state.
     *
     * Hang onto the prior states and convert MOTION into RELEASE events as
     * needed.
     */
    static Q_BOOL old_mouse1 = Q_FALSE;
    static Q_BOOL old_mouse2 = Q_FALSE;
    static Q_BOOL old_mouse3 = Q_FALSE;
    Q_BOOL mouse1 = Q_FALSE;
    Q_BOOL mouse2 = Q_FALSE;
    Q_BOOL mouse3 = Q_FALSE;
    Q_BOOL mouse4 = Q_FALSE;
    Q_BOOL mouse5 = Q_FALSE;
    static int old_x = -1;
    static int old_y = -1;
    Q_BOOL press = Q_FALSE;
    Q_BOOL release = Q_FALSE;
    Q_BOOL motion = Q_FALSE;
    Q_BOOL real_motion = Q_FALSE;

    DLOG(("MOUSE mode %s %s\t",
            q_xterm_mouse_protocol == XTERM_MOUSE_OFF ? "XTERM_MOUSE_OFF" :
            q_xterm_mouse_protocol == XTERM_MOUSE_X10 ? "XTERM_MOUSE_X10" :
            q_xterm_mouse_protocol ==
            XTERM_MOUSE_NORMAL ? "XTERM_MOUSE_NORMAL" :
            q_xterm_mouse_protocol ==
            XTERM_MOUSE_BUTTONEVENT ? "XTERM_MOUSE_BUTTONEVENT" :
            q_xterm_mouse_protocol ==
            XTERM_MOUSE_ANYEVENT ? "XTERM_MOUSE_ANYEVENT" : "UNKOWN",
            q_xterm_mouse_encoding ==
            XTERM_MOUSE_ENCODING_X10 ? "XTERM_MOUSE_ENCODING_X10" :
            q_xterm_mouse_encoding ==
            XTERM_MOUSE_ENCODING_UTF8 ? "XTERM_MOUSE_ENCODING_UTF8" :
            "XTERM_MOUSE_ENCODING_SGR"));

    DLOG2(("raw: %d %d %04x\t", event->x, event->y, event->buttons));

    /*
     * Mouse event is parsed, now decide what to do with it.
     */
    if (q_program_state != Q_STATE_CONSOLE) {
        DLOG2((" DISCARD not in console\n"));

        /*
         * Discard.  We only care about the mouse when connected and in
         * the console.
         */
        return 0;
    }
    if (q_status.online == Q_FALSE) {
        DLOG2((" DISCARD not online\n"));
        return 0;
    }

    if ((old_x != event->x) || (old_y != event->y)) {
        real_motion = Q_TRUE;
    }
    old_x = event->x;
    old_y = event->y;

    if (event->type & GPM_DRAG) {
        motion = Q_TRUE;
    }

    if ((event->buttons & GPM_B_LEFT) && (event->type & GPM_DOWN) &&
        (old_mouse1 == Q_FALSE)
    ) {
        /*
         * This is a fresh press on mouse1.
         */
        mouse1 = Q_TRUE;
        old_mouse1 = Q_TRUE;
    } else if ((event->buttons & GPM_B_LEFT) && (event->type & GPM_DRAG)) {
        /*
         * This is continued dragging on mouse1.
         */
        mouse1 = Q_TRUE;
        old_mouse1 = Q_TRUE;
    } else if ((old_mouse1 == Q_TRUE) && (real_motion == Q_FALSE)) {
        /*
         * Convert this motion into a RELEASE.  Same logic for the other
         * buttons.
         */
        mouse1 = Q_TRUE;
        release = Q_TRUE;
        motion = Q_FALSE;
    }
    if ((event->buttons & GPM_B_RIGHT) && (event->type & GPM_DOWN) &&
        (old_mouse2 == Q_FALSE)
    ) {
        mouse2 = Q_TRUE;
        old_mouse2 = Q_TRUE;
    } else if ((event->buttons & GPM_B_RIGHT) && (event->type & GPM_DRAG)) {
        /*
         * This is continued dragging on mouse2.
         */
        mouse2 = Q_TRUE;
        old_mouse2 = Q_TRUE;
    } else if ((old_mouse2 == Q_TRUE) && (real_motion == Q_FALSE)) {
        mouse2 = Q_TRUE;
        release = Q_TRUE;
        motion = Q_FALSE;
    }
    if ((event->buttons & GPM_B_MIDDLE) && (event->type & GPM_DOWN) &&
        (old_mouse3 == Q_FALSE)
    ) {
        mouse3 = Q_TRUE;
        old_mouse3 = Q_TRUE;
    } else if ((event->buttons & GPM_B_MIDDLE) && (event->type & GPM_DRAG)) {
        /*
         * This is continued dragging on mouse3.
         */
        mouse3 = Q_TRUE;
        old_mouse3 = Q_TRUE;
    } else if ((old_mouse3 == Q_TRUE) && (real_motion == Q_FALSE)) {
        mouse3 = Q_TRUE;
        release = Q_TRUE;
        motion = Q_FALSE;
    }

    if (event->wdy > 0) {
        /* Mouse wheel up. */
        mouse4 = Q_TRUE;
    } else if (event->wdy < 0) {
        /* Mouse wheel down. */
        mouse5 = Q_TRUE;
    } else {
        /* No mouse wheel movement, do nothing. */
    }

    if ((event->buttons & GPM_B_LEFT) && (event->type & GPM_UP)) {
        mouse1 = Q_TRUE;
        old_mouse1 = Q_FALSE;
        release = Q_TRUE;
        motion = Q_FALSE;
    }
    if ((event->buttons & GPM_B_RIGHT) && (event->type & GPM_UP)) {
        mouse2 = Q_TRUE;
        old_mouse2 = Q_FALSE;
        release = Q_TRUE;
        motion = Q_FALSE;
    }
    if ((event->buttons & GPM_B_MIDDLE) && (event->type & GPM_UP)) {
        mouse3 = Q_TRUE;
        old_mouse3 = Q_FALSE;
        release = Q_TRUE;
        motion = Q_FALSE;
    }

    if ((release == Q_FALSE) && (motion == Q_FALSE)) {
        press = Q_TRUE;
    }

    DLOG2(("buttons: %s %s %s %s %s %s %s %s\n",
            (release == Q_TRUE ? "RELEASE" : "       "),
            (press == Q_TRUE ? "PRESS" : "     "),
            (motion == Q_TRUE ? "MOTION" : "      "),
            (mouse1 == Q_TRUE ? "1" : " "),
            (mouse2 == Q_TRUE ? "2" : " "),
            (mouse3 == Q_TRUE ? "3" : " "),
            (mouse4 == Q_TRUE ? "4" : " "),
            (mouse5 == Q_TRUE ? "5" : " ")));

    /*
     * See if we need to report this event based on the requested
     * protocol.
     */
    switch (q_xterm_mouse_protocol) {
    case XTERM_MOUSE_OFF:
        /*
         * Do nothing
         */
        return 0;

    case XTERM_MOUSE_X10:
        /*
         * Only report button presses
         */
        if ((release == Q_TRUE) || (motion == Q_TRUE)) {
            return 0;
        }
        break;

    case XTERM_MOUSE_NORMAL:
        /*
         * Only report button presses and releases
         */
        if ((press == Q_FALSE) && (release == Q_FALSE)) {
            return 0;
        }
        break;

    case XTERM_MOUSE_BUTTONEVENT:
        /*
         * Only report button presses, button releases, and motions that
         * have a button down (i.e. drag-and-drop).
         */
        if (motion == Q_TRUE) {
            if ((mouse1 == Q_FALSE) &&
                (mouse2 == Q_FALSE) &&
                (mouse3 == Q_FALSE) &&
                (mouse4 == Q_FALSE) &&
                (mouse5 == Q_FALSE)
            ) {
                return 0;
            }
        }
        break;

    case XTERM_MOUSE_ANYEVENT:
        /*
         * Report everything
         */
        break;
    }

    DLOG(("   -- DO ENCODE --\n"));

    /*
     * At this point, if motion == true and release == false, then this
     * was a mouse motion event without a button down.  Otherwise there
     * is a button and release is either true or false.
     */
    if (!((motion == Q_TRUE) && (release == Q_FALSE))) {
        DLOG(("   button press or button release or motion while button down\n"));
    } else if (!((release == Q_TRUE) || (real_motion == Q_TRUE))) {
        DLOG(("   button press only\n"));
    } else if (real_motion == Q_FALSE) {
        DLOG(("   button press or button release only\n"));
    } else {
        DLOG(("   motion with no button down: motion %s real_motion %s\n",
                (motion == Q_TRUE ? "TRUE" : "FALSE"),
                (real_motion == Q_TRUE ? "TRUE" : "")));
    }

    if ((mouse4 == Q_TRUE) || (mouse5 == Q_TRUE)) {
        DLOG(("   mouse wheel\n"));
    }

    if (motion == Q_TRUE) {
        assert(release == Q_FALSE);
        DLOG(("   -motion only\n"));
    }

    if (q_xterm_mouse_encoding != XTERM_MOUSE_ENCODING_SGR) {

        raw_buffer[0] = C_ESC;
        raw_buffer[1] = '[';
        raw_buffer[2] = 'M';
        if (release == Q_TRUE) {
            raw_buffer[3] = 3 + 32;
        } else if (mouse1 == Q_TRUE) {
            if (motion == Q_TRUE) {
                raw_buffer[3] = 0 + 32 + 32;
            } else {
                raw_buffer[3] = 0 + 32;
            }
        } else if (mouse2 == Q_TRUE) {
            if (motion == Q_TRUE) {
                raw_buffer[3] = 1 + 32 + 32;
            } else {
                raw_buffer[3] = 1 + 32;
            }
        } else if (mouse3 == Q_TRUE) {
            if (motion == Q_TRUE) {
                raw_buffer[3] = 2 + 32 + 32;
            } else {
                raw_buffer[3] = 2 + 32;
            }
        } else if (mouse4 == Q_TRUE) {
            /*
             * Mouse wheel up.
             */
            raw_buffer[3] = 4 + 64;
        } else if (mouse5 == Q_TRUE) {
            /*
             * Mouse wheel down.
             */
            raw_buffer[3] = 5 + 64;
        } else {
            /*
             * This is motion with no buttons down.
             */
            raw_buffer[3] = 3 + 32;
        }
        raw_buffer[4] = event->x + 32;
        raw_buffer[5] = event->y + 32;

        memset(utf8_buffer, 0, sizeof(utf8_buffer));
        switch (q_xterm_mouse_encoding) {
        case XTERM_MOUSE_ENCODING_X10:
            for (i = 0; i < 6; i++) {
                utf8_buffer[i] = (unsigned char) raw_buffer[i];
            }
            break;
        case XTERM_MOUSE_ENCODING_UTF8:
            rc = 0;
            for (i = 0; i < 6; i++) {
                rc += utf8_encode(raw_buffer[i], utf8_buffer + rc);
            }
            break;
        case XTERM_MOUSE_ENCODING_SGR:
            /*
             * BUG: should never get here
             */
            abort();
        }

        DLOG((" * WRITE %ld bytes: ", (long) strlen(utf8_buffer)));
        for (i = 0; i < strlen(utf8_buffer); i++) {
            DLOG2(("%02x ", utf8_buffer[i]));
        }
        DLOG2(("\n"));
        qodem_write(q_child_tty_fd, utf8_buffer, strlen(utf8_buffer),
            Q_TRUE);
        return 0;

    } else {
        /*
         * SGR encoding.
         */
        memset(sgr_buffer, 0, sizeof(sgr_buffer));
        sgr_buffer[0] = 0x1B;
        sgr_buffer[1] = '[';
        sgr_buffer[2] = '<';

        if (mouse1 == Q_TRUE) {
            if (motion == Q_TRUE) {
                snprintf(sgr_buffer + strlen(sgr_buffer),
                    sizeof(sgr_buffer) - strlen(sgr_buffer) - 1, "%s",
                    "32;");
            } else {
                snprintf(sgr_buffer + strlen(sgr_buffer),
                    sizeof(sgr_buffer) - strlen(sgr_buffer) - 1, "%s",
                    "0;");
            }
        } else if (mouse2 == Q_TRUE) {
            if (motion == Q_TRUE) {
                snprintf(sgr_buffer + strlen(sgr_buffer),
                    sizeof(sgr_buffer) - strlen(sgr_buffer) - 1, "%s",
                    "33;");
            } else {
                snprintf(sgr_buffer + strlen(sgr_buffer),
                    sizeof(sgr_buffer) - strlen(sgr_buffer) - 1, "%s",
                    "1;");
            }
        } else if (mouse3 == Q_TRUE) {
            if (motion == Q_TRUE) {
                snprintf(sgr_buffer + strlen(sgr_buffer),
                    sizeof(sgr_buffer) - strlen(sgr_buffer) - 1, "%s",
                    "34;");
            } else {
                snprintf(sgr_buffer + strlen(sgr_buffer),
                    sizeof(sgr_buffer) - strlen(sgr_buffer) - 1, "%s",
                    "2;");
            }
        } else if (mouse4 == Q_TRUE) {
            snprintf(sgr_buffer + strlen(sgr_buffer),
                sizeof(sgr_buffer) - strlen(sgr_buffer) - 1, "%s",
                "64;");
        } else if (mouse5 == Q_TRUE) {
            snprintf(sgr_buffer + strlen(sgr_buffer),
                sizeof(sgr_buffer) - strlen(sgr_buffer) - 1, "%s",
                "65;");
        } else {
            /*
             * This is motion with no buttons down.
             */
            snprintf(sgr_buffer + strlen(sgr_buffer),
                sizeof(sgr_buffer) - strlen(sgr_buffer) - 1, "%s",
                "35;");
        }

        snprintf(sgr_buffer + strlen(sgr_buffer),
            sizeof(sgr_buffer) - strlen(sgr_buffer) - 1, "%d;%d",
            event->x, event->y);

        if (release == Q_TRUE) {
            sgr_buffer[strlen(sgr_buffer)] = 'm';
        } else {
            sgr_buffer[strlen(sgr_buffer)] = 'M';
        }

        DLOG((" * WRITE %ld bytes: ", (long) strlen(sgr_buffer)));
        for (i = 0; i < strlen(sgr_buffer); i++) {
            DLOG2(("%c ", sgr_buffer[i]));
        }
        DLOG2(("\n"));
        qodem_write(q_child_tty_fd, sgr_buffer, strlen(sgr_buffer), Q_TRUE);

    } /* if (q_xterm_mouse_encoding != XTERM_MOUSE_ENCODING_SGR) */

    /*
     * All done.
     */
    return 0;
}
#endif
