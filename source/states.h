/*
 * states.h
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

#ifndef __STATES_H__
#define __STATES_H__

/* Includes --------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

/**
 * Program states.  First state is always initialization, from there it can
 * be dialing directory or console.
 */
typedef enum Q_PROGRAM_STATES {
    Q_STATE_INITIALIZATION,     /* Initialization */

    /**
     * Modem/network dialer.  See phonebook.c and modem.c.
     */
    Q_STATE_DIALER,             /* Modem dialer */

#ifndef Q_NO_SERIAL
    Q_STATE_MODEM_CONFIG,       /* Modem configuration screen */
#endif

    /**
     * Phonebook.  See phonebook.c.
     */
    Q_STATE_PHONEBOOK,          /* Dialing directory */

    /**
     * TERMINAL mode.  See console.c.
     */
    Q_STATE_CONSOLE,            /* Console */
    Q_STATE_CONSOLE_MENU,       /* Console menu */
    Q_STATE_SCROLLBACK,         /* Console scrollback */
    Q_STATE_INFO,               /* Program info screen */

    /**
     * Uploads and downloads.  See protocols.c.
     */
    Q_STATE_DOWNLOAD,                   /* Downloading file */
    Q_STATE_DOWNLOAD_MENU,              /* Download file menu */
    Q_STATE_DOWNLOAD_PATHDIALOG,        /* Download file/path dialog */
    Q_STATE_UPLOAD,                     /* Uploading file */
    Q_STATE_UPLOAD_BATCH,               /* Uploading many files */
    Q_STATE_UPLOAD_BATCH_DIALOG,        /* Uploading many files dialog */
    Q_STATE_UPLOAD_MENU,                /* Upload file menu */
    Q_STATE_UPLOAD_PATHDIALOG,          /* Upload file/path dialog */

    /**
     * Screensaver.  It's so small that it is just combined with states.c.
     */
    Q_STATE_SCREENSAVER,        /* Screensaver active */

    /**
     * Emulations.  See emulation.c.
     */
    Q_STATE_EMULATION_MENU,     /* Emulation select menu */

    /**
     * Function key editor.  See keyboard.c.
     */
    Q_STATE_FUNCTION_KEY_EDITOR,        /* Function key editor */

    /**
     * Translate table.  See translate.c.
     */
    Q_STATE_TRANSLATE_MENU,             /* Translation table select menu */
    Q_STATE_TRANSLATE_EDITOR_8BIT,      /* Translation table editor (8-bit) */
    Q_STATE_TRANSLATE_EDITOR_UNICODE,   /* Translation table editor (Unicode) */

    /**
     * Codepage dialog.  See codepage.c.
     */
    Q_STATE_CODEPAGE,           /* Codepage dialog */

    /**
     * Script execution.  See script.c.
     */
    Q_STATE_SCRIPT_EXECUTE,     /* Script executing */

    /**
     * Host mode.  See host.c.
     */
    Q_STATE_HOST,               /* In host mode */

    Q_STATE_EXIT                /* Exit program */
} Q_PROGRAM_STATE;

/* Globals ---------------------------------------------------------------- */

/**
 * Global state.
 */
extern Q_PROGRAM_STATE q_program_state;

/**
 * Whether or not the keyboard is supposed to be blocking (last argument to
 * nodelay()).
 */
extern Q_BOOL q_keyboard_blocks;

/**
 * State we were in before the screensaver was activated.
 */
extern Q_PROGRAM_STATE original_state;

/* Functions -------------------------------------------------------------- */

/**
 * Switch to a new state, handling things like visible cursor, blocking
 * keyboard, etc.
 *
 * @param new_state state to switch to
 */
extern void switch_state(const Q_PROGRAM_STATE new_state);

/**
 * Look for input from the keyboard and mouse.  If input came in, dispatch it
 * to the appropriate keyboard handler for the current program state.
 */
extern void keyboard_handler();

/**
 * Dispatch to the appropriate draw function for the current program state.
 */
extern void refresh_handler();

/**
 * Keyboard handler for the screensaver.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void screensaver_keyboard_handler(const int keystroke, const int flags);

/**
 * Draw screen for the screensaver.
 */
extern void screensaver_refresh();

#ifdef __cplusplus
}
#endif

#endif /* __STATES_H__ */
