/*
 * field.h
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

#ifndef __FIELD_H__
#define __FIELD_H__

/* Includes --------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

#define Q_FIELD_SIZE 256

/**
 * This encapsulates a text field on the screen.  Every field has a height of
 * 1.
 */
struct field {

    /**
     * Data in field.
     */
    wchar_t data[Q_FIELD_SIZE];
    int data_n;

    /**
     * Window coordinate X.
     */
    int x;

    /**
     * Window coordinate Y.
     */
    int y;

    /**
     * Visible width on screen.
     */
    int width;

    /**
     * If true, only allow enough characters that will fit in the width.  If
     * false, allow the field to scroll to the right.
     */
    Q_BOOL fixed;

    /**
     * Current editing position within data[].
     */
    int position;

    /**
     * Beginning of visible portion.
     */
    int window_start;

    /**
     * If true, new characters are inserted at position.
     */
    Q_BOOL insert_mode;

    /**
     * One of the Q_COLOR colors.
     */
    Q_COLOR color_active;

    /**
     * One of the Q_COLOR colors.
     */
    Q_COLOR color_inactive;

    /**
     * If true, this field will be rendered as background.
     */
    Q_BOOL invisible;

};

/**
 * A fieldset is a collection of fields.
 */
struct fieldset {
    /**
     * The rendering window.
     */
    void * window;

    /**
     * All fields in this fieldset.
     */
    struct field ** fields;
    int fields_n;

    /**
     * The currently-selected field.
     */
    struct field * active_field;
    int active_field_i;

    /**
     * Special case for phonebook.
     */
    Q_BOOL inactive;
};

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

/**
 * Handle a keystroke for a fieldset.
 *
 * @param fieldset the fieldset
 * @param keystroke Q_KEY_*, see input.h.
 */
extern void fieldset_keystroke(struct fieldset * fieldset, const int keystroke);

/**
 * Handle the left arrow keystroke.
 *
 * @param fieldset the fieldset
 */
extern void fieldset_left(struct fieldset * fieldset);

/**
 * Handle the right arrow keystroke.
 *
 * @param fieldset the fieldset
 */
extern void fieldset_right(struct fieldset * fieldset);

/**
 * Handle the backspace keystroke.
 *
 * @param fieldset the fieldset
 */
extern void fieldset_backspace(struct fieldset * fieldset);

/**
 * Handle the delete keystroke.
 *
 * @param fieldset the fieldset
 */
extern void fieldset_delete_char(struct fieldset * fieldset);

/**
 * Handle the insert keystroke.
 *
 * @param fieldset the fieldset
 */
extern void fieldset_insert_char(struct fieldset * fieldset);

/**
 * Handle the home keystroke.
 *
 * @param fieldset the fieldset
 */
extern void fieldset_home_char(struct fieldset * fieldset);

/**
 * Handle the end keystroke.
 *
 * @param fieldset the fieldset
 */
extern void fieldset_end_char(struct fieldset * fieldset);

/**
 * Switch focus to the next field.
 *
 * @param fieldset the fieldset
 */
extern void fieldset_next_field(struct fieldset * fieldset);

/**
 * Switch focus to the previous field.
 *
 * @param fieldset the fieldset
 */
extern void fieldset_prev_field(struct fieldset * fieldset);

/**
 * Return the value in the field as a newly-allocated string.
 *
 * @param field the field
 * @return the field value as a newly-allocated string
 */
extern wchar_t * field_get_value(struct field * field);

/**
 * Return the value in the field as a newly-allocated string.
 *
 * @param field the field
 * @return the field value as a newly-allocated string
 */
extern char * field_get_char_value(struct field * field);

/**
 * Set the value in the field.
 *
 * @param field the field
 * @param value the new field value
 */
extern void field_set_value(struct field * field, const wchar_t * value);

/**
 * Set the value in the field.
 *
 * @param field the field
 * @param value the new field value
 */
extern void field_set_char_value(struct field * field, const char * value);

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
extern struct field * field_malloc(const int width, const int toprow,
                                   const int leftcol, Q_BOOL fixed,
                                   Q_COLOR color_active,
                                   Q_COLOR color_inactive);

/**
 * Render a fieldset to its window.
 *
 * @param fieldset the fieldset
 */
extern void fieldset_render(struct fieldset * fieldset);

/**
 * Construct a fieldset.
 *
 * @param fields the fields of the fieldset
 * @param fields_n the number of fields
 * @param window the window to draw to
 * @return the new fieldset
 */
extern struct fieldset * fieldset_malloc(struct field ** fields,
                                         const int fields_n, void * window);

/**
 * Destroy a fieldset.
 *
 * @param fieldset the fieldset
 */
extern void fieldset_free(struct fieldset * fieldset);

#ifdef __cplusplus
}
#endif

#endif /* __FIELD_H__ */
