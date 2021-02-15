/*
 * host.h
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

#ifndef __HOST_H__
#define __HOST_H__

/* Includes --------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

/**
 * The available ways host mode can listen for new connections.
 */
typedef enum {

#ifndef Q_NO_SERIAL

    /**
     * Wait for modem to ring, and then answer it.
     */
    Q_HOST_TYPE_MODEM,

    /**
     * Listen on a serial port.
     */
    Q_HOST_TYPE_SERIAL,

#endif

    /**
     * Listen on a socket.
     */
    Q_HOST_TYPE_SOCKET,

    /**
     * Listen on socket and speak the telnet server protocol.
     */
    Q_HOST_TYPE_TELNETD,

#ifdef Q_SSH_CRYPTLIB
    /**
     * Listen on socket and speak the ssh server protocol.
     */
    Q_HOST_TYPE_SSHD,
#endif

} Q_HOST_TYPE;

/* Globals ---------------------------------------------------------------- */

/**
 * When in host mode, the type of host.  This is analagous to q_dial_method.
 */
extern Q_HOST_TYPE q_host_type;

/**
 * Whether or not host mode is active, even through file transfers.
 */
extern Q_BOOL q_host_active;

/* Functions -------------------------------------------------------------- */

/**
 * Begin host mode.
 *
 * @param type the method to listen for
 * @param port the port to listen on for network hosts.  This can also be
 * NEXT_AVAILABLE_PORT_STRING (see netclient.h).
 */
extern void host_start(Q_HOST_TYPE type, const char * port);

/**
 * Keyboard handler for host mode.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void host_keyboard_handler(const int keystroke, const int flags);

/**
 * Draw screen for host mode.
 */
extern void host_refresh();

/**
 * Process raw bytes from the remote side through the host micro-BBS.  See
 * also console_process_incoming_data().
 *
 * @param input the bytes from the remote side
 * @param input_n the number of bytes in input_n
 * @param remaining the number of un-processed bytes that should be sent
 * through a future invocation of host_process_data()
 * @param output a buffer to contain the bytes to send to the remote side
 * @param output_n the number of bytes that this function wrote to output
 * @param output_max the maximum number of bytes this function may write to
 * output
 */
extern void host_process_data(unsigned char * input, const unsigned int input_n,
                              int * remaining, unsigned char * output,
                              unsigned int * output_n,
                              const unsigned int output_max);

#ifdef __cplusplus
}
#endif

#endif /* __HOST_H__ */
