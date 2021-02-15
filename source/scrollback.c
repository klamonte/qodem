/*
 * scrollback.c
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
 * This module contains the scrollback buffer handling code.
 *
 * The scrollback buffer is a linked list of struct q_scrolline_struct *'s
 * starting at q_scrollback_buffer.  However, the behavior of that list is a
 * little unusual:
 *
 *   1)  The visible portion of the scollback buffer is q_scrollback_position
 *       and goes BACKWARD to find_top_scrollback_line().
 *
 *   2)  The current cursor position (q_status.cursor_x/y) is on
 *       q_scrollback_current.
 *
 *   3)  The scrollback buffer is wider and taller than the visible screen.
 *
 * Visibly, this looks like:
 *
 *                              | <-------  q_scrolline_struct.length  ------> |
 *
 *                              | <----------  WIDTH  ----------> |
 *
 * q_scrollback_buffer     ---> .---------------------------------.--------... |
 *                              |---------------------------------|--------... |
 *                              |---------------------------------|--------... |
 * find_top_scrollback_line()-> |0********************************|--------... |
 *                              |*********************************|--------... |
 *                              |*********************************|--------... |
 *                              |*********************************|--------... |
 * q_scrollback_current    ---> |******X**************************|--------... |
 *                              |*********************************|--------... |
 *                              |*********************************|--------... |
 * q_scrollback_position   ---> |*********************************|--------... |
 *                    /-------> |%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%|
 *         STATUS_HEIGHT        |%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%|
 *                    \-------> |%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%|
 *                              |---------------------------------|--------... |
 * q_scrollback_last       ---> ^---------------------------------^--------... ^
 *                              ^     ^
 *                              |     |
 *                              |     ^--- "X" is on row=cursor_y, col=cursor_x
 *                              |
 *                              ^--- "0" is on row=0, col=0
 *
 *
 * Why this design?  Mainly because if the window is re-sized in
 * Q_STATE_CONSOLE state I want to make sure the last line is still visible
 * directly above the status line(s).
 *
 * Note that the only time that q_scrollback_last cannot be equal to
 * q_scrollback_position is when state is Q_STATE_SCROLLBACK.
 *
 * I almost want to add a variable origin (0,0) so that the screen could pan
 * left to right.  But I don't know any of any terminals that require that.
 * Still, it would bring new life to "scroll lock".  :)
 */

#include "common.h"

#include <ctype.h>
#ifndef Q_PDCURSES_WIN32
#include <wctype.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include "qodem.h"
#include "screen.h"
#include "forms.h"
#include "options.h"
#include "states.h"
#include "script.h"
#include "console.h"
#include "translate.h"
#include "scrollback.h"

/**
 * The scrollback buffer.
 */
struct q_scrolline_struct * q_scrollback_buffer = NULL;

/**
 * The last line of the scrollback buffer.
 */
struct q_scrolline_struct * q_scrollback_last = NULL;

/**
 * The current editing position in the scrollback buffer.
 */
struct q_scrolline_struct * q_scrollback_current = NULL;

/**
 * The current view position in the scrollback buffer.  This position is the
 * BOTTOM on the screen, so when we render we count from here and go UP until
 * we run out of available rows to render to.
 */
struct q_scrolline_struct * q_scrollback_position = NULL;

/**
 * The maximum size of the scrollback buffer. Default is 20000.
 */
int q_scrollback_max = 20000;

/**
 * The Find and Find Again search string.
 */
wchar_t * q_scrollback_search_string = NULL;

/**
 * When true, there is text that was found via the Find and Find Again search
 * function that needs to be highlighted.
 */
Q_BOOL q_scrollback_highlight_search_string = Q_FALSE;

/**
 * Special flag for VT100 line wrapping.  The first character in the right
 * margin is printed without moving the cursor.  On the next character, the
 * character is placed as the first character on the next line and then the
 * cursor is at the second column.
 */
static Q_BOOL vt100_wrap_line_flag = Q_FALSE;

#ifndef Q_PDCURSES
/**
 * If true, this console can display true double-width characters by
 * inserting VT100 sequences in the ncurses output.
 */
static Q_BOOL xterm = Q_FALSE;
#endif

/**
 * Find the scrollback line that corresponds to the top line of the screen.
 *
 * @return the line that corresponds to the top line of the screen
 */
static struct q_scrolline_struct * find_top_scrollback_line() {
    struct q_scrolline_struct * line;
    int row;

    /*
     * Start at the bottom
     */
    row = HEIGHT - 1;

    /*
     * Skip the status line
     */
    if (q_status.status_visible == Q_TRUE) {
        row -= STATUS_HEIGHT;
    }

    /*
     * Let's assert that row > 0.  Konsole and xterm won't let the window
     * size reach zero so this should be a non-issue.
     */
    assert(row > 0);

    /*
     * Count the lines available
     */
    line = q_scrollback_position;
    while (row >= 0) {
        if (line->prev == NULL) {
            break;
        }
        line = line->prev;
        row--;
    }

    if ((row < 0) && (line->next != NULL)) {
        line = line->next;
    }

    return line;
}

/**
 * Initialize a new line for the scrollback buffer.  The line is inserted
 * before insert_point.
 *
 * @param insert_point the line that will follow the new line
 */
static void insert_scrollback_line(struct q_scrolline_struct * insert_point) {
    struct q_scrolline_struct * new_line;
    int i;

    assert(insert_point != NULL);

    new_line =
        (struct q_scrolline_struct *) Xmalloc(sizeof(struct q_scrolline_struct),
                                              __FILE__, __LINE__);
    memset(new_line, 0, sizeof(struct q_scrolline_struct));
    for (i = 0; i < Q_MAX_LINE_LENGTH; i++) {
        new_line->chars[i] = ' ';
    }
    new_line->dirty = Q_TRUE;
    new_line->reverse_color = Q_FALSE;
    /*
     * All new lines are always single-width / single-height
     */
    new_line->double_width = Q_FALSE;
    new_line->double_height = 0;

    if (q_status.reverse_video == Q_TRUE) {
        new_line->reverse_color = Q_TRUE;
        for (i = 0; i < Q_MAX_LINE_LENGTH; i++) {
            new_line->colors[i] = scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);
        }
        /*
         * Increase width and pad with spaces so that inverse video appears
         * everwhere.
         */
        new_line->length = WIDTH;
    }

    if (q_scrollback_buffer == NULL) {
        q_scrollback_buffer = new_line;
        q_scrollback_position = new_line;
        q_scrollback_last = new_line;
        q_scrollback_current = new_line;
    } else {
        new_line->prev = insert_point->prev;
        new_line->next = insert_point;
        insert_point->prev = new_line;
        /*
         * ASCII downloads and the console itself both update the scrollback
         * and need to render the new line.
         */
        if ((q_program_state == Q_STATE_CONSOLE) ||
            (q_program_state == Q_STATE_SCRIPT_EXECUTE) ||
            (q_program_state == Q_STATE_HOST) ||
            (q_program_state == Q_STATE_DIALER) ||
            (q_program_state == Q_STATE_DOWNLOAD)
        ) {
            q_scrollback_position = q_scrollback_position->prev;
        }
    }

    if (((q_scrollback_max > 0)
            && (q_status.scrollback_lines >= q_scrollback_max)) ||
        ((q_status.scrollback_enabled == Q_FALSE)
            && (q_in_handle_resize == Q_FALSE)
            && (q_status.scrollback_lines > HEIGHT - STATUS_HEIGHT - 1))
        ) {

        /*
         * Roll the bottom line off the buffer.
         */
        new_line = q_scrollback_last;
        q_scrollback_last = new_line->prev;
        q_scrollback_last->next = NULL;
        Xfree(new_line, __FILE__, __LINE__);

        if (q_scrollback_position == new_line) {
            q_scrollback_position = q_scrollback_position->prev;
        }
    } else {
        q_status.scrollback_lines++;
    }
}

/**
 * Allocate and append a new line to the end of the scrollback, becoming the
 * new q_scrollback_last.  If we are at q_scrollback_max lines, remove and
 * free the first line at q_scrollback_buffer.
 */
void new_scrollback_line() {
    struct q_scrolline_struct * new_line = NULL;
    struct q_scrolline_struct * top_line = NULL;
    int i;

    new_line =
        (struct q_scrolline_struct *) Xmalloc(sizeof(struct q_scrolline_struct),
                                              __FILE__, __LINE__);
    memset(new_line, 0, sizeof(struct q_scrolline_struct));
    for (i = 0; i < Q_MAX_LINE_LENGTH; i++) {
        new_line->chars[i] = ' ';
    }
    new_line->dirty = Q_TRUE;
    new_line->reverse_color = Q_FALSE;
    /*
     * All new lines are always single-width / single-height
     */
    new_line->double_width = Q_FALSE;
    new_line->double_height = 0;

    if (q_status.reverse_video == Q_TRUE) {
        new_line->reverse_color = Q_TRUE;
        for (i = 0; i < Q_MAX_LINE_LENGTH; i++) {
            new_line->colors[i] = scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);
        }
        /*
         * Increase width and pad with spaces so that inverse video appears
         * everwhere.
         */
        new_line->length = WIDTH;
    }

    if (q_status.emulation == Q_EMUL_DEBUG) {
        /*
         * DEBUG emulation plays tricks with the scrollback buffer.  If I
         * don't explicitly set the color the cursor will disappear.
         */
        for (i = 0; i < Q_MAX_LINE_LENGTH; i++) {
            new_line->colors[i] =
                Q_A_REVERSE | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);
        }
    }

    if (q_scrollback_buffer == NULL) {
        q_scrollback_buffer = new_line;
        q_scrollback_position = new_line;
        q_scrollback_last = new_line;
        q_scrollback_current = new_line;
    } else {
        top_line = find_top_scrollback_line();

        new_line->prev = q_scrollback_last;
        q_scrollback_last->next = new_line;
        q_scrollback_last = new_line;
        /*
         * ASCII downloads and the console itself both update the scrollback
         * and need to render the new line.
         *
         * Also, debug_reset() needs to render its new lines.
         */
        if ((q_program_state == Q_STATE_CONSOLE) ||
            (q_program_state == Q_STATE_DOWNLOAD) ||
            (q_program_state == Q_STATE_SCRIPT_EXECUTE) ||
            (q_program_state == Q_STATE_HOST) ||
            (q_program_state == Q_STATE_DIALER) ||
            (q_program_state == Q_STATE_EMULATION_MENU)
        ) {
            q_scrollback_position = new_line;
        }
    }

    if (((q_scrollback_max > 0)
            && (q_status.scrollback_lines >= q_scrollback_max)) ||
        ((q_status.scrollback_enabled == Q_FALSE)
            && (q_in_handle_resize == Q_FALSE)
            && (q_status.scrollback_lines > HEIGHT - STATUS_HEIGHT - 1))
    ) {

        if (q_status.scrollback_enabled == Q_TRUE) {
            /*
             * Roll the top line off the buffer.
             */
            new_line = q_scrollback_buffer;
            q_scrollback_buffer = new_line->next;
            q_scrollback_buffer->prev = NULL;
            Xfree(new_line, __FILE__, __LINE__);
        } else {
            /*
             * Roll the top line in the visible area off the buffer.
             */
            top_line->next->prev = top_line->prev;
            if (top_line->prev != NULL) {
                top_line->prev->next = top_line->next;
            }
            Xfree(top_line, __FILE__, __LINE__);

        }

    } else {
        q_status.scrollback_lines++;
    }
}

/**
 * The code to wrap a line.  It has two different places it might be called,
 * so now I've got to separate it into a function.
 */
static void wrap_current_line() {
    /*
     * Wrap the line
     */
    if (q_scrollback_current->next == NULL) {
        new_scrollback_line();
    }
    q_scrollback_current = q_scrollback_current->next;
    if (q_status.cursor_y < HEIGHT - STATUS_HEIGHT - 1) {
        q_status.cursor_y++;
    }
    q_status.cursor_x = 0;

    /*
     * Capture
     */
    if (q_status.capture == Q_TRUE) {
        if (q_status.capture_type == Q_CAPTURE_TYPE_HTML) {
            /*
             * HTML
             */
            fprintf(q_status.capture_file, "\n");
        } else if (q_status.capture_type == Q_CAPTURE_TYPE_NORMAL) {
            /*
             * Normal
             */
            fprintf(q_status.capture_file, "\n");
        }
        fflush(q_status.capture_file);
        q_status.capture_flush_time = time(NULL);
        q_status.capture_x = 0;
    }
}

/**
 * Print one character to the scrollback buffer, wrapping if necessary.
 */
void print_character(const wchar_t character) {
#ifdef Q_PDCURSES
    static attr_t old_color = (attr_t) 0xdeadbeef;
#else
    static attr_t old_color = 0xdeadbeef;
#endif
    Q_BOOL color_changed = Q_FALSE;
    int right_margin = WIDTH - 1;
    Q_BOOL wrap_the_line = Q_FALSE;
    int i;
    /*
     * I want a const character in the API, but it's convenient for flow
     * control to change character.
     */
    wchar_t character2 = character;

    if (q_scrollback_current->length < q_status.cursor_x) {
        for (i = q_scrollback_current->length; i < q_status.cursor_x; i++) {
            q_scrollback_current->chars[i] = ' ';
            q_scrollback_current->colors[i] =
                scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);
            q_scrollback_current->length = q_status.cursor_x;
        }
    }

    /*
     * Initialize old_color
     */
    if (old_color == 0xdeadbeef) {
        old_color = q_current_color;
        color_changed = Q_FALSE;
    }

    /*
     * BEL
     */
    if (character2 == 0x07) {
        screen_beep();
        return;
    }

    /*
     * NUL
     */
    if (character2 == 0x00) {
        if (q_status.display_null == Q_TRUE) {
            character2 = ' ';
        } else {
            return;
        }
    }

    /*
     * A character will be printed, mark the line dirty
     */
    q_scrollback_current->dirty = Q_TRUE;

    /*
     * Pass the character to a script if we're running one
     */
    if (q_program_state == Q_STATE_SCRIPT_EXECUTE) {
        script_print_character(character2);
    }
    if (q_status.quicklearn == Q_TRUE) {
        quicklearn_print_character(character2);
    }

    /*
     * This isn't the prettiest logic, but whatever
     */
    switch (q_status.emulation) {
    case Q_EMUL_ANSI:
    case Q_EMUL_AVATAR:
    case Q_EMUL_TTY:
        /*
         * BBS-ish emulations:  check the assume_80_columns flag
         */
        if (q_status.assume_80_columns == Q_TRUE) {
            right_margin = 79;
        }
        break;
    case Q_EMUL_ATASCII:
        /*
         * ATASCII is always 40 columns on screen.  But these might be
         * double-width lines, so the visible right margin is 80 columns.
         */
        if (q_status.atascii_has_wide_font == Q_FALSE) {
            /*
             * We think we are running with a narrow font, so will need to
             * use double-width characters.
             */
            right_margin = 79;
        } else {
            /*
             * We already have a wide font, so restrict to 40 columns.
             */
            right_margin = 39;
        }
        break;
    case Q_EMUL_PETSCII:
        /*
         * PETSCII is always 40 columns on screen.  But these might be
         * double-width lines, so the visible right margin is 80 columns.
         */
        if (q_status.petscii_has_wide_font == Q_FALSE) {
            /*
             * We think we are running with a narrow font, so will need to
             * use double-width characters.
             */
            right_margin = 79;
        } else {
            /*
             * We already have a wide font, so restrict to 40 columns.
             */
            right_margin = 39;
        }
        break;
    default:
        /*
         * VT100-ish emulations:  check the actual right margin value
         */
        if (q_emulation_right_margin > 0) {
            right_margin = q_emulation_right_margin;
        }
        break;
    }
    if (q_scrollback_current->double_width == Q_TRUE) {
        right_margin = ((right_margin + 1) / 2) - 1;
    }

    /*
     * Check the unusually-complicated line wrapping conditions...
     */
    if (q_status.cursor_x == right_margin) {

        /*
         * This case happens when: the cursor was already on the right margin
         * (either through printing or by an explicit placement command), and
         * a character was printed.
         */

        if ((q_status.emulation == Q_EMUL_VT100) ||
            (q_status.emulation == Q_EMUL_VT102) ||
            (q_status.emulation == Q_EMUL_VT220) ||
            (q_status.emulation == Q_EMUL_LINUX) ||
            (q_status.emulation == Q_EMUL_LINUX_UTF8) ||
            (q_status.emulation == Q_EMUL_XTERM) ||
            (q_status.emulation == Q_EMUL_XTERM_UTF8)
        ) {
            /*
             * Special case for VT100: the line wraps only when a new
             * character arrives AND the cursor is already on the right
             * margin AND has placed a character in its cell.  Easier to see
             * than to explain.
             */

            if (q_status.line_wrap == Q_TRUE) {

                if (vt100_wrap_line_flag == Q_FALSE) {

                    /*
                     * This block marks the case that we are in the margin
                     * and the first character has been received and printed.
                     */
                    vt100_wrap_line_flag = Q_TRUE;

                } else {
                    /*
                     * This block marks the case that we are in the margin
                     * and the second character has been received and
                     * printed.
                     */
                    vt100_wrap_line_flag = Q_FALSE;
                    wrap_the_line = Q_FALSE;

                    wrap_current_line();
                }
            } else {
                /*
                 * Line wrap is OFF so do not wrap the line.
                 */
                wrap_the_line = Q_FALSE;
            }

        } else {
            /*
             * We are NOT in VT100 emulation...
             */

            if (q_status.line_wrap == Q_TRUE) {
                /*
                 * Regular NON-VT100 case.  The cursor moves to the first
                 * column of the next line as soon as the character has been
                 * printed in the right margin column.
                 */

                wrap_the_line = Q_TRUE;
            } else {
                /*
                 * Line wrap is OFF so do not wrap the line.
                 *
                 * Also, Keep the cursor where it is on the right margin.
                 */
                wrap_the_line = Q_FALSE;

            }

        } /* if (q_status.emulation == Q_EMUL_VT100) */

    } else if (q_status.cursor_x <= right_margin) {
        /*
         * This is the normal case: a character came in and was printed to
         * the left of the right margin column.
         */
        wrap_the_line = Q_FALSE;

        /*
         * Turn off VT100 special-case flag
         */
        vt100_wrap_line_flag = Q_FALSE;
    }

    /*
     * Print the character
     */
    if (q_status.cursor_x < q_scrollback_current->length) {

        /*
         * Insert mode special case
         */
        if (q_status.insert_mode == Q_TRUE) {
            memmove(&q_scrollback_current->chars[q_status.cursor_x + 1],
                    &q_scrollback_current->chars[q_status.cursor_x],
                    sizeof(wchar_t) * (Q_MAX_LINE_LENGTH - q_status.cursor_x -
                                       1));

            memmove(&q_scrollback_current->colors[q_status.cursor_x + 1],
                    &q_scrollback_current->colors[q_status.cursor_x],
                    sizeof(attr_t) * (Q_MAX_LINE_LENGTH - q_status.cursor_x -
                                      1));

            q_scrollback_current->chars[q_status.cursor_x] = character2;
            q_scrollback_current->colors[q_status.cursor_x] = q_current_color;
            if (q_scrollback_current->length < Q_MAX_LINE_LENGTH) {
                q_scrollback_current->length++;
            }
        } else {
            /*
             * Replace an existing character
             */
            q_scrollback_current->colors[q_status.cursor_x] = q_current_color;
            q_scrollback_current->chars[q_status.cursor_x] = character2;
        }
    } else {
        /*
         * New character on the line
         */
        q_scrollback_current->colors[q_scrollback_current->length] =
            q_current_color;
        q_scrollback_current->chars[q_scrollback_current->length] = character2;
        q_scrollback_current->length++;
    }

    /*
     * Check the color
     */
    if (q_current_color != old_color) {
        color_changed = Q_TRUE;
        old_color = q_current_color;
    }

    /*
     * Capture
     */
    if (q_status.capture == Q_TRUE) {
        /*
         * Insert spaces to line up capture
         */
        if (q_status.cursor_x > q_status.capture_x) {
            for (i = 0; i < q_status.cursor_x - q_status.capture_x; i++) {
                if (q_status.capture_type == Q_CAPTURE_TYPE_HTML) {
                    fprintf(q_status.capture_file, "&nbsp;");
                } else {
                    fprintf(q_status.capture_file, " ");
                }
                if ((q_scrollback_current->double_width == Q_TRUE) &&
                    (q_status.emulation != Q_EMUL_PETSCII) &&
                    (q_status.emulation != Q_EMUL_ATASCII)
                ) {
                    fprintf(q_status.capture_file, " ");
                }
            }
            q_status.capture_x = q_status.cursor_x;
        }

        if (q_status.capture_type == Q_CAPTURE_TYPE_HTML) {
            /*
             * HTML
             */
            if (color_changed == Q_TRUE) {
                fprintf(q_status.capture_file, "</font><font %s>",
                        color_to_html(q_current_color));
            }
            if (character2 == ' ') {
                fprintf(q_status.capture_file, "&nbsp;");
            } else if (character2 == '<') {
                fprintf(q_status.capture_file, "&lt;");
            } else if (character2 == '>') {
                fprintf(q_status.capture_file, "&gt;");
            } else if (character2 < 0x7F) {
                fprintf(q_status.capture_file, "%c", (int) character2);
            } else {
                fprintf(q_status.capture_file, "&#%d;", (int) character2);
            }
        } else if (q_status.capture_type == Q_CAPTURE_TYPE_NORMAL) {
            /*
             * Normal
             */
            fprintf(q_status.capture_file, "%lc", (wint_t) character2);
        }
        q_status.capture_x++;

        /*
         * Double-width
         */
        if ((q_scrollback_current->double_width == Q_TRUE) &&
            (q_status.emulation != Q_EMUL_PETSCII) &&
            (q_status.emulation != Q_EMUL_ATASCII)
        ) {
            fprintf(q_status.capture_file, " ");
            q_status.capture_x++;
        }

        /*
         * Flush if we haven't in a while
         */
        if (q_status.capture_flush_time < time(NULL)) {
            fflush(q_status.capture_file);
            q_status.capture_flush_time = time(NULL);
        }

    }

    /*
     * Increment horizontal
     */
    if (vt100_wrap_line_flag == Q_FALSE) {
        q_status.cursor_x++;

        /*
         * Use the right margin instead of WIDTH
         */
        if (q_status.cursor_x > right_margin) {
            q_status.cursor_x--;
        }
    }

    /*
     * Wrap if necessary
     */
    if (wrap_the_line == Q_TRUE) {
        /*
         * NON-VT100 "normal" case: add a character and then wrap the line.
         */
        wrap_current_line();
    } /* if (wrap_the_line == Q_TRUE) */
}

/**
 * Clear all the lines in the scrollback.
 */
static void clear_scrollback() {
    struct q_scrolline_struct * line;
    struct q_scrolline_struct * line_next;
    struct q_scrolline_struct * top;

    top = find_top_scrollback_line();

    line = q_scrollback_buffer;
    while (line != top) {
        line_next = line->next;
        Xfree(line, __FILE__, __LINE__);
        q_status.scrollback_lines--;
        line = line_next;
    }

    /*
     * Mark this the head.
     */
    top->prev = NULL;
    q_scrollback_buffer = top;

}

/**
 * Save one line of the visible scrollback to file, including HTML or NORMAL
 * mode.
 *
 * @param file the file to save to
 * @param line the line to save
 * @param save_type either HTML or NORMAL
 * @param last_color the last color written (cached for cleanliness)
 */
static void save_scrollback_line(FILE * file, struct q_scrolline_struct * line,
                                 Q_CAPTURE_TYPE save_type,
                                 attr_t * last_color) {
    int i;
    wchar_t ch;
    Q_BOOL color_changed = Q_FALSE;

    assert(q_status.read_only == Q_FALSE);

    for (i = 0; i < WIDTH; i++) {
        /*
         * Break out at the end of the screen
         */
        if (line->double_width == Q_TRUE) {
            if ((2 * i) >= WIDTH) {
                break;
            }
        } else {
            if (i >= WIDTH) {
                break;
            }
        }
        if (i >= line->length) {
            ch = ' ';
            if (*last_color !=
                (Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT))) {
                *last_color =
                    Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);
                color_changed = Q_TRUE;
            }
        } else {
            ch = line->chars[i];
            if (line->colors[i] != *last_color) {
                *last_color = line->colors[i];
                color_changed = Q_TRUE;
            }
        }

        if (save_type == Q_CAPTURE_TYPE_HTML) {
            /*
             * HTML
             */
            if (color_changed == Q_TRUE) {
                fprintf(file, "</font><font %s>", color_to_html(*last_color));
                color_changed = Q_FALSE;
            }
            if (ch == ' ') {
                fprintf(file, "&nbsp;");
            } else if (ch == '<') {
                fprintf(file, "&lt;");
            } else if (ch == '>') {
                fprintf(file, "&gt;");
            } else if (ch < 0x7F) {
                fprintf(file, "%c", (int) ch);
            } else {
                fprintf(file, "&#%d;", (int) ch);
            }
        } else if (save_type == Q_CAPTURE_TYPE_NORMAL) {
            /*
             * Normal
             */
            fprintf(file, "%lc", (wint_t) ch);
        }
        if ((line->double_width == Q_TRUE) &&
            (q_status.emulation != Q_EMUL_PETSCII) &&
            (q_status.emulation != Q_EMUL_ATASCII)
        ) {
            if (save_type == Q_CAPTURE_TYPE_HTML) {
                fprintf(file, "&nbsp;");
            } else {
                fprintf(file, " ");
            }
        }

    }                           /* for (i = 0; i < WIDTH; i++) */
    fprintf(file, "\n");
}

/**
 * Save the scrollback to a file.
 *
 * @param filename the file to save to
 * @param visible_only if true, only save the part that is on the screen
 */
static Q_BOOL save_scrollback(const char * filename,
                              const Q_BOOL visible_only) {

    struct q_scrolline_struct * line;
    FILE * file;
    time_t current_time;
    char time_string[TIME_STRING_LENGTH];
    char * new_filename = NULL;
    char notify_message[DIALOG_MESSAGE_SIZE];
    attr_t color = Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);
    int row;

    assert(q_status.read_only == Q_FALSE);

    file = open_workingdir_file(filename, &new_filename);
    if (file == NULL) {
        snprintf(notify_message, sizeof(notify_message),
                 _("Error opening file \"%s\" for writing: %s"), new_filename,
                 strerror(errno));
        notify_form(notify_message, 0);
        if (new_filename != NULL) {
            Xfree(new_filename, __FILE__, __LINE__);
        }
        return Q_FALSE;
    }
    time(&current_time);

    if (q_status.scrollback_save_type == Q_CAPTURE_TYPE_HTML) {
        /*
         * HTML
         */
        strftime(time_string, sizeof(time_string),
                 _("Saved Scrollback Generated %a, %d %b %Y %H:%M:%S %z"),
                 localtime(&current_time));
        fprintf(file, "<html>\n\n");
        fprintf(file, "<!-- * - * Qodem " Q_VERSION " %s BEGIN * - * --> \n\n",
                time_string);
        fprintf(file,
                "<body bgcolor=\"black\">\n<pre {font-family: 'Courier New', monospace;}><code><font %s>",
                color_to_html(q_current_color));
    } else {
        strftime(time_string, sizeof(time_string),
                 _("Saved Scrollback Generated %a, %d %b %Y %H:%M:%S %z"),
                 localtime(&current_time));
        fprintf(file, "* - * Qodem " Q_VERSION " %s BEGIN * - *\n\n",
                time_string);
    }

    if (visible_only == Q_TRUE) {
        /*
         * Save what is visible to file
         */
        line = q_scrollback_position;
        row = HEIGHT - STATUS_HEIGHT - 1;
        while ((row > 0) && (line->prev != NULL)) {
            line = line->prev;
            row--;
        }
        while ((row < HEIGHT - STATUS_HEIGHT) && (line != NULL)) {
            save_scrollback_line(file, line, q_status.scrollback_save_type,
                                 &color);
            line = line->next;
            row++;
        }
    } else {
        /*
         * Save everything to file
         */
        for (line = q_scrollback_buffer; line != NULL; line = line->next) {
            save_scrollback_line(file, line, q_status.scrollback_save_type,
                                 &color);
        }
    }

    if (q_status.scrollback_save_type == Q_CAPTURE_TYPE_HTML) {
        /*
         * HTML
         */
        fprintf(file, "</code></pre></font>\n</body>\n");
        strftime(time_string, sizeof(time_string),
                 _("Saved Scrollback Generated %a, %d %b %Y %H:%M:%S %z"),
                 localtime(&current_time));
        fprintf(file, "\n<!-- * - * Qodem " Q_VERSION " %s END * - * -->\n",
                time_string);
        fprintf(file, "\n</html>\n");
    } else {
        strftime(time_string, sizeof(time_string),
                 _("Saved Scrollback Generated %a, %d %b %Y %H:%M:%S %z"),
                 localtime(&current_time));
        fprintf(file, "\n* - * Qodem " Q_VERSION " %s END * - *\n",
                time_string);
    }

    fclose(file);
    if (new_filename != filename) {
        Xfree(new_filename, __FILE__, __LINE__);
    }
    return Q_TRUE;
}

/**
 * Perform the Alt-T dump screen to a file.
 *
 * @param filename the file to write to
 */
Q_BOOL screen_dump(const char * filename) {
    struct q_scrolline_struct * line;
    FILE * file;
    time_t current_time;
    char time_string[TIME_STRING_LENGTH];
    char * new_filename = NULL;
    char notify_message[DIALOG_MESSAGE_SIZE];
    attr_t color = Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);

    file = open_workingdir_file(filename, &new_filename);
    if (file == NULL) {
        snprintf(notify_message, sizeof(notify_message),
                 _("Error opening file \"%s\" for writing: %s"), new_filename,
                 strerror(errno));
        notify_form(notify_message, 0);
        if (new_filename != NULL) {
            Xfree(new_filename, __FILE__, __LINE__);
        }
        return Q_FALSE;
    }
    time(&current_time);

    if (q_status.screen_dump_type == Q_CAPTURE_TYPE_HTML) {
        /*
         * HTML
         */
        strftime(time_string, sizeof(time_string),
                 _("Screen Dump Generated %a, %d %b %Y %H:%M:%S %z"),
                 localtime(&current_time));
        fprintf(file, "<html>\n\n");
        fprintf(file, "<!-- * - * Qodem " Q_VERSION " %s BEGIN * - * --> \n\n",
                time_string);
        fprintf(file,
                "<body bgcolor=\"black\">\n<pre {font-family: 'Courier New', monospace;}><code><font %s>",
                color_to_html(q_current_color));
    } else {
        strftime(time_string, sizeof(time_string),
                 _("Screen Dump Generated %a, %d %b %Y %H:%M:%S %z"),
                 localtime(&current_time));
        fprintf(file, "* - * Qodem " Q_VERSION " %s BEGIN * - *\n\n",
                time_string);
    }

    line = find_top_scrollback_line();

    /*
     * Now loop from line onward
     */
    while (line != NULL) {
        save_scrollback_line(file, line, q_status.screen_dump_type, &color);
        line = line->next;
    }

    if (q_status.screen_dump_type == Q_CAPTURE_TYPE_HTML) {
        /*
         * HTML
         */
        fprintf(file, "</code></pre></font>\n</body>\n");
        strftime(time_string, sizeof(time_string),
                 _("Screen Dump Generated %a, %d %b %Y %H:%M:%S %z"),
                 localtime(&current_time));
        fprintf(file, "\n<!-- * - * Qodem " Q_VERSION " %s END * - * -->\n",
                time_string);
        fprintf(file, "\n</html>\n");
    } else {
        strftime(time_string, sizeof(time_string),
                 _("Screen Dump Generated %a, %d %b %Y %H:%M:%S %z"),
                 localtime(&current_time));
        fprintf(file, "\n* - * Qodem " Q_VERSION " %s END * - *\n",
                time_string);
    }

    fclose(file);
    if (new_filename != filename) {
        Xfree(new_filename, __FILE__, __LINE__);
    }
    return Q_TRUE;
}

/**
 * Keyboard handler for the Alt-/ view scrollback state.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
void scrollback_keyboard_handler(const int keystroke, const int flags) {
    struct q_scrolline_struct * line = NULL;
    static struct q_scrolline_struct * last_line = NULL;
    static struct q_scrolline_struct * last_position = NULL;
    unsigned int i, j;
    unsigned int row;
    unsigned int local_height;
    char * filename;
    char notify_message[DIALOG_MESSAGE_SIZE];
    wchar_t * lower_line = NULL;
    wchar_t * begin;
    Q_BOOL find_found = Q_FALSE;

    local_height = HEIGHT - STATUS_HEIGHT - 2;

    switch (keystroke) {

    case 'f':
    case 'F':
        /*
         * Find Text
         */
        if (q_scrollback_search_string != NULL) {
            Xfree(q_scrollback_search_string, __FILE__, __LINE__);
            q_scrollback_search_string = NULL;
            q_scrollback_highlight_search_string = Q_FALSE;
        }
        q_cursor_on();
        q_scrollback_search_string = pick_find_string();
        q_cursor_off();
        if (q_scrollback_search_string == NULL) {
            break;
        }
        /*
         * Force lowercase
         */
        for (i = 0; i < wcslen(q_scrollback_search_string); i++) {
            q_scrollback_search_string[i] =
                towlower(q_scrollback_search_string[i]);
        }
        /*
         * Search for the first matching line
         */
        line = q_scrollback_buffer;
        while (line != NULL) {
            if (line->chars[Q_MAX_LINE_LENGTH - 1] != 0) {
                line->chars[Q_MAX_LINE_LENGTH - 1] = 0;
            }
            lower_line = Xwcsdup(line->chars, __FILE__, __LINE__);
            /*
             * Force lowercase
             */
            for (i = 0; i < wcslen(lower_line); i++) {
                lower_line[i] = towlower(lower_line[i]);
            }
            if (wcsstr(lower_line, q_scrollback_search_string) != NULL) {
                /*
                 * Found, highlight it
                 */
                line->search_match = Q_TRUE;
                memcpy(line->search_colors, line->colors, sizeof(line->colors));
                begin = wcsstr(lower_line, q_scrollback_search_string);
                while (begin != NULL) {
                    for (i = 0; i < wcslen(q_scrollback_search_string); i++) {
                        line->search_colors[i + (begin - lower_line)] |=
                            Q_A_BLINK | Q_A_REVERSE;
                    }
                    begin = wcsstr(begin + 1, q_scrollback_search_string);
                }
                find_found = Q_TRUE;
            } else {
                /*
                 * Not found
                 */
                line->search_match = Q_FALSE;
            }

            Xfree(lower_line, __FILE__, __LINE__);
            line = line->next;
        }

        /*
         * Text not found
         */
        if (find_found == Q_FALSE) {
            notify_form(_("Text not found"), 1.5);
            break;
        } else {
            /*
             * Put the first line that matches at the top of the screen
             */
            line = q_scrollback_buffer;
            while (line != NULL) {
                if (line->search_match == Q_TRUE) {
                    break;
                }
                line = line->next;
            }
            /*
             * We claimed to match, there had better be one
             */
            assert(line != NULL);

            q_scrollback_position = line;
            for (row = 0; row < HEIGHT - STATUS_HEIGHT - 2; row++) {
                if (q_scrollback_position->next == NULL) {
                    break;
                }
                q_scrollback_position = q_scrollback_position->next;
            }
            q_scrollback_highlight_search_string = Q_TRUE;
        }

        /*
         * No leak
         */
        Xfree(q_scrollback_search_string, __FILE__, __LINE__);
        q_scrollback_search_string = NULL;
        break;

    case 'a':
    case 'A':
        /*
         * Find Again
         */
        if (q_scrollback_search_string == NULL) {
            /*
             * Reset my state variables
             */
            q_scrollback_highlight_search_string = Q_FALSE;
            last_line = NULL;
            last_position = NULL;

            /*
             * If this is the first search (even though it's "Find Again", go
             * ahead and pop up the find dialog.
             */
            q_cursor_on();
            q_scrollback_search_string = pick_find_string();
            q_cursor_off();
            if (q_scrollback_search_string == NULL) {
                last_line = NULL;
                break;
            }

            /*
             * Force lowercase
             */
            for (i = 0; i < wcslen(q_scrollback_search_string); i++) {
                q_scrollback_search_string[i] =
                    towlower(q_scrollback_search_string[i]);
            }
            /*
             * Search for the first matching line
             */
            line = q_scrollback_buffer;
            while (line != NULL) {
                if (line->chars[Q_MAX_LINE_LENGTH - 1] != 0) {
                    line->chars[Q_MAX_LINE_LENGTH - 1] = 0;
                }
                lower_line = Xwcsdup(line->chars, __FILE__, __LINE__);
                /*
                 * Force lowercase
                 */
                for (i = 0; i < wcslen(lower_line); i++) {
                    lower_line[i] = towlower(lower_line[i]);
                }
                if (wcsstr(lower_line, q_scrollback_search_string) != NULL) {
                    /*
                     * Found, highlight it
                     */
                    line->search_match = Q_TRUE;
                    memcpy(line->search_colors, line->colors,
                           sizeof(line->colors));
                    begin = wcsstr(lower_line, q_scrollback_search_string);
                    while (begin != NULL) {
                        for (i = 0; i < wcslen(q_scrollback_search_string);
                             i++) {

                            line->search_colors[i + (begin - lower_line)] |=
                                Q_A_BLINK | Q_A_REVERSE;
                        }
                        begin = wcsstr(begin + 1, q_scrollback_search_string);
                    }
                    find_found = Q_TRUE;
                } else {
                    /*
                     * Not found
                     */
                    line->search_match = Q_FALSE;
                }

                Xfree(lower_line, __FILE__, __LINE__);
                line = line->next;
            }

            /*
             * Text not found
             */
            if (find_found == Q_FALSE) {
                notify_form(_("Text not found"), 1.5);
                Xfree(q_scrollback_search_string, __FILE__, __LINE__);
                q_scrollback_search_string = NULL;
                break;
            } else {
                /*
                 * It was found, search from the beginning.
                 */
                last_line = q_scrollback_buffer;
                line = last_line;
            }

        } else {

            /*
             * Search from here out
             */
            line = last_line;
            assert(line != NULL);

            /*
             * Advance to the next line for displaying
             */
            if (line != q_scrollback_buffer) {
                line = line->next;
            }

        } /* if (q_scrollback_search_string == NULL) */

keep_moving:

        while (line != NULL) {
            if (line->search_match == Q_TRUE) {
                break;
            }
            line = line->next;
        }
        if (line == NULL) {
            /*
             * No more matches
             */
            notify_form(_("No more matches"), 1.5);
            /*
             * Next time, show the matches from the first line.
             */
            last_line = q_scrollback_buffer;
            break;
        } else {
            last_line = line;
        }

        /*
         * We claimed to match, there had better be one
         */
        assert(line != NULL);

        q_scrollback_position = line;
        for (row = 0; row < HEIGHT - STATUS_HEIGHT - 1; row++) {
            if (q_scrollback_position->next == NULL) {
                break;
            }
            q_scrollback_position = q_scrollback_position->next;
        }
        if (last_position == q_scrollback_position) {
            /*
             * We're at the bottom, head back to top
             */
            notify_form(_("No more matches"), 1.5);
            last_line = q_scrollback_buffer;
            line = last_line;
            last_position = last_line;
            goto keep_moving;
        } else {
            last_position = q_scrollback_position;
        }
        break;

    case 't':
    case 'T':
        /*
         * Save only visible area
         */
        if (q_status.read_only == Q_TRUE) {
            break;
        }

        q_cursor_on();
        reset_scrollback_save_type();
        if (q_status.scrollback_save_type == Q_CAPTURE_TYPE_ASK) {
            q_status.scrollback_save_type = ask_save_type();
            q_screen_dirty = Q_TRUE;
            scrollback_refresh();
        }
        if (q_status.scrollback_save_type != Q_CAPTURE_TYPE_ASK) {
            filename = save_form(_("Scrollback (Visible Only) Save Filename"),
                                 _("saved_scrollback.txt"), Q_FALSE, Q_FALSE);
            q_cursor_off();
            if (filename != NULL) {
                qlog(_("Scrollback (visible only) saved to file '%s'\n"),
                     filename);
                if (save_scrollback(filename, Q_TRUE) == Q_FALSE) {
                    snprintf(notify_message, sizeof(notify_message),
                             _("Error saving to file \"%s\""), filename);
                    notify_form(notify_message, 0);
                }
            }
            /*
             * Do NOT return to console
             */
        }
        break;

    case 's':
    case 'S':
        /*
         * Save
         */
        if (q_status.read_only == Q_TRUE) {
            break;
        }

        q_cursor_on();
        reset_scrollback_save_type();
        if (q_status.scrollback_save_type == Q_CAPTURE_TYPE_ASK) {
            q_status.scrollback_save_type = ask_save_type();
            q_screen_dirty = Q_TRUE;
            scrollback_refresh();
        }
        if (q_status.scrollback_save_type != Q_CAPTURE_TYPE_ASK) {
            filename = save_form(_("Scrollback (All) Save Filename"),
                                 _("saved_scrollback.txt"), Q_FALSE, Q_FALSE);
            q_cursor_off();
            if (filename != NULL) {
                qlog(_("Scrollback (all) saved to file '%s'\n"), filename);
                if (save_scrollback(filename, Q_FALSE) == Q_FALSE) {
                    snprintf(notify_message, sizeof(notify_message),
                             _("Error saving to file \"%s\""), filename);
                    notify_form(notify_message, 0);
                }
            }
            /*
             * Return to console
             */
            q_scrollback_position = q_scrollback_last;
            switch_state(Q_STATE_CONSOLE);
        }
        break;

    case 'c':
    case 'C':
        /*
         * Clear
         */
        clear_scrollback();
        /*
         * Return to console
         */
        q_scrollback_position = q_scrollback_last;
        switch_state(Q_STATE_CONSOLE);
        break;

    case '`':
    case Q_KEY_ESCAPE:
        /*
         * Return to console
         */
        q_scrollback_position = q_scrollback_last;
        switch_state(Q_STATE_CONSOLE);
        break;

    case Q_KEY_UP:
        /*
         * Make sure we need to scroll. This is pretty ugly but it works so I
         * won't bugger with it anymore. Same code is in the KEY_PPAGE case
         * too but j instead of i.
         */
        for (i = 0, line = q_scrollback_buffer; i < local_height + 1; i++) {
            if (line == q_scrollback_position) {
                break;
            }
            if ((line != NULL) && (line->next != NULL)) {
                line = line->next;
            }
        }
        if (i != local_height + 1) {
            break;
        }

        if ((q_scrollback_position != NULL) &&
            (q_scrollback_position->prev != NULL)) {
            q_scrollback_position = q_scrollback_position->prev;
        }
        break;

    case Q_KEY_DOWN:
        if ((q_scrollback_position != NULL)
            && (q_scrollback_position->next != NULL)) {
            q_scrollback_position = q_scrollback_position->next;
        }
        break;

    case Q_KEY_END:
        q_scrollback_position = q_scrollback_last;
        break;

    case Q_KEY_PPAGE:
        for (i = 0; i < local_height; i++) {
            for (j = 0, line = q_scrollback_buffer; j < local_height + 1; j++) {
                if (line == q_scrollback_position) {
                    break;
                }
                if ((line != NULL) && (line->next != NULL)) {
                    line = line->next;
                }
            }
            if (j != local_height + 1) {
                i = local_height;
                break;
            }
            if ((q_scrollback_position != NULL) &&
                (q_scrollback_position->prev != NULL)) {
                q_scrollback_position = q_scrollback_position->prev;
            }
        }
        break;

    case Q_KEY_HOME:
        q_scrollback_position = q_scrollback_buffer;
        for (i = 0; i < local_height; i++) {
            if ((q_scrollback_position != NULL) &&
                (q_scrollback_position->next != NULL)) {
                q_scrollback_position = q_scrollback_position->next;
            }
        }
        break;

    case Q_KEY_NPAGE:
        for (i = 0; i < local_height; i++) {
            if ((q_scrollback_position != NULL) &&
                (q_scrollback_position->next != NULL)) {
                q_scrollback_position = q_scrollback_position->next;
            }
        }
        break;

    default:
        break;
    }

}

/**
 * If true, the physical terminal is capable of displaying double-width
 * characters and qodem is using a trick to do so.
 */
Q_BOOL has_true_doublewidth() {
#if defined(Q_PDCURSES_WIN32)
    /* Win32 has double-width via PDC_set_double(). */
    return Q_TRUE;
#else
#  if defined(Q_PDCURSES) && !defined(Q_PDCURSES_WIN32)
    /* X11 has double-width via PDC_set_double(). */
    return Q_TRUE;
#  else
    return xterm;
#  endif
#endif
}

/**
 * Draw the visible portion of the scrollback buffer to the screen.
 *
 * @param skip_lines adjust by this many lines.  This is used to leave room
 * for split-screen.
 */
void render_scrollback(const int skip_lines) {
    struct q_scrolline_struct * line;
    int row;
    int renderable_lines;
    int i;

#ifndef Q_PDCURSES
    /*
     * Double-height double-width characters can be emitted to the host
     * xterm and get the correct output on screen.
     *
     * However, there is a SEVERE performance penalty everywhere else but
     * double-width / double-height lines, so we only use this method
     * when we actually see double-width / double-height on screen.
     */
    struct q_scrolline_struct * top_line;
    static Q_BOOL first = Q_TRUE;
    static Q_BOOL double_on_last_screen = Q_FALSE;
    Q_BOOL double_on_this_screen = Q_FALSE;
    Q_BOOL odd_line = Q_FALSE;
    char * term;
#endif

#ifndef Q_PDCURSES
    if (first == Q_TRUE) {
        if (q_status.xterm_double == Q_TRUE) {
            term = getenv("TERM");
            if (term != NULL) {
                if (strstr(term, "xterm") != NULL) {
                    xterm = Q_TRUE;
                }
            }
        }
        first = Q_FALSE;
    }
#endif

    row = HEIGHT - 1;

    /*
     * Skip the status line
     */
    if (q_status.status_visible == Q_TRUE) {
        row -= STATUS_HEIGHT;
    } else if (q_program_state == Q_STATE_SCROLLBACK) {
        row -= 1;
    }

    /*
     * Let's assert that row > 0.  Konsole and xterm won't let the window
     * size reach zero so this should be a non-issue.
     */
    assert(row > 0);

    /*
     * If split_screen is enabled, we will skip some lines from the top.
     */
    row -= skip_lines;
    if (row < 0) {
        /*
         * Sanity check.  I don't want to abort, so we'll just return.
         */
        fprintf(stderr,
                "render_scrollback() WARNING: Screen size is too small: height=%d width=%d status_height=%d skip_lines=%d\n",
                HEIGHT, WIDTH, STATUS_HEIGHT, skip_lines);
        return;
    }

    /*
     * Count the lines available
     */
    renderable_lines = 0;
    line = q_scrollback_position;
    while (row >= 0) {
        renderable_lines++;
        if (line->prev == NULL) {
            break;
        }
        line = line->prev;
        row--;
    }

    /*
     * At this point line should point to the line directly above
     * find_top_scrollback_line() IF row is -1.  If row is 0 or more then the
     * scrollback buffer is not as large as the screen.
     */
    if ((row < 0) && (line->next != NULL)) {
        line = line->next;
    }

#ifndef Q_PDCURSES
    /*
     * See if there are any double-width / double-height lines.
     */
    top_line = line;
    for (row = 0; row < renderable_lines; row++) {
        if (line->double_width == Q_TRUE) {
            double_on_this_screen = Q_TRUE;
        }
        if (line->double_height != 0) {
            double_on_this_screen = Q_TRUE;
        }
        line = line->next;
    }
    line = top_line;

    /*
    fprintf(stderr, "double_on_this_screen %s\n",
        (double_on_this_screen == Q_TRUE ? "true" : "false"));
     */

#endif

    /*
     * Now loop from line onward
     */
    for (row = 0; row < renderable_lines; row++) {

        /*
         * ncurses-based drawing needs to refresh the whole screen no matter
         * what.  ncurses itself handles its own dirty state.  But I want to
         * keep the stub for doing dirty-line handling here for the case of
         * rendering to a larger scrollback region (ala Turbo Vision).
         */
        if ((line->dirty == Q_TRUE) || Q_TRUE) {
#ifndef Q_PDCURSES
            /*
             * For xterm, we need to set the double-width flag appropriately
             * BEFORE drawing any of the characters.  If we don't, then when
             * we switch between double-width and single-width we will lose
             * the right half of the screen because we were at double-width,
             * drew 80 columns, xterm ignored columns 41-80, then switched to
             * single-width and xterm shrinks the visible portion.
             */
            if ((xterm == Q_TRUE) &&
                ((double_on_last_screen == Q_TRUE) ||
                 (double_on_this_screen == Q_TRUE)) &&
                ((q_program_state == Q_STATE_CONSOLE) ||
                 (q_program_state == Q_STATE_SCRIPT_EXECUTE) ||
                 (q_program_state == Q_STATE_HOST) ||
                 (q_program_state == Q_STATE_SCROLLBACK))
            ) {
                screen_move_yx(row, 0);
                if ((line->double_width == Q_TRUE) &&
                    (line->double_height == 0)
                ) {
                    odd_line = Q_TRUE;
                    screen_flush();
                    fflush(stdout);
                    fprintf(stdout, "\033#6");
                    odd_line = Q_TRUE;
                } else if (line->double_height == 1) {
                    assert(line->double_width == Q_TRUE);
                    odd_line = Q_TRUE;
                    screen_flush();
                    fflush(stdout);
                    fprintf(stdout, "\033#3");
                } else if (line->double_height == 2) {
                    assert(line->double_width == Q_TRUE);
                    odd_line = Q_TRUE;
                    screen_flush();
                    fflush(stdout);
                    fprintf(stdout, "\033#4");
                } else {
                    assert(line->double_width == Q_FALSE);
                    assert(line->double_height == 0);
                    odd_line = Q_TRUE;
                    screen_flush();
                    fflush(stdout);
                    fprintf(stdout, "\033#5");
                }
            }
#endif /* Q_PDCURSES */

            if (line->length > 0) {
                for (i = 0; i < line->length; i++) {
                    attr_t color = line->colors[i];

                    /*
                     * Check how reverse color needs to be rendered
                     */
                    color =
                        vt100_check_reverse_color(color, line->reverse_color);

                    if ((line->search_match == Q_TRUE) &&
                        ((q_scrollback_search_string != NULL) ||
                         (q_scrollback_highlight_search_string == Q_TRUE)) &&
                        (q_program_state == Q_STATE_SCROLLBACK)
                    ) {
                        color = line->search_colors[i];
                    }

                    /*
                    fprintf(stderr, "i %d double_width %s\n", i,
                        (line->double_width == Q_TRUE ? "true" : "false"));
                     */

                    if (line->double_width == Q_TRUE) {
                        if ((2 * i) >= WIDTH) {
                            break;
                        }
                        if ((has_true_doublewidth() == Q_FALSE) &&
                            (q_status.emulation != Q_EMUL_PETSCII) &&
                            (q_status.emulation != Q_EMUL_ATASCII)
                        ) {
                            screen_put_scrollback_char_yx(row, (2 * i),
                                translate_unicode_in(line->chars[i]), color);
                            screen_put_scrollback_char_yx(row, (2 * i) + 1, ' ',
                                                          color);
                        } else {
                            screen_put_scrollback_char_yx(row, i,
                                translate_unicode_in(line->chars[i]), color);
                        }
                    } else {
                        assert(line->double_height == 0);
                        if (i >= WIDTH) {
                            break;
                        }
                        screen_put_scrollback_char_yx(row, i,
                            translate_unicode_in(line->chars[i]), color);
                    }

                } /* for (i = 0; i < line->length; i++) */

#ifndef Q_PDCURSES
                if ((xterm == Q_TRUE) && (odd_line == Q_TRUE)) {
                    fflush(stdout);
                    screen_flush();
                }
#endif
            } else {
                screen_move_yx(row, 0);
            }

            /*
             * Clear remainder of line
             */
            screen_clear_remaining_line(line->double_width);

#ifdef Q_PDCURSES
            /*
             * For PDcurses, we can render everything and then set
             * double-width afterwards.  This is a performance improvement
             * mainly, to reduce the number of double-width characters the
             * X11 version tries to draw.
             */
            if ((has_true_doublewidth() == Q_TRUE) &&
                ((q_program_state == Q_STATE_CONSOLE) ||
                 (q_program_state == Q_STATE_SCRIPT_EXECUTE) ||
                 (q_program_state == Q_STATE_HOST) ||
                 (q_program_state == Q_STATE_SCROLLBACK))
            ) {
                if ((line->double_width == Q_TRUE) &&
                    (line->double_height == 0)
                ) {
                    PDC_set_double(row, 1);
                } else if (line->double_height == 1) {
                    PDC_set_double(row, 2);
                } else if (line->double_height == 2) {
                    PDC_set_double(row, 3);
                } else {
                    assert(line->double_width == Q_FALSE);
                    assert(line->double_height == 0);
                    PDC_set_double(row, 0);
                }
            }
#endif

            line->dirty = Q_FALSE;

        } /* if (line->dirty == Q_TRUE) */

        line = line->next;

    } /* for (row = 0; row < renderable_lines; row++) */

    for (row = renderable_lines; row < HEIGHT - STATUS_HEIGHT; row++) {
        screen_move_yx(row, 0);

#ifdef Q_PDCURSES
        PDC_set_double(row, 0);
#else
        if ((xterm == Q_TRUE) &&
            ((double_on_last_screen == Q_TRUE) ||
             (double_on_this_screen == Q_TRUE))
        ) {
            screen_flush();
            fprintf(stdout, "\033#5");
            screen_flush();
        }
#endif
        screen_clear_remaining_line(Q_FALSE);
    }

#ifndef Q_PDCURSES
    double_on_last_screen = double_on_this_screen;
#endif
}

/**
 * Draw screen for the Atl-/view scrollback state.
 */
void scrollback_refresh() {
    char * scrollback_string;
    int status_left_stop;
    char lines_count_buffer[16];

    /*
     * Render scrollback
     */
    render_scrollback(0);

    /*
     * Put up the status line
     */
    screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH,
                              Q_COLOR_STATUS);

    scrollback_string =
        _(" SCROLL-BACK    F/A-Find  S/T-Save All/Screen  C-Clear  ESC/`-Exit  Lines:");
    status_left_stop = WIDTH - strlen(scrollback_string) - 6;
    if (status_left_stop <= 0) {
        status_left_stop = 0;
    } else {
        status_left_stop /= 2;
    }

    sprintf(lines_count_buffer, "%u", q_status.scrollback_lines);
    screen_put_color_printf_yx(HEIGHT - 1, status_left_stop, Q_COLOR_STATUS,
                               "%s %s ", scrollback_string, lines_count_buffer);

    /*
     * Add arrows on the status line
     */
    if (q_scrollback_position != q_scrollback_last) {
        /*
         * Down arrow - more lines are below
         */
        screen_put_color_char_yx(HEIGHT - 1, status_left_stop + 14,
                                 cp437_chars[DOWNARROW], Q_COLOR_STATUS);
    }
    if (find_top_scrollback_line() != q_scrollback_buffer) {
        /*
         * Up arrow - more lines are above
         */
        screen_put_color_char_yx(HEIGHT - 1, status_left_stop + 13,
                                 cp437_chars[UPARROW], Q_COLOR_STATUS);
    }

    screen_flush();
}

/**
 * Scroll a rectangular area of the screen up 0 or more lines.
 *
 * @param top the row of the line to scroll
 * @param left the first column of the line to scroll
 * @param bottom the row of the last line to scroll
 * @param right the last column of the line to scroll
 * @param count the number of rows to move
 */
void rectangle_scroll_up(const int top, const int left, const int bottom,
                         const int right, const int count) {
    struct q_scrolline_struct * top_line;
    struct q_scrolline_struct * new_top_line;
    int remaining;
    int i;
    int j;

    if (top >= bottom) {
        return;
    }
    if (left >= right) {
        return;
    }

    /*
     * Sanity check: see if there will be any characters left after the
     * scroll
     */
    if (bottom + 1 - top <= count) {
        /*
         * There won't be anything left in the region, so just call
         * erase_screen() and return.
         */
        erase_screen(top, left, bottom, right - 1, Q_FALSE);
        return;
    }

    /*
     * Set new_top_line to the top of the scrolling region
     */
    new_top_line = find_top_scrollback_line();
    for (i = 0; i < top; i++) {
        if (new_top_line->next == NULL) {
            new_scrollback_line();
        }
        new_top_line = new_top_line->next;
    }

    /*
     * Set top_line to the top line being scrolled up
     */
    top_line = new_top_line;
    remaining = bottom + 1 - top - count;
    for (i = 0; i < count; i++) {
        if (top_line->next == NULL) {
            new_scrollback_line();
        }
        top_line = top_line->next;
    }

    /*
     * Copy the data between top_line and new_top_line
     */
    for (i = 0; i < remaining; i++) {
        if (new_top_line->length < top_line->length) {
            for (j = new_top_line->length; j < right; j++) {
                new_top_line->chars[j] = ' ';
                new_top_line->colors[j] =
                    scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);
                new_top_line->length = right;
            }
        }
        memcpy(new_top_line->chars + left, top_line->chars + left,
               sizeof(wchar_t) * (right - left));
        memcpy(new_top_line->colors + left, top_line->colors + left,
               sizeof(attr_t) * (right - left));
        new_top_line->length = top_line->length;
        new_top_line->double_width = top_line->double_width;
        new_top_line->double_height = top_line->double_height;
        new_top_line->reverse_color = top_line->reverse_color;
        new_top_line->dirty = Q_TRUE;
        new_top_line = new_top_line->next;
        top_line = top_line->next;

        if (top_line == NULL) {
            /*
             * We are scrolling up on the very first screen and lines don't
             * exist for the remainder of the scroll operation.  Just break
             * out and let erase_screen() take care of things.
             */
            break;
        }

    }

    erase_screen(top + remaining, left, bottom, right - 1, Q_FALSE);
}

/**
 * Scroll the entire screen up 0 or more lines.
 *
 * @param count the number of rows to move
 */
void scroll_up(const int count) {
    scrolling_region_scroll_up(0, HEIGHT - STATUS_HEIGHT - 1, count);
}

/**
 * Scroll the lines inside the scrolling region up 0 or more lines.
 *
 * @param region_top the row of the line to scroll
 * @param region_bottom the row of the last line to scroll
 * @param count the number of rows to move
 */
void scrolling_region_scroll_up(const int region_top, const int region_bottom,
                                const int count) {
    rectangle_scroll_up(region_top, 0, region_bottom, Q_MAX_LINE_LENGTH, count);
}

/**
 * Scroll a rectangular area of the screen down 0 or more lines.
 *
 * @param top the row of the line to scroll
 * @param left the first column of the line to scroll
 * @param bottom the row of the last line to scroll
 * @param right the last column of the line to scroll
 * @param count the number of rows to move
 */
void rectangle_scroll_down(const int top, const int left, const int bottom,
                           const int right, const int count) {
    struct q_scrolline_struct * bottom_line;
    struct q_scrolline_struct * new_bottom_line;
    int remaining;
    int i;
    int j;

    if (top >= bottom) {
        return;
    }
    if (left >= right) {
        return;
    }

    /*
     * Sanity check: see if there will be any characters left after the
     * scroll
     */
    if (bottom + 1 - top <= count) {
        /*
         * There won't be anything left in the region, so just call
         * erase_screen() and return.
         */
        erase_screen(top, left, bottom, right - 1, Q_FALSE);
        return;
    }

    /*
     * Set new_bottom_line to the bottom of the scrolling region
     */
    new_bottom_line = find_top_scrollback_line();
    for (i = 0; i < bottom; i++) {
        if (new_bottom_line->next == NULL) {
            new_scrollback_line();
        }
        new_bottom_line = new_bottom_line->next;
    }

    /*
     * Set bottom_line to the bottom line being scrolled down
     */
    bottom_line = new_bottom_line;
    remaining = bottom + 1 - top - count;
    for (i = 0; i < count; i++) {
        if (bottom_line->prev == NULL) {
            /*
             * We're trying to scroll down empty lines from the top line.
             * Insert a new blank line here.
             */
            insert_scrollback_line(bottom_line);
        }
        bottom_line = bottom_line->prev;
    }

    /*
     * Copy the data between bottom_line and new_bottom_line
     */
    for (i = 0; i < remaining; i++) {
        if (new_bottom_line->length < left) {
            for (j = new_bottom_line->length; j < right; j++) {
                new_bottom_line->chars[j] = ' ';
                new_bottom_line->colors[j] =
                    scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);
                new_bottom_line->length = right;
            }
        }
        memcpy(new_bottom_line->chars + left, bottom_line->chars + left,
               sizeof(wchar_t) * (right - left));
        memcpy(new_bottom_line->colors + left, bottom_line->colors + left,
               sizeof(attr_t) * (right - left));
        new_bottom_line->length = bottom_line->length;
        new_bottom_line->double_width = bottom_line->double_width;
        new_bottom_line->double_height = bottom_line->double_height;
        new_bottom_line->reverse_color = bottom_line->reverse_color;
        new_bottom_line->dirty = Q_TRUE;
        new_bottom_line = new_bottom_line->prev;
        bottom_line = bottom_line->prev;
    }
    erase_screen(top, left, top + count - 1, right - 1, Q_FALSE);
}

/**
 * Scroll the lines inside the scrolling region down 0 or more lines.
 *
 * @param region_top the row of the line to scroll
 * @param region_bottom the row of the last line to scroll
 * @param count the number of rows to move
 */
void scrolling_region_scroll_down(const int region_top, const int region_bottom,
                                  const int count) {
    rectangle_scroll_down(region_top, 0, region_bottom, Q_MAX_LINE_LENGTH,
                          count);
}

/**
 * Scroll the entire screen down 0 or more lines.
 *
 * @param count the number of rows to move
 */
void scroll_down(const int count) {
    scrolling_region_scroll_down(0, HEIGHT - STATUS_HEIGHT - 1, count);
}

/**
 * Move the cursor up zero or more rows.
 *
 * @param count the number of rows to move
 * @param honor_scroll_region if true, only move within the scrolling region
 */
void cursor_up(const int count, const Q_BOOL honor_scroll_region) {
    int i;
    int top;

    /*
     * Special case: if a user moves the cursor from the right margin, we
     * have to reset the VT100 right margin flag.
     */
    if (count > 0) {
        vt100_wrap_line_flag = Q_FALSE;
    }

    for (i = 0; i < count; i++) {

        if (honor_scroll_region == Q_TRUE) {
            /*
             * Honor the scrolling region
             */
            if (q_status.cursor_y < q_status.scroll_region_top) {
                /*
                 * Outside region, do nothing
                 */
                return;
            }
            /*
             * Inside region, go up
             */
            top = q_status.scroll_region_top;
        } else {
            /*
             * Non-scrolling case
             */
            top = 0;
        }

        /*
         * Non-scrolling case
         */
        if (q_status.cursor_y > top) {
            q_status.cursor_y--;
            q_scrollback_current = q_scrollback_current->prev;
        }
    } /* for (i = 0; i < count; i++) */
}

/**
 * Move the cursor down zero or more rows.
 *
 * @param count the number of rows to move
 * @param honor_scroll_region if true, only move within the scrolling region
 */
void cursor_down(const int count, const Q_BOOL honor_scroll_region) {
    int i, j;
    int bottom;

    /*
     * Special case: if a user moves the cursor from the right margin, we
     * have to reset the VT100 right margin flag.
     */
    if (count > 0) {
        vt100_wrap_line_flag = Q_FALSE;
    }

    for (i = 0; i < count; i++) {

        if (honor_scroll_region == Q_TRUE) {
            /*
             * Honor the scrolling region
             */
            if (q_status.cursor_y > q_status.scroll_region_bottom) {
                /*
                 * Outside region, do nothing
                 */
                return;
            }
            /*
             * Inside region, go down
             */
            bottom = q_status.scroll_region_bottom;
        } else {
            /*
             * Non-scrolling case
             */
            bottom = HEIGHT - STATUS_HEIGHT - 1;
        }

        if (q_status.cursor_y < bottom) {
            q_status.cursor_y++;
            if (q_scrollback_current->next == NULL) {
                new_scrollback_line();
                q_scrollback_current = q_scrollback_current->next;
                /*
                 * Pad spaces if necessary
                 */
                for (j = q_status.cursor_x; j > q_scrollback_current->length;) {
                    q_scrollback_last->colors[q_scrollback_last->length] =
                        scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);
                    q_scrollback_last->length++;
                }
            } else {
                q_scrollback_current = q_scrollback_current->next;
            }
        }

        /*
         * Capture
         */
        if (q_status.capture == Q_TRUE) {
            if (q_status.capture_type == Q_CAPTURE_TYPE_HTML) {
                /*
                 * HTML
                 */
                fprintf(q_status.capture_file, "\n");
            } else if (q_status.capture_type == Q_CAPTURE_TYPE_NORMAL) {
                /*
                 * Normal
                 */
                fprintf(q_status.capture_file, "\n");
            }
            fflush(q_status.capture_file);
            q_status.capture_flush_time = time(NULL);
            q_status.capture_x = 0;
        }

    } /* for (i = 0; i < count; i++) */
}

/**
 * Move the cursor left zero or more rows.
 *
 * @param count the number of columns to move
 * @param honor_scroll_region if true, only move within the scrolling region
 */
void cursor_left(const int count, const Q_BOOL honor_scroll_region) {
    int i;

    /*
     * Special case: if a user moves the cursor from the right margin, we
     * have to reset the VT100 right margin flag.
     */
    if (count > 0) {
        vt100_wrap_line_flag = Q_FALSE;
    }

    for (i = 0; i < count; i++) {
        if (honor_scroll_region == Q_TRUE) {
            /*
             * Honor the scrolling region
             */
            if ((q_status.cursor_y < q_status.scroll_region_top) ||
                (q_status.cursor_y > q_status.scroll_region_bottom)) {
                /*
                 * Outside region, do nothing
                 */
                return;
            }
        }

        if (q_status.cursor_x > 0) {
            q_status.cursor_x--;
        }
    }
}

/**
 * Move the cursor right zero or more rows.
 *
 * @param count the number of columns to move
 * @param honor_scroll_region if true, only move within the scrolling region
 */
void cursor_right(const int count, const Q_BOOL honor_scroll_region) {
    int i;
    int right_margin;

    /*
     * Special case: if a user moves the cursor from the right margin, we
     * have to reset the VT100 right margin flag.
     */
    if (count > 0) {
        vt100_wrap_line_flag = Q_FALSE;
    }

    if (q_emulation_right_margin > 0) {
        right_margin = q_emulation_right_margin;
    } else {
        right_margin = WIDTH - 1;
    }
    if (q_scrollback_current->double_width == Q_TRUE) {
        right_margin = ((right_margin + 1) / 2) - 1;
    }

    for (i = 0; i < count; i++) {
        if (honor_scroll_region == Q_TRUE) {
            /*
             * Honor the scrolling region
             */
            if ((q_status.cursor_y < q_status.scroll_region_top)
                || (q_status.cursor_y > q_status.scroll_region_bottom)) {
                /*
                 * Outside region, do nothing
                 */
                return;
            }
        }

        if (q_status.cursor_x < right_margin) {
            if (q_status.cursor_x >= q_scrollback_current->length) {
                /*
                 * Append a space and push the line out
                 */
                q_scrollback_current->colors[q_scrollback_current->length] =
                    q_current_color;
                q_scrollback_current->chars[q_scrollback_current->length] = ' ';
                q_scrollback_current->length++;
            }
            q_status.cursor_x++;
        }
    }
}

/**
 * Move the cursor to a specific position.
 *
 * @param row the row position to move to.  The top-most row is 0.
 * @param col the column position to move to.  The left-most column is 0.
 */
void cursor_position(int row, int col) {
    int right_margin;

    assert(col >= 0);
    assert(row >= 0);

    if (q_emulation_right_margin > 0) {
        right_margin = q_emulation_right_margin;
    } else {
        right_margin = WIDTH - 1;
    }
    if (q_scrollback_current->double_width == Q_TRUE) {
        right_margin = ((right_margin + 1) / 2) - 1;
    }

    /*
     * Set column number
     */
    q_status.cursor_x = col;
    if (q_status.cursor_x > WIDTH - 1) {
        q_status.cursor_x = WIDTH - 1;
    }

    /*
     * Sanity check, bring column back to margin.
     */
    if (q_emulation_right_margin > 0) {
        if (q_status.cursor_x > q_emulation_right_margin) {
            q_status.cursor_x = right_margin;
        }
    }

    /*
     * Set row number
     */
    if (q_status.origin_mode == Q_TRUE) {
        row += q_status.scroll_region_top;
    }
    if (q_status.cursor_y < row) {
        cursor_down(row - q_status.cursor_y, Q_FALSE);
    } else if (q_status.cursor_y > row) {
        cursor_up(q_status.cursor_y - row, Q_FALSE);
    }

    vt100_wrap_line_flag = Q_FALSE;
}

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
void fill_line_with_character(const int start, const int end,
                              const wchar_t character,
                              const Q_BOOL honor_protected) {
    int i;

    if (start > end) {
        return;
    }

    /*
     * Mark line dirty
     */
    q_scrollback_current->dirty = Q_TRUE;

    if (q_scrollback_current->length < start) {
        for (i = q_scrollback_current->length; i < start; i++) {
            q_scrollback_current->chars[i] = ' ';
            q_scrollback_current->colors[i] =
                scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);
            q_scrollback_current->length = start;
        }
    }

    /*
     * Pad the characters leading up start
     */
    for (i = q_scrollback_current->length; i < start; i++) {
        q_scrollback_current->chars[i] = character;

        switch (q_status.emulation) {
        case Q_EMUL_VT100:
        case Q_EMUL_VT102:
        case Q_EMUL_VT220:
            /*
             * From the VT102 manual:
             *
             * Erasing a character also erases any character attribute of the
             * character.
             */
            q_scrollback_current->colors[i] =
                scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);
            break;
        default:
            /*
             * Most other consoles erase with the current color
             * a.k.a. back-color erase (bce).
             */
            q_scrollback_current->colors[i] =
                color_to_attr(color_from_attr(q_current_color));
            break;
        }
    }

    /*
     * Now erase from start to end
     */
    for (i = start; i <= end; i++) {
        if ((honor_protected == Q_FALSE) ||
            ((honor_protected == Q_TRUE) &&
                ((q_scrollback_current->colors[i] & Q_A_PROTECT) == 0))
        ) {
            q_scrollback_current->chars[i] = character;

            switch (q_status.emulation) {
            case Q_EMUL_VT100:
            case Q_EMUL_VT102:
            case Q_EMUL_VT220:
                /*
                 * From the VT102 manual:
                 *
                 * Erasing a character also erases any character attribute of
                 * the character.
                 */
                q_scrollback_current->colors[i] =
                    scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);
                break;
            default:
                /*
                 * Most other consoles erase with the current color
                 * a.k.a. back-color erase (bce).
                 */
                q_scrollback_current->colors[i] =
                    color_to_attr(color_from_attr(q_current_color));
                break;
            }

        }

    }

    /*
     * If we erased beyond the end of the line, increase the line length to
     * include the new spaces.
     */
    if (end > q_scrollback_current->length) {
        q_scrollback_current->length = end + 1;
    }

    /*
     * If the line is now longer than the screen, shorten it to WIDTH or else
     * we'll have problems with line wrapping.
     */
    if (q_scrollback_current->length > WIDTH) {
        q_scrollback_current->length = WIDTH;
    }
}

/**
 * Erase the characters in the current line from the start column (from 0 to
 * WIDTH - 1) to the end column (from 0 to WIDTH - 1), inclusive.
 *
 * @param start the column of the first character to erase
 * @param end the column of the last character to erase
 * @param honor_protected if true then only erase characters that do not have
 * the A_PROTECT attribute set
 */
void erase_line(const int start, const int end, const Q_BOOL honor_protected) {
    fill_line_with_character(start, end, ' ', honor_protected);
}

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
void erase_screen(const int start_row, const int start_col, const int end_row,
                  const int end_col, const Q_BOOL honor_protected) {
    struct q_scrolline_struct * start_line;
    struct q_scrolline_struct * line;
    struct q_scrolline_struct * original_current_line;

    int i;

    if ((start_row < 0) || (start_col < 0) ||
        (end_row < 0) || (end_col < 0) ||
        (end_row < start_row) ||
        (end_col < start_col)
    ) {
        return;
    }

    /*
     * Hang onto the original cursor position
     */
    original_current_line = q_scrollback_current;

    /*
     * Get to the starting line
     */
    start_line = find_top_scrollback_line();
    for (i = 0; i < start_row; i++) {
        if (start_line->next == NULL) {
            new_scrollback_line();
        }
        start_line = start_line->next;
    }

    line = start_line;

    for (i = start_row; i <= end_row; i++) {
        q_scrollback_current = line;
        erase_line(start_col, end_col, honor_protected);

        /*
         * Note: we don't add a line when (i == end_row) because if end_row
         * is the last line in scrollback new_scrollback_line() will make the
         * total screen one line larger than it really is.  This causes lots
         * of trouble and looks like crap.
         */
        if ((line->next == NULL) && (i < end_row)) {
            /*
             * Add a line
             */
            new_scrollback_line();
        }
        line = line->next;
    }

    /*
     * Restore the cursor position
     */
    q_scrollback_current = original_current_line;

}

/**
 * Advance one line down and set the q_status.cursor_x column to 0.
 */
void cursor_carriage_return() {
    /*
     * Reset line
     */
    q_status.cursor_x = 0;

    if (q_status.line_feed_on_cr == Q_TRUE) {
        cursor_linefeed(Q_FALSE);
    }

    vt100_wrap_line_flag = Q_FALSE;

    /*
     * Pass a carriage return to a script if we're running one
     */
    if (q_program_state == Q_STATE_SCRIPT_EXECUTE) {
        script_print_character(0x0D);
    }
    if (q_status.quicklearn == Q_TRUE) {
        quicklearn_print_character(0x0D);
    }
}

/**
 * Advance the entire screen to a new "page" and home the cursor.
 */
void cursor_formfeed() {
    int i;

    /*
     * Print the remaining number of linefeeds to clear the screen, then home
     * the cursor.
     */
    for (i = q_status.cursor_y; i <= 2 * (HEIGHT - STATUS_HEIGHT - 1); i++) {
        cursor_linefeed(Q_FALSE);
    }

    /*
     * Erase the whole screen also, because a scroll region might be set.
     */
    erase_screen(0, 0, HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1, Q_FALSE);

    /*
     * Finally, home the cursor
     */
    cursor_position(0, 0);
}

/**
 * Advance one line down and optionally set the q_status.cursor_x column to
 * 0.
 *
 * @param new_line_mode if true, set the column to 0
 */
void cursor_linefeed(const Q_BOOL new_line_mode) {
    int i;
    struct q_scrolline_struct *line = NULL;

    /*
     * Capture
     */
    if (q_status.capture == Q_TRUE) {
        if (q_status.capture_type == Q_CAPTURE_TYPE_HTML) {
            /*
             * HTML
             */
            fprintf(q_status.capture_file, "\n");
        } else if (q_status.capture_type == Q_CAPTURE_TYPE_NORMAL) {
            /*
             * Normal
             */
            fprintf(q_status.capture_file, "\n");
        }
        if (q_status.capture_flush_time != time(NULL)) {
            fflush(q_status.capture_file);
            q_status.capture_flush_time = time(NULL);
        }
    }

    if (q_status.cursor_y < q_status.scroll_region_bottom) {
        /*
         * Increment screen y
         */
        q_status.cursor_y++;

        /*
         * New line
         */
        if (q_scrollback_current->next == NULL) {
            new_scrollback_line();
        }

        /*
         * Write into the new line.
         */
        q_scrollback_current = q_scrollback_current->next;

    } else {
        /*
         * Screen y does not increment
         */

        /*
         * Two cases: either we're inside a scrolling region or not.  If the
         * scrolling region bottom is the bottom of the screen, then push the
         * top line into the buffer.  Else scroll the scrolling region up.
         */
        if ((q_status.scroll_region_bottom == HEIGHT - STATUS_HEIGHT - 1) &&
            (q_status.scroll_region_top == 0)) {

            /*
             * We're at the bottom of the scroll region, AND the scroll
             * region is the entire screen.
             */

            /*
             * New line
             */
            if (q_scrollback_current->next == NULL) {
                new_scrollback_line();
            }
            /*
             * Write into the new line.
             */
            q_scrollback_current = q_scrollback_current->next;

            /*
             * Set length to current X
             */
            q_scrollback_current->length = q_status.cursor_x;

            if (q_status.reverse_video == Q_TRUE) {
                /*
                 * Increase width and pad with spaces so that inverse video
                 * appears everwhere.
                 */
                q_scrollback_current->length = WIDTH;
            }

            /*
             * Mark every line on the screen dirty
             */
            line = q_scrollback_position;
            for (i = 0; i < HEIGHT - STATUS_HEIGHT - 1; i++) {
                if (line != NULL) {
                    line->dirty = Q_TRUE;
                    line = line->prev;
                }
            }

        } else {
            /*
             * We're at the bottom of the scroll region, AND the scroll
             * region is NOT the entire screen.
             */

            scrolling_region_scroll_up(q_status.scroll_region_top,
                                       q_status.scroll_region_bottom, 1);
        }
    }

    if (new_line_mode == Q_TRUE) {
        q_status.cursor_x = 0;
    }

    vt100_wrap_line_flag = Q_FALSE;

    /*
     * Pass a carriage return to a script if we're running one
     */
    if (q_program_state == Q_STATE_SCRIPT_EXECUTE) {
        script_print_character(0x0A);
    }
    if (q_status.quicklearn == Q_TRUE) {
        quicklearn_print_character(0x0A);
    }
}

/**
 * Delete 0 or more characters at the current position, shifting the rest of
 * the line left.
 *
 * @param count number of characters to delete
 */
void delete_character(const int count) {
    int i;

    for (i = 0; i < count; i++) {

        /*
         * cursor_x and cursor_y don't change. We just copy the existing line
         * leftwise one char.
         */
        memmove(&q_scrollback_current->chars[q_status.cursor_x],
                &q_scrollback_current->chars[q_status.cursor_x + 1],
                sizeof(wchar_t) * (Q_MAX_LINE_LENGTH - q_status.cursor_x - 1));
        memmove(&q_scrollback_current->colors[q_status.cursor_x],
                &q_scrollback_current->colors[q_status.cursor_x + 1],
                sizeof(attr_t) * (Q_MAX_LINE_LENGTH - q_status.cursor_x - 1));

        if (q_scrollback_current->length > q_status.cursor_x) {
            q_scrollback_current->length--;
        }
    }
    q_scrollback_current->dirty = Q_TRUE;
    return;
}

/**
 * Insert 0 or more spaces at the current position, shifting the rest of the
 * line right.
 *
 * @param count number of characters to insert
 */
void insert_blanks(const int count) {
    int i;

    /*
     * cursor_x and cursor_y don't change. We just copy the existing line
     * rightwise one char.
     */
    memmove(&q_scrollback_current->chars[q_status.cursor_x + count],
            &q_scrollback_current->chars[q_status.cursor_x],
            sizeof(wchar_t) * (Q_MAX_LINE_LENGTH - q_status.cursor_x - count));
    memmove(&q_scrollback_current->colors[q_status.cursor_x + count],
            &q_scrollback_current->colors[q_status.cursor_x],
            sizeof(attr_t) * (Q_MAX_LINE_LENGTH - q_status.cursor_x - count));

    for (i = 0; i < count; i++) {
        q_scrollback_current->chars[q_status.cursor_x] = ' ';
        q_scrollback_current->colors[q_status.cursor_x] = q_current_color;
        if (q_scrollback_current->length < Q_MAX_LINE_LENGTH) {
            q_scrollback_current->length++;
        }
    }
    q_scrollback_current->dirty = Q_TRUE;
    return;
}

/**
 * Save the visible portion of the scrollback buffer to file for debugging
 * purposes.
 *
 * @param file the file to save to
 */
void render_screen_to_debug_file(FILE * file) {
    struct q_scrolline_struct * line;
    int row;
    int renderable_lines;
    int i;

    fprintf(file, "\n");
    fprintf(file, "Variables:\n");
    fprintf(file, "    HEIGHT: %d STATUS_HEIGHT: %d\n", HEIGHT, STATUS_HEIGHT);
    fprintf(file, "    q_status.scrollback_lines: %d\n",
            q_status.scrollback_lines);
    fprintf(file, "    q_scrollback_buffer:   %p\n", q_scrollback_buffer);
    fprintf(file, "    q_scrollback_last:     %p\n", q_scrollback_last);
    fprintf(file, "    q_scrollback_position: %p\n", q_scrollback_position);
    fprintf(file, "    q_scrollback_current:  %p\n", q_scrollback_current);
    fprintf(file, "    q_status.cursor_x: %d\n", q_status.cursor_x);
    fprintf(file, "    q_status.cursor_y: %d\n", q_status.cursor_y);
    fprintf(file, "    q_emulation_right_margin: %d\n",
            q_emulation_right_margin);
    fprintf(file, "    vt100_wrap_line_flag:     %d\n", vt100_wrap_line_flag);
    fprintf(file, "    q_status.reverse_video:        %d\n",
            q_status.reverse_video);
    fprintf(file, "    q_status.insert_mode:          %d\n",
            q_status.insert_mode);
    fprintf(file, "    q_status.scroll_region_top:    %d\n",
            q_status.scroll_region_top);
    fprintf(file, "    q_status.scroll_region_bottom: %d\n",
            q_status.scroll_region_bottom);
    fprintf(file, "\n");

    row = HEIGHT - 1;

    /*
     * Skip the status line
     */
    if (q_status.status_visible == Q_TRUE) {
        row -= STATUS_HEIGHT;
    }

    /*
     * Let's assert that row > 0.  Konsole and xterm won't let the window
     * size reach zero so this should be a non-issue.
     */
    assert(row > 0);

    /*
     * Count the lines available
     */
    renderable_lines = 0;
    line = q_scrollback_position;
    while (row >= 0) {
        renderable_lines++;
        if (line->prev == NULL) {
            break;
        }
        line = line->prev;
        row--;
    }

    if ((row < 0) && (line->next != NULL)) {
        line = line->next;
    }

    fprintf(file, "----------------SCREEN BEGIN----------------\n");
    /*
     * Now loop from line onward
     */
    for (row = 0; row < renderable_lines; row++) {
        fprintf(file, "(%p) %d W%d H%d", line,
                line->length, line->double_width, line->double_height);
        for (i = 0; i < line->length; i++) {

            if (line->double_width == Q_TRUE) {
                if ((2 * i) >= WIDTH) {
                    break;
                }

                /*
                 * Print Q_A_PROTECT attribute
                 */
                if (line->colors[i] & Q_A_PROTECT) {
                    fprintf(file, "|");
                } else {
                    fprintf(file, " ");
                }
                fprintf(file, "%lc", (wint_t) line->chars[i]);

                /*
                 * Print Q_A_PROTECT attribute
                 */
                if (line->colors[i] & Q_A_PROTECT) {
                    fprintf(file, "|");
                } else {
                    fprintf(file, " ");
                }
                fprintf(file, " ");

            } else {

                if (i >= WIDTH) {
                    break;
                }

                /*
                 * Print Q_A_PROTECT attribute
                 */
                if (line->colors[i] & Q_A_PROTECT) {
                    fprintf(file, "|");
                } else {
                    fprintf(file, " ");
                }

                fprintf(file, "%lc", (wint_t) line->chars[i]);
            }
        }
        /*
         * Clear remainder of line
         */
        fprintf(file, "\n");

        /*
         * Point to next line
         */
        line = line->next;
    }

    for (row = renderable_lines; row < HEIGHT - STATUS_HEIGHT; row++) {
        fprintf(file, "(%p) <FILL LINE...>\n", (int *) 0x0);
    }

    fprintf(file, "----------------SCREEN END------------------\n");

}

/**
 * Reverse the foreground and background of every character in the visible
 * portion of the scrollback.
 */
void invert_scrollback_colors() {
    struct q_scrolline_struct * original_current_line;
    int row;
    int bottom;

    bottom = HEIGHT - STATUS_HEIGHT - 1;

    original_current_line = q_scrollback_current;
    q_scrollback_current = find_top_scrollback_line();
    for (row = 0; row <= bottom; row++) {
        q_scrollback_current->dirty = Q_TRUE;
        if (q_scrollback_current->reverse_color == Q_TRUE) {
            q_scrollback_current->reverse_color = Q_FALSE;
        } else {
            q_scrollback_current->reverse_color = Q_TRUE;
        }

        if (q_scrollback_current->length < WIDTH) {
            erase_line(q_scrollback_current->length, WIDTH - 1, Q_FALSE);
        }
        if ((q_scrollback_current->next == NULL) && (row < bottom)) {
            new_scrollback_line();
        }

        /*
         * Point to next line
         */
        q_scrollback_current = q_scrollback_current->next;
    }

    q_scrollback_current = original_current_line;
}

/**
 * Reverse the foreground and background of every character in the visible
 * portion of the scrollback.
 */
void deinvert_scrollback_colors() {
    /*
     * These two functions are now identical
     */
    invert_scrollback_colors();
}

/**
 * Set the double_width flag for the current line.  This will also unset
 * double-height.
 *
 * @param double_width if true, this line will be rendered double-width
 */
void set_double_width(Q_BOOL double_width) {
    q_scrollback_current->double_width = double_width;
    q_scrollback_current->double_height = 0;
}

/**
 * Set the double_height value for the current line.  This will also set
 * double-width.
 *
 * @param double_height 0, 1, 2
 */
void set_double_height(int double_height) {
    q_scrollback_current->double_width = Q_TRUE;
    q_scrollback_current->double_height = double_height;
}

/**
 * Set a number of lines to single-width.
 *
 * @param start_row the first row to set single-width
 * @param end_row the last row to set single-width
 */
void set_single_width(const int start_row, const int end_row) {
    struct q_scrolline_struct * start_line;
    struct q_scrolline_struct * line;
    struct q_scrolline_struct * original_current_line;

    int i;

    if ((start_row < 0) || (end_row < 0) || (end_row < start_row)) {
        return;
    }

    /*
     * Hang onto the original cursor position
     */
    original_current_line = q_scrollback_current;

    /*
     * Get to the starting line
     */
    start_line = find_top_scrollback_line();
    for (i = 0; i < start_row; i++) {
        if (start_line->next == NULL) {
            new_scrollback_line();
        }
        start_line = start_line->next;
    }

    line = start_line;

    for (i = start_row; i <= end_row; i++) {
        q_scrollback_current = line;

        set_double_width(Q_FALSE);

        /*
         * Note: we don't add a line when (i == end_row) because if end_row
         * is the last line in scrollback new_scrollback_line() will make the
         * total screen one line larger than it really is.  This causes lots
         * of trouble and looks like crap.
         */
        if ((line->next == NULL) && (i < end_row)) {
            /*
             * Add a line
             */
            new_scrollback_line();
        }
        line = line->next;
    }

    /*
     * Restore the cursor position
     */
    q_scrollback_current = original_current_line;

}
