/*
 * keyboard.c
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
 * We've got a LOT of keyboards here!
 *
 * When we want to send the text for a function key, the following keyboards
 * are checked in this order.  As soon as a mapping is found, the search
 * stops and the keystroke is sent on.
 *
 * 1) We look at "current_bound_keyboard" to see if a keystroke is defined
 *    there for it.  current_bound_keyboard is what you get when you specify
 *    a keybindings file in the phonebook OR you load one from the function
 *    key editor.  This keyboard is the "horse's mouth" as it were: the user
 *    explicitly said to use it, so it gets first dibs at defining a
 *    keystroke.
 *
 * 2) We look at "emulation_bound_keyboard <current emulation>".  This is the
 *    keyboard you get when you DON'T specify a keybindings file, and it maps
 *    automatically to the current emulation.  If you defined a string for
 *    F10 only in V100, you'll get that F10 every time you connect
 *    with/switch to VT100.
 *
 * 3) We look at "default_bound_keyboard".  This is the catch-all keyboard
 *    for ANY situation.  If you want F10 to be "$PASSWORD^M" most of the
 *    time, but for VT100 need a different meaning for F10, then you can
 *    define F10 in this keyboard and get it everywhere except VT100.
 *
 * 4) We look at the hardcoded keystroke for this emulation.  If the user has
 *    not edited their keybindings this is the most likely place the
 *    keystroke will come from.  Most keystrokes have reasonable mappings for
 *    all the emulations.
 *
 * 5) As a last-ditch effort, we check "terminfo_keyboards
 *    <current_emulation>" .  These keyboards are populated from the local
 *    terminfo database.  If no key is defined by the user in any of the
 *    other choices (current, emulation, default, hardcoded), this keyboard
 *    will see if terminfo "knows" what to do.  As a practical matter the
 *    only keyboard likely to use this is ANSI since everyone disagrees what
 *    TERM=ansi means.
 *
 *
 *
 * editing_keyboard is set in three different ways:
 *
 * 1) Dialing out from the phonebook will change it to current_bound_keyboard
 *    or emulation_bound_keybords <current_emulation>, depending on whether
 *    keybindings_filename exists.
 *
 * 2) Switching emulation will change it to emulation_bound_keybords
 *    <current_emulation>.
 *
 * 3) Loading a new keyboard from the function key editor will change both
 *    current_bound_keyboard AND editing_keyboard.
 *
 *
 *
 * Note that ALL keyboard filenames (current, default, emulations) are
 * relative to q_home_directory.
 *
 */
#include "common.h"
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#ifdef __BORLANDC__
#  include <mem.h>
#endif
#include <libgen.h>
#include "qodem.h"
#include "screen.h"
#include "forms.h"
#include "states.h"
#include "console.h"
#include "ansi.h"
#include "petscii.h"
#include "atascii.h"
#include "vt52.h"
#include "vt100.h"
#include "options.h"
#include "field.h"
#include "help.h"
#include "netclient.h"
#include "translate.h"
#include "keyboard.h"

#if !defined(Q_PDCURSES) && !defined(Q_PDCURSES_WIN32)
/*
 * The ncurses screen representing stdin/stdout.  Stored in screen.c.
 */
extern SCREEN * q_main_screen;
#endif

/* Buffer used to generate a keyboard macro */
static wchar_t macro_output_buffer[KEYBOARD_MACRO_SIZE];

/*
 * A keyboard.
 */
struct emulation_keyboard {
    Q_EMULATION emulation;
    char * terminfo_name;

    /*
     * Function keys
     */
    wchar_t * kf1;
    wchar_t * kf2;
    wchar_t * kf3;
    wchar_t * kf4;
    wchar_t * kf5;
    wchar_t * kf6;
    wchar_t * kf7;
    wchar_t * kf8;
    wchar_t * kf9;
    wchar_t * kf10;
    wchar_t * kf11;
    wchar_t * kf12;

    /*
     * Shifted function keys
     */
    wchar_t * kf13;
    wchar_t * kf14;
    wchar_t * kf15;
    wchar_t * kf16;
    wchar_t * kf17;
    wchar_t * kf18;
    wchar_t * kf19;
    wchar_t * kf20;
    wchar_t * kf21;
    wchar_t * kf22;
    wchar_t * kf23;
    wchar_t * kf24;

    /*
     * Control function keys
     */
    wchar_t * kf25;
    wchar_t * kf26;
    wchar_t * kf27;
    wchar_t * kf28;
    wchar_t * kf29;
    wchar_t * kf30;
    wchar_t * kf31;
    wchar_t * kf32;
    wchar_t * kf33;
    wchar_t * kf34;
    wchar_t * kf35;
    wchar_t * kf36;

    /*
     * Cursor movement keys
     */
    wchar_t * knp;              /* PgDn */
    wchar_t * kpp;              /* PgUp */
    wchar_t * kcuu1;            /* Up */
    wchar_t * kcud1;            /* Down */
    wchar_t * kcuf1;            /* Right */
    wchar_t * kcub1;            /* Left */
    wchar_t * kbs;              /* Backspace */
    wchar_t * khome;            /* Home */
    wchar_t * kend;             /* End */
    wchar_t * kich1;            /* Insert */
    wchar_t * kdch1;            /* Delete */

    /*
     * Number pad keys.  These are NOT used by terminfo_keystroke, rather
     * they can only be bound by the function key editor.
     */
    wchar_t * np_0;             /* Number pad 0 */
    wchar_t * np_1;             /* Number pad 1 */
    wchar_t * np_2;             /* Number pad 2 */
    wchar_t * np_3;             /* Number pad 3 */
    wchar_t * np_4;             /* Number pad 4 */
    wchar_t * np_5;             /* Number pad 5 */
    wchar_t * np_6;             /* Number pad 6 */
    wchar_t * np_7;             /* Number pad 7 */
    wchar_t * np_8;             /* Number pad 8 */
    wchar_t * np_9;             /* Number pad 9 */
    wchar_t * np_period;        /* Number pad . */
    wchar_t * np_divide;        /* Number pad / */
    wchar_t * np_multiply;      /* Number pad * */
    wchar_t * np_subtract;      /* Number pad - */
    wchar_t * np_add;           /* Number pad + */
    wchar_t * np_enter;         /* Number pad <ENTER> */

    /*
     * ALT Function keys.  These are NOT used by terminfo_keystroke, rather
     * they can only be bound by the function key editor.
     */
    wchar_t * alt_f1;
    wchar_t * alt_f2;
    wchar_t * alt_f3;
    wchar_t * alt_f4;
    wchar_t * alt_f5;
    wchar_t * alt_f6;
    wchar_t * alt_f7;
    wchar_t * alt_f8;
    wchar_t * alt_f9;
    wchar_t * alt_f10;
    wchar_t * alt_f11;
    wchar_t * alt_f12;

};

/*
 * The terminfo-derived keyboards, used by terminfo_keystroke()
 */
static struct emulation_keyboard terminfo_keyboards[Q_EMULATION_MAX + 1] = {
    {Q_EMUL_TTY, "tty"},
    {Q_EMUL_ANSI, "ansi"},
    {Q_EMUL_VT52, "vt52"},
    {Q_EMUL_VT100, "vt100"},
    {Q_EMUL_VT102, "vt102"},
    {Q_EMUL_VT220, "vt220"},
    {Q_EMUL_AVATAR, "avatar"},
    {Q_EMUL_PETSCII, "petscii"},
    {Q_EMUL_ATASCII, "atascii"},
    {Q_EMUL_DEBUG, "tty"},
    {Q_EMUL_LINUX, "linux"},
    {Q_EMUL_LINUX_UTF8, "linux"},
    {Q_EMUL_XTERM, "xterm"},
    {Q_EMUL_XTERM_UTF8, "xterm"},
    {-1, NULL}
};

/*
 * The keybound "emulation" keyboards, used by
 * userbound_emulation_keystroke()
 */
static struct emulation_keyboard emulation_bound_keyboards[Q_EMULATION_MAX +
                                                           1] = {
    {Q_EMUL_TTY, "tty"},
    {Q_EMUL_ANSI, "ansi"},
    {Q_EMUL_VT52, "vt52"},
    {Q_EMUL_VT100, "vt100"},
    {Q_EMUL_VT102, "vt102"},
    {Q_EMUL_VT220, "vt220"},
    {Q_EMUL_AVATAR, "avatar"},
    {Q_EMUL_PETSCII, "petscii"},
    {Q_EMUL_ATASCII, "atascii"},
    {Q_EMUL_DEBUG, "tty"},
    {Q_EMUL_LINUX, "linux"},
    {Q_EMUL_LINUX_UTF8, "linux"},
    {Q_EMUL_XTERM, "xterm"},
    {Q_EMUL_XTERM_UTF8, "xterm"},
    {-1, NULL}
};

/* The keybound "default" keyboard, used by userbound_default_keystroke() */
static struct emulation_keyboard default_bound_keyboard;

/*
 * The currently-loaded keybound keyboard, used by
 * userbound_current_keystroke()
 */
static struct emulation_keyboard current_bound_keyboard;

/* Filename for current_bound_keyboard */
static char * current_bound_keyboard_filename = NULL;

/* The keyboard being edited in the function key editor */
static struct emulation_keyboard editing_keyboard;

/* Filename for the keyboard being edited in the function key editor */
static char * editing_keyboard_filename = NULL;

/*
 * The labels used in the function key editor.
 */
struct function_key_textbox {
    Q_BOOL highlighted;
    unsigned int label_top;
    unsigned int label_left;
    char * label_text;
    unsigned int value_left;
    unsigned int value_length;
    wchar_t * value;
};

/* 48 function keys, 10 grey keys, 16 number pad keys */
#define NUMBER_OF_TEXTBOXES     48 + 10 + 16

/* The textboxes exposed in the function key editor */
static struct function_key_textbox function_key_textboxes[NUMBER_OF_TEXTBOXES];

/*
 * Whether or not we are editing a key definition in the function key
 * editor.
 */
static Q_BOOL editing_key = Q_FALSE;

/* The keyboard being edited in the function key editor */
static struct function_key_textbox * editing_textbox = NULL;

/**
 * Generate a sequence of bytes to send to the remote side that correspond to
 * a keystroke for the TTY emulation.
 *
 * @param keystroke one of the Q_KEY values, OR a Unicode code point.  See
 * input.h.
 * @return a wide string that is appropriate to send to the remote side.
 * Note that ANSI emulation is an 8-bit emulation: only the bottom 8 bits are
 * transmitted to the remote side.  See post_keystroke().
 */
static wchar_t * tty_keystroke(const int keystroke) {

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
    case Q_KEY_RIGHT:
    case Q_KEY_UP:
    case Q_KEY_DOWN:
    case Q_KEY_HOME:
    case Q_KEY_END:
    case Q_KEY_PPAGE:
    case Q_KEY_NPAGE:
    case Q_KEY_IC:
        return L"";
    case Q_KEY_DC:
        return L"\177";
    case Q_KEY_SIC:
    case Q_KEY_SDC:
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

/**
 * Generate a sequence of bytes to send to the remote side that correspond to
 * a keystroke, using what was found in the terminfo database.
 *
 * @param keystroke one of the Q_KEY values, OR a Unicode code point.  See
 * input.h.
 * @return a wide string that is appropriate to send to the remote side.
 * Note that ANSI emulation is an 8-bit emulation: only the bottom 8 bits are
 * transmitted to the remote side.  See post_keystroke().
 */
static wchar_t * terminfo_keystroke(const int keystroke) {
    int i;
    for (i = 0; ; i++) {
        if (terminfo_keyboards[i].emulation == q_status.emulation) {
            break;
        }
    }

    switch (keystroke) {

    case Q_KEY_BACKSPACE:
        return terminfo_keyboards[i].kbs;

    case Q_KEY_SLEFT:
        /*
         * Shifted left - treat like LEFT, fall through...
         */
    case Q_KEY_LEFT:
        return terminfo_keyboards[i].kcub1;

    case Q_KEY_SRIGHT:
        /*
         * Shifted right - treat like RIGHT, fall through...
         */
    case Q_KEY_RIGHT:
        return terminfo_keyboards[i].kcuf1;

    case Q_KEY_SR:
        /*
         * Shifted up - treat like UP, fall through...
         */
    case Q_KEY_UP:
        return terminfo_keyboards[i].kcuu1;

    case Q_KEY_SF:
        /*
         * Shifted down - treat like DOWN, fall through...
         */
    case Q_KEY_DOWN:
        return terminfo_keyboards[i].kcud1;

    case Q_KEY_HOME:
        return terminfo_keyboards[i].khome;

    case Q_KEY_END:
        return terminfo_keyboards[i].kend;

    case Q_KEY_F(1):
        return terminfo_keyboards[i].kf1;

    case Q_KEY_F(2):
        return terminfo_keyboards[i].kf2;

    case Q_KEY_F(3):
        return terminfo_keyboards[i].kf3;

    case Q_KEY_F(4):
        return terminfo_keyboards[i].kf4;

    case Q_KEY_F(5):
        return terminfo_keyboards[i].kf5;

    case Q_KEY_F(6):
        return terminfo_keyboards[i].kf6;

    case Q_KEY_F(7):
        return terminfo_keyboards[i].kf7;

    case Q_KEY_F(8):
        return terminfo_keyboards[i].kf8;

    case Q_KEY_F(9):
        return terminfo_keyboards[i].kf9;

    case Q_KEY_F(10):
        return terminfo_keyboards[i].kf10;

    case Q_KEY_F(11):
        return terminfo_keyboards[i].kf11;

    case Q_KEY_F(12):
        return terminfo_keyboards[i].kf12;

    case Q_KEY_F(13):
        return terminfo_keyboards[i].kf13;

    case Q_KEY_F(14):
        return terminfo_keyboards[i].kf14;

    case Q_KEY_F(15):
        return terminfo_keyboards[i].kf15;

    case Q_KEY_F(16):
        return terminfo_keyboards[i].kf16;

    case Q_KEY_F(17):
        return terminfo_keyboards[i].kf17;

    case Q_KEY_F(18):
        return terminfo_keyboards[i].kf18;

    case Q_KEY_F(19):
        return terminfo_keyboards[i].kf19;

    case Q_KEY_F(20):
        return terminfo_keyboards[i].kf20;

    case Q_KEY_F(21):
        return terminfo_keyboards[i].kf21;

    case Q_KEY_F(22):
        return terminfo_keyboards[i].kf22;

    case Q_KEY_F(23):
        return terminfo_keyboards[i].kf23;

    case Q_KEY_F(24):
        return terminfo_keyboards[i].kf24;

    case Q_KEY_F(25):
        return terminfo_keyboards[i].kf25;

    case Q_KEY_F(26):
        return terminfo_keyboards[i].kf26;

    case Q_KEY_F(27):
        return terminfo_keyboards[i].kf27;

    case Q_KEY_F(28):
        return terminfo_keyboards[i].kf28;

    case Q_KEY_F(29):
        return terminfo_keyboards[i].kf29;

    case Q_KEY_F(30):
        return terminfo_keyboards[i].kf30;

    case Q_KEY_F(31):
        return terminfo_keyboards[i].kf31;

    case Q_KEY_F(32):
        return terminfo_keyboards[i].kf32;

    case Q_KEY_F(33):
        return terminfo_keyboards[i].kf33;

    case Q_KEY_F(34):
        return terminfo_keyboards[i].kf34;

    case Q_KEY_F(35):
        return terminfo_keyboards[i].kf35;

    case Q_KEY_F(36):
        return terminfo_keyboards[i].kf36;

    case Q_KEY_PPAGE:
        return terminfo_keyboards[i].kpp;

    case Q_KEY_NPAGE:
        return terminfo_keyboards[i].knp;

    case Q_KEY_IC:
    case Q_KEY_SIC:
        return terminfo_keyboards[i].kich1;

    case Q_KEY_DC:
    case Q_KEY_SDC:
        return terminfo_keyboards[i].kdch1;

    case Q_KEY_C1:
        return L"1";

        /*
         * Number pad 2 is Q_KEY_DOWN inside curses
         */

    case Q_KEY_C3:
        return L"3";

        /*
         * Number pad 4 is Q_KEY_LEFT inside curses
         */

    case Q_KEY_B2:
        return L"5";

        /*
         * Number pad 6 is Q_KEY_RIGHT inside curses
         */

    case Q_KEY_A1:
        return L"7";

        /*
         * Number pad 8 is Q_KEY_UP inside curses
         */

    case Q_KEY_A3:
        return L"9";

    default:
        break;
    }

    return L"";
}

/**
 * Generate a sequence of bytes to send to the remote side that correspond to
 * a keystroke, using what was found in the terminfo database.
 *
 * @param keystroke one of the Q_KEY values, OR a Unicode code point.  See
 * input.h.
 * @param keyboard the keyboard to look in
 * @param find_something if true, use xterm's defaults for keystrokes that
 * would otherwise lead to "Unknown keycode".
 * @return a wide string that is appropriate to send to the remote side.
 * Note that ANSI emulation is an 8-bit emulation: only the bottom 8 bits are
 * transmitted to the remote side.  See post_keystroke().
 */
static wchar_t * bound_keyboard_keystroke(const int keystroke,
                                          const struct emulation_keyboard * keyboard,
                                          const Q_BOOL find_something) {
    assert(keyboard != NULL);

    switch (keystroke) {

    case Q_KEY_ENTER:
        return L"\015";

    case Q_KEY_BACKSPACE:
        return keyboard->kbs;

    case Q_KEY_SLEFT:
        /*
         * Shifted left - treat like LEFT, fall through...
         */
    case Q_KEY_LEFT:
        return keyboard->kcub1;

    case Q_KEY_SRIGHT:
        /*
         * Shifted right - treat like RIGHT, fall through...
         */
    case Q_KEY_RIGHT:
        return keyboard->kcuf1;

    case Q_KEY_SR:
        /*
         * Shifted up - treat like UP, fall through...
         */
    case Q_KEY_UP:
        return keyboard->kcuu1;

    case Q_KEY_SF:
        /*
         * Shifted down - treat like DOWN, fall through...
         */
    case Q_KEY_DOWN:
        return keyboard->kcud1;

    case Q_KEY_HOME:
        return keyboard->khome;

    case Q_KEY_END:
        return keyboard->kend;

    case Q_KEY_F(1):
        return keyboard->kf1;

    case Q_KEY_F(2):
        return keyboard->kf2;

    case Q_KEY_F(3):
        return keyboard->kf3;

    case Q_KEY_F(4):
        return keyboard->kf4;

    case Q_KEY_F(5):
        return keyboard->kf5;

    case Q_KEY_F(6):
        return keyboard->kf6;

    case Q_KEY_F(7):
        return keyboard->kf7;

    case Q_KEY_F(8):
        return keyboard->kf8;

    case Q_KEY_F(9):
        return keyboard->kf9;

    case Q_KEY_F(10):
        return keyboard->kf10;

    case Q_KEY_F(11):
        return keyboard->kf11;

    case Q_KEY_F(12):
        return keyboard->kf12;

    case Q_KEY_F(13):
        return keyboard->kf13;

    case Q_KEY_F(14):
        return keyboard->kf14;

    case Q_KEY_F(15):
        return keyboard->kf15;

    case Q_KEY_F(16):
        return keyboard->kf16;

    case Q_KEY_F(17):
        return keyboard->kf17;

    case Q_KEY_F(18):
        return keyboard->kf18;

    case Q_KEY_F(19):
        return keyboard->kf19;

    case Q_KEY_F(20):
        return keyboard->kf20;

    case Q_KEY_F(21):
        return keyboard->kf21;

    case Q_KEY_F(22):
        return keyboard->kf22;

    case Q_KEY_F(23):
        return keyboard->kf23;

    case Q_KEY_F(24):
        return keyboard->kf24;

    case Q_KEY_F(25):
        return keyboard->kf25;

    case Q_KEY_F(26):
        return keyboard->kf26;

    case Q_KEY_F(27):
        return keyboard->kf27;

    case Q_KEY_F(28):
        return keyboard->kf28;

    case Q_KEY_F(29):
        return keyboard->kf29;

    case Q_KEY_F(30):
        return keyboard->kf30;

    case Q_KEY_F(31):
        return keyboard->kf31;

    case Q_KEY_F(32):
        return keyboard->kf32;

    case Q_KEY_F(33):
        return keyboard->kf33;

    case Q_KEY_F(34):
        return keyboard->kf34;

    case Q_KEY_F(35):
        return keyboard->kf35;

    case Q_KEY_F(36):
        return keyboard->kf36;

    case Q_KEY_PPAGE:
        return keyboard->kpp;

    case Q_KEY_NPAGE:
        return keyboard->knp;

    case Q_KEY_IC:
    case Q_KEY_SIC:
        return keyboard->kich1;

    case Q_KEY_DC:
    case Q_KEY_SDC:
        return keyboard->kdch1;

#if defined(Q_PDCURSES) || defined(Q_PDCURSES_WIN32)

    case Q_KEY_PAD0:
        return keyboard->np_0;

    case Q_KEY_PAD1:
    case Q_KEY_C1:
        return keyboard->np_1;

    case Q_KEY_PAD2:
    case Q_KEY_C2:
        return keyboard->np_2;

    case Q_KEY_PAD3:
    case Q_KEY_C3:
        return keyboard->np_3;

    case Q_KEY_PAD4:
    case Q_KEY_B1:
        return keyboard->np_4;

    case Q_KEY_PAD5:
    case Q_KEY_B2:
        return keyboard->np_5;

    case Q_KEY_PAD6:
    case Q_KEY_B3:
        return keyboard->np_6;

    case Q_KEY_PAD7:
    case Q_KEY_A1:
        return keyboard->np_7;

    case Q_KEY_PAD8:
    case Q_KEY_A2:
        return keyboard->np_8;

    case Q_KEY_PAD9:
    case Q_KEY_A3:
        return keyboard->np_9;

    case Q_KEY_PAD_STOP:
        return keyboard->np_period;

    case Q_KEY_PAD_SLASH:
        return keyboard->np_divide;

    case Q_KEY_PAD_STAR:
        return keyboard->np_multiply;

    case Q_KEY_PAD_MINUS:
        return keyboard->np_subtract;

    case Q_KEY_PAD_PLUS:
        return keyboard->np_add;

    case Q_KEY_PAD_ENTER:
        return keyboard->np_enter;

#else

    case Q_KEY_C1:
        return L"1";

        /*
         * Number pad 2 is Q_KEY_DOWN inside curses
         */

    case Q_KEY_C3:
        return L"3";

        /*
         * Number pad 4 is Q_KEY_LEFT inside curses
         */

    case Q_KEY_B2:
        return L"5";

        /*
         * Number pad 6 is Q_KEY_RIGHT inside curses
         */

    case Q_KEY_A1:
        return L"7";

        /*
         * Number pad 8 is Q_KEY_UP inside curses
         */

    case Q_KEY_A3:
        return L"9";

#endif

    default:
        break;
    }

    /*
     * Use xterm's defaults for keystrokes that will otherwise lead to
     * "Unknown keycode".
     */
    if (find_something == Q_TRUE) {
        switch (keystroke) {
        case Q_KEY_BTAB:
            /*
             * Shift-tab
             */
            return L"\033[Z";
        default:
            break;
        }
    }

    return L"";
}

/**
 * Convert all instances of a control character in macro_output_buffer from
 * hat notation (^A, ..., ^_) to a true control character.  Due to the fact
 * ^@ is NUL and terminates a string, we do not support ^@ in a keyboard
 * macro.
 *
 * @param ch a letter between 'A' and '_'
 */
static void substitute_ctrl_char(unsigned char ch) {
    wchar_t ch_string[3];
    wchar_t ch_char[2];
    wchar_t * substituted_string;
    assert(toupper(ch) >= 'A');
    assert(toupper(ch) <= '_');

    ch_string[0] = '^';
    ch_string[1] = ch;
    ch_string[2] = 0;
    ch_char[0] = toupper(ch) - 0x40;
    ch_char[1] = 0;
    substituted_string =
        substitute_wcs(macro_output_buffer, ch_string, ch_char);
    wcsncpy(macro_output_buffer, substituted_string, KEYBOARD_MACRO_SIZE);
    Xfree(substituted_string, __FILE__, __LINE__);
}

/**
 * Convert a macro string from "$PASSWORD^M" to "mypassword\r".
 * Sets *macro_string to macro_output_buffer.
 *
 * @param macro_string set to point to the converted string
 */
static void postprocess_keyboard_macro(wchar_t ** macro_string) {
    wchar_t * substituted_string;
    unsigned char control_ch;
    int i;

    assert(macro_string != NULL);
    assert(*macro_string != NULL);

    wmemset(macro_output_buffer, 0, KEYBOARD_MACRO_SIZE);
    wmemcpy(macro_output_buffer, *macro_string, wcslen(*macro_string) + 1);

    /*
     * Process all control (hat) characters.  This works, but is awkward:
     *
     * First we turn "^^" into something unusual so that sequences like
     * "^^Hello" will be seen as "^Hello" rather than "^<Ctrl-H>ello".
     *
     * Then we perform all the substitutions for control characters, and
     * lowercase/uppercase are both allowed.
     *
     * Finally we put that "something unusual" back into "^" (the original
     * substitution for "^^").
     */
    substituted_string = substitute_wcs(macro_output_buffer, L"^^", L"@|@#@|@");
    wcsncpy(macro_output_buffer, substituted_string, KEYBOARD_MACRO_SIZE);
    Xfree(substituted_string, __FILE__, __LINE__);
    for (control_ch = 'A'; control_ch <= '_'; control_ch++) {
        substitute_ctrl_char(control_ch);
        if ((control_ch >= 'A') && (control_ch <= 'Z')) {
            substitute_ctrl_char((unsigned char) tolower(control_ch));
        }
    }
    substituted_string = substitute_wcs(macro_output_buffer, L"@|@#@|@", L"^");
    wcsncpy(macro_output_buffer, substituted_string, KEYBOARD_MACRO_SIZE);
    Xfree(substituted_string, __FILE__, __LINE__);

    /*
     * $USERNAME
     */
    if (q_status.current_username != NULL) {
        substituted_string =
            substitute_wcs(macro_output_buffer, L"$USERNAME",
                           q_status.current_username);
        wcsncpy(macro_output_buffer, substituted_string, KEYBOARD_MACRO_SIZE);
        Xfree(substituted_string, __FILE__, __LINE__);
    }

    /*
     * $PASSWORD
     */
    if (q_status.current_password != NULL) {
        substituted_string =
            substitute_wcs(macro_output_buffer, L"$PASSWORD",
                           q_status.current_password);
        wcsncpy(macro_output_buffer, substituted_string, KEYBOARD_MACRO_SIZE);
        Xfree(substituted_string, __FILE__, __LINE__);
    }

    /*
     * ...and we are not quite done!  PETSCII maps uppercase and lowercase in
     * reverse from ASCII, so perform that flip here.
     */
    if (q_status.emulation == Q_EMUL_PETSCII) {
        for (i = 0; i < wcslen(macro_output_buffer); i++) {
            if ((macro_output_buffer[i] > 0x20) &&
                (macro_output_buffer[i] < 0x7F)
            ) {
                macro_output_buffer[i] = petscii_ascii_to_petscii(
                                                macro_output_buffer[i]);
            }
        }
    }

    *macro_string = macro_output_buffer;
}

/* UTF-8 encode support */
static char utf8_buffer[6];

/**
 * Convert one wide char to UTF8 and save that in utf8_buffer.
 *
 * @param ch the character to convert
 */
static void encode_utf8_char(const wchar_t ch) {
    int rc = utf8_encode(ch, utf8_buffer);
    utf8_buffer[rc] = 0;
    return;
}

/**
 * Send a local keystroke to the remote side.
 *
 * @param keystroke the keypress
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
void post_keystroke(const int keystroke, const int flags) {

#ifdef SHOW_UNKNOWN_MESSAGE
    /*
     * This is used to display "Unknown keycode blah...."
     */
    char unknown_string[64];
#endif

    wchar_t * term_string = L"";
    unsigned int i;
    int keystroke2 = keystroke;

    /*
     * Be a NOP if not connected to anything
     */
    if ((q_status.online == Q_FALSE) && !Q_SERIAL_OPEN) {
        return;
    }

    /*
     * We need to do some PETSCII processing separately from everything else.
     */
    if (q_status.emulation == Q_EMUL_PETSCII) {
        if ((keystroke2 > 0x20) && (keystroke2 < 0x7F)) {
            keystroke2 = petscii_ascii_to_petscii(keystroke2);
        }
    }

    if (!q_key_code_yes(keystroke2) || ((flags & KEY_FLAG_UNICODE) != 0)) {
        /*
         * Normal key, pass on
         */
        if (flags & KEY_FLAG_ALT) {
            /*
             * Send the ALT ESCAPE character first
             */
            encode_utf8_char(C_ESC);
            qodem_buffered_write(utf8_buffer, strlen(utf8_buffer));

            if (q_status.emulation == Q_EMUL_DEBUG) {
                for (i = 0; i < strlen(utf8_buffer); i++) {
                    debug_local_echo(utf8_buffer[i]);
                }
                /*
                 * Force the console to refresh
                 */
                q_screen_dirty = Q_TRUE;
            }
        }

        /*
         * Special case: ^@
         */
        if ((keystroke2 == 0) && (flags & KEY_FLAG_CTRL)) {
            qodem_buffered_write("\0", 1);
            if (q_status.emulation == Q_EMUL_DEBUG) {
                debug_local_echo('\0');
                /*
                 * Force the console to refresh
                 */
                q_screen_dirty = Q_TRUE;
            }
        } else {
            if ((q_status.emulation == Q_EMUL_XTERM_UTF8) ||
                (q_status.emulation == Q_EMUL_LINUX_UTF8)) {

                /*
                 * UTF-8 emulations: encode outbound keystroke, after running
                 * it through the direct Unicode translation map.
                 */
                encode_utf8_char(translate_unicode_out((wchar_t) keystroke2));
            } else {
                /*
                 * Everyone else: try to backmap to the codepage, including
                 * allowing Unicode synonyms.
                 */
                utf8_buffer[0] = translate_unicode_to_8bit((wchar_t) keystroke2,
                    q_status.codepage);
                utf8_buffer[1] = 0;
            }
            qodem_buffered_write(utf8_buffer, strlen(utf8_buffer));
        }

        if (q_status.emulation == Q_EMUL_DEBUG) {
            for (i = 0; i < strlen(utf8_buffer); i++) {
                debug_local_echo(utf8_buffer[i]);
            }

            /*
             * Force the console to refresh
             */
            q_screen_dirty = Q_TRUE;
        } else {
            /*
             * DUPLEX
             */
            if (q_status.full_duplex == Q_FALSE) {
                /*
                 * If this is a control character, process it like it came
                 * from the remote side.
                 */
                if (keystroke2 < 0x20) {
                    generic_handle_control_char((unsigned char) keystroke2);
                } else {
                    /*
                     * Local echo for everything else
                     */
                    print_character((wchar_t) keystroke2);
                }

                /*
                 * Force the console to refresh
                 */
                q_screen_dirty = Q_TRUE;
            }
        }

        /*
         * Done
         */
        qodem_buffered_write_flush(q_child_tty_fd);
        return;

    } /* if (!q_key_code_yes(keystroke2) || ((flags & KEY_FLAG_UNICODE) != 0)) */

    /*
     * Bind keystroke only if doorway mode is OFF or MIXED
     */
    if ((q_status.doorway_mode == Q_DOORWAY_MODE_OFF) ||
        (q_status.doorway_mode == Q_DOORWAY_MODE_MIXED)
    ) {

        /*
         * term_string is empty string when a key is recognized but has no
         * binding.
         */

        /*
         * See if this key is bound in current_bound_keyboard
         */
        if ((wcslen(term_string) == 0) &&
            (current_bound_keyboard_filename != NULL)) {

            term_string =
                bound_keyboard_keystroke(keystroke, &current_bound_keyboard,
                                         Q_FALSE);
        }

        /*
         * See if this key is bound in emulation_bound_keyboard
         */
        if (wcslen(term_string) == 0) {
            /*
             * Switch to current emulation keyboard
             */
            for (i = 0; ; i++) {
                if (emulation_bound_keyboards[i].emulation == q_status.emulation) {
                    break;
                }
            }
            term_string =
                bound_keyboard_keystroke(keystroke,
                                         &emulation_bound_keyboards[i],
                                         Q_FALSE);
        }

        /*
         * See if this key is bound in default_bound_keyboard
         */
        if (wcslen(term_string) == 0) {
            term_string =
                bound_keyboard_keystroke(keystroke, &default_bound_keyboard,
                                         Q_TRUE);
        }

    }

    if (wcslen(term_string) > 0) {
        /*
         * The key was bound to something.  Convert it and then send it out.
         */
        postprocess_keyboard_macro(&term_string);
    }

    /*
     * Check the term_string length again.  If we had a keyboard macro
     * assigned such as $USERNAME^M and username wasn't set, pass the string
     * on to the underlying emulation.
     */
    if ((wcslen(term_string) == 0) ||
        ((wcslen(term_string) == 1) && (term_string[0] == C_CR))
    ) {

        /*
         * Send "special" keys through the proper emulator keyboard function
         */
        switch (q_status.emulation) {
        case Q_EMUL_TTY:
        case Q_EMUL_DEBUG:
            term_string = tty_keystroke(keystroke);
            break;
        case Q_EMUL_ANSI:
        case Q_EMUL_AVATAR:
            term_string = ansi_keystroke(keystroke);
            break;
        case Q_EMUL_PETSCII:
            term_string = petscii_keystroke(keystroke);
            break;
        case Q_EMUL_ATASCII:
            term_string = atascii_keystroke(keystroke);
            break;
        case Q_EMUL_VT52:
            term_string = vt52_keystroke(keystroke);
            break;
        case Q_EMUL_VT100:
        case Q_EMUL_VT102:
        case Q_EMUL_VT220:
            term_string = vt100_keystroke(keystroke);
            break;
        case Q_EMUL_LINUX:
        case Q_EMUL_LINUX_UTF8:
            term_string = linux_keystroke(keystroke);
            break;
        case Q_EMUL_XTERM:
        case Q_EMUL_XTERM_UTF8:
            term_string = xterm_keystroke(keystroke, flags);
            break;
        }
    }

    if (term_string == NULL) {
        /*
         * At this point, a keypress has come in from curses that Qodem does
         * not recognize:
         *
         *   - It is not a normal Unicode keystroke, it is a function key of
         *     some kind.
         *
         *   - It is not one of the function keys that can drive a keyboard
         *     macro (the bound keyboards).
         *
         *   - It is not one of the function keys that is specifically
         *     defined by an emulation (X_keypress()) or likely to be defined
         *     by a terminfo entry.
         *
         * There are a LOT of keypresses defined in ncurses and PDCurses that
         * did not exist on the old DOS keyboard.  For debugging, one can set
         * SHOW_UNKNOWN_MESSAGE to see when this happens and (perhaps) define
         * a sequence that can be sent to the remote side.  Otherwise, Qodem
         * will quietly ignore the keystroke.
         */

#ifdef SHOW_UNKNOWN_MESSAGE
        snprintf(unknown_string, sizeof(unknown_string),
                 _("[Unknown keycode 0x%04x %04o]"), keystroke, keystroke);
        for (i = 0; i < strlen(unknown_string); i++) {
            print_character(unknown_string[i]);
        }
        q_screen_dirty = Q_TRUE;
#else
        return;
#endif
    } else {
        /*
         * See if a string from the terminfo database can be used.
         */
        if (wcslen(term_string) == 0) {
            term_string = terminfo_keystroke(keystroke);
        }

        /*
         * Convert to UTF-8
         */
        if (wcslen(term_string) > 0) {
            for (i = 0; i < wcslen(term_string); i++) {
                if ((q_status.emulation == Q_EMUL_XTERM_UTF8) ||
                    (q_status.emulation == Q_EMUL_LINUX_UTF8)) {
                    /*
                     * UTF-8 emulations: encode outbound keystroke, after
                     * running it through the direct Unicode translation map.
                     */
                    encode_utf8_char(translate_unicode_out(term_string[i]));
                } else {
                    /*
                     * Everyone else: try to backmap to the codepage,
                     * including allowing Unicode synonyms.
                     */
                    utf8_buffer[0] = translate_unicode_to_8bit(term_string[i],
                        q_status.codepage);
                    utf8_buffer[1] = 0;
                }
                qodem_buffered_write(utf8_buffer, strlen(utf8_buffer));

                if (q_status.emulation == Q_EMUL_DEBUG) {
                    for (i = 0; i < strlen(utf8_buffer); i++) {
                        debug_local_echo(utf8_buffer[i]);
                    }
                }

#ifdef Q_PDCURSES_WIN32

                /*
                 * Windows special case: local shells (cmd.exe) require CRLF.
                 */
                if ((q_status.online == Q_TRUE) &&
                    ((q_status.dial_method == Q_DIAL_METHOD_SHELL) ||
                     (q_status.dial_method == Q_DIAL_METHOD_COMMANDLINE))
                ) {
                    if (keystroke == Q_KEY_ENTER) {
                        encode_utf8_char(C_LF);
                        qodem_buffered_write(utf8_buffer, strlen(utf8_buffer));
                        if (q_status.emulation == Q_EMUL_DEBUG) {
                            debug_local_echo(C_LF);
                            /*
                             * Force the console to refresh
                             */
                            q_screen_dirty = Q_TRUE;
                        }
                    }
                }

#endif

                /*
                 * VT100-ish special case: when new_line_mode is true, post a
                 * LF after a CR.
                 */
                if (((q_status.emulation == Q_EMUL_VT100) ||
                     (q_status.emulation == Q_EMUL_VT102) ||
                     (q_status.emulation == Q_EMUL_VT220) ||
                     (q_status.emulation == Q_EMUL_LINUX) ||
                     (q_status.emulation == Q_EMUL_LINUX_UTF8) ||
                     (q_status.emulation == Q_EMUL_XTERM) ||
                     (q_status.emulation == Q_EMUL_XTERM_UTF8)
                   ) && (keystroke == Q_KEY_ENTER)
                ) {
                    if (q_vt100_new_line_mode == Q_TRUE) {
                        encode_utf8_char(C_LF);
                        qodem_buffered_write(utf8_buffer, strlen(utf8_buffer));
                    }
                }
            }
        }
    }

    /*
     * Send it out to the wire.
     */
    qodem_buffered_write_flush(q_child_tty_fd);
}

/**
 * Reset a keyboard such that all keys are empty string "".
 *
 * @param keyboard the keyboard
 */
static void reset_keyboard(struct emulation_keyboard * keyboard) {
    assert(keyboard != NULL);

    keyboard->kf1 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf2 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf3 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf4 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf5 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf6 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf7 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf8 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf9 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf10 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf11 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf12 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf13 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf14 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf15 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf16 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf17 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf18 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf19 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf20 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf21 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf22 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf23 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf24 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf25 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf26 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf27 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf28 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf29 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf30 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf31 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf32 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf33 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf34 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf35 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kf36 = Xwcsdup(L"", __FILE__, __LINE__);

    keyboard->knp = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kpp = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kcuu1 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kcud1 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kcuf1 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kcub1 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kbs = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->khome = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kend = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kich1 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->kdch1 = Xwcsdup(L"", __FILE__, __LINE__);

    keyboard->np_0 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->np_1 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->np_2 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->np_3 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->np_4 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->np_5 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->np_6 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->np_7 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->np_8 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->np_9 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->np_period = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->np_divide = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->np_multiply = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->np_subtract = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->np_add = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->np_enter = Xwcsdup(L"", __FILE__, __LINE__);

    keyboard->alt_f1 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->alt_f2 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->alt_f3 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->alt_f4 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->alt_f5 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->alt_f6 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->alt_f7 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->alt_f8 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->alt_f9 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->alt_f10 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->alt_f11 = Xwcsdup(L"", __FILE__, __LINE__);
    keyboard->alt_f12 = Xwcsdup(L"", __FILE__, __LINE__);
}

/*
 * Copy key Z from keyboard Y to keyboard X.
 */
#define COPY_KEY(X, Y, Z) \
        if (X->Z != NULL) { \
                Xfree(X->Z, __FILE__, __LINE__); \
        } \
        X->Z = Xwcsdup(Y->Z, __FILE__, __LINE__)

/**
 * Perform a deep copy from src to dest.
 *
 * @param dest the keyboard to overwrite
 * @param src the source keyboard
 */
static void copy_keyboard(struct emulation_keyboard * dest,
                          const struct emulation_keyboard * src) {
    assert(dest != NULL);
    assert(src != NULL);

    dest->emulation = src->emulation;
    dest->terminfo_name = src->terminfo_name;

    COPY_KEY(dest, src, kf1);
    COPY_KEY(dest, src, kf2);
    COPY_KEY(dest, src, kf3);
    COPY_KEY(dest, src, kf4);
    COPY_KEY(dest, src, kf5);
    COPY_KEY(dest, src, kf6);
    COPY_KEY(dest, src, kf7);
    COPY_KEY(dest, src, kf8);
    COPY_KEY(dest, src, kf9);
    COPY_KEY(dest, src, kf10);
    COPY_KEY(dest, src, kf11);
    COPY_KEY(dest, src, kf12);
    COPY_KEY(dest, src, kf13);
    COPY_KEY(dest, src, kf14);
    COPY_KEY(dest, src, kf15);
    COPY_KEY(dest, src, kf16);
    COPY_KEY(dest, src, kf17);
    COPY_KEY(dest, src, kf18);
    COPY_KEY(dest, src, kf19);
    COPY_KEY(dest, src, kf20);
    COPY_KEY(dest, src, kf21);
    COPY_KEY(dest, src, kf22);
    COPY_KEY(dest, src, kf23);
    COPY_KEY(dest, src, kf24);
    COPY_KEY(dest, src, kf25);
    COPY_KEY(dest, src, kf26);
    COPY_KEY(dest, src, kf27);
    COPY_KEY(dest, src, kf28);
    COPY_KEY(dest, src, kf29);
    COPY_KEY(dest, src, kf30);
    COPY_KEY(dest, src, kf31);
    COPY_KEY(dest, src, kf32);
    COPY_KEY(dest, src, kf33);
    COPY_KEY(dest, src, kf34);
    COPY_KEY(dest, src, kf35);
    COPY_KEY(dest, src, kf36);

    COPY_KEY(dest, src, knp);
    COPY_KEY(dest, src, kpp);
    COPY_KEY(dest, src, kcuu1);
    COPY_KEY(dest, src, kcud1);
    COPY_KEY(dest, src, kcuf1);
    COPY_KEY(dest, src, kcub1);
    COPY_KEY(dest, src, kbs);
    COPY_KEY(dest, src, khome);
    COPY_KEY(dest, src, kend);
    COPY_KEY(dest, src, kich1);
    COPY_KEY(dest, src, kdch1);
    COPY_KEY(dest, src, np_0);
    COPY_KEY(dest, src, np_1);
    COPY_KEY(dest, src, np_2);
    COPY_KEY(dest, src, np_3);
    COPY_KEY(dest, src, np_4);
    COPY_KEY(dest, src, np_5);
    COPY_KEY(dest, src, np_6);
    COPY_KEY(dest, src, np_7);
    COPY_KEY(dest, src, np_8);
    COPY_KEY(dest, src, np_9);
    COPY_KEY(dest, src, np_period);
    COPY_KEY(dest, src, np_divide);
    COPY_KEY(dest, src, np_multiply);
    COPY_KEY(dest, src, np_subtract);
    COPY_KEY(dest, src, np_add);
    COPY_KEY(dest, src, np_enter);

    COPY_KEY(dest, src, alt_f1);
    COPY_KEY(dest, src, alt_f2);
    COPY_KEY(dest, src, alt_f3);
    COPY_KEY(dest, src, alt_f4);
    COPY_KEY(dest, src, alt_f5);
    COPY_KEY(dest, src, alt_f6);
    COPY_KEY(dest, src, alt_f7);
    COPY_KEY(dest, src, alt_f8);
    COPY_KEY(dest, src, alt_f9);
    COPY_KEY(dest, src, alt_f10);
    COPY_KEY(dest, src, alt_f11);
    COPY_KEY(dest, src, alt_f12);
}

/*
 * Save key C from the "keyboard"'s field B to file A.
 */
#define SAVE_KEY_TO_FILE(A, B, C) \
        fprintf(A, "%s=%ls\n", C, keyboard->B == NULL ? L"" : keyboard->B);

/*
 * Load key B from file A into a field in "keyboard".
 */
#define LOAD_KEY_FROM_FILE(A, B) \
        if (strcmp(buffer, B) == 0) { \
                if (keyboard->A != NULL) { \
                        Xfree(keyboard->A, __FILE__, __LINE__); \
                } \
                keyboard->A = Xstring_to_wcsdup(begin, __FILE__, __LINE__); \
        }

#define KEYBINDINGS_LINE_SIZE 128

/**
 * Save keybindings to a file from a struct emulation_keyboard.
 *
 * @param filename file to write to
 * @param keyboard the keyboard to read bindings from
 */
static void save_keybindings_to_file(const char * filename,
    const struct emulation_keyboard * keyboard) {

    char notify_message[DIALOG_MESSAGE_SIZE];
    char * full_filename;
    FILE * file;

    if (q_status.read_only == Q_TRUE) {
        return;
    }

    assert(filename != NULL);
    assert(keyboard != NULL);

    file = open_datadir_file(filename, &full_filename, "w");
    if (file == NULL) {
        snprintf(notify_message, sizeof(notify_message),
                 _("Error opening file \"%s\" for writing: %s"), filename,
                 strerror(errno));
        notify_form(notify_message, 0);
        /*
         * No leak
         */
        Xfree(full_filename, __FILE__, __LINE__);
        return;
    }

    fprintf(file, "# Qodem key bindings file\n");
    fprintf(file, "#\n");
    fprintf(file, "\n");

    SAVE_KEY_TO_FILE(file, kf1, "kf1");
    SAVE_KEY_TO_FILE(file, kf2, "kf2");
    SAVE_KEY_TO_FILE(file, kf3, "kf3");
    SAVE_KEY_TO_FILE(file, kf4, "kf4");
    SAVE_KEY_TO_FILE(file, kf5, "kf5");
    SAVE_KEY_TO_FILE(file, kf6, "kf6");
    SAVE_KEY_TO_FILE(file, kf7, "kf7");
    SAVE_KEY_TO_FILE(file, kf8, "kf8");
    SAVE_KEY_TO_FILE(file, kf9, "kf9");
    SAVE_KEY_TO_FILE(file, kf10, "kf10");
    SAVE_KEY_TO_FILE(file, kf11, "kf11");
    SAVE_KEY_TO_FILE(file, kf12, "kf12");
    SAVE_KEY_TO_FILE(file, kf13, "kf13");
    SAVE_KEY_TO_FILE(file, kf14, "kf14");
    SAVE_KEY_TO_FILE(file, kf15, "kf15");
    SAVE_KEY_TO_FILE(file, kf16, "kf16");
    SAVE_KEY_TO_FILE(file, kf17, "kf17");
    SAVE_KEY_TO_FILE(file, kf18, "kf18");
    SAVE_KEY_TO_FILE(file, kf19, "kf19");
    SAVE_KEY_TO_FILE(file, kf20, "kf20");
    SAVE_KEY_TO_FILE(file, kf21, "kf21");
    SAVE_KEY_TO_FILE(file, kf22, "kf22");
    SAVE_KEY_TO_FILE(file, kf23, "kf23");
    SAVE_KEY_TO_FILE(file, kf24, "kf24");
    SAVE_KEY_TO_FILE(file, kf25, "kf25");
    SAVE_KEY_TO_FILE(file, kf26, "kf26");
    SAVE_KEY_TO_FILE(file, kf27, "kf27");
    SAVE_KEY_TO_FILE(file, kf28, "kf28");
    SAVE_KEY_TO_FILE(file, kf29, "kf29");
    SAVE_KEY_TO_FILE(file, kf30, "kf30");
    SAVE_KEY_TO_FILE(file, kf31, "kf31");
    SAVE_KEY_TO_FILE(file, kf32, "kf32");
    SAVE_KEY_TO_FILE(file, kf33, "kf33");
    SAVE_KEY_TO_FILE(file, kf34, "kf34");
    SAVE_KEY_TO_FILE(file, kf35, "kf35");
    SAVE_KEY_TO_FILE(file, kf36, "kf36");
    SAVE_KEY_TO_FILE(file, knp, "knp");
    SAVE_KEY_TO_FILE(file, kpp, "kpp");
    SAVE_KEY_TO_FILE(file, kcuu1, "kcuu1");
    SAVE_KEY_TO_FILE(file, kcud1, "kcud1");
    SAVE_KEY_TO_FILE(file, kcuf1, "kcuf1");
    SAVE_KEY_TO_FILE(file, kcub1, "kcub1");
    SAVE_KEY_TO_FILE(file, kbs, "kbs");
    SAVE_KEY_TO_FILE(file, khome, "khome");
    SAVE_KEY_TO_FILE(file, kend, "kend");
    SAVE_KEY_TO_FILE(file, kich1, "kich1");
    SAVE_KEY_TO_FILE(file, kdch1, "kdch1");

    SAVE_KEY_TO_FILE(file, alt_f1, "alt_f1");
    SAVE_KEY_TO_FILE(file, alt_f2, "alt_f2");
    SAVE_KEY_TO_FILE(file, alt_f3, "alt_f3");
    SAVE_KEY_TO_FILE(file, alt_f4, "alt_f4");
    SAVE_KEY_TO_FILE(file, alt_f5, "alt_f5");
    SAVE_KEY_TO_FILE(file, alt_f6, "alt_f6");
    SAVE_KEY_TO_FILE(file, alt_f7, "alt_f7");
    SAVE_KEY_TO_FILE(file, alt_f8, "alt_f8");
    SAVE_KEY_TO_FILE(file, alt_f9, "alt_f9");
    SAVE_KEY_TO_FILE(file, alt_f10, "alt_f10");
    SAVE_KEY_TO_FILE(file, alt_f11, "alt_f11");
    SAVE_KEY_TO_FILE(file, alt_f12, "alt_f12");

    /*
     * 0-9
     */
    SAVE_KEY_TO_FILE(file, np_0, "np_0");
    SAVE_KEY_TO_FILE(file, np_1, "np_1");
    SAVE_KEY_TO_FILE(file, np_2, "np_2");
    SAVE_KEY_TO_FILE(file, np_3, "np_3");
    SAVE_KEY_TO_FILE(file, np_4, "np_4");
    SAVE_KEY_TO_FILE(file, np_5, "np_5");
    SAVE_KEY_TO_FILE(file, np_6, "np_6");
    SAVE_KEY_TO_FILE(file, np_7, "np_7");
    SAVE_KEY_TO_FILE(file, np_8, "np_8");
    SAVE_KEY_TO_FILE(file, np_9, "np_9");

    /*
     * .
     */
    SAVE_KEY_TO_FILE(file, np_period, "np_period");

    /*
     * /
     */
    SAVE_KEY_TO_FILE(file, np_divide, "np_divide");

    /*
     * */
    SAVE_KEY_TO_FILE(file, np_multiply, "np_multiply");

    /*
     * -
     */
    SAVE_KEY_TO_FILE(file, np_subtract, "np_subtract");

    /*
     * +
     */
    SAVE_KEY_TO_FILE(file, np_add, "np_add");

    /*
     * <-
     */
    SAVE_KEY_TO_FILE(file, np_enter, "np_enter");

    Xfree(full_filename, __FILE__, __LINE__);
    fclose(file);
}

/**
 * Load keybindings from a file into a struct emulation_keyboard.
 *
 * @param filename the file to read keybindings from
 * @param keyboard the keyboard to write bindings to
 */
static void load_keybindings_from_file(const char * filename,
                                       struct emulation_keyboard * keyboard) {
    char * full_filename;
    FILE * file;
    char * begin;
    char * end;
    char line[KEYBINDINGS_LINE_SIZE];
    char buffer[KEYBINDINGS_LINE_SIZE];
    char notify_message[DIALOG_MESSAGE_SIZE];

    assert(filename != NULL);
    assert(keyboard != NULL);

    file = open_datadir_file(filename, &full_filename, "r");
    if (file == NULL) {
        /*
         * If the file can't be opened, use the defaults.  Keep this in sync
         * with create_keybindings_files().
         */
        Xfree(full_filename, __FILE__, __LINE__);
        file = open_datadir_file(filename, &full_filename, "w");
        if (file != NULL) {
            fclose(file);
            save_keybindings_to_file(filename, keyboard);
        } else {
            snprintf(notify_message, sizeof(notify_message),
                _("Error creating key file \"%s\": %s"), full_filename,
                strerror(errno));
            notify_form(notify_message, 0);
        }
        /*
         * Avoid leak
         */
        Xfree(full_filename, __FILE__, __LINE__);
        return;
    }
    while (!feof(file)) {

        if (fgets(line, sizeof(line), file) == NULL) {
            /*
             * This will cause the outer while's feof() check to fail and
             * smoothly exit the while loop.
             */
            continue;
        }
        begin = line;

        while ((strlen(line) > 0) && q_isspace(line[strlen(line) - 1])) {
            /*
             * Trim trailing whitespace
             */
            line[strlen(line) - 1] = '\0';
        }
        while (q_isspace(*begin)) {
            /*
             * Trim leading whitespace
             */
            begin++;
        }

        if ((strlen(begin) == 0) || (*begin == '#')) {
            /*
             * Ignore blank lines and commented lines between entries
             */
            continue;
        }

        end = strchr(begin, '=');
        if (end == NULL) {
            /*
             * Ignore this line.
             */
            continue;
        }
        memset(buffer, 0, sizeof(buffer));
        strncpy(buffer, begin, end - begin);
        begin = end + 1;

        LOAD_KEY_FROM_FILE(kf1, "kf1");
        LOAD_KEY_FROM_FILE(kf2, "kf2");
        LOAD_KEY_FROM_FILE(kf3, "kf3");
        LOAD_KEY_FROM_FILE(kf4, "kf4");
        LOAD_KEY_FROM_FILE(kf5, "kf5");
        LOAD_KEY_FROM_FILE(kf6, "kf6");
        LOAD_KEY_FROM_FILE(kf7, "kf7");
        LOAD_KEY_FROM_FILE(kf8, "kf8");
        LOAD_KEY_FROM_FILE(kf9, "kf9");
        LOAD_KEY_FROM_FILE(kf10, "kf10");
        LOAD_KEY_FROM_FILE(kf11, "kf11");
        LOAD_KEY_FROM_FILE(kf12, "kf12");
        LOAD_KEY_FROM_FILE(kf13, "kf13");
        LOAD_KEY_FROM_FILE(kf14, "kf14");
        LOAD_KEY_FROM_FILE(kf15, "kf15");
        LOAD_KEY_FROM_FILE(kf16, "kf16");
        LOAD_KEY_FROM_FILE(kf17, "kf17");
        LOAD_KEY_FROM_FILE(kf18, "kf18");
        LOAD_KEY_FROM_FILE(kf19, "kf19");
        LOAD_KEY_FROM_FILE(kf20, "kf20");
        LOAD_KEY_FROM_FILE(kf21, "kf21");
        LOAD_KEY_FROM_FILE(kf22, "kf22");
        LOAD_KEY_FROM_FILE(kf23, "kf23");
        LOAD_KEY_FROM_FILE(kf24, "kf24");
        LOAD_KEY_FROM_FILE(kf25, "kf25");
        LOAD_KEY_FROM_FILE(kf26, "kf26");
        LOAD_KEY_FROM_FILE(kf27, "kf27");
        LOAD_KEY_FROM_FILE(kf28, "kf28");
        LOAD_KEY_FROM_FILE(kf29, "kf29");
        LOAD_KEY_FROM_FILE(kf30, "kf30");
        LOAD_KEY_FROM_FILE(kf31, "kf31");
        LOAD_KEY_FROM_FILE(kf32, "kf32");
        LOAD_KEY_FROM_FILE(kf33, "kf33");
        LOAD_KEY_FROM_FILE(kf34, "kf34");
        LOAD_KEY_FROM_FILE(kf35, "kf35");
        LOAD_KEY_FROM_FILE(kf36, "kf36");
        LOAD_KEY_FROM_FILE(knp, "knp");
        LOAD_KEY_FROM_FILE(kpp, "kpp");
        LOAD_KEY_FROM_FILE(kcuu1, "kcuu1");
        LOAD_KEY_FROM_FILE(kcud1, "kcud1");
        LOAD_KEY_FROM_FILE(kcuf1, "kcuf1");
        LOAD_KEY_FROM_FILE(kcub1, "kcub1");
        LOAD_KEY_FROM_FILE(kbs, "kbs");
        LOAD_KEY_FROM_FILE(khome, "khome");
        LOAD_KEY_FROM_FILE(kend, "kend");
        LOAD_KEY_FROM_FILE(kich1, "kich1");
        LOAD_KEY_FROM_FILE(kdch1, "kdch1");

        LOAD_KEY_FROM_FILE(alt_f1, "alt_f1");
        LOAD_KEY_FROM_FILE(alt_f2, "alt_f2");
        LOAD_KEY_FROM_FILE(alt_f3, "alt_f3");
        LOAD_KEY_FROM_FILE(alt_f4, "alt_f4");
        LOAD_KEY_FROM_FILE(alt_f5, "alt_f5");
        LOAD_KEY_FROM_FILE(alt_f6, "alt_f6");
        LOAD_KEY_FROM_FILE(alt_f7, "alt_f7");
        LOAD_KEY_FROM_FILE(alt_f8, "alt_f8");
        LOAD_KEY_FROM_FILE(alt_f9, "alt_f9");
        LOAD_KEY_FROM_FILE(alt_f10, "alt_f10");
        LOAD_KEY_FROM_FILE(alt_f11, "alt_f11");
        LOAD_KEY_FROM_FILE(alt_f12, "alt_f12");

        /*
         * 0-9
         */
        LOAD_KEY_FROM_FILE(np_0, "np_0");
        LOAD_KEY_FROM_FILE(np_1, "np_1");
        LOAD_KEY_FROM_FILE(np_2, "np_2");
        LOAD_KEY_FROM_FILE(np_3, "np_3");
        LOAD_KEY_FROM_FILE(np_4, "np_4");
        LOAD_KEY_FROM_FILE(np_5, "np_5");
        LOAD_KEY_FROM_FILE(np_6, "np_6");
        LOAD_KEY_FROM_FILE(np_7, "np_7");
        LOAD_KEY_FROM_FILE(np_8, "np_8");
        LOAD_KEY_FROM_FILE(np_9, "np_9");

        /*
         * .
         */
        LOAD_KEY_FROM_FILE(np_period, "np_period");

        /*
         * /
         */
        LOAD_KEY_FROM_FILE(np_divide, "np_divide");

        /*
         * */
        LOAD_KEY_FROM_FILE(np_multiply, "np_multiply");

        /*
         * -
         */
        LOAD_KEY_FROM_FILE(np_subtract, "np_subtract");

        /*
         * +
         */
        LOAD_KEY_FROM_FILE(np_add, "np_add");

        /*
         * <-
         */
        LOAD_KEY_FROM_FILE(np_enter, "np_enter");

    } /* while (!feof(file)) */

    Xfree(full_filename, __FILE__, __LINE__);
    fclose(file);
}

/**
 * Load all keybindings from the keybindings files.
 */
static void load_keybindings() {
    int i;
    char buffer[FILENAME_SIZE];

    /*
     * Load the emulation keyboards.
     */
    for (i = 0; emulation_bound_keyboards[i].terminfo_name != NULL; i++) {
        sprintf(buffer, "%s.key", emulation_bound_keyboards[i].terminfo_name);
        load_keybindings_from_file(buffer, &emulation_bound_keyboards[i]);
    }

    /*
     * Load the default keyboard.
     */
    sprintf(buffer, "default.key");
    load_keybindings_from_file(buffer, &default_bound_keyboard);
}

/**
 * Create empty keybindings files in the data directory ($HOME/.qodem).
 */
void create_keybindings_files() {
    int i;
    FILE * file;
    char buffer[FILENAME_SIZE];
    char * full_filename;
    char notify_message[DIALOG_MESSAGE_SIZE];

    /*
     * Create the emulation keyboards.
     */
    for (i = 0; terminfo_keyboards[i].terminfo_name != NULL; i++) {
        sprintf(buffer, "%s.key", terminfo_keyboards[i].terminfo_name);
        file = open_datadir_file(buffer, &full_filename, "a");
        if (file != NULL) {
            fclose(file);
            save_keybindings_to_file(buffer, &terminfo_keyboards[i]);
        } else {
            snprintf(notify_message, sizeof(notify_message),
                _("Error creating key file \"%s\": %s"), full_filename,
                strerror(errno));
            notify_form(notify_message, 0);
        }

        /*
         * No leak
         */
        Xfree(full_filename, __FILE__, __LINE__);
    }

    /*
     * Create the default keyboard.
     */
    sprintf(buffer, "default.key");
    file = open_datadir_file(buffer, &full_filename, "a");
    if (file != NULL) {
        fclose(file);
        save_keybindings_to_file(buffer, &default_bound_keyboard);
    } else {
        snprintf(notify_message, sizeof(notify_message),
            _("Error creating key file \"%s\": %s"), full_filename,
            strerror(errno));
        notify_form(notify_message, 0);
    }
    Xfree(full_filename, __FILE__, __LINE__);
}

/**
 * This function sets up the function key editor textboxes.
 */
static void reset_function_key_editor_textboxes() {
    int i;
    /*
     * This is used to generate the labels on the keypad screen
     */
    char buffer[16];

    for (i = 0; i < NUMBER_OF_TEXTBOXES; i++) {
        function_key_textboxes[i].highlighted = Q_FALSE;
        function_key_textboxes[i].value = Xwcsdup(L"", __FILE__, __LINE__);
    }

    for (i = 0; i < 12; i++) {
        /*
         * Normal
         */
        function_key_textboxes[i].label_top = 2 + i;
        function_key_textboxes[i].label_left = 2;
        function_key_textboxes[i].value_left = 6;
        function_key_textboxes[i].value_length = 10;
        sprintf(buffer, "F%d", i + 1);
        function_key_textboxes[i].label_text =
            Xstrdup(buffer, __FILE__, __LINE__);

        /*
         * Shifted
         */
        function_key_textboxes[i + 12].label_top = 2 + i;
        function_key_textboxes[i + 12].label_left = 18;
        function_key_textboxes[i + 12].value_left = 23;
        function_key_textboxes[i + 12].value_length = 10;
        sprintf(buffer, "SF%d", i + 1);
        function_key_textboxes[i + 12].label_text =
            Xstrdup(buffer, __FILE__, __LINE__);

        /*
         * Control
         */
        function_key_textboxes[i + 24].label_top = 2 + i;
        function_key_textboxes[i + 24].label_left = 35;
        function_key_textboxes[i + 24].value_left = 40;
        function_key_textboxes[i + 24].value_length = 10;
        sprintf(buffer, "CF%d", i + 1);
        function_key_textboxes[i + 24].label_text =
            Xstrdup(buffer, __FILE__, __LINE__);

        /*
         * Alt
         */
        function_key_textboxes[i + 36].label_top = 2 + i;
        function_key_textboxes[i + 36].label_left = 52;
        function_key_textboxes[i + 36].value_left = 57;
        function_key_textboxes[i + 36].value_length = 10;
        sprintf(buffer, "AF%d", i + 1);
        function_key_textboxes[i + 36].label_text =
            Xstrdup(buffer, __FILE__, __LINE__);
    }

    /*
     * INS
     */
    function_key_textboxes[48].label_top = 15;
    function_key_textboxes[48].label_left = 2;
    function_key_textboxes[48].value_left = 7;
    function_key_textboxes[48].value_length = 5;
    function_key_textboxes[48].label_text = "INS";

    /*
     * DEL
     */
    function_key_textboxes[49].label_top = 16;
    function_key_textboxes[49].label_left = 2;
    function_key_textboxes[49].value_left = 7;
    function_key_textboxes[49].value_length = 5;
    function_key_textboxes[49].label_text = "DEL";

    /*
     * HOME
     */
    function_key_textboxes[50].label_top = 17;
    function_key_textboxes[50].label_left = 2;
    function_key_textboxes[50].value_left = 7;
    function_key_textboxes[50].value_length = 5;
    function_key_textboxes[50].label_text = "HOME";

    /*
     * END
     */
    function_key_textboxes[51].label_top = 18;
    function_key_textboxes[51].label_left = 2;
    function_key_textboxes[51].value_left = 7;
    function_key_textboxes[51].value_length = 5;
    function_key_textboxes[51].label_text = "END";

    /*
     * PGUP
     */
    function_key_textboxes[52].label_top = 19;
    function_key_textboxes[52].label_left = 2;
    function_key_textboxes[52].value_left = 7;
    function_key_textboxes[52].value_length = 5;
    function_key_textboxes[52].label_text = "PGUP";

    /*
     * PGDN
     */
    function_key_textboxes[53].label_top = 20;
    function_key_textboxes[53].label_left = 2;
    function_key_textboxes[53].value_left = 7;
    function_key_textboxes[53].value_length = 5;
    function_key_textboxes[53].label_text = "PGDN";

    /*
     * UP
     */
    function_key_textboxes[54].label_top = 15;
    function_key_textboxes[54].label_left = 14;
    function_key_textboxes[54].value_left = 20;
    function_key_textboxes[54].value_length = 5;
    function_key_textboxes[54].label_text = "UP";

    /*
     * DOWN
     */
    function_key_textboxes[55].label_top = 16;
    function_key_textboxes[55].label_left = 14;
    function_key_textboxes[55].value_left = 20;
    function_key_textboxes[55].value_length = 5;
    function_key_textboxes[55].label_text = "DOWN";

    /*
     * LEFT
     */
    function_key_textboxes[56].label_top = 17;
    function_key_textboxes[56].label_left = 14;
    function_key_textboxes[56].value_left = 20;
    function_key_textboxes[56].value_length = 5;
    function_key_textboxes[56].label_text = "LEFT";

    /*
     * RIGHT
     */
    function_key_textboxes[57].label_top = 18;
    function_key_textboxes[57].label_left = 14;
    function_key_textboxes[57].value_left = 20;
    function_key_textboxes[57].value_length = 5;
    function_key_textboxes[57].label_text = "RIGHT";

    /*
     * 0-9
     */
    for (i = 0; i < 10; i++) {
        function_key_textboxes[i + 58].label_top = 2 + i;
        function_key_textboxes[i + 58].label_left = 69;
        function_key_textboxes[i + 58].value_left = 72;
        function_key_textboxes[i + 58].value_length = 5;
        sprintf(buffer, "%d", i);
        function_key_textboxes[i + 58].label_text =
            Xstrdup(buffer, __FILE__, __LINE__);
    }

    /*
     * .
     */
    function_key_textboxes[68].label_top = 12;
    function_key_textboxes[68].label_left = 69;
    function_key_textboxes[68].value_left = 72;
    function_key_textboxes[68].value_length = 5;
    function_key_textboxes[68].label_text = ".";

    /*
     * /
     */
    function_key_textboxes[69].label_top = 13;
    function_key_textboxes[69].label_left = 69;
    function_key_textboxes[69].value_left = 72;
    function_key_textboxes[69].value_length = 5;
    function_key_textboxes[69].label_text = "/";

    /*
     * * (star)
     */
    function_key_textboxes[70].label_top = 14;
    function_key_textboxes[70].label_left = 69;
    function_key_textboxes[70].value_left = 72;
    function_key_textboxes[70].value_length = 5;
    function_key_textboxes[70].label_text = "*";

    /*
     * -
     */
    function_key_textboxes[71].label_top = 15;
    function_key_textboxes[71].label_left = 69;
    function_key_textboxes[71].value_left = 72;
    function_key_textboxes[71].value_length = 5;
    function_key_textboxes[71].label_text = "-";

    /*
     * +
     */
    function_key_textboxes[72].label_top = 16;
    function_key_textboxes[72].label_left = 69;
    function_key_textboxes[72].value_left = 72;
    function_key_textboxes[72].value_length = 5;
    function_key_textboxes[72].label_text = "+";

    /*
     * <-
     */
    function_key_textboxes[73].label_top = 17;
    function_key_textboxes[73].label_left = 69;
    function_key_textboxes[73].value_left = 72;
    function_key_textboxes[73].value_length = 5;
    function_key_textboxes[73].label_text = "";

}

/*
 * Read a string for key Y from the terminfo database, and save it to
 * terminfo_keyboards[i].X.
 */
#define GET_TERMINFO_KEY(X, Y) \
        if (terminfo_keyboards[i].X != NULL) { \
                Xfree(terminfo_keyboards[i].X, __FILE__, __LINE__); \
        } \
        if (tigetstr(Y) != NULL) { \
                terminfo_keyboards[i].X = Xstring_to_wcsdup(tigetstr(Y), __FILE__, __LINE__); \
        } else { \
                terminfo_keyboards[i].X = Xwcsdup(L"", __FILE__, __LINE__); \
        }

/**
 * This function populates the function key table with key bindings from the
 * local terminfo database.
 */
void initialize_keyboard() {
    int i;
    SCREEN * fake_screen;
    FILE * dev_null;

#ifdef Q_PDCURSES_WIN32

    for (i = 0; terminfo_keyboards[i].terminfo_name != NULL; i++) {
        reset_keyboard(&terminfo_keyboards[i]);
    }

#else

    /*
     * For each emulation, create a SCREEN and interrogate terminfo via
     * tigetstr().
     */
    dev_null = fopen("/dev/null", "r+");
    if (dev_null == NULL) {
        fprintf(stderr, _("Error opening file \"%s\" for reading: %s"),
                "/dev/null", strerror(errno));
        return;
    }
    for (i = 0; terminfo_keyboards[i].terminfo_name != NULL; i++) {
        /*
         * Reset the keyboard.  We have to do this outside the terminfo block
         * because some terminals (TTY, DEBUG) don't have terminfo entries.
         */
        reset_keyboard(&terminfo_keyboards[i]);

        /*
         * New terminal
         */
#if defined(__CYGWIN__) || defined(Q_PDCURSES) || defined(Q_NO_NEWTERM)
        fake_screen = NULL;
#else
        fake_screen =
            newterm(terminfo_keyboards[i].terminfo_name, dev_null, dev_null);
#endif

        if (fake_screen != NULL) {
            set_term(fake_screen);

            GET_TERMINFO_KEY(kf1, "kf1");
            GET_TERMINFO_KEY(kf2, "kf2");
            GET_TERMINFO_KEY(kf3, "kf3");
            GET_TERMINFO_KEY(kf4, "kf4");
            GET_TERMINFO_KEY(kf5, "kf5");
            GET_TERMINFO_KEY(kf6, "kf6");
            GET_TERMINFO_KEY(kf7, "kf7");
            GET_TERMINFO_KEY(kf8, "kf8");
            GET_TERMINFO_KEY(kf9, "kf9");
            GET_TERMINFO_KEY(kf10, "kf10");
            GET_TERMINFO_KEY(kf11, "kf11");
            GET_TERMINFO_KEY(kf12, "kf12");
            GET_TERMINFO_KEY(kf13, "kf13");
            GET_TERMINFO_KEY(kf14, "kf14");
            GET_TERMINFO_KEY(kf15, "kf15");
            GET_TERMINFO_KEY(kf16, "kf16");
            GET_TERMINFO_KEY(kf17, "kf17");
            GET_TERMINFO_KEY(kf18, "kf18");
            GET_TERMINFO_KEY(kf19, "kf19");
            GET_TERMINFO_KEY(kf20, "kf20");
            GET_TERMINFO_KEY(kf21, "kf21");
            GET_TERMINFO_KEY(kf22, "kf22");
            GET_TERMINFO_KEY(kf23, "kf23");
            GET_TERMINFO_KEY(kf24, "kf24");
            GET_TERMINFO_KEY(kf25, "kf25");
            GET_TERMINFO_KEY(kf26, "kf26");
            GET_TERMINFO_KEY(kf27, "kf27");
            GET_TERMINFO_KEY(kf28, "kf28");
            GET_TERMINFO_KEY(kf29, "kf29");
            GET_TERMINFO_KEY(kf30, "kf30");
            GET_TERMINFO_KEY(kf31, "kf31");
            GET_TERMINFO_KEY(kf32, "kf32");
            GET_TERMINFO_KEY(kf33, "kf33");
            GET_TERMINFO_KEY(kf34, "kf34");
            GET_TERMINFO_KEY(kf35, "kf35");
            GET_TERMINFO_KEY(kf36, "kf36");
            GET_TERMINFO_KEY(knp, "knp");
            GET_TERMINFO_KEY(kpp, "kpp");
            GET_TERMINFO_KEY(kcuu1, "kcuu1");
            GET_TERMINFO_KEY(kcud1, "kcud1");
            GET_TERMINFO_KEY(kcuf1, "kcuf1");
            GET_TERMINFO_KEY(kcub1, "kcub1");
            GET_TERMINFO_KEY(kbs, "kbs");
            GET_TERMINFO_KEY(khome, "khome");
            GET_TERMINFO_KEY(kend, "kend");
            GET_TERMINFO_KEY(kich1, "kich1");
            GET_TERMINFO_KEY(kdch1, "kdch1");

            /*
             * Delete terminal
             */
            endwin();
            delscreen(fake_screen);
        }
    }
    fclose(dev_null);

#if !defined(Q_PDCURSES) && !defined(Q_PDCURSES_WIN32) && !defined(Q_NO_NEWTERM)
    set_term(q_main_screen);
#endif

#endif /* Q_PDCURSES_WIN32 */

    /*
     * Reset the emulation keyboards
     */
    for (i = 0; emulation_bound_keyboards[i].terminfo_name != NULL; i++) {
        reset_keyboard(&emulation_bound_keyboards[i]);
    }

    /*
     * Reset the catch-all default keyboard
     */
    reset_keyboard(&default_bound_keyboard);

    /*
     * Reset the custom keyboard
     */
    reset_keyboard(&current_bound_keyboard);

    /*
     * Load the existing key bindings from the files
     */
    load_keybindings();

    /*
     * Reset the editor textboxes
     */
    reset_function_key_editor_textboxes();

}

/*
 * Copy the string B to textbox key A.
 */
#define COPY_KEY_TO_TEXTBOX(A, B) \
        if (function_key_textboxes[A].value != NULL) { \
                Xfree(function_key_textboxes[A].value, __FILE__, __LINE__); \
        } \
        function_key_textboxes[A].value = Xwcsdup(B, __FILE__, __LINE__)

/**
 * Copy all of the keybindings for a keyboard to the function key editor
 * textboxes.
 *
 * @param keyboard the keybindings to read from
 */
static void copy_keyboard_to_function_key_editor_textboxes(const struct
                                                           emulation_keyboard
                                                           * keyboard) {
    assert(keyboard != NULL);

    /*
     * Function keys
     */
    COPY_KEY_TO_TEXTBOX(0, keyboard->kf1);
    COPY_KEY_TO_TEXTBOX(1, keyboard->kf2);
    COPY_KEY_TO_TEXTBOX(2, keyboard->kf3);
    COPY_KEY_TO_TEXTBOX(3, keyboard->kf4);
    COPY_KEY_TO_TEXTBOX(4, keyboard->kf5);
    COPY_KEY_TO_TEXTBOX(5, keyboard->kf6);
    COPY_KEY_TO_TEXTBOX(6, keyboard->kf7);
    COPY_KEY_TO_TEXTBOX(7, keyboard->kf8);
    COPY_KEY_TO_TEXTBOX(8, keyboard->kf9);
    COPY_KEY_TO_TEXTBOX(9, keyboard->kf10);
    COPY_KEY_TO_TEXTBOX(10, keyboard->kf11);
    COPY_KEY_TO_TEXTBOX(11, keyboard->kf12);
    COPY_KEY_TO_TEXTBOX(12, keyboard->kf13);
    COPY_KEY_TO_TEXTBOX(13, keyboard->kf14);
    COPY_KEY_TO_TEXTBOX(14, keyboard->kf15);
    COPY_KEY_TO_TEXTBOX(15, keyboard->kf16);
    COPY_KEY_TO_TEXTBOX(16, keyboard->kf17);
    COPY_KEY_TO_TEXTBOX(17, keyboard->kf18);
    COPY_KEY_TO_TEXTBOX(18, keyboard->kf19);
    COPY_KEY_TO_TEXTBOX(19, keyboard->kf20);
    COPY_KEY_TO_TEXTBOX(20, keyboard->kf21);
    COPY_KEY_TO_TEXTBOX(21, keyboard->kf22);
    COPY_KEY_TO_TEXTBOX(22, keyboard->kf23);
    COPY_KEY_TO_TEXTBOX(23, keyboard->kf24);
    COPY_KEY_TO_TEXTBOX(24, keyboard->kf25);
    COPY_KEY_TO_TEXTBOX(25, keyboard->kf26);
    COPY_KEY_TO_TEXTBOX(26, keyboard->kf27);
    COPY_KEY_TO_TEXTBOX(27, keyboard->kf28);
    COPY_KEY_TO_TEXTBOX(28, keyboard->kf29);
    COPY_KEY_TO_TEXTBOX(29, keyboard->kf30);
    COPY_KEY_TO_TEXTBOX(30, keyboard->kf31);
    COPY_KEY_TO_TEXTBOX(31, keyboard->kf32);
    COPY_KEY_TO_TEXTBOX(32, keyboard->kf33);
    COPY_KEY_TO_TEXTBOX(33, keyboard->kf34);
    COPY_KEY_TO_TEXTBOX(34, keyboard->kf35);
    COPY_KEY_TO_TEXTBOX(35, keyboard->kf36);
    COPY_KEY_TO_TEXTBOX(36, keyboard->alt_f1);
    COPY_KEY_TO_TEXTBOX(37, keyboard->alt_f2);
    COPY_KEY_TO_TEXTBOX(38, keyboard->alt_f3);
    COPY_KEY_TO_TEXTBOX(39, keyboard->alt_f4);
    COPY_KEY_TO_TEXTBOX(40, keyboard->alt_f5);
    COPY_KEY_TO_TEXTBOX(41, keyboard->alt_f6);
    COPY_KEY_TO_TEXTBOX(42, keyboard->alt_f7);
    COPY_KEY_TO_TEXTBOX(43, keyboard->alt_f8);
    COPY_KEY_TO_TEXTBOX(44, keyboard->alt_f9);
    COPY_KEY_TO_TEXTBOX(45, keyboard->alt_f10);
    COPY_KEY_TO_TEXTBOX(46, keyboard->alt_f11);
    COPY_KEY_TO_TEXTBOX(47, keyboard->alt_f12);

    /*
     * INS
     */
    COPY_KEY_TO_TEXTBOX(48, keyboard->kich1);

    /*
     * DEL
     */
    COPY_KEY_TO_TEXTBOX(49, keyboard->kdch1);

    /*
     * HOME
     */
    COPY_KEY_TO_TEXTBOX(50, keyboard->khome);

    /*
     * END
     */
    COPY_KEY_TO_TEXTBOX(51, keyboard->kend);

    /*
     * PGUP
     */
    COPY_KEY_TO_TEXTBOX(52, keyboard->kpp);

    /*
     * PGDN
     */
    COPY_KEY_TO_TEXTBOX(53, keyboard->knp);

    /*
     * UP
     */
    COPY_KEY_TO_TEXTBOX(54, keyboard->kcuu1);

    /*
     * DOWN
     */
    COPY_KEY_TO_TEXTBOX(55, keyboard->kcud1);

    /*
     * LEFT
     */
    COPY_KEY_TO_TEXTBOX(56, keyboard->kcub1);

    /*
     * RIGHT
     */
    COPY_KEY_TO_TEXTBOX(57, keyboard->kcuf1);

    /*
     * 0-9
     */
    COPY_KEY_TO_TEXTBOX(58, keyboard->np_0);
    COPY_KEY_TO_TEXTBOX(59, keyboard->np_1);
    COPY_KEY_TO_TEXTBOX(60, keyboard->np_2);
    COPY_KEY_TO_TEXTBOX(61, keyboard->np_3);
    COPY_KEY_TO_TEXTBOX(62, keyboard->np_4);
    COPY_KEY_TO_TEXTBOX(63, keyboard->np_5);
    COPY_KEY_TO_TEXTBOX(64, keyboard->np_6);
    COPY_KEY_TO_TEXTBOX(65, keyboard->np_7);
    COPY_KEY_TO_TEXTBOX(66, keyboard->np_8);
    COPY_KEY_TO_TEXTBOX(67, keyboard->np_9);

    /*
     * .
     */
    COPY_KEY_TO_TEXTBOX(68, keyboard->np_period);

    /*
     * /
     */
    COPY_KEY_TO_TEXTBOX(69, keyboard->np_divide);

    /*
     * */
    COPY_KEY_TO_TEXTBOX(70, keyboard->np_multiply);

    /*
     * -
     */
    COPY_KEY_TO_TEXTBOX(71, keyboard->np_subtract);

    /*
     * +
     */
    COPY_KEY_TO_TEXTBOX(72, keyboard->np_add);

    /*
     * <-
     */
    COPY_KEY_TO_TEXTBOX(73, keyboard->np_enter);

}

/*
 * Copy a keybinding string from textbox A to keyboard field B.
 */
#define COPY_TEXTBOX_TO_KEY(A, B) \
        if (B != NULL) { \
                Xfree(B, __FILE__, __LINE__); \
        } \
        B = Xwcsdup(function_key_textboxes[A].value, __FILE__, __LINE__)

/**
 * Copy all of the keybindings for a keyboard from the function key editor
 * textboxes to a keyboard.
 *
 * @param keyboard the keybindings to write to
 */
static void copy_function_key_editor_textboxes_to_keyboard(struct
                                                           emulation_keyboard
                                                           * keyboard) {
    assert(keyboard != NULL);

    /*
     * Function keys
     */
    COPY_TEXTBOX_TO_KEY(0, keyboard->kf1);
    COPY_TEXTBOX_TO_KEY(1, keyboard->kf2);
    COPY_TEXTBOX_TO_KEY(2, keyboard->kf3);
    COPY_TEXTBOX_TO_KEY(3, keyboard->kf4);
    COPY_TEXTBOX_TO_KEY(4, keyboard->kf5);
    COPY_TEXTBOX_TO_KEY(5, keyboard->kf6);
    COPY_TEXTBOX_TO_KEY(6, keyboard->kf7);
    COPY_TEXTBOX_TO_KEY(7, keyboard->kf8);
    COPY_TEXTBOX_TO_KEY(8, keyboard->kf9);
    COPY_TEXTBOX_TO_KEY(9, keyboard->kf10);
    COPY_TEXTBOX_TO_KEY(10, keyboard->kf11);
    COPY_TEXTBOX_TO_KEY(11, keyboard->kf12);
    COPY_TEXTBOX_TO_KEY(12, keyboard->kf13);
    COPY_TEXTBOX_TO_KEY(13, keyboard->kf14);
    COPY_TEXTBOX_TO_KEY(14, keyboard->kf15);
    COPY_TEXTBOX_TO_KEY(15, keyboard->kf16);
    COPY_TEXTBOX_TO_KEY(16, keyboard->kf17);
    COPY_TEXTBOX_TO_KEY(17, keyboard->kf18);
    COPY_TEXTBOX_TO_KEY(18, keyboard->kf19);
    COPY_TEXTBOX_TO_KEY(19, keyboard->kf20);
    COPY_TEXTBOX_TO_KEY(20, keyboard->kf21);
    COPY_TEXTBOX_TO_KEY(21, keyboard->kf22);
    COPY_TEXTBOX_TO_KEY(22, keyboard->kf23);
    COPY_TEXTBOX_TO_KEY(23, keyboard->kf24);
    COPY_TEXTBOX_TO_KEY(24, keyboard->kf25);
    COPY_TEXTBOX_TO_KEY(25, keyboard->kf26);
    COPY_TEXTBOX_TO_KEY(26, keyboard->kf27);
    COPY_TEXTBOX_TO_KEY(27, keyboard->kf28);
    COPY_TEXTBOX_TO_KEY(28, keyboard->kf29);
    COPY_TEXTBOX_TO_KEY(29, keyboard->kf30);
    COPY_TEXTBOX_TO_KEY(30, keyboard->kf31);
    COPY_TEXTBOX_TO_KEY(31, keyboard->kf32);
    COPY_TEXTBOX_TO_KEY(32, keyboard->kf33);
    COPY_TEXTBOX_TO_KEY(33, keyboard->kf34);
    COPY_TEXTBOX_TO_KEY(34, keyboard->kf35);
    COPY_TEXTBOX_TO_KEY(35, keyboard->kf36);
    COPY_TEXTBOX_TO_KEY(36, keyboard->alt_f1);
    COPY_TEXTBOX_TO_KEY(37, keyboard->alt_f2);
    COPY_TEXTBOX_TO_KEY(38, keyboard->alt_f3);
    COPY_TEXTBOX_TO_KEY(39, keyboard->alt_f4);
    COPY_TEXTBOX_TO_KEY(40, keyboard->alt_f5);
    COPY_TEXTBOX_TO_KEY(41, keyboard->alt_f6);
    COPY_TEXTBOX_TO_KEY(42, keyboard->alt_f7);
    COPY_TEXTBOX_TO_KEY(43, keyboard->alt_f8);
    COPY_TEXTBOX_TO_KEY(44, keyboard->alt_f9);
    COPY_TEXTBOX_TO_KEY(45, keyboard->alt_f10);
    COPY_TEXTBOX_TO_KEY(46, keyboard->alt_f11);
    COPY_TEXTBOX_TO_KEY(47, keyboard->alt_f12);

    /*
     * INS
     */
    COPY_TEXTBOX_TO_KEY(48, keyboard->kich1);

    /*
     * DEL
     */
    COPY_TEXTBOX_TO_KEY(49, keyboard->kdch1);

    /*
     * HOME
     */
    COPY_TEXTBOX_TO_KEY(50, keyboard->khome);

    /*
     * END
     */
    COPY_TEXTBOX_TO_KEY(51, keyboard->kend);

    /*
     * PGUP
     */
    COPY_TEXTBOX_TO_KEY(52, keyboard->kpp);

    /*
     * PGDN
     */
    COPY_TEXTBOX_TO_KEY(53, keyboard->knp);

    /*
     * UP
     */
    COPY_TEXTBOX_TO_KEY(54, keyboard->kcuu1);

    /*
     * DOWN
     */
    COPY_TEXTBOX_TO_KEY(55, keyboard->kcud1);

    /*
     * LEFT
     */
    COPY_TEXTBOX_TO_KEY(56, keyboard->kcub1);

    /*
     * RIGHT
     */
    COPY_TEXTBOX_TO_KEY(57, keyboard->kcuf1);

    /*
     * 0-9
     */
    COPY_TEXTBOX_TO_KEY(58, keyboard->np_0);
    COPY_TEXTBOX_TO_KEY(59, keyboard->np_1);
    COPY_TEXTBOX_TO_KEY(60, keyboard->np_2);
    COPY_TEXTBOX_TO_KEY(61, keyboard->np_3);
    COPY_TEXTBOX_TO_KEY(62, keyboard->np_4);
    COPY_TEXTBOX_TO_KEY(63, keyboard->np_5);
    COPY_TEXTBOX_TO_KEY(64, keyboard->np_6);
    COPY_TEXTBOX_TO_KEY(65, keyboard->np_7);
    COPY_TEXTBOX_TO_KEY(66, keyboard->np_8);
    COPY_TEXTBOX_TO_KEY(67, keyboard->np_9);

    /*
     * .
     */
    COPY_TEXTBOX_TO_KEY(68, keyboard->np_period);

    /*
     * /
     */
    COPY_TEXTBOX_TO_KEY(69, keyboard->np_divide);

    /*
     * */
    COPY_TEXTBOX_TO_KEY(70, keyboard->np_multiply);

    /*
     * -
     */
    COPY_TEXTBOX_TO_KEY(71, keyboard->np_subtract);

    /*
     * +
     */
    COPY_TEXTBOX_TO_KEY(72, keyboard->np_add);

    /*
     * <-
     */
    COPY_TEXTBOX_TO_KEY(73, keyboard->np_enter);

}

/* A form + fields to handle the editing of a given key binding value */
void * edit_keybinding_window;
struct fieldset * edit_keybinding_form;
struct field * edit_keybinding_field;

/*
 * This is analogous to q_screen_dirty, but just for the top half of the
 * screen with the editor boxes.
 */
static Q_BOOL redraw_boxes = Q_FALSE;

/**
 * Keyboard handler for the Alt-J function key editor screen.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
void function_key_editor_keyboard_handler(const int keystroke,
                                          const int flags) {

    int menu_left = (WIDTH - 80) / 2;
    int menu_top = (HEIGHT - 24) / 2;
    struct file_info * new_file;
    struct function_key_textbox * new_selected_key = NULL;
    int keystroke2;

    /*
     * keystroke2 is what we test in the second switch statement.  I want to
     * keep the 'const int keystroke' in the API, but it's more convenient
     * inside this function to reassign keystroke.
     */
    keystroke2 = keystroke;

    switch (keystroke2) {
    case '?':
        if (editing_key == Q_FALSE) {
            /*
             * Enter help system
             */
            launch_help(Q_HELP_FUNCTION_KEYS);

            /*
             * Explicitly freshen the background console image
             */
            q_screen_dirty = Q_TRUE;
            console_refresh(Q_FALSE);
            /*
             * Return here
             */
            q_screen_dirty = Q_TRUE;
            return;
        } else {
            /*
             * If editing a key, pass the keystroke to the form handler
             */
            if (!q_key_code_yes(keystroke2)) {
                /*
                 * Pass normal keys to form driver
                 */
                fieldset_keystroke(edit_keybinding_form, keystroke2);
            }

            /*
             * Return here.  The logic below the switch is all about
             * switching the editing key.
             */
            return;
        }

    case 'L':
    case 'l':
        if (editing_key == Q_FALSE) {
            /*
             * Load a new keyboard from file
             */
            new_file = view_directory(q_home_directory, "*.key");
            /*
             * Explicitly freshen the background console image
             */
            q_screen_dirty = Q_TRUE;
            console_refresh(Q_FALSE);
            if (new_file != NULL) {
                /*
                 * We call basename() which is normally a bad thing to do.
                 * But we're only one line away from tossing new_filename
                 * anyway.
                 */
                switch_current_keyboard(basename(new_file->name));
                Xfree(new_file->name, __FILE__, __LINE__);
                Xfree(new_file, __FILE__, __LINE__);
            } else {
                /*
                 * Nothing to do.
                 */
            }
            /*
             * Return here
             */
            q_screen_dirty = Q_TRUE;
            return;
        } else {
            /*
             * If editing a key, pass the keystroke to the form handler
             */
            if (!q_key_code_yes(keystroke2)) {
                /*
                 * Pass normal keys to form driver
                 */
                fieldset_keystroke(edit_keybinding_form, keystroke2);
            }

            /*
             * Return here.  The logic below the switch is all about
             * switching the editing key.
             */
            return;
        }
    case 'S':
    case 's':
        /*
         * If editing a key, pass the keystroke to the form handler
         */
        if (editing_key == Q_TRUE) {
            if (!q_key_code_yes(keystroke2)) {
                /*
                 * Pass normal keys to form driver
                 */
                fieldset_keystroke(edit_keybinding_form, keystroke2);
            }

            /*
             * Return here.  The logic below the switch is all about
             * switching the editing key.
             */
            return;
        }

        /*
         * Copy from the editor to the keyboard
         */
        copy_function_key_editor_textboxes_to_keyboard(&editing_keyboard);

        /*
         * Save and exit
         */
        save_keybindings_to_file(editing_keyboard_filename, &editing_keyboard);

        /*
         * Switch to reload the bindings from file
         */
        switch_current_keyboard(editing_keyboard_filename);

        /*
         * Fall through for the exit part
         */
        keystroke2 = '`';
        break;

    case '\\':
        /*
         * Alt-\ Alt Code key
         */
        if (editing_key == Q_TRUE) {
            if (flags & KEY_FLAG_ALT) {

                if ((q_status.emulation == Q_EMUL_XTERM_UTF8) ||
                    (q_status.emulation == Q_EMUL_LINUX_UTF8)) {
                    keystroke2 = alt_code_key(Q_TRUE);
                } else {
                    /*
                     * Get an 8-bit codepage value.
                     */
                    keystroke2 = alt_code_key(Q_FALSE);
                    if (keystroke2 > 0) {
                        if (q_status.codepage == Q_CODEPAGE_DEC) {
                            /*
                             * Always use CP437 glyphs for the VT100 terminals.
                             */
                            keystroke2 = cp437_chars[keystroke2 & 0xFF];
                        } else {
                            keystroke2 = codepage_map_char(keystroke2);
                        }
                    }
                }

                /*
                 * alt_code_key() sets q_screen_dirty to true, which is the
                 * right thing to do everywhere EXCEPT here.
                 */
                q_screen_dirty = Q_FALSE;
                redraw_boxes = Q_TRUE;
                if (keystroke2 > 0) {
                    if (keystroke2 < 0x20) {
                        /*
                         * This is a control character, insert it in
                         * hat-notation.
                         */
                        fieldset_keystroke(edit_keybinding_form, '^');
                        fieldset_keystroke(edit_keybinding_form,
                                           keystroke2 + 0x40);
                    } else if (keystroke2 == '^') {
                        /*
                         * This is a hat character, insert it in
                         * hat-notation.
                         */
                        fieldset_keystroke(edit_keybinding_form, '^');
                        fieldset_keystroke(edit_keybinding_form, '^');
                    } else {
                        /*
                         * Pass normal keys to form driver
                         */
                        if (q_key_code_yes(keystroke2) == 0) {
                            fieldset_keystroke(edit_keybinding_form,
                                               keystroke2);
                        }
                    }
                }
            } else {
                /*
                 * Pass normal keys to form driver
                 */
                if (q_key_code_yes(keystroke2) == 0) {
                    fieldset_keystroke(edit_keybinding_form, keystroke2);
                }
            }
        }

        /*
         * Return here
         */
        return;

    default:
        break;

    }

    switch (keystroke2) {

    case '`':
        /*
         * Backtick works to exit a field, but can also be used in a macro
         */
        if (editing_key == Q_TRUE) {
            /*
             * Pass to form driver
             */
            fieldset_keystroke(edit_keybinding_form, keystroke2);

            /*
             * Return here.  The logic below the switch is all about
             * switching the editing key.
             */
            return;
        }
        /*
         * Fall through ...
         */
    case Q_KEY_ESCAPE:
        /*
         * ESC return to TERMINAL mode
         */
        if (editing_key == Q_TRUE) {
            editing_key = Q_FALSE;
            editing_textbox->highlighted = Q_FALSE;
            q_cursor_off();

            /*
             * Delete the editing form
             */
            fieldset_free(edit_keybinding_form);
            screen_delwin(edit_keybinding_window);
        } else {
            /*
             * Switch to reload the bindings from file
             */
            switch_current_keyboard(editing_keyboard_filename);

            /*
             * Editing form is already deleted, so just escape out
             */
            switch_state(original_state);
        }

        /*
         * Refresh
         */
        q_screen_dirty = Q_TRUE;
        return;

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
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            if (flags & KEY_FLAG_ALT) {
                new_selected_key =
                    &function_key_textboxes[36 + (keystroke2 - Q_KEY_F(1))];
            } else {
                new_selected_key =
                    &function_key_textboxes[(keystroke2 - Q_KEY_F(1))];
            }
        } else {
            return;
        }

        break;

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
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key =
                &function_key_textboxes[(keystroke2 - Q_KEY_F(1))];
        } else {
            return;
        }
        break;

    case Q_KEY_IC:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[48];
        } else {
            fieldset_insert_char(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_DC:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[49];
        } else {
            fieldset_delete_char(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_HOME:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[50];
        } else {
            fieldset_home_char(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_END:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[51];
        } else {
            fieldset_end_char(edit_keybinding_form);
            return;
        }

        break;

    case Q_KEY_PPAGE:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[52];
        } else {
            return;
        }
        break;

    case Q_KEY_NPAGE:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[53];
        } else {
            return;
        }
        break;

    case Q_KEY_UP:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[54];
        } else {
            return;
        }
        break;

    case Q_KEY_DOWN:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[55];
        } else {
            return;
        }
        break;

    case Q_KEY_LEFT:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[56];
        } else {
            fieldset_left(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_RIGHT:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[57];
        } else {
            fieldset_right(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_PAD0:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[58];
        } else {
            fieldset_right(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_PAD1:
    case Q_KEY_C1:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[59];
        } else {
            fieldset_right(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_PAD2:
    case Q_KEY_C2:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[60];
        } else {
            fieldset_right(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_PAD3:
    case Q_KEY_C3:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[61];
        } else {
            fieldset_right(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_PAD4:
    case Q_KEY_B1:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[62];
        } else {
            fieldset_right(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_PAD5:
    case Q_KEY_B2:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[63];
        } else {
            fieldset_right(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_PAD6:
    case Q_KEY_B3:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[64];
        } else {
            fieldset_right(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_PAD7:
    case Q_KEY_A1:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[65];
        } else {
            fieldset_right(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_PAD8:
    case Q_KEY_A2:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[66];
        } else {
            fieldset_right(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_PAD9:
    case Q_KEY_A3:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[67];
        } else {
            fieldset_right(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_PAD_STOP:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[68];
        } else {
            fieldset_right(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_PAD_SLASH:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[69];
        } else {
            fieldset_right(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_PAD_STAR:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[70];
        } else {
            fieldset_right(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_PAD_MINUS:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[71];
        } else {
            fieldset_right(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_PAD_PLUS:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[72];
        } else {
            fieldset_right(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_PAD_ENTER:
        if (editing_key == Q_FALSE) {
            editing_key = Q_TRUE;
            new_selected_key = &function_key_textboxes[73];
        } else {
            fieldset_right(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_BACKSPACE:
        if (editing_key == Q_TRUE) {
            fieldset_backspace(edit_keybinding_form);
            return;
        }
        break;

    case Q_KEY_ENTER:
        if (editing_key == Q_TRUE) {
            /*
             * The OK exit point
             */
            Xfree(editing_textbox->value, __FILE__, __LINE__);
            editing_textbox->value = field_get_value(edit_keybinding_field);
            fieldset_free(edit_keybinding_form);
            screen_delwin(edit_keybinding_window);
            editing_key = Q_FALSE;
            editing_textbox->highlighted = Q_FALSE;
            q_cursor_off();
        }
        /*
         * Refresh
         */
        q_screen_dirty = Q_TRUE;
        return;

    default:
        /*
         * Pass to form handler
         */
        if (editing_key == Q_TRUE) {
            if (keystroke2 < 0x20) {
                /*
                 * This is a control character, insert it in hat-notation.
                 */
                fieldset_keystroke(edit_keybinding_form, '^');
                fieldset_keystroke(edit_keybinding_form, keystroke2 + 0x40);
            } else if (!q_key_code_yes(keystroke2)) {
                /*
                 * Pass normal keys to form driver
                 */
                fieldset_keystroke(edit_keybinding_form, keystroke2);
            }

            /*
             * Return here.  The logic below the switch is all about
             * switching the editing key.
             */
            return;
        }
        break;
    }

    /*
     * Flip the highlighted flag
     */
    editing_textbox = new_selected_key;
    if (editing_textbox != NULL) {
        editing_textbox->highlighted = Q_TRUE;

        /*
         * Force the screen to re-draw so the key will blink
         */
        q_screen_dirty = Q_TRUE;
        function_key_editor_refresh();

        edit_keybinding_window =
            screen_subwin(1, 70, menu_top + 22, menu_left + 8);
        if (check_subwin_result(edit_keybinding_window) == Q_FALSE) {
            editing_key = Q_FALSE;
            editing_textbox->highlighted = Q_FALSE;
            q_cursor_off();
            q_screen_dirty = Q_TRUE;
            return;
        }

        edit_keybinding_field = field_malloc(70, 0, 0, Q_FALSE,
                                             Q_COLOR_PHONEBOOK_FIELD_TEXT,
                                             Q_COLOR_PHONEBOOK_FIELD_TEXT);
        edit_keybinding_form =
            fieldset_malloc(&edit_keybinding_field, 1, edit_keybinding_window);

        screen_put_color_str_yx(menu_top + 22, menu_left + 2, _("Edit:"),
                                Q_COLOR_MENU_COMMAND);

        field_set_value(edit_keybinding_field, editing_textbox->value);
        q_cursor_on();
        screen_flush();
        fieldset_render(edit_keybinding_form);
    }

}

/**
 * Draw screen for the Alt-J function key editor screen.
 */
void function_key_editor_refresh() {
    char * status_string;
    int status_left_stop;
    int window_left;
    int window_top;
    unsigned int i, j;
    char filename[FILENAME_SIZE];

    window_left = (WIDTH - 80) / 2;
    window_top = (HEIGHT - 24) / 2;

    if (editing_keyboard_filename == NULL) {
        switch_current_keyboard("");
    }

    if (redraw_boxes == Q_FALSE) {
        if (q_screen_dirty == Q_FALSE) {
            return;
        }
    }

    /*
     * Clear screen for when it resizes
     */
    console_refresh(Q_FALSE);

    if (redraw_boxes == Q_TRUE) {
        /*
         * Box out the background
         */
        for (i = 1; i < 21; i++) {
            screen_put_color_hline_yx(window_top + i, window_left + 1, ' ', 78,
                                      Q_COLOR_WINDOW);
        }
    } else {
        /*
         * The menu window border
         */
        screen_draw_box(window_left, window_top, window_left + 80,
                        window_top + 24);

        /*
         * Place the title
         */
        screen_put_color_str_yx(window_top, window_left + 27,
                                _(" Function Key Assignment "),
                                Q_COLOR_WINDOW_BORDER);

        /*
         * Put up the status line
         */
        screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH,
                                  Q_COLOR_STATUS);

        if (editing_key == Q_FALSE) {
            status_string =
                _(" FILE:XXXXXXXX.XXX  KEY-Edit  L-Load  S-Save  ESC/`-Exit  ?-Help ");
        } else {
            status_string = _(" ENTER-Save Changes  ESC/`-Exit ");
        }

        status_left_stop = WIDTH - strlen(status_string);
        if (status_left_stop <= 0) {
            status_left_stop = 0;
        } else {
            status_left_stop /= 2;
        }
        screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string,
                                Q_COLOR_STATUS);
        /*
         * Replace XXXXXXXX.XXX with editing_keyboard_filename
         */
        if (editing_key == Q_FALSE) {
            snprintf(filename, sizeof(filename), "%s",
                     editing_keyboard_filename);
            for (i = 0; i < strlen(filename); i++) {
                screen_put_color_char_yx(HEIGHT - 1, status_left_stop + 6 + i,
                                         filename[i], Q_COLOR_STATUS);
            }
            for (; i < 12; i++) {
                screen_put_color_char_yx(HEIGHT - 1, status_left_stop + 6 + i,
                                         ' ', Q_COLOR_STATUS);
            }
        }
    }

    /*
     * Function keys
     */
    screen_put_color_char_yx(window_top + 1, window_left + 2,
                             cp437_chars[Q_WINDOW_LEFT_TOP_DOUBLESIDE],
                             Q_COLOR_MENU_COMMAND);
    screen_put_color_hline_yx(window_top + 1, window_left + 3,
                              cp437_chars[SINGLE_BAR], 25,
                              Q_COLOR_MENU_COMMAND);
    screen_put_color_str_yx(window_top + 1, window_left + 28,
                            _(" Function Keys "), Q_COLOR_MENU_COMMAND);
    screen_put_color_hline_yx(window_top + 1, window_left + 43,
                              cp437_chars[SINGLE_BAR], 23,
                              Q_COLOR_MENU_COMMAND);
    screen_put_color_char_yx(window_top + 1, window_left + 66,
                             cp437_chars[Q_WINDOW_RIGHT_TOP_DOUBLESIDE],
                             Q_COLOR_MENU_COMMAND);

    /*
     * 101 grey keys
     */
    screen_put_color_char_yx(window_top + 14, window_left + 2,
                             cp437_chars[Q_WINDOW_LEFT_TOP_DOUBLESIDE],
                             Q_COLOR_MENU_COMMAND);
    screen_put_color_hline_yx(window_top + 14, window_left + 3,
                              cp437_chars[SINGLE_BAR], 3, Q_COLOR_MENU_COMMAND);
    screen_put_color_str_yx(window_top + 14, window_left + 6,
                            _(" 101 Grey Keys "), Q_COLOR_MENU_COMMAND);
    screen_put_color_hline_yx(window_top + 14, window_left + 21,
                              cp437_chars[SINGLE_BAR], 3, Q_COLOR_MENU_COMMAND);
    screen_put_color_char_yx(window_top + 14, window_left + 24,
                             cp437_chars[Q_WINDOW_RIGHT_TOP_DOUBLESIDE],
                             Q_COLOR_MENU_COMMAND);

    /*
     * Keypad
     */
    screen_put_color_char_yx(window_top + 1, window_left + 69,
                             cp437_chars[Q_WINDOW_LEFT_TOP_DOUBLESIDE],
                             Q_COLOR_MENU_COMMAND);
    screen_put_color_str_yx(window_top + 1, window_left + 70, _("Keypad"),
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_char_yx(window_top + 1, window_left + 76,
                             cp437_chars[Q_WINDOW_RIGHT_TOP_DOUBLESIDE],
                             Q_COLOR_MENU_COMMAND);

    /*
     * Loop through each label and print it
     */
    for (i = 0; i < NUMBER_OF_TEXTBOXES; i++) {

        if (function_key_textboxes[i].highlighted == Q_TRUE) {
            screen_put_printf_yx(window_top +
                                 function_key_textboxes[i].label_top,
                                 window_left +
                                 function_key_textboxes[i].label_left,
                                 Q_A_BLINK | screen_attr(Q_COLOR_MENU_COMMAND),
                                 screen_color(Q_COLOR_MENU_COMMAND), "%s",
                                 function_key_textboxes[i].label_text);
        } else {
            screen_put_color_printf_yx(window_top +
                                       function_key_textboxes[i].label_top,
                                       window_left +
                                       function_key_textboxes[i].label_left,
                                       Q_COLOR_MENU_COMMAND, "%s",
                                       function_key_textboxes[i].label_text);
        }

        for (j = 0; j < wcslen(function_key_textboxes[i].value); j++) {
            if (j == function_key_textboxes[i].value_length) {
                break;
            }
            if (function_key_textboxes[i].highlighted == Q_TRUE) {
                screen_put_color_char_yx(window_top +
                                         function_key_textboxes[i].label_top,
                                         window_left +
                                         function_key_textboxes[i].value_left +
                                         j, function_key_textboxes[i].value[j],
                                         Q_COLOR_MENU_COMMAND);
            } else {
                screen_put_color_char_yx(window_top +
                                         function_key_textboxes[i].label_top,
                                         window_left +
                                         function_key_textboxes[i].value_left +
                                         j, function_key_textboxes[i].value[j],
                                         Q_COLOR_MENU_TEXT);
            }
        }

        if (function_key_textboxes[i].highlighted == Q_TRUE) {
            screen_put_color_hline_yx(window_top +
                                      function_key_textboxes[i].label_top,
                                      window_left +
                                      function_key_textboxes[i].value_left + j,
                                      cp437_chars[HATCH],
                                      function_key_textboxes[i].value_length -
                                      j, Q_COLOR_MENU_COMMAND);
        } else {
            screen_put_color_hline_yx(window_top +
                                      function_key_textboxes[i].label_top,
                                      window_left +
                                      function_key_textboxes[i].value_left + j,
                                      cp437_chars[HATCH],
                                      function_key_textboxes[i].value_length -
                                      j, Q_COLOR_MENU_TEXT);
        }

    }

    /*
     * Special case for <- : I need BACK_ARROWHEAD and LRCORNER
     */
    screen_put_color_char_yx(window_top + 17, window_left + 69,
                             cp437_chars[BACK_ARROWHEAD], Q_COLOR_MENU_COMMAND);
    screen_put_color_char_yx(window_top + 17, window_left + 70,
                             cp437_chars[LRCORNER], Q_COLOR_MENU_COMMAND);

    if (redraw_boxes == Q_FALSE) {
        screen_flush();
        if (editing_key == Q_FALSE) {
            /*
             * Press a key to edit
             */
            screen_put_color_str_yx(window_top + 22, window_left + 2,
                                    _("Press a KEY to edit"),
                                    Q_COLOR_MENU_COMMAND);
        } else {
            /*
             * Edit
             */
            /*
             * Handled completely by a form in the keyboard handler
             */
        }
    } else {
        /*
         * Reposition cursor back into edit form
         */
        screen_win_flush(edit_keybinding_window);
        wcursyncup((WINDOW *) edit_keybinding_window);
    }

    q_screen_dirty = Q_FALSE;
    redraw_boxes = Q_FALSE;
}

/**
 * Load keybindings from a filename into the currently-bound keyboard.
 *
 * @param filename name of file with key bindings
 */
void switch_current_keyboard(const char * filename) {
    int i;
    char buffer[FILENAME_SIZE];

    /*
     * This filename of the appropriate keyboard
     */
    char * keyboard_filename;

    /*
     * The keyboard that needs to be loaded
     */
    struct emulation_keyboard * keyboard_keyboard = NULL;

    assert(filename != NULL);

    /*
     * Clear out existing keyboard
     */
    if ((current_bound_keyboard_filename != NULL)
        && (filename != current_bound_keyboard_filename)) {
        Xfree(current_bound_keyboard_filename, __FILE__, __LINE__);
        current_bound_keyboard_filename = NULL;
    }
    if ((editing_keyboard_filename != NULL)
        && (filename != editing_keyboard_filename)) {
        Xfree(editing_keyboard_filename, __FILE__, __LINE);
        editing_keyboard_filename = NULL;
    }

    /*
     * Figure out which to switch to
     */
    if (strlen(filename) == 0) {
        /*
         * Unset current_bound_keyboard
         */
        current_bound_keyboard_filename = NULL;

        /*
         * Switch to current emulation keyboard
         */
        for (i = 0; emulation_bound_keyboards[i].terminfo_name != NULL; i++) {
            if (emulation_bound_keyboards[i].emulation == q_status.emulation) {
                break;
            }
        }
        /*
         * Set editing_keyboard and editing_keyboard_filename
         */
        snprintf(buffer, sizeof(buffer), "%s.key",
                 emulation_bound_keyboards[i].terminfo_name);
        keyboard_filename = buffer;
        keyboard_keyboard = &emulation_bound_keyboards[i];

    } else {

        /*
         * Load a keyboard from file.  Cast to avoid compiler warning.
         */
        keyboard_filename = (char *) filename;

        /*
         * See if the keyboard is an emulation keyboard (or default) or a
         * custom keyboard.
         */
        for (i = 0; emulation_bound_keyboards[i].terminfo_name != NULL; i++) {
            snprintf(buffer, sizeof(buffer), "%s.key",
                     emulation_bound_keyboards[i].terminfo_name);
            if (strcmp(buffer, filename) == 0) {
                /*
                 * Bing!  This is an emulation keybaord
                 */
                keyboard_keyboard = &emulation_bound_keyboards[i];
            }
        }

        /*
         * Check if this is fact the default keyboard
         */
        if (keyboard_keyboard == NULL) {
            if (strcmp(filename, "default.key") == 0) {
                /*
                 * This is the default keybaord
                 */
                keyboard_keyboard = &default_bound_keyboard;
            }
        }

        /*
         * This must be a custom keyboard
         */
        if (keyboard_keyboard == NULL) {
            if (filename != current_bound_keyboard_filename) {
                current_bound_keyboard_filename =
                    Xstrdup(filename, __FILE__, __LINE__);
            }
            keyboard_keyboard = &current_bound_keyboard;
        }
    }

    assert(keyboard_filename != NULL);
    assert(keyboard_keyboard != NULL);

    /*
     * Save the new filename
     */
    editing_keyboard_filename = Xstrdup(keyboard_filename, __FILE__, __LINE__);

    /*
     * Load into the custom keyboard
     */
    load_keybindings_from_file(keyboard_filename, keyboard_keyboard);

    /*
     * Copy to the editing keyboard
     */
    copy_keyboard(&editing_keyboard, keyboard_keyboard);

    /*
     * Populate the text boxes with the new bindings
     */
    copy_keyboard_to_function_key_editor_textboxes(&editing_keyboard);
}
