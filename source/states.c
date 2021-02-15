/*
 * states.c
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

#include <assert.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include "qodem.h"
#include "console.h"
#include "script.h"
#include "protocols.h"
#include "keyboard.h"
#include "translate.h"
#include "screen.h"
#include "options.h"
#include "states.h"

/**
 * Global state.
 */
Q_PROGRAM_STATE q_program_state;

/**
 * Whether or not the keyboard is supposed to be blocking (last argument to
 * nodelay()).
 */
Q_BOOL q_keyboard_blocks;

/**
 * Look for input from the keyboard and mouse.  If input came in, dispatch it
 * to the appropriate keyboard handler for the current program state.
 */
void keyboard_handler() {

    int flags = 0;              /* passed to *_keyboard_handler() */
    int keystroke;              /* " */

    /*
     * Grab keystroke
     */
    if (q_keyboard_blocks == Q_TRUE) {
        qodem_getch(&keystroke, &flags, Q_KEYBOARD_DELAY);
    } else {
        qodem_getch(&keystroke, &flags, 0);
    }

    if (keystroke == Q_ERR) {
        /*
         * No data, return
         */
        return;
    }

    switch (q_program_state) {

    case Q_STATE_CONSOLE:
        if (q_status.quicklearn == Q_FALSE) {
            console_keyboard_handler(keystroke, flags);
        } else {
            console_quicklearn_keyboard_handler(keystroke, flags);
        }
        break;
    case Q_STATE_CONSOLE_MENU:
        console_menu_keyboard_handler(keystroke, flags);
        break;
    case Q_STATE_INFO:
        console_info_keyboard_handler(keystroke, flags);
        break;
    case Q_STATE_SCROLLBACK:
        scrollback_keyboard_handler(keystroke, flags);
        break;
    case Q_STATE_SCRIPT_EXECUTE:
        script_keyboard_handler(keystroke, flags);
        break;
    case Q_STATE_HOST:
        host_keyboard_handler(keystroke, flags);
        break;

    case Q_STATE_DOWNLOAD_MENU:
    case Q_STATE_UPLOAD_MENU:
        protocol_menu_keyboard_handler(keystroke, flags);
        break;
    case Q_STATE_DOWNLOAD_PATHDIALOG:
    case Q_STATE_UPLOAD_PATHDIALOG:
    case Q_STATE_UPLOAD_BATCH_DIALOG:
        protocol_pathdialog_keyboard_handler(keystroke, flags);
        break;
    case Q_STATE_UPLOAD:
    case Q_STATE_UPLOAD_BATCH:
    case Q_STATE_DOWNLOAD:
        protocol_transfer_keyboard_handler(keystroke, flags);
        break;

    case Q_STATE_EMULATION_MENU:
        emulation_menu_keyboard_handler(keystroke, flags);
        break;

    case Q_STATE_TRANSLATE_MENU:
        translate_table_menu_keyboard_handler(keystroke, flags);
        break;

    case Q_STATE_TRANSLATE_EDITOR_8BIT:
        translate_table_editor_8bit_keyboard_handler(keystroke, flags);
        break;

    case Q_STATE_TRANSLATE_EDITOR_UNICODE:
        translate_table_editor_unicode_keyboard_handler(keystroke, flags);
        break;

    case Q_STATE_PHONEBOOK:
        phonebook_keyboard_handler(keystroke, flags);
        break;

    case Q_STATE_DIALER:
        dialer_keyboard_handler(keystroke, flags);
        break;

    case Q_STATE_SCREENSAVER:
        screensaver_keyboard_handler(keystroke, flags);
        break;

    case Q_STATE_FUNCTION_KEY_EDITOR:
        function_key_editor_keyboard_handler(keystroke, flags);
        break;

#ifndef Q_NO_SERIAL
    case Q_STATE_MODEM_CONFIG:
        modem_config_keyboard_handler(keystroke, flags);
        break;
#endif

    case Q_STATE_CODEPAGE:
        codepage_keyboard_handler(keystroke, flags);
        break;

    case Q_STATE_INITIALIZATION:
    case Q_STATE_EXIT:
        /*
         * Program BUG
         */
        abort();
    }
}

/**
 * Dispatch to the appropriate draw function for the current program state.
 */
void refresh_handler() {

#ifdef Q_PDCURSES_WIN32
    static long last_time = 1000000;
#else
    static suseconds_t last_time = 1000000;
#endif

    struct timeval tv;

    switch (q_program_state) {

    case Q_STATE_CONSOLE:
        /*
         * Only update the console 16 times a second when in flood.
         */
        if (q_console_flood == Q_TRUE) {
            gettimeofday(&tv, NULL);
            if ((tv.tv_usec < last_time) || (tv.tv_usec - last_time > 62500)) {
                console_refresh(Q_TRUE);
                last_time = tv.tv_usec;
            }
        } else {
            console_refresh(Q_TRUE);
        }
        break;
    case Q_STATE_SCRIPT_EXECUTE:
        /*
         * Only update the console 8 times a second
         */
        gettimeofday(&tv, NULL);
        if ((tv.tv_usec < last_time) || (tv.tv_usec - last_time > 125000)) {
            script_refresh();
            last_time = tv.tv_usec;
        }
        break;
    case Q_STATE_HOST:
        /*
         * Only update the console 8 times a second
         */
        gettimeofday(&tv, NULL);
        if ((tv.tv_usec < last_time) || (tv.tv_usec - last_time > 125000)) {
            host_refresh();
            last_time = tv.tv_usec;
        }
        break;
    case Q_STATE_CONSOLE_MENU:
        console_menu_refresh();
        break;
    case Q_STATE_INFO:
        console_info_refresh();
        break;
    case Q_STATE_SCROLLBACK:
        scrollback_refresh();
        break;

    case Q_STATE_DOWNLOAD_MENU:
    case Q_STATE_UPLOAD_MENU:
        protocol_menu_refresh();
        break;
    case Q_STATE_DOWNLOAD_PATHDIALOG:
    case Q_STATE_UPLOAD_PATHDIALOG:
    case Q_STATE_UPLOAD_BATCH_DIALOG:
        protocol_pathdialog_refresh();
        break;
    case Q_STATE_UPLOAD:
    case Q_STATE_UPLOAD_BATCH:
    case Q_STATE_DOWNLOAD:
        protocol_transfer_refresh();
        break;

    case Q_STATE_EMULATION_MENU:
        emulation_menu_refresh();
        break;

    case Q_STATE_TRANSLATE_MENU:
        translate_table_menu_refresh();
        break;

    case Q_STATE_TRANSLATE_EDITOR_8BIT:
        translate_table_editor_8bit_refresh();
        break;

    case Q_STATE_TRANSLATE_EDITOR_UNICODE:
        translate_table_editor_unicode_refresh();
        break;

    case Q_STATE_PHONEBOOK:
    case Q_STATE_DIALER:
        phonebook_refresh();
        break;

    case Q_STATE_SCREENSAVER:
        screensaver_refresh();
        break;

    case Q_STATE_FUNCTION_KEY_EDITOR:
        function_key_editor_refresh();
        break;

#ifndef Q_NO_SERIAL
    case Q_STATE_MODEM_CONFIG:
        modem_config_refresh();
        break;
#endif

    case Q_STATE_CODEPAGE:
        codepage_refresh();
        break;

    case Q_STATE_INITIALIZATION:
    case Q_STATE_EXIT:
        /*
         * Program BUG
         */
        abort();

    }
}

/**
 * Switch to a new state, handling things like visible cursor, blocking
 * keyboard, etc.
 *
 * @param new_state state to switch to
 */
void switch_state(const Q_PROGRAM_STATE new_state) {

    if ((q_program_state == Q_STATE_CONSOLE) &&
        (has_true_doublewidth() == Q_TRUE)) {

        screen_clear();
    }

    switch (new_state) {

    case Q_STATE_DOWNLOAD_MENU:
    case Q_STATE_UPLOAD_MENU:
    case Q_STATE_DOWNLOAD_PATHDIALOG:
    case Q_STATE_UPLOAD_PATHDIALOG:
    case Q_STATE_EMULATION_MENU:
    case Q_STATE_TRANSLATE_MENU:
    case Q_STATE_INITIALIZATION:
    case Q_STATE_UPLOAD_BATCH_DIALOG:
    case Q_STATE_CODEPAGE:
        set_blocking_input(Q_TRUE);
        q_keyboard_blocks = Q_TRUE;
        q_screen_dirty = Q_TRUE;
        q_cursor_on();
        break;

    case Q_STATE_SCROLLBACK:
        if (q_scrollback_search_string != NULL) {
            Xfree(q_scrollback_search_string, __FILE__, __LINE__);
            q_scrollback_search_string = NULL;
        }
        q_scrollback_highlight_search_string = Q_FALSE;
        /*
         * Fall through...
         */
    case Q_STATE_DIALER:
    case Q_STATE_UPLOAD:
    case Q_STATE_UPLOAD_BATCH:
    case Q_STATE_DOWNLOAD:
    case Q_STATE_CONSOLE_MENU:
    case Q_STATE_INFO:
    case Q_STATE_SCRIPT_EXECUTE:
        set_blocking_input(Q_FALSE);
        q_keyboard_blocks = Q_FALSE;
        q_screen_dirty = Q_TRUE;
        q_cursor_off();
        original_state = q_program_state;
        break;

    case Q_STATE_HOST:
        set_blocking_input(Q_FALSE);
        q_keyboard_blocks = Q_FALSE;
        q_screen_dirty = Q_TRUE;
        q_cursor_on();
        break;

    case Q_STATE_CONSOLE:
        set_blocking_input(Q_FALSE);
        q_keyboard_blocks = Q_FALSE;
        screen_clear();
        q_screen_dirty = Q_TRUE;
        if (q_status.split_screen == Q_TRUE) {
            q_split_screen_dirty = Q_TRUE;
        }
        if (((q_status.emulation == Q_EMUL_LINUX)
             || (q_status.emulation == Q_EMUL_LINUX_UTF8)
             || (q_status.emulation == Q_EMUL_VT220))
            && (q_status.visible_cursor == Q_FALSE)) {
            q_cursor_off();
        } else {
            q_cursor_on();
        }
        break;

    case Q_STATE_FUNCTION_KEY_EDITOR:
        /*
         * Fall through ...
         */
        original_state = q_program_state;
#ifndef Q_NO_SERIAL
    case Q_STATE_MODEM_CONFIG:
#endif
    case Q_STATE_PHONEBOOK:
    case Q_STATE_TRANSLATE_EDITOR_8BIT:
    case Q_STATE_TRANSLATE_EDITOR_UNICODE:
        set_blocking_input(Q_TRUE);
        q_keyboard_blocks = Q_TRUE;
        q_screen_dirty = Q_TRUE;
        q_cursor_off();
        break;

    case Q_STATE_EXIT:
        q_cursor_on();
        break;

    case Q_STATE_SCREENSAVER:
        if (q_program_state == Q_STATE_SCREENSAVER) {
            break;
        }
        original_state = q_program_state;
        set_blocking_input(Q_TRUE);
        q_keyboard_blocks = Q_TRUE;
        q_cursor_off();
        q_screen_dirty = Q_TRUE;
        break;

    }

    q_program_state = new_state;
}

/* Screensaver feature ------------------------------------------------------ */

/**
 * Maximum password length is 64 chars.
 */
static char password_buffer[64];
static int password_buffer_n = 0;

/**
 * State we were in before the screensaver was activated.
 */
Q_PROGRAM_STATE original_state;

/**
 * Keyboard handler for the screensaver.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
void screensaver_keyboard_handler(const int keystroke, const int flags) {

    if (keystroke == Q_KEY_ENTER) {
        if (password_buffer_n > 0) {
            if (strcmp(password_buffer,
                       get_option(Q_OPTION_SCREENSAVER_PASSWORD)) == 0) {

                /*
                 * UNLOCK
                 */
                switch_state(original_state);
                qlog(_("SCREENSAVER ending, returning to original state %u...\n"),
                     original_state);
            } else {
                qlog(_("SCREENSAVER invalid password entered.\n"));
            }
        }
        memset(password_buffer, 0, sizeof(password_buffer));
        password_buffer_n = 0;

    } else {
        if (q_key_code_yes(keystroke) == 0) {
            password_buffer[password_buffer_n] = keystroke & 0xFF;
            password_buffer_n++;
            if (password_buffer_n == sizeof(password_buffer)) {
                memset(password_buffer, 0, sizeof(password_buffer));
                password_buffer_n = 0;
            }
        }
    }
}

/**
 * Draw screen for the screensaver.
 */
void screensaver_refresh() {
    screen_clear();
    screen_put_color_str_yx(HEIGHT - 1, 0, _("Enter password to unlock: "),
                            Q_COLOR_CONSOLE);
    screen_flush();
}
