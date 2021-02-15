/*
 * help.h
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

#ifndef __HELP_H__
#define __HELP_H__

/* Includes --------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

/**
 * The available entry points into the online help system.
 */
typedef enum {
    Q_HELP_BATCH_ENTRY_WINDOW,          /* At the Batch Entry Window */

#ifndef Q_NO_SERIAL
    Q_HELP_MODEM_CONFIG,                /* Modem configuration screen */
    Q_HELP_COMM_PARMS,                  /* Serial port parameters screen */
#endif

    Q_HELP_PHONEBOOK,                   /* Dialing directory */
    Q_HELP_PHONEBOOK_REVISE_ENTRY,      /* Dialing directory */
    Q_HELP_CONSOLE_MENU,                /* Console menu */
    Q_HELP_PROTOCOLS,                   /* Upload/Download file menu */
    Q_HELP_EMULATION_MENU,              /* Emulation select menu */
    Q_HELP_TRANSLATE_EDITOR,            /* Translation table editor */
    Q_HELP_CODEPAGE,                    /* Codepage dialog */
    Q_HELP_FUNCTION_KEYS,               /* Function key editor */
} Q_HELP_SCREEN;

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

/**
 * This must be called to initialize the help screens from the long raw help
 * text string.
 */
extern void setup_help();

/**
 * Enter the online help system.
 *
 * @param help_screen the screen to start with
 */
extern void launch_help(Q_HELP_SCREEN help_screen);

#ifdef __cplusplus
}
#endif

#endif /* __HELP_H__ */
