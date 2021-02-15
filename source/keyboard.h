/*
 * keyboard.h
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

#ifndef __KEYBOARD_H__
#define __KEYBOARD_H__

/* Includes --------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

/* Maximum length of a keyboard macro */
#define KEYBOARD_MACRO_SIZE 128

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

/**
 * This must be called to initialize the various keyboards from the config
 * files.
 */
extern void initialize_keyboard();

/**
 * Send a local keystroke to the remote side.
 *
 * @param keystroke the keypress
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void post_keystroke(const int keystroke, const int flags);

/**
 * Keyboard handler for the Alt-J function key editor screen.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void function_key_editor_keyboard_handler(const int keystroke,
                                                 const int flags);
/**
 * Draw screen for the Alt-J function key editor screen.
 */
extern void function_key_editor_refresh();

/**
 * Load keybindings from a filename into the currently-bound keyboard.
 *
 * @param filename name of file with key bindings
 */
extern void switch_current_keyboard(const char * filename);

/**
 * Create the config files for the keybindings (default.key, ansi.key,
 * vt100.key, etc.)
 */
extern void create_keybindings_files();

#ifdef __cplusplus
}
#endif

#endif /* __KEYBOARD_H__ */
