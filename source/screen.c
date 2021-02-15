/*
 * screen.c
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
 * This module contains the ncurses-based screen drawing functions.
 */

#include "qcurses.h"
#include "common.h"

#include <string.h>
#include <assert.h>
#include "qodem.h"
#include "music.h"
#include "vt100.h"
#include "states.h"
#include "screen.h"

#ifdef _MSC_VER
#define inline __inline
#endif

/* Set this to a not-NULL value to enable debug log. */
/* static const char * DLOGNAME = "screen"; */
static const char * DLOGNAME = NULL;

/**
 * The offset between normal and bolded colors.  Stored in colors.c.
 */
extern short q_color_bold_offset;

/**
 * The geometry as requested by the command line arguments.  Stored in
 * qodem.c.
 */
extern unsigned char q_rows_arg;
extern unsigned char q_cols_arg;

/* If true, then initscr() has been called. */
static Q_BOOL curses_initted = Q_FALSE;

#if defined(__linux) && defined(Q_ENABLE_GPM)
#include <gpm.h>
#include "input.h"

/* If true, then GPM is available for mouse events. */
Q_BOOL q_gpm_mouse = Q_FALSE;

/* The handle to GPM. */
static Gpm_Connect gpm_connection;

#endif

#if !defined(Q_PDCURSES) && !defined(Q_PDCURSES_WIN32)
#include <stdlib.h>             /* getenv() */
/*
 * The ncurses screen representing stdin/stdout.
 */
SCREEN * q_main_screen = NULL;
#endif

/**
 * Get the to-screen color index for a logical attr that has COLOR_X and
 * A_BOLD set.
 *
 * @param attr a curses attr
 * @param color the pair number
 * @return the pair number for the to-screen color
 */
static inline short physical_color_from_attr(const attr_t attr,
                                             const short color) {
    short color2 = color;
    if (color2 == 0x38) {
        color2 = q_white_color_pair_num;
    } else if (color2 == 0x00) {
        color2 = 0x38;
    }
    if (attr & Q_A_BOLD) {
        color2 += q_color_bold_offset;
    }
    return color2;
}

/**
 * Get the to-screen attr for a logical attr that has COLOR_X and A_BOLD set.
 *
 * @param attr a curses attr
 * @return the attr for the to-screen color
 */
static inline attr_t physical_attr_from_attr(const attr_t attr) {
    if (q_color_bold_offset != 0) {
        return (attr & ~Q_A_COLOR) & ~Q_A_BOLD;
    } else {
        return (attr & ~Q_A_COLOR);
    }
}

/**
 * Turn a Q_COLOR enum into a color pair index.  This is the color code path
 * for UI elements to screen.
 *
 * @param q_color the color enum
 * @return the pair number
 */
short screen_color(const Q_COLOR q_color) {
    short color = (q_text_colors[q_color].fg << 3) | q_text_colors[q_color].bg;
    return color;
}

/**
 * Turn a Q_COLOR enum into a color pair index.  This is the color code path
 * for UI elements to screen.
 *
 * @param q_color the color enum
 * @return a curses attr
 */
attr_t screen_attr(const Q_COLOR q_color) {
    attr_t attr = Q_A_NORMAL;
    if (q_text_colors[q_color].bold == Q_TRUE) {
        attr |= Q_A_BOLD;
    }
    return attr;
}

/**
 * Turn a Q_COLOR enum into a ncurses attr_t.  This is used to specify the
 * background (normal) terminal color for the emulations.  Note that even if
 * one specifies a terminal default like "bold yellow on blue", then the
 * background might not have the A_BOLD attribute set depending on the number
 * of colors the UI can support.
 *
 * @param q_color the color enum
 * @return a curses attr
 */
attr_t scrollback_full_attr(const Q_COLOR q_color) {
    attr_t attr = color_to_attr(screen_color(q_color)) | screen_attr(q_color);
    return attr;
}

/**
 * Given an attr, find the color index (pair number).
 *
 * @param attr a curses attr
 * @return the pair number
 */
short color_from_attr(const attr_t attr) {
    return ((short) PAIR_NUMBER(attr));
}

/**
 * Given a color index (pair number), find the attr.
 *
 * @param color the pair number
 * @return a curses attr
 */
attr_t color_to_attr(const short color) {
    return COLOR_PAIR(color);
}

/**
 * Handle reverse-video and A_REVERSE in light of VT100 flags to provide the
 * same output as DOS used to.
 *
 * @param color the attr
 * @param reverse if true, reverse video (DECSCNM) is enabled
 * @return a curses attr that does not have A_REVERSE set, and might have
 * foreground and background colors reversed.
 */
attr_t vt100_check_reverse_color(const attr_t color, const Q_BOOL reverse) {
    attr_t attrs = color & NO_COLOR_MASK;
    short old_color = color_from_attr(color);
    short old_color_bg = (old_color & 0x07);
    short old_color_fg = (old_color & 0x38) >> 3;
    short flip_color;
    Q_BOOL do_flip = Q_FALSE;

    switch (q_status.emulation) {
    case Q_EMUL_TTY:
    case Q_EMUL_VT52:
    case Q_EMUL_DEBUG:
        /*
         * These guys just do the braindead stuff
         */
        return color;
    case Q_EMUL_ANSI:
    case Q_EMUL_AVATAR:
    case Q_EMUL_PETSCII:
    case Q_EMUL_ATASCII:
    case Q_EMUL_VT100:
    case Q_EMUL_VT102:
    case Q_EMUL_VT220:
    case Q_EMUL_LINUX:
    case Q_EMUL_LINUX_UTF8:
    case Q_EMUL_XTERM:
    case Q_EMUL_XTERM_UTF8:
        break;
    }

    if ((reverse == Q_TRUE) && ((attrs & Q_A_REVERSE) != 0)) {
        /*
         * Reverse character on a reverse screen.  Keep the original color,
         * turn off A_REVERSE.
         */
        attrs &= ~Q_A_REVERSE;
        do_flip = Q_FALSE;
    } else if ((reverse == Q_FALSE) && ((attrs & Q_A_REVERSE) != 0)) {
        /*
         * Reverse. on a normal screen.  Turn off A_REVERSE and flip
         * foreground/background colors.
         */
        attrs &= ~Q_A_REVERSE;
        do_flip = Q_TRUE;
    } else if (reverse == Q_TRUE) {
        /*
         * Normal character on a reverse screen.  Flip foreground and
         * background.
         */
        do_flip = Q_TRUE;
    }

    /*
     * At this point, A_REVERSE should NOT be set.
     */
    assert((attrs & Q_A_REVERSE) == 0);

    if (do_flip == Q_TRUE) {
        flip_color = old_color_fg;
        flip_color |= old_color_bg << 3;
    } else {
        flip_color = old_color;
    }

    return color_to_attr(flip_color) | attrs;
}

/**
 * Draw a character from the scrollback to the screen.  This function also
 * performs some caching to reduce calls to setcchar().  This is the color
 * code path for the scrollback.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param ch the character to write
 * @param attr the attributes to use
 */
void screen_put_scrollback_char_yx(const int y, const int x, const wchar_t ch,
                                   const attr_t attr) {

    static cchar_t ncurses_ch;
    static wchar_t ch_cached = -1;
    /*
     * Something I won't use often.
     */
    static attr_t attr_cached = Q_A_BLINK | Q_A_PROTECT;
    static int cache_count = 0;
    wchar_t wch[2];
    short color = color_from_attr(attr);

    if (curses_initted == Q_FALSE) {
        /* Handle lazy-loading curses. */
        screen_setup(q_rows_arg, q_cols_arg);
    }

    if ((ch == ch_cached) && (attr == attr_cached)) {
        /*
         * NOP
         */
        cache_count++;
    } else {
        wch[0] = ch;
        wch[1] = 0;
        setcchar(&ncurses_ch, wch, physical_attr_from_attr(attr),
                 physical_color_from_attr(attr, color), NULL);
        ch_cached = ch;
        attr_cached = attr;
    }
    mvwadd_wch(stdscr, y, x, &ncurses_ch);
}

/**
 * Draw a character to a window at the current drawing position.
 *
 * @param win the WINDOW to draw to
 * @param ch the character to write
 * @param attr the attributes to use
 * @param color the pair number
 */
static void screen_win_put_char(void * win, const wchar_t ch, const attr_t attr,
                                const short color) {
    cchar_t ncurses_ch;
    wchar_t wch[2];

    if (curses_initted == Q_FALSE) {
        /* Handle lazy-loading curses. */
        screen_setup(q_rows_arg, q_cols_arg);
    }

    wch[0] = ch;
    wch[1] = 0;
    setcchar(&ncurses_ch, wch, physical_attr_from_attr(attr),
             physical_color_from_attr(attr, color), NULL);
    wadd_wch((WINDOW *) win, &ncurses_ch);
}

/**
 * Draw a character to a window.
 *
 * @param win the WINDOW to draw to
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param ch the character to write
 * @param attr the attributes to use
 * @param color the pair number
 */
static void screen_win_put_char_yx(void * win, const int y, const int x,
                                   const wchar_t ch, const attr_t attr,
                                   const short color) {
    cchar_t ncurses_ch;
    wchar_t wch[2];

    if (curses_initted == Q_FALSE) {
        /* Handle lazy-loading curses. */
        screen_setup(q_rows_arg, q_cols_arg);
    }

    wch[0] = ch;
    wch[1] = 0;
    setcchar(&ncurses_ch, wch, physical_attr_from_attr(attr),
             physical_color_from_attr(attr, color), NULL);
    mvwadd_wch((WINDOW *) win, y, x, &ncurses_ch);
}

/**
 * Draw a string to a window at the current drawing position.
 *
 * @param win the WINDOW to draw to
 * @param str the string to write
 * @param attr the attributes to use
 * @param color the pair number
 */
static void screen_win_put_str(void * win, const char * str, const attr_t attr,
                               const short color) {
    unsigned int i;
    for (i = 0; i < strlen(str); i++) {
        screen_win_put_char(win, str[i], attr, color);
    }
}

/**
 * Draw a wide string to a window at the current drawing position.
 *
 * @param win the WINDOW to draw to
 * @param wcs the string to write
 * @param attr the attributes to use
 * @param color the pair number
 */
static void screen_win_put_wcs(void * win, const wchar_t * wcs,
                               const attr_t attr, const short color) {
    unsigned int i;
    for (i = 0; i < wcslen(wcs); i++) {
        screen_win_put_char(win, wcs[i], attr, color);
    }
}

/**
 * Draw a string to a window.
 *
 * @param win the WINDOW to draw to
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param str the string to write
 * @param attr the attributes to use
 * @param color the pair number
 */
static void screen_win_put_str_yx(void * win, const int y, const int x,
                                  const char * str, const attr_t attr,
                                  const short color) {
    unsigned int i;
    for (i = 0; i < strlen(str); i++) {
        screen_win_put_char_yx(win, y, x + i, str[i], attr, color);
    }
}

/**
 * Draw a string to a window at the current drawing position.
 *
 * @param win the WINDOW to draw to
 * @param str the string to write
 * @param n the maximum number of characters to draw
 * @param attr the attributes to use
 * @param color the pair number
 */
static void screen_win_put_strn(void * win, const char * str,
                                const unsigned int n, const attr_t attr,
                                const short color) {
    unsigned int i;
    for (i = 0; (i < strlen(str)) && (i < n); i++) {
        screen_win_put_char(win, str[i], attr, color);
    }
}

/**
 * Draw a string to a window.
 *
 * @param win the WINDOW to draw to
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param str the string to write
 * @param n the maximum number of characters to draw
 * @param attr the attributes to use
 * @param color the pair number
 */
static void screen_win_put_strn_yx(void * win, const int y, const int x,
                                   const char * str, const unsigned int n,
                                   const attr_t attr, const short color) {
    unsigned int i;
    for (i = 0; (i < strlen(str)) && (i < n); i++) {
        screen_win_put_char_yx(win, y, x + i, str[i], attr, color);
    }
}

/**
 * Draw a wide string to a window.
 *
 * @param win the WINDOW to draw to
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param wcs the string to write
 * @param attr the attributes to use
 * @param color the pair number
 */
static void screen_win_put_wcs_yx(void * win, const int y, const int x,
                                  const wchar_t * wcs, const attr_t attr,
                                  const short color) {
    unsigned int i;
    for (i = 0; i < wcslen(wcs); i++) {
        screen_win_put_char_yx(win, y, x + i, wcs[i], attr, color);
    }
}

/**
 * Draw a horizontal line to a window.
 *
 * @param win the WINDOW to draw to
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param ch the character to write
 * @param n the number of characters to draw
 * @param attr the attributes to use
 * @param color the pair number
 */
static void screen_win_put_hline_yx(void * win, const int y, const int x,
                                    const wchar_t ch, const int n,
                                    const attr_t attr, const short color) {
    cchar_t ncurses_ch;
    wchar_t wch[2];

    if (curses_initted == Q_FALSE) {
        /* Handle lazy-loading curses. */
        screen_setup(q_rows_arg, q_cols_arg);
    }

    wch[0] = ch;
    wch[1] = 0;
    setcchar(&ncurses_ch, wch, physical_attr_from_attr(attr),
             physical_color_from_attr(attr, color), NULL);
    mvwhline_set((WINDOW *) win, y, x, &ncurses_ch, n);
}

/**
 * Draw a vertical line to a window.
 *
 * @param win the WINDOW to draw to
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param ch the character to write
 * @param n the number of characters to draw
 * @param q_color the color enum
 */
static void screen_win_put_vline_yx(void * win, const int y, const int x,
                                    const wchar_t ch, const int n,
                                    const attr_t attr, const short color) {
    cchar_t ncurses_ch;
    wchar_t wch[2];

    if (curses_initted == Q_FALSE) {
        /* Handle lazy-loading curses. */
        screen_setup(q_rows_arg, q_cols_arg);
    }

    wch[0] = ch;
    wch[1] = 0;
    setcchar(&ncurses_ch, wch, physical_attr_from_attr(attr),
             physical_color_from_attr(attr, color), NULL);
    mvwvline_set((WINDOW *) win, y, x, &ncurses_ch, n);
}

/**
 * Draw a character to the screen.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param ch the character to write
 * @param attr the attributes to use
 * @param color the pair number
 */
void screen_put_char_yx(const int y, const int x, const wchar_t ch,
                        const attr_t attr, const short color) {
    screen_win_put_char_yx(stdscr, y, x, ch, attr, color);
}

/**
 * Draw a string to the screen.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param str the string to write
 * @param attr the attributes to use
 * @param color the pair number
 */
void screen_put_str_yx(const int y, const int x, const char * str,
                       const attr_t attr, const short color) {
#if defined(Q_PDCURSES) || defined(Q_PDCURSES_WIN32)
    /*
     * PDCurses doesn't display "\n"'s as processed newlines, instead it
     * shows little boxes.  Since this function is only called with newlines
     * when I am about to spawn an X11 terminal in another window, I will NOP
     * if I see a newline in the string.
     */
    unsigned int i;
    for (i = 0; i < strlen(str); i++) {
        if (str[i] == '\n') {
            return;
        }
    }
#endif
    screen_win_put_str_yx(stdscr, y, x, str, attr, color);
}

/**
 * Draw a printf-style format string plus optional arguments to the screen.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param attr the attributes to use
 * @param color the pair number
 * @param format the format string
 */
void screen_put_printf_yx(const int y, const int x, const attr_t attr,
                          const short color, const char * format, ...) {
    va_list arglist;
    char outbuf[DIALOG_MESSAGE_SIZE];
    assert(strlen(format) < DIALOG_MESSAGE_SIZE);
    memset(outbuf, 0, sizeof(outbuf));
    va_start(arglist, format);
#ifdef __BORLANDC__
    vsprintf((char *) (outbuf + strlen(outbuf)), format, arglist);
#else
    vsnprintf((char *) (outbuf + strlen(outbuf)),
              (DIALOG_MESSAGE_SIZE - strlen(outbuf)), format, arglist);
#endif
    va_end(arglist);

    screen_win_put_strn_yx(stdscr, y, x, outbuf, strlen(outbuf), attr, color);
}

/**
 * Draw a character to a window at the current drawing position.
 *
 * @param win the WINDOW to draw to
 * @param ch the character to write
 * @param q_color the color enum
 */
void screen_win_put_color_char(void * win, const wchar_t ch,
                               const Q_COLOR q_color) {
    screen_win_put_char((WINDOW *) win, ch, screen_attr(q_color),
                        screen_color(q_color));
}

/**
 * Draw a character to a window.
 *
 * @param win the WINDOW to draw to
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param ch the character to write
 * @param q_color the color enum
 */
void screen_win_put_color_char_yx(void * win, const int y, const int x,
                                  const wchar_t ch, const Q_COLOR q_color) {
    screen_win_put_char_yx((WINDOW *) win, y, x, ch, screen_attr(q_color),
                           screen_color(q_color));
}

/**
 * Draw a string to a window at the current drawing position.
 *
 * @param win the WINDOW to draw to
 * @param str the string to write
 * @param q_color the color enum
 */
void screen_win_put_color_str(void * win, const char * str, const Q_COLOR q_color) {
    screen_win_put_str((WINDOW *) win, str, screen_attr(q_color),
                       screen_color(q_color));
}

/**
 * Draw a wide string to a window at the current drawing position.
 *
 * @param win the WINDOW to draw to
 * @param wcs the string to write
 * @param q_color the color enum
 */
void screen_win_put_color_wcs(void * win, const wchar_t * wcs,
                              const Q_COLOR q_color) {
    screen_win_put_wcs((WINDOW *) win, wcs, screen_attr(q_color),
                       screen_color(q_color));
}

/**
 * Draw a string to a window.
 *
 * @param win the WINDOW to draw to
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param str the string to write
 * @param q_color the color enum
 */
void screen_win_put_color_str_yx(void * win, const int y, const int x,
                                 const char * str, const Q_COLOR q_color) {
    screen_win_put_str_yx((WINDOW *) win, y, x, str, screen_attr(q_color),
                          screen_color(q_color));
}

/**
 * Draw a wide string to a window.
 *
 * @param win the WINDOW to draw to
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param wcs the string to write
 * @param q_color the color enum
 */
void screen_win_put_color_wcs_yx(void * win, const int y, const int x,
                                 const wchar_t * wcs, const Q_COLOR q_color) {
    screen_win_put_wcs_yx((WINDOW *) win, y, x, wcs, screen_attr(q_color),
                          screen_color(q_color));
}

/**
 * Draw a string to a window at the current drawing position.
 *
 * @param win the WINDOW to draw to
 * @param str the string to write
 * @param n the maximum number of characters to draw
 * @param q_color the color enum
 */
void screen_win_put_color_strn(void * win, const char * str, const int n,
                               const Q_COLOR q_color) {
    screen_win_put_strn((WINDOW *) win, str, n, screen_attr(q_color),
                        screen_color(q_color));
}

/**
 * Draw a string to a window.
 *
 * @param win the WINDOW to draw to
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param str the string to write
 * @param n the maximum number of characters to draw
 * @param q_color the color enum
 */
void screen_win_put_color_strn_yx(void * win, const int y, const int x,
                                  const char * str, const int n,
                                  const Q_COLOR q_color) {
    screen_win_put_strn_yx((WINDOW *) win, y, x, str, n, screen_attr(q_color),
                           screen_color(q_color));
}

/**
 * Draw a horizontal line to a window.
 *
 * @param win the WINDOW to draw to
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param ch the character to write
 * @param n the number of characters to draw
 * @param q_color the color enum
 */
void screen_win_put_color_hline_yx(void * win, const int y, const int x,
                                   const wchar_t ch, const int n,
                                   const Q_COLOR q_color) {
    screen_win_put_hline_yx((WINDOW *) win, y, x, ch, n, screen_attr(q_color),
                            screen_color(q_color));
}

/**
 * Draw a vertical line to a window.
 *
 * @param win the WINDOW to draw to
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param ch the character to write
 * @param n the number of characters to draw
 * @param q_color the color enum
 */
void screen_win_put_color_vline_yx(void * win, const int y, const int x,
                                   const wchar_t ch, const int n,
                                   const Q_COLOR q_color) {
    screen_win_put_vline_yx((WINDOW *) win, y, x, ch, n, screen_attr(q_color),
                            screen_color(q_color));
}

/**
 * Draw a printf-style format string plus optional arguments to a window at
 * the current drawing position.
 *
 * @param win the WINDOW to draw to
 * @param color the pair number
 * @param format the format string
 */
void screen_win_put_color_printf(void * win, const Q_COLOR q_color,
                                 const char * format, ...) {
    va_list arglist;
    char outbuf[DIALOG_MESSAGE_SIZE];
    assert(strlen(format) < DIALOG_MESSAGE_SIZE);
    memset(outbuf, 0, sizeof(outbuf));
    va_start(arglist, format);
#ifdef __BORLANDC__
    vsprintf((char *) (outbuf + strlen(outbuf)), format, arglist);
#else
    vsnprintf((char *) (outbuf + strlen(outbuf)),
              (DIALOG_MESSAGE_SIZE - strlen(outbuf)), format, arglist);
#endif
    va_end(arglist);

    screen_win_put_strn((WINDOW *) win, outbuf, strlen(outbuf),
                        screen_attr(q_color), screen_color(q_color));
}

/**
 * Draw a printf-style format string plus optional arguments to a window.
 *
 * @param win the WINDOW to draw to
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param color the pair number
 * @param format the format string
 */
void screen_win_put_color_printf_yx(void * win, const int y, const int x,
                                    const Q_COLOR q_color, const char * format,
                                    ...) {
    va_list arglist;
    char outbuf[DIALOG_MESSAGE_SIZE];
    assert(strlen(format) < DIALOG_MESSAGE_SIZE);
    memset(outbuf, 0, sizeof(outbuf));
    va_start(arglist, format);
#ifdef __BORLANDC__
    vsprintf((char *) (outbuf + strlen(outbuf)), format, arglist);
#else
    vsnprintf((char *) (outbuf + strlen(outbuf)),
              (DIALOG_MESSAGE_SIZE - strlen(outbuf)), format, arglist);
#endif
    va_end(arglist);

    screen_win_put_strn_yx((WINDOW *) win, y, x, outbuf, strlen(outbuf),
                           screen_attr(q_color), screen_color(q_color));
}

/**
 * Draw a character to the screen at the current drawing position.
 *
 * @param ch the character to write
 * @param q_color the color enum
 */
void screen_put_color_char(const wchar_t ch, const Q_COLOR q_color) {
    screen_win_put_char(stdscr, ch, screen_attr(q_color),
                        screen_color(q_color));
}

/**
 * Draw a character to the screen.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param ch the character to write
 * @param q_color the color enum
 */
void screen_put_color_char_yx(const int y, const int x, const wchar_t ch,
                              const Q_COLOR q_color) {
    screen_win_put_char_yx(stdscr, y, x, ch, screen_attr(q_color),
                           screen_color(q_color));
}

/**
 * Draw a string to the screen at the current drawing position.
 *
 * @param str the string to write
 * @param q_color the color enum
 */
void screen_put_color_str(const char * str, const Q_COLOR q_color) {
    screen_win_put_str(stdscr, str, screen_attr(q_color),
                       screen_color(q_color));
}

/**
 * Draw a wide string to the screen at the current drawing position.
 *
 * @param wcs the string to write
 * @param q_color the color enum
 */
void screen_put_color_wcs(const wchar_t * wcs, const Q_COLOR q_color) {
    screen_win_put_wcs(stdscr, wcs, screen_attr(q_color),
                       screen_color(q_color));
}

/**
 * Draw a string to the screen.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param str the string to write
 * @param q_color the color enum
 */
void screen_put_color_str_yx(const int y, const int x, const char * str,
                             const Q_COLOR q_color) {
    screen_win_put_str_yx(stdscr, y, x, str, screen_attr(q_color),
                          screen_color(q_color));
}

/**
 * Draw a wide string to the screen.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param wcs the string to write
 * @param q_color the color enum
 */
void screen_put_color_wcs_yx(const int y, const int x, const wchar_t * wcs,
                             const Q_COLOR q_color) {
    screen_win_put_wcs_yx(stdscr, y, x, wcs, screen_attr(q_color),
                          screen_color(q_color));
}

/**
 * Draw a string to the screen at the current drawing position.
 *
 * @param str the string to write
 * @param n the maximum number of characters to draw
 * @param q_color the color enum
 */
void screen_put_color_strn(const char * str, const int n, const Q_COLOR q_color) {
    screen_win_put_strn(stdscr, str, n, screen_attr(q_color),
                        screen_color(q_color));
}

/**
 * Draw a string to the screen.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param str the string to write
 * @param n the maximum number of characters to draw
 * @param q_color the color enum
 */
void screen_put_color_strn_yx(const int y, const int x, const char * str,
                              const int n, const Q_COLOR q_color) {
    screen_win_put_strn_yx(stdscr, y, x, str, n, screen_attr(q_color),
                           screen_color(q_color));
}

/**
 * Draw a horizontal line to the screen.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param ch the character to write
 * @param n the number of characters to draw
 * @param q_color the color enum
 */
void screen_put_color_hline_yx(const int y, const int x, const wchar_t ch,
                               const int n, const Q_COLOR q_color) {
    screen_win_put_hline_yx(stdscr, y, x, ch, n, screen_attr(q_color),
                            screen_color(q_color));
}

/**
 * Draw a vertical line to the screen.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param ch the character to write
 * @param n the number of characters to draw
 * @param q_color the color enum
 */
void screen_put_color_vline_yx(const int y, const int x, const wchar_t ch,
                               const int n, const Q_COLOR q_color) {
    screen_win_put_vline_yx(stdscr, y, x, ch, n, screen_attr(q_color),
                            screen_color(q_color));
}

/**
 * Draw a printf-style format string plus optional arguments to the screen at
 * the current drawing position.
 *
 * @param color the pair number
 * @param format the format string
 */
void screen_put_color_printf(const Q_COLOR q_color, const char * format, ...) {
    va_list arglist;
    char outbuf[DIALOG_MESSAGE_SIZE];
    assert(strlen(format) < DIALOG_MESSAGE_SIZE);
    memset(outbuf, 0, sizeof(outbuf));
    va_start(arglist, format);
#ifdef __BORLANDC__
    vsprintf((char *) (outbuf + strlen(outbuf)), format, arglist);
#else
    vsnprintf((char *) (outbuf + strlen(outbuf)),
              (DIALOG_MESSAGE_SIZE - strlen(outbuf)), format, arglist);
#endif
    va_end(arglist);

    screen_win_put_strn(stdscr, outbuf, strlen(outbuf), screen_attr(q_color),
                        screen_color(q_color));
}

/**
 * Draw a printf-style format string plus optional arguments to the screen.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param color the pair number
 * @param format the format string
 */
void screen_put_color_printf_yx(const int y, const int x, const Q_COLOR q_color,
                                const char * format, ...) {
    va_list arglist;
    char outbuf[DIALOG_MESSAGE_SIZE];
    assert(strlen(format) < DIALOG_MESSAGE_SIZE);
    memset(outbuf, 0, sizeof(outbuf));
    va_start(arglist, format);
#ifdef __BORLANDC__
    vsprintf((char *) (outbuf + strlen(outbuf)), format, arglist);
#else
    vsnprintf((char *) (outbuf + strlen(outbuf)),
              (DIALOG_MESSAGE_SIZE - strlen(outbuf)), format, arglist);
#endif
    va_end(arglist);

    screen_win_put_strn_yx(stdscr, y, x, outbuf, strlen(outbuf),
                           screen_attr(q_color), screen_color(q_color));
}

/**
 * Change the current drawing position on the screen.
 *
 * @param y new row position to write to.  The top-most row is 0.
 * @param x new column position to write to.  The left-most column is 0.
 */
void screen_move_yx(const int y, const int x) {
    if (curses_initted == Q_FALSE) {
        /* Handle lazy-loading curses. */
        screen_setup(q_rows_arg, q_cols_arg);
    }

    move(y, x);
}

/**
 * Change the current drawing position on a window.
 *
 * @param win the curses WINDOW
 * @param y new row position to write to.  The top-most row is 0.
 * @param x new column position to write to.  The left-most column is 0.
 */
void screen_win_move_yx(void * win, const int y, const int x) {
    if (curses_initted == Q_FALSE) {
        /* Handle lazy-loading curses. */
        screen_setup(q_rows_arg, q_cols_arg);
    }

    wmove((WINDOW *) win, y, x);
}

/**
 * Force any pending updates to be written to the physical terminal.
 */
void screen_flush() {
    if (curses_initted == Q_FALSE) {
        /* Handle lazy-loading curses. */
        screen_setup(q_rows_arg, q_cols_arg);
    }

    refresh();
}

/**
 * Force any pending updates to be written to the physical terminal.
 *
 * @param win the curses WINDOW
 */
void screen_win_flush(void * win) {
    if (curses_initted == Q_FALSE) {
        /* Handle lazy-loading curses. */
        screen_setup(q_rows_arg, q_cols_arg);
    }

    wrefresh((WINDOW *) win);
}

/**
 * Clear the entire screen using the curses werase() call.
 */
void screen_clear() {
    int i;

    if (curses_initted == Q_FALSE) {
        /* Handle lazy-loading curses. */
        screen_setup(q_rows_arg, q_cols_arg);
    }

    if (has_true_doublewidth() == Q_TRUE) {
        for (i = 0; i < HEIGHT; i++) {
#ifdef Q_PDCURSES
            PDC_set_double(i, 0);
#else
            screen_move_yx(i, 0);
            screen_flush();
            fflush(stdout);
            fprintf(stdout, "\033#5");
            fflush(stdout);
#endif
        }
    }

    werase(stdscr);
}

/**
 * Clear the entire screen by explicitly writing to every cell and then
 * calling refresh().  This is used to restore the screen after a system
 * call.
 */
void screen_really_clear() {
    int i;
    cchar_t ncurses_ch;
    wchar_t wch[2];

    if (curses_initted == Q_FALSE) {
        /* Handle lazy-loading curses. */
        screen_setup(q_rows_arg, q_cols_arg);
    }

    if (has_true_doublewidth() == Q_TRUE) {
        for (i = 0; i < HEIGHT; i++) {
#ifdef Q_PDCURSES
            PDC_set_double(i, 0);
#else
            screen_move_yx(i, 0);
            screen_flush();
            fflush(stdout);
            fprintf(stdout, "\033#5");
            fflush(stdout);
#endif
        }
    }

    wch[0] = ' ';
    wch[1] = 0;
    setcchar(&ncurses_ch, wch, A_NORMAL, 0x1, NULL);
    for (i = 0; i < HEIGHT; i++) {
        mvhline_set(i, 0, &ncurses_ch, WIDTH);
    }
    refresh();
}

/**
 * Write a WINDOW's current dimensions to height and width.
 *
 * @param win the curses WINDOW
 * @param height the location to store the height
 * @param height the location to store the width
 */
void screen_win_get_yx(void * win, int * y, int * x) {
    int screen_x;
    int screen_y;

    if (curses_initted == Q_FALSE) {
        /* Handle lazy-loading curses. */
        screen_setup(q_rows_arg, q_cols_arg);
    }

    getyx((WINDOW *) win, screen_y, screen_x);
    *y = screen_y;
    *x = screen_x;
}

/**
 * Play a short beep.  Note that Linux emulations will use the duration and
 * tone set by the Linux-specific CSI sequence ('man console_codes' to see
 * more).
 */
void screen_beep() {
    struct q_music_struct p;
    static time_t last_beep = 0;
    time_t now;

    if (q_status.beeps == Q_FALSE) {
        /*
         * Don't beep
         */
        return;
    }

    if (curses_initted == Q_FALSE) {
        /* Handle lazy-loading curses. */
        screen_setup(q_rows_arg, q_cols_arg);
    }

    /*
     * Do not beep more than once per second.  Ever.
     */
    time(&now);
    if ((now - last_beep) < 1) {
        return;
    }
    last_beep = now;

    switch (q_status.emulation) {

    case Q_EMUL_TTY:
    case Q_EMUL_DEBUG:
    case Q_EMUL_ANSI:
    case Q_EMUL_AVATAR:
    case Q_EMUL_PETSCII:
    case Q_EMUL_ATASCII:
    case Q_EMUL_VT52:
    case Q_EMUL_VT100:
    case Q_EMUL_VT102:
    case Q_EMUL_VT220:
    case Q_EMUL_XTERM:
    case Q_EMUL_XTERM_UTF8:
        /*
         * Most emulations just beep normally.
         */
        beep();
        break;

    case Q_EMUL_LINUX:
    case Q_EMUL_LINUX_UTF8:
        /*
         * Linux emulation is different:  we have to beep using the
         * correct frequency and duration.
         */
        /*
         * "Play" the beep
         */
        memset(&p, 0, sizeof(struct q_music_struct));
        p.hertz = q_linux_beep_frequency;
        p.duration = q_linux_beep_duration;
        play_music(&p, Q_TRUE);
        break;
    }

}

/**
 * This must be called to initialize the curses UI.  Rows and columns can be
 * passed in, but might not be honored on all systems.
 *
 * @param rows the desired number of rows
 * @param cols the desired number of columns
 * @param setup_color if true, initialize colors
 */
void screen_setup(const unsigned char rows, const unsigned char cols) {
#if defined(Q_PDCURSES) || defined(Q_PDCURSES_WIN32)
#ifdef XCURSES
    /*
     * Setup for X11-based PDCurses
     */
    char * pdcursesOptions[6] = {
        "qodem",
        "-lines",
        "25",
        "-cols",
        "80",
        0
    };
    char rowString[10];
    char colString[10];

    if (curses_initted == Q_TRUE) {
        /* Don't init twice. */
        return;
    }

    if ((rows > 25) && (rows < 250)) {
        sprintf(rowString, "%d", rows);
        pdcursesOptions[2] = rowString;
    }

    if ((cols > 80) && (cols < 250)) {
        sprintf(colString, "%d", cols);
        pdcursesOptions[4] = colString;
    }
    Xinitscr(5, pdcursesOptions);

#else
    /*
     * Setup for Win32-based PDCurses
     */

    if (curses_initted == Q_TRUE) {
        /* Don't init twice. */
        return;
    }

    /*
     * Size limits: 25-250 rows, 80-250 columns.  This is only in the Win32a
     * version.  The user can maximize the window beyond these limits.
     */
    pdc_ttytype[0] = 25;
    pdc_ttytype[1] = (unsigned char) 250;
    pdc_ttytype[2] = 80;
    pdc_ttytype[3] = (unsigned char) 250;
    initscr();

    /*
     * Set to default 80x25 size.
     */
    resize_term(((rows < 250) && (rows >= 25) ? rows : 25),
        ((cols < 250) && (cols >= 80) ? cols : 80));
#endif /* XCURSES */

    /*
     * Additional common setup for PDCurses
     */
    PDC_set_title("qodem " Q_VERSION);

#else
    /*
     * This is the standard ncurses case.
     *
     * Since we use newterm() in initialize_keyboard() to interrogate a bunch
     * of emulation keyboards, we need to use newterm() here also so that we
     * are not mixing the use of initscr() and newterm().
     */

    if (curses_initted == Q_TRUE) {
        /* Don't init twice. */
        return;
    }

    /*
     * Ask ncurses to use extended names.  qodem_win_getch() should work
     * either way, but it would be slightly nicer to use the ncurses API
     * which will be a bit more future-proof rather than custom parse these
     * extended keys.
     */
    use_extended_names(TRUE);

    q_main_screen = newterm(getenv("TERM"), stdout, stdin);
    if (q_main_screen == NULL) {
        /*
         * We had a problem setting up ncurses, bail out right now.
         */
        fprintf(stderr, _("Unable to initialize curses!\n\n"));
        fprintf(stderr, _("Is the TERM environment variable ('%s') correct?\n"),
            getenv("TERM"));
        exit(EXIT_ERROR_CURSES);
    }
    set_term(q_main_screen);
#endif /* Q_PDCURSES */

    getmaxyx(stdscr, HEIGHT, WIDTH);
    /*
     * I remember re-reading the worklog.html and wondering how I managed to
     * get ^Z and ^C passed in.  Here it is: curses call to enable raw mode.
     */
    raw();
    nodelay(stdscr, TRUE);
    q_keyboard_blocks = Q_FALSE;
    noecho();
    nonl();
    intrflush(stdscr, FALSE);
    meta(stdscr, TRUE);
    keypad(stdscr, TRUE);
    start_color();

#ifdef Q_PDCURSES
    /*
     * For the win32a version, putting this last makes it work.  No idea why
     * yet.
     */
    PDC_set_blink(1);
#endif

    curses_initted = Q_TRUE;
}

/**
 * Shut down the curses UI.
 */
void screen_teardown() {

    if (curses_initted == Q_FALSE) {
        /* Don't shutdown twice. */
        return;
    }

    disable_mouse_listener();

    endwin();

    curses_initted = Q_FALSE;
}

/**
 * Clear the line from the current cursor position to the right edge.
 *
 * @param double_width is true, only clear up the WIDTH / 2
 */
void screen_clear_remaining_line(Q_BOOL double_width) {
    int x, y;
    int i;
    int n = WIDTH;

    if (curses_initted == Q_FALSE) {
        /* Handle lazy-loading curses. */
        screen_setup(q_rows_arg, q_cols_arg);
    }

    getyx(stdscr, y, x);
    if (double_width == Q_TRUE) {
        n /= 2;
    }
    if (x < n - 1) {
        for (i = x; i < n; i++) {
            screen_put_char_yx(y, i, ' ', 0,
                               screen_color(Q_COLOR_CONSOLE_BACKGROUND));
        }
    }
    move(y, x);
}

/**
 * Write the screen's current dimensions to height and width.
 *
 * @param height the location to store the height
 * @param height the location to store the width
 */
void screen_get_dimensions(int * height, int * width) {
    int local_height;
    int local_width;

    if (curses_initted == Q_FALSE) {
        /* Handle lazy-loading curses. */
        screen_setup(q_rows_arg, q_cols_arg);
    }

    getmaxyx(stdscr, local_height, local_width);
    *height = local_height;
    *width = local_width;
}

/**
 * Create a new window.
 *
 * @param win the curses WINDOW
 * @param height the desired height
 * @param width the desired width
 * @param top row position for the top-left corner of the new window.  The
 * top-most row on the screen is 0.
 * @param left column position for the top-left corner of the new window.
 * The left-most column on the screen is 0.
 * @return the new WINDOW, or NULL if subwin() failed
 */
static void * screen_win_subwin(void * win, int height, int width, int top,
                                int left) {

    WINDOW * window;

    if (curses_initted == Q_FALSE) {
        /* Handle lazy-loading curses. */
        screen_setup(q_rows_arg, q_cols_arg);
    }

    window = subwin((WINDOW *) win, height, width, top, left);
    if (window != NULL) {
        meta(window, TRUE);
        keypad(window, TRUE);
    }
    return window;
}

/**
 * Create a new window from stdscr.
 *
 * @param height the desired height
 * @param width the desired width
 * @param top row position for the top-left corner of the new window.  The
 * top-most row on the screen is 0.
 * @param left column position for the top-left corner of the new window.
 * The left-most column on the screen is 0.
 * @return the new WINDOW, or NULL if subwin() failed
 */
void * screen_subwin(int height, int width, int top, int left) {
    return screen_win_subwin(stdscr, height, width, top, left);
}

/**
 * Delete a window created by screen_subwin().
 *
 * @param win the curses WINDOW
 */
void screen_delwin(void * win) {
    assert(curses_initted == Q_TRUE);
    assert(win != NULL);
    delwin((WINDOW *) win);
}

/**
 * Draw a box on the screen.  It will have box-drawing characters on the
 * border and use the Q_COLOR_WINDOW and Q_COLOR_WINDOW_BACKGROUND colors.
 *
 * @param left column position for the top-left corner of the box.  The
 * left-most column on the screen is 0.
 * @param top row position for the top-left corner of the box.  The top-most
 * row on the screen is 0.
 * @param right column position for the bottom-right corner of the box.
 * @param bottom row position for the bottom-right corner of the box.
 */
void screen_draw_box(const int left, const int top, const int right,
                     const int bottom) {
    screen_win_draw_box(stdscr, left, top, right, bottom);
}

/**
 * Draw a box inside a curses WINDOW.  It will have box-drawing characters on
 * the border and use the Q_COLOR_WINDOW and Q_COLOR_WINDOW_BACKGROUND
 * colors.
 *
 * @param win the curses WINDOW
 * @param left column position for the top-left corner of the box.  The
 * left-most column on the screen is 0.
 * @param top row position for the top-left corner of the box.  The top-most
 * row on the screen is 0.
 * @param right column position for the bottom-right corner of the box.
 * @param bottom row position for the bottom-right corner of the box.
 */
void screen_win_draw_box(void * window, const int left, const int top,
                         const int right, const int bottom) {
    screen_win_draw_box_color(window, left, top, right, bottom,
                              Q_COLOR_WINDOW_BORDER, Q_COLOR_WINDOW);
}

/**
 * Draw a box inside a curses WINDOW.  It will have box-drawing characters on
 * the border.
 *
 * @param win the curses WINDOW
 * @param left column position for the top-left corner of the box.  The
 * left-most column on the screen is 0.
 * @param top row position for the top-left corner of the box.  The top-most
 * row on the screen is 0.
 * @param right column position for the bottom-right corner of the box.
 * @param bottom row position for the bottom-right corner of the box.
 * @param border a Q_COLOR enum for the border color
 * @param background a Q_COLOR enum for the background color
 */
void screen_win_draw_box_color(void * window, const int left, const int top,
                               const int right, const int bottom,
                               const Q_COLOR border, const Q_COLOR background) {
    int i;
    int window_height;
    int window_length;
    int window_top;
    int window_left;

    if (curses_initted == Q_FALSE) {
        /* Handle lazy-loading curses. */
        screen_setup(q_rows_arg, q_cols_arg);
    }

    window_length = right - left;
    window_height = bottom - top;

    screen_win_put_color_char_yx(window, top, left,
                                 cp437_chars[Q_WINDOW_LEFT_TOP], border);
    screen_win_put_color_char_yx(window, top, left + window_length - 1,
                                 cp437_chars[Q_WINDOW_RIGHT_TOP], border);
    screen_win_put_color_char_yx(window, top + window_height - 1, left,
                                 cp437_chars[Q_WINDOW_LEFT_BOTTOM], border);
    screen_win_put_color_char_yx(window, top + window_height - 1,
                                 left + window_length - 1,
                                 cp437_chars[Q_WINDOW_RIGHT_BOTTOM], border);
    screen_win_put_color_hline_yx(window, top, left + 1,
                                  cp437_chars[Q_WINDOW_TOP], window_length - 2,
                                  border);
    screen_win_put_color_vline_yx(window, top + 1, left,
                                  cp437_chars[Q_WINDOW_SIDE], window_height - 2,
                                  border);
    screen_win_put_color_hline_yx(window, top + window_height - 1, left + 1,
                                  cp437_chars[Q_WINDOW_TOP], window_length - 2,
                                  border);
    screen_win_put_color_vline_yx(window, top + 1, left + window_length - 1,
                                  cp437_chars[Q_WINDOW_SIDE], window_height - 2,
                                  border);

    /*
     * Background
     */
    for (i = 1; i < window_height - 1; i++) {
        screen_win_put_color_hline_yx(window, i + top, 1 + left, ' ',
                                      window_length - 2, background);
    }

    /*
     * Draw a shadow directly on stdscr
     */
    if (window == stdscr) {
        window_top = top;
        window_left = left;
    } else {
        getbegyx((WINDOW *) window, window_top, window_left);
    }

    for (i = 1; i < window_height + 1; i++) {
        mvwchgat(stdscr, window_top + i, window_left + window_length, 2, 0,
                 q_white_color_pair_num, NULL);
    }
    mvwchgat(stdscr, window_top + window_height, window_left + 2, window_length,
             0, q_white_color_pair_num, NULL);

    if ((q_program_state == Q_STATE_CONSOLE) ||
        (q_program_state == Q_STATE_SCROLLBACK)
    ) {
        /*
         * Switch the lines the box is drawn on to normal-width.
         */
        if (has_true_doublewidth() == Q_TRUE) {
            for (i = 0; i <= window_height; i++) {
#ifdef Q_PDCURSES
                PDC_set_double(window_top + i, 0);
#else
                screen_move_yx(window_top + i, 0);
                screen_flush();
                fflush(stdout);
                fprintf(stdout, "\033#5");
                fflush(stdout);
#endif
            }
        }
    }

}

/**
 * Enable listening for mouse events.
 */
void enable_mouse_listener() {
#if defined(__linux) && defined(Q_ENABLE_GPM)
    char * term;

    if (q_gpm_mouse == Q_FALSE) {
        /*
         * If term is "linux", try to connect to GPM.
         */
        term = getenv("TERM");
        if (term != NULL) {
            int rc;
            if (strstr(term, "linux") != NULL) {
                DLOG(("enable_mouse_listener() connect to GPM\n"));
                gpm_connection.eventMask = GPM_MOVE | GPM_DRAG;
                gpm_connection.eventMask |= GPM_DOWN | GPM_UP;
                gpm_connection.defaultMask = 0;
                gpm_connection.minMod = 0;
                gpm_connection.maxMod = 0;

                rc = Gpm_Open(&gpm_connection, 0);
                if (rc != -1) {
                    DLOG(("enable_mouse_listener() Gpm_Open OK\n"));
                    q_gpm_mouse = Q_TRUE;
                    return;
                }
                DLOG(("enable_mouse_listener() Gpm_Open FAILED\n"));
            }
        }

        /*
         * GPM didn't initialize happily, fall through to ncurses.
         */
    }

    if (q_gpm_mouse == Q_TRUE) {
        /*
         * We already have the mouse, bail out.
         */
        return;
    }
#endif

    if (curses_initted == Q_TRUE) {
        DLOG(("enable_mouse_listener() call mousemask()\n"));

        /*
         * ncurses case: ask for KEY_MOUSE events.
         */
        mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
        mouseinterval(0);
    }
}

/**
 * Disable listening for mouse events.
 */
void disable_mouse_listener() {
#if defined(__linux) && defined(Q_ENABLE_GPM)
    if (q_gpm_mouse == Q_TRUE) {
        /*
         * GPM case: stop listening for mouse events.
         */
        DLOG(("disable_mouse_listener() disconnect from GPM\n"));
        Gpm_Close();
        q_gpm_mouse = Q_FALSE;
        return;
    }
#endif

    if (curses_initted == Q_TRUE) {
        DLOG(("disable_mouse_listener() call mousemask()\n"));

        /*
         * ncurses case: turn off KEY_MOUSE events.
         */
        mousemask(0, NULL);
    }
}
