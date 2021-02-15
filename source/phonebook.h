/*
 * phonebook.h
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

#ifndef __PHONEBOOK_H__
#define __PHONEBOOK_H__

/* Includes --------------------------------------------------------------- */

#include "common.h"             /* Q_BOOL */
#include <time.h>               /* time_t */
#include "modem.h"
#include "emulation.h"          /* Q_EMULATION */
#include "codepage.h"           /* Q_CODEPAGE */

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

/**
 * The available connection methods.
 */
typedef enum {
#ifndef Q_NO_SERIAL
    Q_DIAL_METHOD_MODEM,
#endif
    Q_DIAL_METHOD_SHELL,
    Q_DIAL_METHOD_RLOGIN,
    Q_DIAL_METHOD_SSH,
    Q_DIAL_METHOD_TELNET,
    Q_DIAL_METHOD_SOCKET,
    Q_DIAL_METHOD_COMMANDLINE
} Q_DIAL_METHOD;

#define Q_DIAL_METHOD_MAX (Q_DIAL_METHOD_COMMANDLINE + 1)

/**
 * Available doorway modes.
 */
typedef enum {
    Q_DOORWAY_CONFIG,           /* Follow the option in .qodemrc */
    Q_DOORWAY_ALWAYS_DOORWAY,   /* Always start full doorway on connect */
    Q_DOORWAY_ALWAYS_MIXED,     /* Always start mixed-mode doorway on connect */
    Q_DOORWAY_NEVER             /* Never start doorway on connect */
} Q_DOORWAY;

/**
 * A single entry in a phonebook.
 */
struct q_phone_struct {
    Q_DIAL_METHOD method;
    wchar_t * name;
    char * address;
    char * port;
    wchar_t * username;
    wchar_t * password;
    wchar_t ** notes;
    char * script_filename;
    char * capture_filename;
    char * translate_8bit_filename;
    char * translate_unicode_filename;
    char * keybindings_filename;
    Q_EMULATION emulation;
    Q_CODEPAGE codepage;
    time_t last_call;
    unsigned int times_on;
    Q_DOORWAY doorway;
    Q_BOOL use_default_toggles;
    int toggles;
    Q_BOOL tagged;

#ifndef Q_NO_SERIAL
    Q_BOOL use_modem_cfg;
    Q_BAUD_RATE baud;
    Q_DATA_BITS data_bits;
    Q_STOP_BITS stop_bits;
    Q_PARITY parity;
    Q_BOOL xonxoff;
    Q_BOOL rtscts;
    Q_BOOL lock_dte_baud;
#endif

    Q_BOOL quicklearn;

    struct q_phone_struct * next;
    struct q_phone_struct * prev;
};

/**
 * A phonebook.
 */
struct q_phonebook_struct {
    char * filename;
    int tagged;
    time_t last_save_time;
    int view_mode;
    struct q_phone_struct * entries;
    int entry_count;
    struct q_phone_struct * selected_entry;
};

#define DEFAULT_PHONEBOOK       "fonebook.txt"

/* The maximum size of one line in the phonebook file. */
#define PHONEBOOK_LINE_SIZE     1024

/* Globals ---------------------------------------------------------------- */

/**
 * The phonebook.
 */
extern struct q_phonebook_struct q_phonebook;

/**
 * The currently-connected entry.
 */
extern struct q_phone_struct * q_current_dial_entry;

/* Functions -------------------------------------------------------------- */

/**
 * Keyboard handler for the phonebook screen.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void phonebook_keyboard_handler(const int keystroke, const int flags);

/**
 * Draw screen for the phonebook, including both the phonebook and dialer
 * states.
 */
extern void phonebook_refresh();

/**
 * Reset the phonebook selection display.  This is called when the screen is
 * resized.
 */
extern void phonebook_reset();

/**
 * Load the phonebook from file.
 *
 * @param backup_version if true, load from the backup copy
 */
extern void load_phonebook(const Q_BOOL backup_version);

/**
 * Create the initial default phonebook.
 */
extern void create_phonebook();

/**
 * Return a string for a Q_DIAL_METHOD enum.
 *
 * @param method Q_DIAL_METHOD_TELNET etc.
 * @return "TELNET" etc.
 */
extern char * method_string(const Q_DIAL_METHOD method);

/**
 * Keyboard handler for the modem/connection dialer.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void dialer_keyboard_handler(const int keystroke, const int flags);

/**
 * Process raw bytes from the remote side through the modem/connection
 * dialer.  This is analogous to console_process_incoming_data().
 *
 * @param input the bytes from the remote side
 * @param input_n the number of bytes in input_n
 * @param remaining the number of un-processed bytes that should be sent
 * through a future invocation of protocol_process_data()
 * @param output a buffer to contain the bytes to send to the remote side
 * @param output_n the number of bytes that this function wrote to output
 * @param output_max the maximum number of bytes this function may write to
 * output
 */
extern void dialer_process_data(unsigned char * input,
                                const unsigned int input_n,
                                int * remaining, unsigned char * output,
                                unsigned int * output_n,
                                const unsigned int output_max);

/**
 * Set the global state based on a phonebook entry toggles.  This is used to
 * do things like selectively enable linefeed-after-cr, session log, beeps,
 * etc.
 *
 * @param toggles a bitmask of the options to change
 */
extern void set_dial_out_toggles(int toggles);

/**
 * Fix the internal page and entry indices so that the current selected entry
 * is visible in the phonebook display screen.
 */
extern void phonebook_normalize();

/**
 * This is the top-level call to "dial" the selected phonebook entry.  It
 * prompts for password if needed, sets up capture, quicklearn, etc, and
 * ultimately calls dial_out() in dialer to obtain the modem/network
 * connection.
 */
extern void do_dialer();

#ifdef __cplusplus
}
#endif

#endif /* __PHONEBOOK_H__ */
