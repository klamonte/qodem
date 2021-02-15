/*
 * dialer.h
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

#ifndef __DIALER_H__
#define __DIALER_H__

/* Includes --------------------------------------------------------------- */

#include <time.h>
#include "phonebook.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

/**
 * The states for the phonebook dialer display used during Q_STATE_DIALER.
 */
typedef enum {
    Q_DIAL_DIALING,
    Q_DIAL_MANUAL_CYCLE,
    Q_DIAL_CYCLE,
    Q_DIAL_KILLED,
    Q_DIAL_LINE_BUSY,
    Q_DIAL_BETWEEN_PAUSE,
    Q_DIAL_NO_NUMBERS_LEFT,
    Q_DIAL_USER_ABORTED,
    Q_DIAL_CONNECTED
} Q_DIAL_STATE;

/* Globals ---------------------------------------------------------------- */

/**
 * Our current dialing state.
 */
extern Q_DIAL_STATE q_dial_state;

/**
 * When we started dialing.
 */
extern time_t q_dialer_start_time;

/**
 * How much time is left (in seconds) on the cycle clock.
 */
extern time_t q_dialer_cycle_time;

/**
 * When the cycle clock started.
 */
extern time_t q_dialer_cycle_start_time;

/**
 * How many calls have been attempted.
 */
extern int q_dialer_attempts;

/**
 * The status line to report on the redialer screen.
 */
extern char q_dialer_status_message[DIALOG_MESSAGE_SIZE];

/**
 * The modem line to report on the redialer screen.
 */
extern char q_dialer_modem_message[DIALOG_MESSAGE_SIZE];

/* Functions -------------------------------------------------------------- */

/**
 * Convert a command line string with spaces into a NULL-terminated array of
 * strings appropriate to passing to the execvp().
 *
 * @param argv the command line string
 * @return the array
 */
extern char ** tokenize_command(const char * argv);

/**
 * Connect to a remote system.
 *
 * @param number the phonebook entry to connect to
 */
extern void dial_out(struct q_phone_struct * number);

/**
 * Set a file descriptor or Winsock socket handle to non-blocking mode.
 *
 * @param fd the descriptor
 */
extern void set_nonblock(const int fd);

/**
 * Set a file descriptor or Winsock socket handle to blocking mode.
 *
 * @param fd the descriptor
 */
extern void set_blocking(const int fd);

/**
 * Set a tty into raw mode.
 *
 * @param tty_fd the tty descriptor
 */
extern void set_raw_termios(const int tty_fd);

/**
 * Called upon the completion of a successful connection.
 */
extern void dial_success();

/**
 * See if a dialup script was specified, and if so execute it.
 */
extern void check_for_dialup_script();

#ifdef __cplusplus
}
#endif

#endif /* __DIALER_H__ */
