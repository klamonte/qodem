/*
 * translate.h
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

#ifndef __TRANSLATE_H__
#define __TRANSLATE_H__

/* Includes --------------------------------------------------------------- */

#include "codepage.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

/**
 * Keyboard handler for the Alt-A translation table selection dialog.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void translate_table_menu_keyboard_handler(const int keystroke,
                                                  const int flags);

/**
 * Draw screen for the Alt-A translation table selection dialog.
 */
extern void translate_table_menu_refresh();

/**
 * Keyboard handler for the Alt-A translation table editor screen.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void translate_table_editor_8bit_keyboard_handler(const int keystroke,
                                                    const int flags);

/**
 * Draw screen for the Alt-A translation table editor screen.
 */
extern void translate_table_editor_8bit_refresh();

/**
 * Keyboard handler for the Alt-A translation table editor screen.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void translate_table_editor_unicode_keyboard_handler(const int keystroke,
                                                            const int flags);

/**
 * Draw screen for the Alt-A translation table editor screen.
 */
extern void translate_table_editor_unicode_refresh();

/**
 * Loads the default translate table pairs.
 */
extern void initialize_translate_tables();

/**
 * Load an 8-bit translate table pair from a file and begin using it for
 * translate_8bit().
 *
 * @param filename the basename of a file in the data directory to read from
 */
extern void use_translate_table_8bit(const char * filename);

/**
 * Load a Unicode translate table pair from a file and begin using it for
 * translate_unicode().
 *
 * @param filename the basename of a file in the data directory to read from
 */
extern void use_translate_table_unicode(const char * filename);

/**
 * Translate an 8-bit byte using the input table read via
 * use_translate_table_8bit().
 *
 * @param in the byte to translate
 * @return the translated byte
 */
extern unsigned char translate_8bit_in(const unsigned char in);

/**
 * Translate an 8-bit byte using the output table read via
 * use_translate_table_8bit().
 *
 * @param in the byte to translate
 * @return the translated byte
 */
extern unsigned char translate_8bit_out(const unsigned char in);

/**
 * Translate a Unicode code point using the input tables read via
 * use_translate_table_unicode().
 *
 * @param in the code point to translate
 * @return the translated code point
 */
extern wchar_t translate_unicode_in(const wchar_t in);

/**
 * Translate a Unicode code point using the output tables read via
 * use_translate_table_unicode().
 *
 * @param in the code point to translate
 * @return the translated code point
 */
extern wchar_t translate_unicode_out(const wchar_t in);

/**
 * Try to map a Unicode code point to an 8-bit byte of a codepage.  Use both
 * of the input/output tables read via use_translate_table_unicode(), and the
 * codepage mappings.
 *
 * @param in the code point to translate
 * @param codepage the 8-bit codepage to map to
 * @return the mapped byte, or '?' if nothing can be mapped to it
 */
extern unsigned char translate_unicode_to_8bit(const wchar_t in,
                                               const Q_CODEPAGE codepage);

#ifdef __cplusplus
}
#endif

#endif /* __TRANSLATE_H__ */
