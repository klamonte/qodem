/*
 * scrollback.h
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

#ifndef __SCROLLBACK_H__
#define __SCROLLBACK_H__

/* Includes --------------------------------------------------------------- */

#include <stdio.h>              /* FILE */
#include <stddef.h>             /* wchar_t */
#include "common.h"             /* Q_BOOL */

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

/**
 * The maximum number of characters (horizontal length) of a scrollback line.
 */
#define Q_MAX_LINE_LENGTH 256

/**
 * This struct represents a single line in the scrollback buffer.
 */
struct q_scrolline_struct {

    /**
     * Actual length of line.
     */
    int length;

    /**
     * Color values for each char.
     */
    attr_t colors[Q_MAX_LINE_LENGTH];

    /**
     * Char values of line.
     */
    wchar_t chars[Q_MAX_LINE_LENGTH];

    /**
     * Pointer to next line.
     */
    struct q_scrolline_struct * next;

    /**
     * Pointer to previous line.
     */
    struct q_scrolline_struct * prev;

    /**
     * If true, this line is dirty.
     */
    Q_BOOL dirty;

    /**
     * If true, this is a double-width line.
     */
    Q_BOOL double_width;

    /**
     * Double-height line flag:
     *
     *   0 = single height
     *   1 = top half double height
     *   2 = bottom half double height
     */
    int double_height;

    /**
     * DECSCNM - reverse video.
     */
    Q_BOOL reverse_color;

    /**
     * Color values for each char after a search function.
     */
    attr_t search_colors[Q_MAX_LINE_LENGTH];

    /**
     * If true, render with search_colors.
     */
    Q_BOOL search_match;

};

/* Globals ---------------------------------------------------------------- */

/**
 * The scrollback buffer.
 */
extern struct q_scrolline_struct * q_scrollback_buffer;

/**
 * The last line of the scrollback buffer.
 */
extern struct q_scrolline_struct * q_scrollback_last;

/**
 * The current view position in the scrollback buffer.
 */
extern struct q_scrolline_struct * q_scrollback_position;

/**
 * The current editing position in the scrollback buffer.
 */
extern struct q_scrolline_struct * q_scrollback_current;

/**
 * The maximum size of the scrollback buffer. Default is 20000.
 */
extern int q_scrollback_max;

/**
 * The Find and Find Again search string.
 */
extern wchar_t * q_scrollback_search_string;

/**
 * When true, there is text that was found via the Find and Find Again search
 * function that needs to be highlighted.
 */
extern Q_BOOL q_scrollback_highlight_search_string;

/* Functions -------------------------------------------------------------- */

/**
 * Keyboard handler for the Alt-/ view scrollback state.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void scrollback_keyboard_handler(const int keystroke, const int flags);

/**
 * Draw screen for the Atl-/view scrollback state.
 */
extern void scrollback_refresh();

/**
 * Allocate and append a new line to the end of the scrollback, becoming the
 * new q_scrollback_last.  If we are at q_scrollback_max lines, remove and
 * free the first line at q_scrollback_buffer.
 */
extern void new_scrollback_line();

/**
 * Draw the visible portion of the scrollback buffer to the screen.
 *
 * @param skip_lines adjust by this many lines.  This is used to leave room
 * for split-screen.
 */
extern void render_scrollback(const int skip_lines);

/**
 * Print one character to the scrollback buffer, wrapping if necessary.
 */
extern void print_character(const wchar_t character);

/**
 * Perform the Alt-T dump screen to a file.
 *
 * @param filename the file to write to
 */
extern Q_BOOL screen_dump(const char * filename);

/**
 * Move the cursor up zero or more rows.
 *
 * @param count the number of rows to move
 * @param honor_scroll_region if true, only move within the scrolling region
 */
extern void cursor_up(const int count, const Q_BOOL honor_scroll_region);

/**
 * Move the cursor down zero or more rows.
 *
 * @param count the number of rows to move
 * @param honor_scroll_region if true, only move within the scrolling region
 */
extern void cursor_down(const int count, const Q_BOOL honor_scroll_region);

/**
 * Move the cursor left zero or more rows.
 *
 * @param count the number of columns to move
 * @param honor_scroll_region if true, only move within the scrolling region
 */
extern void cursor_left(const int count, const Q_BOOL honor_scroll_region);

/**
 * Move the cursor right zero or more rows.
 *
 * @param count the number of columns to move
 * @param honor_scroll_region if true, only move within the scrolling region
 */
extern void cursor_right(const int count, const Q_BOOL honor_scroll_region);

/**
 * Move the cursor to a specific position.
 *
 * @param row the row position to move to.  The top-most row is 0.
 * @param col the column position to move to.  The left-most column is 0.
 */
extern void cursor_position(int row, int col);

/**
 * Erase the characters in the current line from the start column (from 0 to
 * WIDTH - 1) to the end column (from 0 to WIDTH - 1), inclusive.
 *
 * @param start the column of the first character to erase
 * @param end the column of the last character to erase
 * @param honor_protected if true then only erase characters that do not have
 * the A_PROTECT attribute set
 */
extern void erase_line(const int start, const int end,
                       const Q_BOOL honor_protected);

/**
 * Replace characters in the current line from the start column (from 0 to
 * WIDTH - 1) to the end column (from 0 to WIDTH - 1), inclusive, with a new
 * character.  The attribute is replaced with either the current drawing
 * color or the terminal background color, depending on emulation.
 *
 * @param start the column of the first character to replace
 * @param end the column of the last character to replace
 * @param honor_protected if true then only replace characters that do not
 * have the A_PROTECT attribute set
 */
extern void fill_line_with_character(const int start, const int end,
                                     const wchar_t character,
                                     const Q_BOOL honor_protected);

/**
 * Erase a rectangular area of the screen.
 *
 * @param start_row the row of the first character to erase
 * @param start_col the column of the first character to erase
 * @param end_row the row of the last character to erase
 * @param end_col the column of the last character to erase
 * @param honor_protected if true then only erase characters that do not have
 * the A_PROTECT attribute set
 */
extern void erase_screen(const int start_row, const int start_col,
                         const int end_row, const int end_col,
                         const Q_BOOL honor_protected);

/**
 * Advance the entire screen to a new "page" and home the cursor.
 */
extern void cursor_formfeed();

/**
 * Advance one line down and set the q_status.cursor_x column to 0.
 */
extern void cursor_carriage_return();

/**
 * Advance one line down and optionally set the q_status.cursor_x column to
 * 0.
 *
 * @param new_line_mode if true, set the column to 0
 */
extern void cursor_linefeed(const Q_BOOL new_line_mode);

/**
 * Scroll the lines inside the scrolling region down 0 or more lines.
 *
 * @param region_top the row of the line to scroll
 * @param region_bottom the row of the last line to scroll
 * @param count the number of rows to move
 */
extern void scrolling_region_scroll_down(const int region_top,
                                         const int region_bottom,
                                         const int count);

/**
 * Scroll the lines inside the scrolling region up 0 or more lines.
 *
 * @param region_top the row of the line to scroll
 * @param region_bottom the row of the last line to scroll
 * @param count the number of rows to move
 */
extern void scrolling_region_scroll_up(const int region_top,
                                       const int region_bottom,
                                       const int count);

/**
 * Scroll a rectangular area of the screen down 0 or more lines.
 *
 * @param top the row of the line to scroll
 * @param left the first column of the line to scroll
 * @param bottom the row of the last line to scroll
 * @param right the last column of the line to scroll
 * @param count the number of rows to move
 */
extern void rectangle_scroll_down(const int top, const int left,
                                  const int bottom, const int right,
                                  const int count);

/**
 * Scroll a rectangular area of the screen up 0 or more lines.
 *
 * @param top the row of the line to scroll
 * @param left the first column of the line to scroll
 * @param bottom the row of the last line to scroll
 * @param right the last column of the line to scroll
 * @param count the number of rows to move
 */
extern void rectangle_scroll_up(const int top, const int left, const int bottom,
                                const int right, const int count);

/**
 * Scroll the entire screen down 0 or more lines.
 *
 * @param count the number of rows to move
 */
extern void scroll_down(const int count);

/**
 * Scroll the entire screen up 0 or more lines.
 *
 * @param count the number of rows to move
 */
extern void scroll_up(const int count);

/**
 * Delete 0 or more characters at the current position, shifting the rest of
 * the line left.
 *
 * @param count number of characters to delete
 */
extern void delete_character(const int count);

/**
 * Insert 0 or more spaces at the current position, shifting the rest of the
 * line right.
 *
 * @param count number of characters to insert
 */
extern void insert_blanks(const int count);

/**
 * Reverse the foreground and background of every character in the visible
 * portion of the scrollback.
 */
extern void invert_scrollback_colors();

/**
 * Reverse the foreground and background of every character in the visible
 * portion of the scrollback.
 */
extern void deinvert_scrollback_colors();

/**
 * Set the double_width flag for the current line.  This will also unset
 * double-height.
 *
 * @param double_width if true, this line will be rendered double-width
 */
extern void set_double_width(Q_BOOL double_width);

/**
 * Set the double_height value for the current line.  This will also set
 * double-width.
 *
 * @param double_height 0, 1, 2
 */
extern void set_double_height(int double_height);

/**
 * Set a number of lines to single-width.
 *
 * @param start_row the first row to set single-width
 * @param end_row the last row to set single-width
 */
extern void set_single_width(const int start_row, const int end_row);

/**
 * Save the visible portion of the scrollback buffer to file for debugging
 * purposes.
 *
 * @param file the file to save to
 */
extern void render_screen_to_debug_file(FILE * file);

/**
 * If true, the physical terminal is capable of displaying double-width
 * characters and qodem is using a trick to do so.
 */
extern Q_BOOL has_true_doublewidth();

#ifdef __cplusplus
}
#endif

#endif /* __SCROLLBACK_H__ */
