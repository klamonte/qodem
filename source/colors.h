/*
 * colors.h
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

#ifndef __COLORS_H__
#define __COLORS_H__

/* Includes --------------------------------------------------------------- */

#include "scrollback.h"

/*
 * Colors are represented internally in two ways: 1) as one of the Q_COLORS
 * enum values, which is set by colors.cfg, and 2) as a 6-bit number
 * corresponding to a curses PAIR_NUMBER.  For the latter, bits 0-2 are the
 * background color and 3-5 are the foreground; these colors are the
 * Q_COLOR_X defined in input.h.
 *
 * The UI elements (forms, fields, etc) almost always refer to the Q_COLOR
 * kind of color, and use screen drawing functions like
 * screen_win_put_color_X().  The emulations use the PAIR_NUMBER kind of
 * color, figuring out which SGR number corresponds to Q_COLOR_X and then
 * setting q_current_color accordingly.
 *
 * Two of the PAIR_NUMBER indexes get special treatment in several places:
 *
 *   - 0x00 is black-on-black as far as bitmasks are concerned, but in curses
 *     it is white-on-black.
 *
 *   - 0x38 is white-on-black, but q_setup_colors() defines it as
 *     black-on-black.
 *
 * Emulations drawing to the scrollback will always use the "true" bitmask
 * value for an attr_t type color, i.e. 0x00 is black in the scrollback
 * buffer and 0x38 is white-on-black.  There is a little bit of magic in
 * screen.c to remap 0x38 to the "curses white color" and 0x00 to the "curses
 * black color".
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

/**
 * Mask to remove the color attribute from an ncurses attr_t.  This is used
 * by the emulations to change colors without altering other attributes.
 */
#define NO_COLOR_MASK (~Q_A_COLOR)

/**
 * One entry in the colors.cfg list.  Each entry currently has a foreground,
 * background, and boldness flag, but we could easily add other attributes
 * later.
 */
struct q_text_color_struct {
    Q_BOOL bold;
    short fg;
    short bg;
};

/**
 * These are the colors used by the UI elements.
 */
typedef enum Q_COLORS {

    /**
     * Console banner ("You are now in TERMINAL mode").
     */
    Q_COLOR_CONSOLE,

    /**
     * Console normal text.
     */
    Q_COLOR_CONSOLE_TEXT,

    /**
     * Console background.
     */
    Q_COLOR_CONSOLE_BACKGROUND,

    /**
     * Debug emulation sent chars.
     */
    Q_COLOR_DEBUG_ECHO,

    /**
     * Status bar.
     */
    Q_COLOR_STATUS,

    /**
     * Status bar disabled toggle.
     */
    Q_COLOR_STATUS_DISABLED,

    /**
     * Window border.
     */
    Q_COLOR_WINDOW_BORDER,

    /**
     * Window background.
     */
    Q_COLOR_WINDOW,

    /**
     * Menu command help.
     */
    Q_COLOR_MENU_COMMAND,

    /**
     * Menu command - unavailable option.
     */
    Q_COLOR_MENU_COMMAND_UNAVAILABLE,

    /**
     * Menu normal text.
     */
    Q_COLOR_MENU_TEXT,

    /**
     * Highlighted field.
     */
    Q_COLOR_WINDOW_FIELD_HIGHLIGHTED,

    /**
     * Highlighted field text.
     */
    Q_COLOR_WINDOW_FIELD_TEXT_HIGHLIGHTED,

    /**
     * Normal entry.
     */
    Q_COLOR_PHONEBOOK_ENTRY,

    /**
     * Selected entry.
     */
    Q_COLOR_PHONEBOOK_SELECTED,

    /**
     * Selected and tagged entry.
     */
    Q_COLOR_PHONEBOOK_SELECTED_TAGGED,

    /**
     * Tagged entry.
     */
    Q_COLOR_PHONEBOOK_TAGGED,

    /**
     * Text entry boxes in phonebook.
     */
    Q_COLOR_PHONEBOOK_FIELD_TEXT,

    /**
     * Script is running.
     */
    Q_COLOR_SCRIPT_RUNNING,

    /**
     * Script is finished with rc != 0.
     */
    Q_COLOR_SCRIPT_FINISHED,

    /**
     * Script is finished with rc=0.
     */
    Q_COLOR_SCRIPT_FINISHED_OK,

    /**
     * Help text - border.
     */
    Q_COLOR_HELP_BORDER,

    /**
     * Help text - background.
     */
    Q_COLOR_HELP_BACKGROUND,

    /**
     * Help text - bolded text.
     */
    Q_COLOR_HELP_BOLD,

    /**
     * Help text - "See Also" link.
     */
    Q_COLOR_HELP_LINK,

    /**
     * Help text - "See Also" link.
     */
    Q_COLOR_HELP_LINK_SELECTED,

    /**
     * Max color index, not actually used.
     */
    Q_COLOR_MAX
} Q_COLOR;

/* Globals ---------------------------------------------------------------- */

/**
 * Global colormap table.
 */
extern struct q_text_color_struct q_text_colors[Q_COLOR_MAX];

/**
 * Global Alt-I information screen.
 */
extern unsigned char q_info_screen[];

/**
 * The color pair number that is white foreground black background.
 */
extern short q_white_color_pair_num;

/* Functions -------------------------------------------------------------- */

/**
 * This must be called to initialize the colors from the config file.
 */
extern void q_setup_colors();

/**
 * Convert an array of 8-bit attribute/character VGA cells into scrollback
 * lines that can be displayed through curses.
 *
 * @param screen the data array
 * @param length the number of bytes in screen
 * @param output_line a previously-allocated scrollback line to contain the
 * first row of screen data.  Additional lines are allocated and added to the
 * list as needed.
 */
extern void convert_thedraw_screen(const unsigned char * screen,
                                   const int length,
                                   struct q_scrolline_struct * output_line);

/**
 * Convert a curses attr_t into an HTML &lt;font color&gt; tag.  Note that
 * the string returned is a single static buffer, i.e. this is NOT
 * thread-safe.
 *
 * @param attr the curses attribute
 * @return the HTML string
 */
extern char * color_to_html(const attr_t attr);

/**
 * Get the full path to the colors.cfg file.
 *
 * @return the full path to colors.cfg (usually ~/qodem/colors.cfg or My
 * Documents\\qodem\\prefs\\colors.cfg).
 */
extern char * get_colors_filename();

/**
 * Load (or reload) colors from the colors.cfg file.
 */
extern void load_colors();

#ifdef __cplusplus
}
#endif

#endif /* __COLORS_H__ */
