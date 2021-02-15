/*
 * field.c
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

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "screen.h"
#include "codepage.h"
#include "field.h"

/**
 * Render a field to its window.
 *
 * @param field the field
 * @param window the curses WINDOW
 * @param active if true, use the active field color
 */
static void field_render(struct field * field, void * window, Q_BOOL active) {
    int data_i;
    int i;
    Q_COLOR color;

    if (active == Q_TRUE) {
        color = field->color_active;
    } else {
        color = field->color_inactive;
    }
    data_i = field->window_start;

    if (field->invisible == Q_TRUE) {
        for (i = 0; (i < field->width); i++) {
            screen_win_put_color_char_yx(window, field->y, field->x + i,
                                         ' ', color);
        }
        return;
    }

    for (i = 0; (data_i < field->data_n) && (i < field->width); i++, data_i++) {
        screen_win_put_color_char_yx(window, field->y, field->x + i,
                                     field->data[data_i], color);
    }
    /*
     * Pad with HATCH the rest of the way
     */
    for (; i < field->width; i++) {
        screen_win_put_color_char_yx(window, field->y, field->x + i,
                                     cp437_chars[HATCH], color);
    }
}

/**
 * Append a character to the end of the field.
 *
 * @param field the field
 * @param keystroke Q_KEY_*, see input.h.
 */
static void append_char(struct field * field, int keystroke) {
    /*
     * Append the LAST character
     */
    field->data[field->data_n] = keystroke;
    field->data_n++;
    field->position++;
    if (field->fixed == Q_TRUE) {
        if (field->position == field->width) {
            field->position--;
        }
    } else {
        if ((field->position - field->window_start) == field->width) {
            field->window_start++;
        }
    }
}

/**
 * Insert a character at the current field position.
 *
 * @param field the field
 * @param keystroke Q_KEY_*, see input.h.
 */
static void insert_char(struct field * field, int keystroke) {
    memmove(field->data + (field->position + 1),
            field->data + (field->position),
            sizeof(wchar_t) * (field->data_n - field->position));
    field->data[field->position] = keystroke;
    field->data_n++;
    field->position++;
    if ((field->position - field->window_start) == field->width) {
        assert(field->fixed == Q_FALSE);
        field->window_start++;
    }
}

/**
 * Handle a keystroke for a fieldset.
 *
 * @param fieldset the fieldset
 * @param keystroke Q_KEY_*, see input.h.
 */
void fieldset_keystroke(struct fieldset * fieldset, const int keystroke) {
    struct field * field = fieldset->active_field;
    assert(fieldset->active_field != NULL);
    assert(q_key_code_yes(keystroke) == 0);
    assert(keystroke != -1);

    /*
     * Disregard enter key
     */
    if (keystroke == Q_KEY_ENTER) {
        return;
    }

    /*
     * Disregard bracketed paste keys
     */
    if ((keystroke == Q_KEY_BRACKET_ON) || (keystroke == Q_KEY_BRACKET_OFF)) {
        return;
    }

    /*
     * Process keystroke
     */
    if ((field->position == field->data_n) && (field->data_n < field->width)) {
        /*
         * Append case
         */
        append_char(field, keystroke);
    } else if ((field->position < field->data_n)
               && (field->data_n < field->width)) {
        /*
         * Overwrite or insert a character
         */
        if (field->insert_mode == Q_FALSE) {
            /*
             * Replace character
             */
            field->data[field->position] = keystroke;
            field->position++;
        } else {
            /*
             * Insert character
             */
            insert_char(field, keystroke);
        }
    } else if ((field->position < field->data_n)
               && (field->data_n >= field->width)) {
        /*
         * Multiple cases here
         */
        if ((field->fixed == Q_TRUE) && (field->insert_mode == Q_TRUE)) {
            /*
             * Buffer is full, do nothing
             */
        } else if ((field->fixed == Q_TRUE) &&
                   (field->insert_mode == Q_FALSE)) {

            /*
             * Overwrite the last character, maybe move position
             */
            field->data[field->position] = keystroke;
            if (field->position < field->width - 1) {
                field->position++;
            }
        } else if ((field->fixed == Q_FALSE) &&
                   (field->insert_mode == Q_FALSE)) {

            /*
             * Overwrite the last character, do move position
             */
            field->data[field->position] = keystroke;
            field->position++;
        } else {
            if (field->position == field->data_n) {
                /*
                 * Append this character
                 */
                append_char(field, keystroke);
            } else {
                /*
                 * Insert this character
                 */
                insert_char(field, keystroke);
            }
        }
    } else {
        assert(field->fixed == Q_FALSE);

        /*
         * Append this character
         */
        append_char(field, keystroke);
    }

    /*
     * Update the display
     */
    fieldset_render(fieldset);
}

/**
 * Handle the home keystroke.
 *
 * @param fieldset the fieldset
 */
void fieldset_home_char(struct fieldset * fieldset) {
    struct field * field = fieldset->active_field;
    assert(fieldset->active_field != NULL);
    field->position = 0;
    field->window_start = 0;

    /*
     * Update the display
     */
    fieldset_render(fieldset);
}

/**
 * Handle the end keystroke.
 *
 * @param fieldset the fieldset
 */
void fieldset_end_char(struct fieldset * fieldset) {
    struct field * field = fieldset->active_field;
    assert(fieldset->active_field != NULL);
    field->position = field->data_n;
    if (field->fixed == Q_TRUE) {
        if (field->position >= field->width) {
            field->position = field->data_n - 1;
        }
    } else {
        field->window_start = field->data_n - field->width + 1;
        if (field->window_start < 0) {
            field->window_start = 0;
        }
    }

    /*
     * Update the display
     */
    fieldset_render(fieldset);
}

/**
 * Handle the left arrow keystroke.
 *
 * @param fieldset the fieldset
 */
void fieldset_left(struct fieldset * fieldset) {
    struct field * field = fieldset->active_field;
    assert(fieldset->active_field != NULL);
    if (field->position > 0) {
        field->position--;
    }
    if (field->fixed == Q_FALSE) {
        if ((field->position == field->window_start)
            && (field->window_start > 0)) {
            field->window_start--;
        }
    }

    /*
     * Update the display
     */
    fieldset_render(fieldset);
}

/**
 * Handle the right arrow keystroke.
 *
 * @param fieldset the fieldset
 */
void fieldset_right(struct fieldset * fieldset) {
    struct field * field = fieldset->active_field;
    assert(fieldset->active_field != NULL);
    if (field->position < field->data_n) {
        field->position++;
        if (field->fixed == Q_TRUE) {
            if (field->position == field->width) {
                field->position--;
            }
        } else {
            if ((field->position - field->window_start) == field->width) {
                field->window_start++;
            }
        }
    }

    /*
     * Update the display
     */
    fieldset_render(fieldset);
}

/**
 * Handle the backspace keystroke.
 *
 * @param fieldset the fieldset
 */
void fieldset_backspace(struct fieldset * fieldset) {
    struct field * field = fieldset->active_field;
    assert(fieldset->active_field != NULL);
    if (field->position > 0) {
        field->position--;
        memmove(field->data + field->position,
                field->data + field->position + 1,
                sizeof(wchar_t) * (field->data_n - field->position));
        field->data_n--;
    }
    if (field->fixed == Q_FALSE) {
        if ((field->position == field->window_start) &&
            (field->window_start > 0)
            ) {
            field->window_start--;
        }
    }

    /*
     * Update the display
     */
    fieldset_render(fieldset);
}

/**
 * Handle the insert keystroke.
 *
 * @param fieldset the fieldset
 */
void fieldset_insert_char(struct fieldset * fieldset) {
    struct field * field = fieldset->active_field;
    assert(fieldset->active_field != NULL);
    if (field->insert_mode == Q_TRUE) {
        field->insert_mode = Q_FALSE;
    } else {
        field->insert_mode = Q_TRUE;
    }
}

/**
 * Handle the delete keystroke.
 *
 * @param fieldset the fieldset
 */
void fieldset_delete_char(struct fieldset * fieldset) {
    struct field * field = fieldset->active_field;
    assert(fieldset->active_field != NULL);
    if ((field->data_n > 0) && (field->position < field->data_n)) {
        memmove(field->data + field->position,
                field->data + field->position + 1,
                sizeof(wchar_t) * (field->data_n - field->position));
        field->data_n--;
    }

    /*
     * Update the display
     */
    fieldset_render(fieldset);
}

/**
 * Render a fieldset to its window.
 *
 * @param fieldset the fieldset
 */
void fieldset_render(struct fieldset * fieldset) {
    int i;
    int x;
    struct field * field = fieldset->active_field;
    assert(fieldset->active_field != NULL);

    /*
     * Render the fields
     */
    for (i = 0; i < fieldset->fields_n; i++) {
        if ((fieldset->active_field == fieldset->fields[i])
            && (fieldset->inactive == Q_FALSE)) {
            field_render(fieldset->fields[i], fieldset->window, Q_TRUE);
        } else {
            field_render(fieldset->fields[i], fieldset->window, Q_FALSE);
        }
    }

    /*
     * Drop the cursor
     */
    if ((field->position > field->width) && (field->fixed == Q_TRUE)) {
        x = field->x + field->width;
    } else if ((field->position - field->window_start == field->width)
               && (field->fixed == Q_FALSE)) {
        x = field->x + field->width - 1;
    } else {
        x = field->x + field->position - field->window_start;
    }
    screen_win_move_yx(fieldset->window, field->y, x);

    /*
     * Push the changes to the display
     */
    screen_win_flush(fieldset->window);
    wcursyncup((WINDOW *) fieldset->window);
}

/**
 * Construct a new field.
 *
 * @param width the number of columns to display
 * @param toprow the row position
 * @param leftcol the column position
 * @param fixed if true, the text cannot get longer than the display width
 * @param color_active the Q_COLOR value to use when the field has focus
 * @param color_active the Q_COLOR value to use when the field does not have
 * focus
 * @return the new field
 */
struct field * field_malloc(const int width, const int toprow,
                            const int leftcol, Q_BOOL fixed,
                            Q_COLOR color_active, Q_COLOR color_inactive) {

    struct field * field =
        (struct field *) Xmalloc(sizeof(struct field), __FILE__, __LINE__);
    field->width = width;
    field->y = toprow;
    field->x = leftcol;
    field->fixed = fixed;
    field->color_active = color_active;
    field->color_inactive = color_inactive;
    field->insert_mode = Q_TRUE;
    field->position = 0;
    field->window_start = 0;
    memset(field->data, 0, sizeof(field->data));
    field->data_n = 0;
    field->invisible = Q_FALSE;
    return field;
}

/**
 * Construct a fieldset.
 *
 * @param fields the fields of the fieldset
 * @param fields_n the number of fields
 * @param window the window to draw to
 * @return the new fieldset
 */
struct fieldset * fieldset_malloc(struct field ** fields, const int fields_n,
                                  void * window) {
    struct fieldset * fieldset =
        (struct fieldset *) Xmalloc(sizeof(struct fieldset), __FILE__,
                                    __LINE__);
    fieldset->fields = fields;
    fieldset->fields_n = fields_n;
    fieldset->window = window;
    fieldset->active_field_i = 0;
    fieldset->active_field = fieldset->fields[fieldset->active_field_i];
    fieldset->inactive = Q_FALSE;
    return fieldset;
}

/**
 * Destroy a fieldset.
 *
 * @param fieldset the fieldset
 */
void fieldset_free(struct fieldset * fieldset) {
    int i;
    for (i = 0; i < fieldset->fields_n; i++) {
        Xfree(fieldset->fields[i], __FILE__, __LINE__);
    }
    Xfree(fieldset, __FILE__, __LINE__);
}

/**
 * Return the value in the field as a newly-allocated string.
 *
 * @param field the field
 * @return the field value as a newly-allocated string
 */
wchar_t * field_get_value(struct field * field) {
    wchar_t * value;
    value =
        (wchar_t *) Xmalloc(sizeof(wchar_t) * (field->data_n + 1), __FILE__,
                            __LINE__);
    memcpy(value, field->data, sizeof(wchar_t) * field->data_n);
    value[field->data_n] = 0;
    return value;
}

/**
 * Return the value in the field as a newly-allocated string.
 *
 * @param field the field
 * @return the field value as a newly-allocated string
 */
char * field_get_char_value(struct field * field) {
    char * value;
    int i;
    value =
        (char *) Xmalloc(sizeof(char) * (field->data_n + 1), __FILE__,
                         __LINE__);
    for (i = 0; i < field->data_n; i++) {
        value[i] = (char) field->data[i];
    }
    value[field->data_n] = 0;
    return value;
}

/**
 * Set the value in the field.
 *
 * @param field the field
 * @param value the new field value
 */
void field_set_value(struct field * field, const wchar_t * value) {
    assert(wcslen(value) <= Q_FIELD_SIZE);
    memcpy(field->data, value, sizeof(wchar_t) * wcslen(value));
    if (field->fixed == Q_TRUE) {
        assert(wcslen(value) <= field->width);
        field->data_n = wcslen(value);
        field->position = wcslen(value);
        if (field->position >= field->width) {
            field->position = field->data_n - 1;
        }
        field->window_start = 0;
    } else {
        field->data_n = wcslen(value);
        field->position = wcslen(value);
        field->window_start = field->data_n - field->width + 1;
        if (field->window_start < 0) {
            field->window_start = 0;
        }
    }
}

/**
 * Set the value in the field.
 *
 * @param field the field
 * @param value the new field value
 */
void field_set_char_value(struct field * field, const char * value) {
    unsigned int i;
    assert(strlen(value) <= Q_FIELD_SIZE);

    if (field->fixed == Q_TRUE) {
        assert(strlen(value) <= field->width);
        field->data_n = strlen(value);
        field->position = strlen(value);
        if (field->position >= field->width) {
            field->position = field->data_n - 1;
        }
        field->window_start = 0;
    } else {
        field->data_n = strlen(value);
        field->position = strlen(value);
        field->window_start = field->data_n - field->width + 1;
        if (field->window_start < 0) {
            field->window_start = 0;
        }
    }
    for (i = 0; i < strlen(value); i++) {
        field->data[i] = value[i];
    }
}

/**
 * Switch focus to the next field.
 *
 * @param fieldset the fieldset
 */
void fieldset_next_field(struct fieldset * fieldset) {
    if (fieldset->active_field_i < fieldset->fields_n - 1) {
        fieldset->active_field_i++;
    }
    fieldset->active_field = fieldset->fields[fieldset->active_field_i];
}

/**
 * Switch focus to the previous field.
 *
 * @param fieldset the fieldset
 */
void fieldset_prev_field(struct fieldset * fieldset) {
    if (fieldset->active_field_i > 0) {
        fieldset->active_field_i--;
    }
    fieldset->active_field = fieldset->fields[fieldset->active_field_i];
}
