/*
 * qodem.c
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

#include <stdlib.h>
#ifdef Q_PDCURSES_WIN32
#include <tchar.h>
#include <windows.h>
#include <io.h>
#else
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <pwd.h>
#endif
#include <errno.h>
#include <string.h>
#include <assert.h>

#ifdef Q_SSH_CRYPTLIB
#include <sys/time.h>

/*
 * SSH uses cryptlib, it's a very straightforward library.  We need to define
 * __WINDOWS__ or __UNIX__ before loading crypt.h.
 */
#ifdef Q_PDCURSES_WIN32
#define __WINDOWS__
#else
#define __UNIX__
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <crypt.h>

#ifdef __cplusplus
}
#endif

#endif

#include "qodem.h"
#include "screen.h"
#include "protocols.h"
#include "console.h"
#include "keyboard.h"
#include "translate.h"
#include "music.h"
#include "states.h"
#include "options.h"
#include "dialer.h"
#include "script.h"
#include "help.h"
#include "netclient.h"
#include "getopt.h"

/* Set this to a not-NULL value to enable debug log. */
/* static const char * DLOGNAME = "qodem"; */
static const char * DLOGNAME = NULL;

/*
 * Define this to enable random line noise on both sides of the link.  There
 * is a random chance that 1 in every line_noise_per_bytes/2 is turned into
 * crap.
 */
/* #define LINE_NOISE 1 */
#undef LINE_NOISE
#ifdef LINE_NOISE
static const int line_noise_per_bytes = 10000;
static Q_BOOL noise_stop = Q_FALSE;
#endif /* LINE_NOISE */

/* Global exit return code */
static int q_exitrc = EXIT_OK;

/**
 * The TTY name of the child TTY.
 */
char * q_child_ttyname = NULL;

/**
 * The child TTY descriptor.  For POSIX, this is the same descriptor for
 * command line programs, network connections, and serial port.  For Windows,
 * this is only for network connections.
 */
int q_child_tty_fd = -1;

/**
 * The child process ID.
 */
pid_t q_child_pid = -1;

#ifdef Q_PDCURSES_WIN32

/*
 * Win32 case: we have to create pipes that are connected to the child
 * process' streams.  These are initialized in spawn_process() and stored in
 * dialer.c.
 */
extern HANDLE q_child_stdin;
extern HANDLE q_child_stdout;
extern HANDLE q_child_process;
extern HANDLE q_child_thread;
extern HANDLE q_script_stdout;

#ifndef Q_NO_SERIAL

/**
 * The serial port handle, stored in modem.c.
 */
extern HANDLE q_serial_handle;

/**
 * If true, the serial port was readable on the last call to
 * WaitCommEvent().
 */
static Q_BOOL q_serial_readable;

#endif /* Q_NO_SERIAL */

#else

/**
 * If true, we have received a SIGCHLD that matches q_child_pid.
 */
Q_BOOL q_child_exited = Q_FALSE;

#endif /* Q_PDCURSES_WIN32 */

/**
 * Global status struct.
 */
struct q_status_struct q_status;

/**
 * The physical screen width.
 */
int WIDTH;

/**
 * The physical screen height.
 */
int HEIGHT;

/**
 * The height of the status bar.  Currently this is either 0 or 1, but in the
 * future it could become several lines.
 */
int STATUS_HEIGHT;

/**
 * The base working directory where qodem stores its config files and
 * phonebook.  For POSIX this is usually ~/.qodem, for Windows it is My
 * Documents\\qodem.
 */
char * q_home_directory = NULL;

/**
 * The screensaver timeout in seconds.
 */
int q_screensaver_timeout;

/**
 * How long it's been since user input came in, stored in input.c.
 */
extern time_t screensaver_time;

/**
 * The keepalive timeout in seconds.
 */
int q_keepalive_timeout;

/**
 * The bytes to send to the remote side when the keepalive timeout is
 * reached.
 */
char q_keepalive_bytes[128];

/**
 * The number of bytes in the q_keepalive_bytes buffer.
 */
unsigned int q_keepalive_bytes_n;

/*
 * The input buffer for raw bytes seen from the remote side.
 */
static unsigned char q_buffer_raw[Q_BUFFER_SIZE];
static int q_buffer_raw_n = 0;

/*
 * The output buffer used by qodem_buffered_write() and
 * qodem_buffered_write_flush().
 */
static char * buffered_write_buffer = NULL;
static int buffered_write_buffer_i = 0;
static int buffered_write_buffer_n = 0;

/*
 * The output buffer for sending raw bytes to the remote side.  This is used
 * by the modem dialer, scripts, host mode, and file transfer protocols.
 * Console (keyboard) output does not use this, that is sent directly to
 * qodem_write().
 */
static unsigned char q_transfer_buffer_raw[Q_BUFFER_SIZE];
static unsigned int q_transfer_buffer_raw_n = 0;

/* These are used by the select() call in data_handler() */
static fd_set readfds;
static fd_set writefds;
static fd_set exceptfds;

/* The last time we saw data. */
static time_t data_time;

/**
 * The last time we sent data, used by the keepalive feature.
 */
time_t q_data_sent_time;

/* The initial call to make as requested by the command line arguments */
static struct q_phone_struct initial_call;
static int dial_phonebook_entry_n;

/* For the --play and --play-exit arguments */
static unsigned char * play_music_string = NULL;
static Q_BOOL play_music_exit = Q_FALSE;

/**
 * The geometry as requested by the command line arguments.
 */
unsigned char q_rows_arg = 25;
unsigned char q_cols_arg = 80;

/* The --status-line command line argument */
static Q_BOOL status_line_disabled = Q_FALSE;

/**
 * The --keyfile command line argument.
 */
char * q_keyfile = NULL;

/**
 * The --scrfile command line argument.
 */
char * q_scrfile = NULL;

/**
 * The --xl8file command line argument.
 */
char * q_xl8file = NULL;

/**
 * The --xlufile command line argument.
 */
char * q_xlufile = NULL;

/**
 * The --config command line argument.
 */
char * q_config_filename = NULL;

/**
 * The --dotqodem-dir command line argument.
 */
char * q_dotqodem_dir = NULL;

/**
 * The --codepage command line argument.
 */
static char * q_codepage_option = NULL;

/**
 * The --doorway command line argument.
 */
static char * q_doorway_option = NULL;

/**
 * The --emulation command line argument.
 */
static char * q_emulation_option = NULL;

/**
 * The --capfile command line argument.
 */
static char * q_capfile_option = NULL;

/**
 * The -x / --exit-on-completion command line argument.  We need it here
 * because of the sequence of read command line options, load_options(), then
 * set variables.
 */
static Q_BOOL q_exit_on_disconnect = Q_FALSE;

/* Command-line options */
static struct option q_getopt_long_options[] = {
    {"dial",                1,      0,      0},
    {"connect",             1,      0,      0},
    {"connect-method",      1,      0,      0},
    {"capfile",             1,      0,      0},
    {"logfile",             1,      0,      0},
    {"keyfile",             1,      0,      0},
    {"xl8file",             1,      0,      0},
    {"xlufile",             1,      0,      0},
    {"scrfile",             1,      0,      0},
    {"config",              1,      0,      0},
    {"create-config",       1,      0,      0},
    {"dotqodem-dir",        1,      0,      0},
    {"read-only",           0,      0,      0},
    {"help",                0,      0,      0},
    {"username",            1,      0,      0},
    {"play",                1,      0,      0},
    {"play-exit",           0,      0,      0},
    {"version",             0,      0,      0},
    {"xterm",               0,      0,      0},
    {"exit-on-completion",  0,      0,      0},
    {"doorway",             1,      0,      0},
    {"codepage",            1,      0,      0},
    {"emulation",           1,      0,      0},
    {"status-line",         1,      0,      0},
    {"geometry",            1,      0,      0},
    {0,                     0,      0,      0}
};

/**
 * The filename returned by get_datadir_filename(),
 * get_workingdir_filename(), and get_scriptdir_filename().
 */
static char datadir_filename[FILENAME_SIZE];
#ifdef Q_SSH_CRYPTLIB

/*
 * The SSH socket will always be periodically checked.
 */
#ifdef Q_PDCURSES_WIN32
static long ssh_last_time = 1000000;
#else
static suseconds_t ssh_last_time = 1000000;
#endif
static struct timeval ssh_tv;

#endif /* Q_SSH_CRYPTLIB */

#if defined(Q_PDCURSES) && !defined(Q_PDCURSES_WIN32)
/*
 * The socket used to convey keystrokes from the X11 process back to
 * PDCurses.  data_handler() selects on this to improve latency.
 */
extern int xc_key_sock;
#endif

/*
 * A function pointer containing the appropriate network close connection to
 * call, set by dial_out().
 */
void (*close_function)();

#ifndef WIN32

/*
 * The final return code retrieved when the script exited, stored in
 * script.c.
 */
extern int script_rc;

/**
 * SIGCHLD signal handler.
 *
 * @param sig the signal number
 */
static void handle_sigchld(int sig) {
    pid_t pid;
    int status;

    if (q_child_pid == -1) {
        /*
         * We got SIGCHLD, but think we are offline anyway.  Just say we
         * exited.
         */
        q_child_exited = Q_TRUE;
    }

    for (;;) {
        pid = waitpid(-1, &status, WNOHANG);
        if (pid == -1) {
            /*
             * Error in the waitpid() call.
             */
            DLOG(("Error in waitpid(): %s (%d)\n", strerror(errno), errno));
            break;
        }
        if (pid == 0) {
            /*
             * Reaped the last zombie, bail out now.
             */
            DLOG(("No more zombies\n"));
            break;
        }
        if (pid == q_child_pid) {
            /*
             * The connection has closed.
             */
            q_child_exited = Q_TRUE;
            DLOG(("SIGCHLD: CONNECTION CLOSED\n"));
        }
        if (pid == q_running_script.script_pid) {
            /*
             * The script has exited.
             */
            DLOG(("SIGCHLD: SCRIPT DONE\n"));

            if (WIFEXITED(status)) {
                qlog(_("Script exited with RC=%u\n"),
                    (WEXITSTATUS(status) & 0xFF));
                script_rc = (WEXITSTATUS(status) & 0xFF);
            } else if (WIFSIGNALED(status)) {
                qlog(_("Script exited with signal=%u\n"), WTERMSIG(status));
                script_rc = WTERMSIG(status) + 128;
            }
            q_running_script.script_pid = -1;
        }

        DLOG(("Reaped process %d\n", pid));
    }
}

#endif

/**
 * Write data from a buffer to the remote system, dispatching to the
 * appropriate connection-specific write function.
 *
 * @param fd the socket descriptor
 * @param data the buffer to read from
 * @param data_n the number of bytes to write to the remote side
 * @param sync if true, do not return until all of the bytes have been
 * written, performing a busy wait and retry.
 * @return the number of bytes written
 */
int qodem_write(const int fd, const char * data, const int data_n,
                const Q_BOOL sync) {

#ifdef Q_PDCURSES_WIN32
    char notify_message[DIALOG_MESSAGE_SIZE];
#endif
    int i;
    int rc;
    int old_errno;
    int begin = 0;
    int n = data_n;

    /*
     * We were passed a const data so that callers can pass read-only string
     * literals, but might need to change data before it hits the wire.
     */
    char write_buffer_data[Q_BUFFER_SIZE];
    char * write_buffer = (char *) data;

    if (data_n == 0) {
        /* NOP */
        return 0;
    }

    if (sync == Q_TRUE) {
        DLOG(("qodem_write() SYNC is TRUE\n"));

        /*
         * Every caller that syncs is sending data in console mode: emulation
         * responses, modem command strings, and keystrokes.  The only caller
         * that doesn't sync is process_incoming_data().  We run bytes
         * through the 8-bit translate table here and in
         * process_incoming_data() so that everything is converted only once.
         */
        write_buffer = write_buffer_data;
        for (i = 0; i < data_n; i++) {
            write_buffer_data[i] = translate_8bit_out(data[i]);
        }
    }

    /* Quicklearn */
    if (q_status.quicklearn == Q_TRUE) {
        for (i = 0; i < data_n; i++) {
            quicklearn_send_byte(write_buffer[i]);
        }
    }

#if !defined(Q_NO_SERIAL) && !defined(Q_PDCURSES_WIN32)
    /* Mark/space parity */
    if (Q_SERIAL_OPEN && (q_serial_port.parity == Q_PARITY_MARK)) {
        /* Outgoing data as MARK parity:  set the 8th bit */
        if (write_buffer != data) {
            /*
             * data has already been copied to write_data.
             */
            for (i = 0; i < data_n; i++) {
                write_buffer_data[i] |= 0x80;
            }
        } else {
            /*
             * This is the first destructive change of data, copy it to
             * write_data.
             */
            write_buffer = write_buffer_data;
            for (i = 0; i < data_n; i++) {
                write_buffer_data[i] = data[i] | 0x80;
            }
        }
    }
    if (Q_SERIAL_OPEN && (q_serial_port.parity == Q_PARITY_SPACE)) {
        /* Outgoing data as SPACE parity:  strip the 8th bit */
        if (write_buffer != data) {
            /*
             * data has already been copied to write_data.
             */
            for (i = 0; i < data_n; i++) {
                write_buffer_data[i] &= 0x7F;
            }
        } else {
            /*
             * This is the first destructive change of data, copy it to
             * write_data.
             */
            write_buffer = write_buffer_data;
            for (i = 0; i < data_n; i++) {
                write_buffer_data[i] = data[i] & 0x7F;
            }
        }
    }
#endif

    if (DLOGNAME != NULL) {

        DLOG(("qodem_write() OUTPUT bytes: "));
        for (i = 0; i < data_n; i++) {
            DLOG2(("%02x ", write_buffer[i] & 0xFF));
        }
        DLOG2(("\n"));
        DLOG(("qodem_write() OUTPUT bytes (ASCII): "));
        for (i = 0; i < data_n; i++) {
            DLOG2(("%c ", write_buffer[i] & 0xFF));
        }
        DLOG2(("\n"));
    }

do_write:

    /* Write bytes out */
    /* Which function to call depends on the connection method */
    if (((q_status.dial_method == Q_DIAL_METHOD_TELNET) &&
            (net_is_connected() == Q_TRUE)) ||
        (((q_program_state == Q_STATE_HOST) || (q_host_active == Q_TRUE)) &&
            (q_host_type == Q_HOST_TYPE_TELNETD))
    ) {
        /* Telnet */
        rc = telnet_write(fd, write_buffer + begin, data_n);
    } else if ((q_status.dial_method == Q_DIAL_METHOD_RLOGIN) &&
        (net_is_connected() == Q_TRUE)
    ) {
        /* Rlogin */
        rc = rlogin_write(fd, write_buffer + begin, data_n);
    } else if (((q_status.dial_method == Q_DIAL_METHOD_SOCKET) &&
            (net_is_connected() == Q_TRUE)) ||
        (((q_program_state == Q_STATE_HOST) || (q_host_active == Q_TRUE)) &&
            (q_host_type == Q_HOST_TYPE_SOCKET))
    ) {
        /* Socket */
        rc = send(fd, write_buffer + begin, data_n, 0);
#ifdef Q_SSH_CRYPTLIB
    } else if (((q_status.dial_method == Q_DIAL_METHOD_SSH) &&
            (net_is_connected() == Q_TRUE)) ||
        (((q_program_state == Q_STATE_HOST) || (q_host_active == Q_TRUE)) &&
            (q_host_type == Q_HOST_TYPE_SSHD))
    ) {
        /* SSH */
        rc = ssh_write(fd, write_buffer + begin, data_n);
#endif

    } else {

#ifdef Q_PDCURSES_WIN32

        /*
         * If wrapping a process (e.g. LOCAL or CMDLINE), write to the
         * q_child_stdin handle.
         */
        if ((q_status.dial_method == Q_DIAL_METHOD_COMMANDLINE) ||
            (q_status.dial_method == Q_DIAL_METHOD_SHELL)
        ) {
            DWORD bytes_written = 0;
            if (WriteFile(q_child_stdin, write_buffer + begin, data_n,
                    &bytes_written, NULL) == TRUE) {

                rc = bytes_written;
                DLOG(("qodem_write() PIPE WriteFile() %d bytes written\n",
                        bytes_written));

                /* Force this sucker to flush */
                FlushFileBuffers(q_child_stdin);
            } else {
                DWORD error = GetLastError();

                /* Error in write */
                snprintf(notify_message, sizeof(notify_message),
                    _("Call to WriteFile() failed: %d (%s)"), error,
                    strerror(error));
                notify_form(notify_message, 0);
                rc = -1;
            }
#ifndef Q_NO_SERIAL
        } else if ((q_status.dial_method == Q_DIAL_METHOD_MODEM) ||
                   Q_SERIAL_OPEN
        ) {
            DWORD bytes_written = 0;
            OVERLAPPED serial_overlapped;
            HANDLE serial_event = CreateEvent(NULL, FALSE, FALSE, NULL);
            assert(q_serial_handle != NULL);
            ZeroMemory(&serial_overlapped, sizeof(serial_overlapped));
            serial_overlapped.hEvent = serial_event;
            if (WriteFile(q_serial_handle, write_buffer + begin, data_n, NULL,
                    &serial_overlapped) == TRUE) {

                if (GetOverlappedResult(q_serial_handle, &serial_overlapped,
                        &bytes_written, TRUE) == TRUE) {
                    rc = bytes_written;
                    DLOG(("qodem_write() SERIAL WriteFile() %d bytes written (async)\n",
                            bytes_written));
                } else {
                    /*
                     * GetOverlappedResult failed.
                     */
                    DWORD error = GetLastError();
                    snprintf(notify_message, sizeof(notify_message),
                        _("Call to GetOverlappedResult() failed: %d (%s)"),
                        error, strerror(error));
                    notify_form(notify_message, 0);
                    return -1;
                }
            } else {
                DWORD error = GetLastError();
                if (error == ERROR_IO_PENDING) {
                    /* Wait for the write to complete. */
                    if (GetOverlappedResult(q_serial_handle, &serial_overlapped,
                            &bytes_written, TRUE) == TRUE) {
                        rc = bytes_written;
                        DLOG(("qodem_write() SERIAL WriteFile() %d bytes written (async)\n",
                                bytes_written));
                    } else {
                        /*
                         * GetOverlappedResult failed.
                         */
                        DWORD error = GetLastError();
                        snprintf(notify_message, sizeof(notify_message),
                            _("Call to GetOverlappedResult() failed: %d (%s)"),
                            error, strerror(error));
                        notify_form(notify_message, 0);
                        return -1;
                    }
                } else {
                    /* Error in write */
                    snprintf(notify_message, sizeof(notify_message),
                        _("Call to WriteFile() failed: %d (%s)"), error,
                        strerror(error));
                    notify_form(notify_message, 0);
                    rc = -1;
                }
            }
#endif
        } else {
            DLOG(("qodem_write() write() %d bytes to fd %d\n", data_n, fd));
            /* Everyone else */
            rc = write(fd, write_buffer + begin, data_n);
        }
#else

        /* Everyone else */
        rc = write(fd, write_buffer + begin, data_n);

#endif /* Q_PDCURSES_WIN32 */

    }

    old_errno = get_errno();
    if (rc < 0) {
        DLOG(("qodem_write() write() error %s (%d)\n", get_strerror(old_errno),
                old_errno));
    } else if (rc == 0) {
        DLOG(("qodem_write() write() RC=0\n"));

        if (sync == Q_TRUE) {
            DLOG(("qodem_write() write() RC=0 SYNC is true, go back\n"));
            goto do_write;
        }
    } else {
        DLOG(("qodem_write() write() %d bytes written\n", rc));
    }

    if (sync == Q_TRUE) {
        int error = get_errno();
        if (rc > 0) {
            n -= rc;
            begin += n;
            if (n > 0) {
                /*
                 * The last write was successful, and there are more bytes to
                 * write.
                 */
                goto do_write;
            } else {
#ifndef Q_NO_SERIAL
                /*
                 * The last write was successful, all bytes are written.  Now
                 * encourage the bytes to actually go out.
                 */
                if (Q_SERIAL_OPEN) {
#ifdef Q_PDCURSES_WIN32
                    FlushFileBuffers(q_serial_handle);
#else
                    tcdrain(fd);
#endif /* Q_PDCURSES_WIN32*/
                }
#endif /* Q_NO_SERIAL */
            }
        } else if ((rc == 0) || (error == EAGAIN) ||
#ifdef Q_PDCURSES_WIN32
            (error == WSAEWOULDBLOCK)
#else
            (error == EWOULDBLOCK)
#endif
        ) {
            /*
             * Do a busy wait on the write until everything goes out.
             */
            goto do_write;
        }

    }

    /* Reset clock for keepalive/idle timeouts */
    time(&q_data_sent_time);

    /* Reset errno for our caller */
    set_errno(old_errno);

    /* All done */
    return rc;
}

/**
 * Buffer up data to write to the remote system.
 *
 * @param data the buffer to read from
 * @param data_n the number of bytes to write to the remote side
 */
void qodem_buffered_write(const char * data, const int data_n) {
    if (DLOGNAME != NULL) {
        int i;

        DLOG(("qodem_buffered_write() OUTPUT bytes: "));
        for (i = 0; i < data_n; i++) {
            DLOG2(("%02x ", data[i] & 0xFF));
        }
        DLOG2(("\n"));
        DLOG(("qodem_buffered_write() OUTPUT bytes (ASCII): "));
        for (i = 0; i < data_n; i++) {
            DLOG2(("%c ", data[i] & 0xFF));
        }
        DLOG2(("\n"));
    }

    if (buffered_write_buffer == NULL) {
        assert(buffered_write_buffer_n == 0);
        buffered_write_buffer = (char *) Xmalloc(sizeof(char) * data_n,
            __FILE__, __LINE__);
        buffered_write_buffer_n += data_n;
    } else if ((buffered_write_buffer_i + data_n) > buffered_write_buffer_n) {
        buffered_write_buffer = (char *) Xrealloc(buffered_write_buffer,
            sizeof(char) * (buffered_write_buffer_n + data_n), __FILE__,
            __LINE__);
        buffered_write_buffer_n += data_n;
    } else {
        assert(buffered_write_buffer_i + data_n <= buffered_write_buffer_n);
    }

    memcpy(buffered_write_buffer + buffered_write_buffer_i, data, data_n);
    buffered_write_buffer_i += data_n;
}

/**
 * Write data from the buffer of qodem_buffered_write() to the remote system,
 * dispatching to the appropriate connection-specific write function.
 *
 * @param fd the socket descriptor
 */
void qodem_buffered_write_flush(const int fd) {
    DLOG(("qodem_buffered_write_flush()\n"));

    if (buffered_write_buffer_i > 0) {
        qodem_write(fd, buffered_write_buffer, buffered_write_buffer_i, Q_TRUE);
    } else {
        assert(buffered_write_buffer_i == 0);
    }
    buffered_write_buffer_i = 0;
}

/**
 * Read data from remote system to a buffer, dispatching to the
 * appropriate connection-specific read function.
 *
 * @param fd the socket descriptor
 * @param buf the buffer to write to
 * @param count the number of bytes requested
 * @return the number of bytes read into buf
 */
static ssize_t qodem_read(const int fd, void * buf, size_t count) {
#ifdef Q_PDCURSES_WIN32
    char notify_message[DIALOG_MESSAGE_SIZE];
    DWORD actual_bytes = 0;
    DWORD bytes_read = 0;
#endif

    /* Which function to call depends on the connection method */
    if (((q_status.dial_method == Q_DIAL_METHOD_TELNET) &&
            (net_is_connected() == Q_TRUE)) ||
        (((q_program_state == Q_STATE_HOST) || (q_host_active == Q_TRUE)) &&
            (q_host_type == Q_HOST_TYPE_TELNETD))
    ) {
        /* Telnet */
        return telnet_read(fd, buf, count);
    }
    if ((q_status.dial_method == Q_DIAL_METHOD_RLOGIN) &&
        (net_is_connected() == Q_TRUE)
    ) {
        /* Rlogin */
        if (FD_ISSET(fd, &exceptfds)) {
            return rlogin_read(fd, buf, count, Q_TRUE);
        }
        return rlogin_read(fd, buf, count, Q_FALSE);
    }
    if (((q_status.dial_method == Q_DIAL_METHOD_SOCKET) &&
            (net_is_connected() == Q_TRUE)) ||
        (((q_program_state == Q_STATE_HOST) || (q_host_active == Q_TRUE)) &&
            (q_host_type == Q_HOST_TYPE_SOCKET))
    ) {
        /* Socket */
        return recv(fd, (char *)buf, count, 0);
    }
#ifdef Q_SSH_CRYPTLIB
    if (((q_status.dial_method == Q_DIAL_METHOD_SSH) &&
            (net_is_connected() == Q_TRUE)) ||
        (((q_program_state == Q_STATE_HOST) || (q_host_active == Q_TRUE)) &&
            (q_host_type == Q_HOST_TYPE_SSHD))
    ) {
        /* SSH */
        return ssh_read(fd, buf, count);
    }
#endif

#ifdef Q_PDCURSES_WIN32

    /*
     * If wrapping a process (e.g. LOCAL or CMDLINE), read from the
     * q_child_stdout handle.
     */
    if ((q_status.online == Q_TRUE) &&
        ((q_status.dial_method == Q_DIAL_METHOD_COMMANDLINE) ||
            (q_status.dial_method == Q_DIAL_METHOD_SHELL))
    ) {
        assert(q_child_process != NULL);
        if (PeekNamedPipe(q_child_stdout, NULL, 0, NULL, &actual_bytes,
                NULL) == 0) {

            /* Error peeking */
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                /* This is EOF */
                SetLastError(EIO);
                return -1;
            }

            snprintf(notify_message, sizeof(notify_message),
                _("Call to PeekNamedPipe() failed: %d (%s)"), GetLastError(),
                strerror(GetLastError()));
            notify_form(notify_message, 0);
            return -1;
        } else {
            DLOG(("qodem_read() PeekNamedPipe: %d bytes available\n",
                    actual_bytes));

            if (actual_bytes == 0) {
                set_errno(EAGAIN);
                return -1;
            } else if (actual_bytes > count) {
                actual_bytes = count;
            }
            if (ReadFile(q_child_stdout, buf, actual_bytes, &bytes_read,
                    NULL) == TRUE) {
                return bytes_read;
            } else {
                /* Error in read */
                snprintf(notify_message, sizeof(notify_message),
                    _("Call to ReadFile() failed: %d (%s)"), GetLastError(),
                    strerror(GetLastError()));
                notify_form(notify_message, 0);
                return -1;
            }
        }
#ifndef Q_NO_SERIAL
    } else if (((q_status.online == Q_TRUE) &&
                (q_status.dial_method == Q_DIAL_METHOD_MODEM)) ||
               Q_SERIAL_OPEN
    ) {
        OVERLAPPED serial_overlapped;
        HANDLE serial_event = CreateEvent(NULL, FALSE, FALSE, NULL);
        COMSTAT com_stat;
        assert(q_serial_handle != NULL);
        ZeroMemory(&serial_overlapped, sizeof(serial_overlapped));
        serial_overlapped.hEvent = serial_event;
        ClearCommError(q_serial_handle, NULL, &com_stat);
        actual_bytes = com_stat.cbInQue;
        DLOG(("qodem_read() SERIAL actual_bytes %d\n", actual_bytes));
        if (actual_bytes == 0) {
            DLOG(("qodem_read() SERIAL bailing out\n"));
            /* Bail out as EAGAIN */
            set_errno(EAGAIN);
            return -1;
        }

        if (ReadFile(q_serial_handle, buf, actual_bytes, NULL,
                &serial_overlapped) == TRUE) {

            DLOG(("qodem_read() SERIAL ReadFile() returned TRUE\n"));

serial_read_result:
            DLOG(("qodem_read() SERIAL calling GetOverlappedResult()...\n"));

            if (GetOverlappedResult(q_serial_handle, &serial_overlapped,
                    &bytes_read, TRUE) == TRUE) {

                DLOG(("qodem_read() SERIAL bytes_read %d\n", bytes_read));

                if (bytes_read == 0) {
                    DLOG(("qodem_read() SERIAL return EAGAIN\n"));
                    /*
                     * Turn this into EAGAIN, as serial ports don't really do
                     * EOF.
                     */
                    set_errno(EAGAIN);
                    return -1;
                } else {
                    DLOG(("qodem_read() SERIAL return %d bytes read\n",
                        bytes_read));
                    /*
                     * We read some bytes.
                     */
                    return bytes_read;
                }
            } else {
                /*
                 * GetOverlappedResult failed.
                 */
                DWORD error = GetLastError();
                snprintf(notify_message, sizeof(notify_message),
                    _("Call to GetOverlappedResult() failed: %d (%s)"), error,
                    strerror(error));
                notify_form(notify_message, 0);
                return -1;
            }

        } else {
            DWORD error = GetLastError();
            DLOG(("qodem_read() SERIAL ReadFile() returned FALSE\n"));
            if (error == ERROR_IO_PENDING) {
                DLOG(("qodem_read() SERIAL ERROR_IO_PENDING\n"));
                goto serial_read_result;
            } else {
                /* Error in read */
                snprintf(notify_message, sizeof(notify_message),
                    _("Call to ReadFile() failed: %d (%s)"), error,
                    strerror(error));
                notify_form(notify_message, 0);
                return -1;
            }
        }
#endif
    }

#endif /* Q_PDCURSES_WIN32 */

    /* Everyone else */
    return read(fd, buf, count);
}

/**
 * Get the command-line help screen.
 *
 * @return the help screen
 */
static char * usage_string() {
        return _(""
"'qodem' is a terminal emulator with support for scrollback, capture, file\n"
"transfers, keyboard macros, scripting, and more.  This is version 1.0.1.\n"
"\n"
"Usage: qodem [OPTIONS] { [ --dial N ] | [ --connect ] | [ command line ] }\n"
"\n"
"Options:\n"
"\n"
"      --dial N                    Immediately connect to the phonebook\n"
"                                  entry numbered N.\n"
"      --dotqodem-dir DIRNAME      Use DIRNAME instead of $HOME/.qodem for\n"
"                                  config/data files.\n"
"      --config FILENAME           Load options from FILENAME (only).\n"
"      --create-config FILENAME    Write a new options file to FILENAME and exit.\n"
"      --connect HOST              Immediately open a connection to HOST.\n"
"                                  The default connection method is \"ssh"
"\".\n"
"      --connect-method METHOD     Use METHOD to connect for the --connect\n"
"                                  option.  Valid values are \"ssh\", \"rlogin\",\n"
"                                  \"telnet,\", and \"shell\".\n"
"      --username USERNAME         Log in as USERNAME\n"
"      --capfile FILENAME          Capture the entire session and save to\n"
"                                  FILENAME.\n"
"      --logfile FILENAME          Enable the session log and save to FILENAME.\n"
"      --keyfile FILENAME          Load keyboard macros from FILENAME\n"
"      --xl8file FILENAME          Load 8-bit translate tables from FILENAME.\n"
"      --xlufile FILENAME          Load Unicode translate tables from FILENAME.\n"
"      --srcfile FILENAME          Start script FILENAME after connect.\n"
"      --read-only                 Disable all writes to disk.\n"
"  -x, --exit-on-completion        Exit after connection/command finishes.\n"
"      --doorway MODE              Select doorway MODE.  Valid values for\n"
"                                  MODE are \"doorway\", \"mixed\", and \"off\".\n"
"      --codepage CODEPAGE         Select codepage CODEPAGE.  See Alt-; list\n"
"                                  for valid codepages.  Example: \"CP437\",\n"
"                                  \"CP850\", \"Windows-1252\", etc.\n"
"      --emulation EMULATION       Select emulation EMULATION.  Valid values are\n"
"                                  \"ansi\", \"avatar\", \"debug\", \"vt52\", \"vt100\",\n"
"                                  \"vt102\", \"vt220\", \"linux\", \"l_utf8\", \"xterm\",\n"
"                                  \"x_utf8\", \"petscii\", and \"atascii\".\n"
"      --status-line { on | off }  If \"on\" enable status line.  If \"off\" disable\n"
"                                  status line.\n"
"      --play MUSIC                Play MUSIC as ANSI Music\n"
"      --play-exit                 Immediately exit after playing MUSIC\n"
"      --geometry COLSxROWS        Request text window size COLS x ROWS\n"
"      --xterm                     Enable X11 terminal mode\n"
"      --version                   Display program version\n"
"  -h, --help                      This help screen\n"
"\n"
"qodem can also open a raw shell with the command line given.  For example\n"
"'qodem --connect my.host --connect-method ssh' is equivalent to 'qodem ssh\n"
"my.host' .\n"
"\n");
}

/**
 * Get the version string
 *
 * @return the version string
 */
static char * version_string() {
    return _(""
"qodem version 1.0.1\n"
"Written 2003-2021 by Autumn Lamonte ⚧ Trans Liberation Now\n"
"\n"
"To the extent possible under law, the author(s) have dedicated all\n"
"copyright and related and neighboring rights to this software to the\n"
"public domain worldwide. This software is distributed without any\n"
"warranty.\n"
"\n");
}

/**
 * Display a multi-line string to the user.  This is used to emit the help
 * and version text.
 *
 * @param str the string
 */
static void page_string(const char * str) {

#ifdef Q_PDCURSES
    int i;
    char ch;
    int row = 0;
    int col = 0;

    screen_setup(25, 80);
    set_blocking_input(Q_TRUE);
    screen_clear();
    screen_move_yx(0, 0);

    for (i = 0; i < strlen(str); i++) {
        ch = str[i];
        if (ch == '\n') {
            row++;
            col = 0;
            if (row == 24) {
                screen_put_str_yx(row, 0, _("Press any key for more..."),
                    A_NORMAL, 0x38);
                screen_flush();
                getch();
                row = 0;
                col = 0;
                screen_clear();
                screen_move_yx(0, 0);
            }
        } else {
            screen_put_char_yx(row, col, ch, A_NORMAL, 0x38);
            col++;
            if (col == 80) {
                col = 0;
            }
        }
    }

    screen_put_str_yx(row, 0, _("Press any key to exit..."),
        A_NORMAL, 0x38);
    screen_flush();
    getch();

    screen_teardown();

#else

    printf("%s", str);

#endif
}

/**
 * See if the user asked for help or version information, and if so provide a
 * return code to main().
 *
 * @param argc the program's command line argument count
 * @param argc the program's command line arguments
 * @return an exit code if the user asked for help or version, or 0 for main
 * to continue
 */
static int check_for_help(int argc, char * const argv[]) {
    int i;

    for (i = 0; i < argc; i++) {

        /* Special case: help means exit */
        if (strncmp(argv[i], "--help", strlen("--help")) == 0) {
            page_string(usage_string());
            return EXIT_HELP;
        }
        if (strncmp(argv[i], "-h", strlen("-h")) == 0) {
            page_string(usage_string());
            return EXIT_HELP;
        }
        if (strncmp(argv[i], "-?", strlen("-?")) == 0) {
            page_string(usage_string());
            return EXIT_HELP;
        }
        if (strncmp(argv[i], "--version", strlen("--version")) == 0) {
            page_string(version_string());
            return EXIT_VERSION;
        }
    }

    /* The user did not ask for help or version */
    return 0;
}

/**
 * Process one command line option.
 *
 * @param option the option, e.g. "play"
 * @param value the value for the option, e.g. "M BCEBCEBCE"
 */
static void process_command_line_option(const char * option,
                                        const char * value) {

    wchar_t value_wchar[128];
    int rows_arg_int;
    int cols_arg_int;

    /*
     fprintf(stdout, "OPTION=%s VALUE=%s\n", option, value);
     */

    /* Special case: help means exit */
    if (strncmp(option, "help", strlen("help")) == 0) {
        page_string(usage_string());
        q_program_state = Q_STATE_EXIT;
    }

    /* Special case: version means exit */
    if (strncmp(option, "version", strlen("version")) == 0) {
        page_string(version_string());
        q_program_state = Q_STATE_EXIT;
    }

    if (strncmp(option, "logfile", strlen("logfile")) == 0) {
        start_logging(value);
    }

    if (strncmp(option, "keyfile", strlen("keyfile")) == 0) {
        q_keyfile = Xstrdup(value, __FILE__, __LINE__);
    }

    if (strncmp(option, "scrfile", strlen("scrfile")) == 0) {
        q_scrfile = Xstrdup(value, __FILE__, __LINE__);
    }

    if (strncmp(option, "xl8file", strlen("xl8file")) == 0) {
        q_xl8file = Xstrdup(value, __FILE__, __LINE__);
    }

    if (strncmp(option, "xlufile", strlen("xlufile")) == 0) {
        q_xlufile = Xstrdup(value, __FILE__, __LINE__);
    }
    if (strncmp(option, "config", strlen("config")) == 0) {
        q_config_filename = Xstrdup(value, __FILE__, __LINE__);
    }
    if (strncmp(option, "create-config", strlen("create-config")) == 0) {
        reset_options();
        save_options(value);
        q_program_state = Q_STATE_EXIT;
    }
    if (strncmp(option, "dotqodem-dir", strlen("dotqodem-dir")) == 0) {
        q_dotqodem_dir = Xstrdup(value, __FILE__, __LINE__);
    }
    if (strncmp(option, "read-only", strlen("read-only")) == 0) {
        q_status.read_only = Q_TRUE;
    }

    if (strncmp(option, "xterm", strlen("xterm")) == 0) {
        q_status.xterm_mode = Q_TRUE;
        q_exit_on_disconnect = Q_TRUE;
    }

    if (strncmp(option, "exit-on-completion",
            strlen("exit-on-completion")) == 0) {
        q_exit_on_disconnect = Q_TRUE;
    }

    if (strncmp(option, "doorway", strlen("doorway")) == 0) {
        q_doorway_option = Xstrdup(value, __FILE__, __LINE__);
    }

    if (strncmp(option, "codepage", strlen("codepage")) == 0) {
        q_codepage_option = Xstrdup(value, __FILE__, __LINE__);
    }

    if (strncmp(option, "emulation", strlen("emulation")) == 0) {
        q_emulation_option = Xstrdup(value, __FILE__, __LINE__);
    }

    if (strncmp(option, "status-line", strlen("status-line")) == 0) {
        if (strcasecmp(value, "off") == 0) {
            set_status_line(Q_FALSE);
            status_line_disabled = Q_TRUE;
        } else {
            set_status_line(Q_TRUE);
            status_line_disabled = Q_FALSE;
        }
    }

    if (strncmp(option, "capfile", strlen("capfile")) == 0) {
        q_capfile_option = Xstrdup(value, __FILE__, __LINE__);
    }

    if (strncmp(option, "geometry", strlen("geometry")) == 0) {
        sscanf(value, "%dx%d", &cols_arg_int, &rows_arg_int);
        if (rows_arg_int < 25) {
            rows_arg_int = 25;
        }
        if (rows_arg_int > 250) {
            rows_arg_int = 250;
        }
        if (cols_arg_int < 80) {
            cols_arg_int = 80;
        }
        if (cols_arg_int > 250) {
            cols_arg_int = 250;
        }
        q_rows_arg = (unsigned char) rows_arg_int;
        q_cols_arg = (unsigned char) cols_arg_int;
    }

    if (strcmp(option, "dial") == 0) {
        dial_phonebook_entry_n = atoi(value);
        if (errno == EINVAL) {
            dial_phonebook_entry_n = -1;
        }
    }

    if (strcmp(option, "play") == 0) {
        play_music_string = (unsigned char *)Xstrdup(value, __FILE__, __LINE__);
    }

    if (strcmp(option, "play-exit") == 0) {
        play_music_exit = Q_TRUE;
    }

    if (strcmp(option, "connect") == 0) {
        initial_call.address = (char *)value;
        memset(value_wchar, 0, sizeof(value_wchar));
        mbstowcs(value_wchar, value, strlen(value));
        initial_call.name = Xwcsdup(value_wchar, __FILE__, __LINE__);
    }

    if (strncmp(option, "username", strlen("username")) == 0) {
        memset(value_wchar, 0, sizeof(value_wchar));
        mbstowcs(value_wchar, value, strlen(value));
        initial_call.username = Xwcsdup(value_wchar, __FILE__, __LINE__);
    }

    if (strncmp(option, "connect-method", strlen("connect-method")) == 0) {
        initial_call.port = "";
        if (strncmp(value, "ssh", strlen("ssh")) == 0) {
            initial_call.method = Q_DIAL_METHOD_SSH;
            initial_call.port = "22";
        } else if (strncmp(value, "shell", strlen("shell")) == 0) {
            initial_call.method = Q_DIAL_METHOD_SHELL;
            initial_call.address = "";
        } else if (strncmp(value, "rlogin", strlen("rlogin")) == 0) {
            initial_call.method = Q_DIAL_METHOD_RLOGIN;
        } else if (strncmp(value, "telnet", strlen("telnet")) == 0) {
            initial_call.method = Q_DIAL_METHOD_TELNET;
            initial_call.port = "23";
        } else if (strncmp(value, "socket", strlen("socket")) == 0) {
            initial_call.method = Q_DIAL_METHOD_SOCKET;
            initial_call.port = "23";
        }
    }

}

/**
 * Resolve conflicts between command line options and the options file.
 */
static void resolve_command_line_options() {

    if (q_status.xterm_mode == Q_TRUE) {
        q_status.doorway_mode = Q_DOORWAY_MODE_MIXED;
        set_status_line(Q_FALSE);
    }

    if (q_doorway_option != NULL) {
        if (strcasecmp(q_doorway_option, "doorway") == 0) {
            q_status.doorway_mode = Q_DOORWAY_MODE_FULL;
        } else if (strcasecmp(q_doorway_option, "mixed") == 0) {
            q_status.doorway_mode = Q_DOORWAY_MODE_MIXED;
        } else {
            q_status.doorway_mode = Q_DOORWAY_MODE_OFF;
        }
        initial_call.doorway = q_status.doorway_mode;
    }

    if (q_emulation_option != NULL) {
        q_status.emulation = emulation_from_string(q_emulation_option);
        q_status.codepage = default_codepage(q_status.emulation);
        initial_call.emulation = q_status.emulation;
        initial_call.codepage = q_status.codepage;
    }

    if (q_codepage_option != NULL) {
        q_status.codepage = codepage_from_string(q_codepage_option);
        initial_call.codepage = q_status.codepage;
    }

    if (q_capfile_option != NULL) {
        start_capture(q_capfile_option);
    }

    q_status.exit_on_disconnect = q_exit_on_disconnect;
}

/**
 * Emit a message to the log file.
 *
 * @param format a printf-style format string
 */
void qlog(const char * format, ...) {
    char outbuf[SESSION_LOG_LINE_SIZE];
    time_t current_time;
    va_list arglist;

    DLOG(("QLOG: %s", format));

    if (q_status.logging == Q_FALSE) {
        return;
    }

    time(&current_time);
    strftime(outbuf, sizeof(outbuf), "[%Y-%m-%d %H:%M:%S] ",
        localtime(&current_time));

    va_start(arglist, format);
    vsprintf((char *)(outbuf+strlen(outbuf)), format, arglist);
    va_end(arglist);

    fprintf(q_status.logging_file, "%s", outbuf);
    fflush(q_status.logging_file);
}

/**
 * Close a remote network connection.
 */
void close_network_connection() {

    DLOG(("close_network_connection()\n"));

#ifndef Q_NO_SERIAL
    assert(q_status.dial_method != Q_DIAL_METHOD_MODEM);
#endif

    assert((q_status.dial_method == Q_DIAL_METHOD_SOCKET) ||
        (q_status.dial_method == Q_DIAL_METHOD_TELNET) ||
        (q_status.dial_method == Q_DIAL_METHOD_RLOGIN) ||
        (q_status.dial_method == Q_DIAL_METHOD_SSH));

    if (q_status.dial_method == Q_DIAL_METHOD_SOCKET) {
        net_force_close();
    } else {
        net_close();
    }

#ifdef Q_PDCURSES_WIN32
    closesocket(q_child_tty_fd);
#else
    close(q_child_tty_fd);
#endif
    q_child_tty_fd = -1;
    qlog(_("Connection closed.\n"));
}

/**
 * Close a wrapped shell connection.
 */
void close_shell_connection() {

#ifdef Q_PDCURSES_WIN32
    DWORD status;
#else
    int status;
#endif

    DLOG(("close_shell_connection()\n"));

#ifndef Q_NO_SERIAL
    assert(q_status.dial_method != Q_DIAL_METHOD_MODEM);
#endif

#ifdef Q_PDCURSES_WIN32

    assert(q_child_tty_fd == -1);

    if (GetExitCodeProcess(q_child_process, &status) == TRUE) {
        /* Got return code */
        if (status == STILL_ACTIVE) {
            /*
             * Process thinks it's still running, DIE!
             */
            TerminateProcess(q_child_process, -1);
            status = -1;
            qlog(_("Connection forcibly terminated: still thinks it is alive.\n"));
        } else {
            qlog(_("Connection exited with RC=%u\n"), status);
        }
    } else {
        /*
         * Can't get process exit code
         */
        TerminateProcess(q_child_process, -1);
        qlog(_("Connection forcibly terminated: unable to get exit code.\n"));
    }

    /* Close pipes */
    CloseHandle(q_child_stdin);
    q_child_stdin = NULL;
    CloseHandle(q_child_stdout);
    q_child_stdout = NULL;
    CloseHandle(q_child_process);
    q_child_process = NULL;
    CloseHandle(q_child_thread);
    q_child_thread = NULL;

#else

    assert(q_child_pid != -1);
    assert(q_child_tty_fd != -1);

    /* Close pty */
    close(q_child_tty_fd);
    q_child_tty_fd = -1;
    Xfree(q_child_ttyname, __FILE__, __LINE__);
    wait4(q_child_pid, &status, WNOHANG, NULL);
    if (WIFEXITED(status)) {
        qlog(_("Connection exited with RC=%u\n"), WEXITSTATUS(status));
        if (q_status.exit_on_disconnect == Q_TRUE) {
            q_exitrc = WEXITSTATUS(status);
        }
    } else if (WIFSIGNALED(status)) {
        qlog(_("Connection exited with signal=%u\n"), WTERMSIG(status));
    }
    q_child_pid = -1;

#endif /* Q_PDCURSES_WIN32 */
}

/**
 * Cleanup connection resources, called AFTER read() has returned 0.
 */
static void cleanup_connection() {

    DLOG(("cleanup_connection()\n"));

    if ((q_program_state == Q_STATE_HOST) || (q_host_active == Q_TRUE)) {
        switch (q_host_type) {
        case Q_HOST_TYPE_SOCKET:
            /* Fall through... */
        case Q_HOST_TYPE_TELNETD:
            /* Fall through... */
#ifdef Q_SSH_CRYPTLIB
        case Q_HOST_TYPE_SSHD:
            /* Fall through... */
#endif
#ifdef Q_PDCURSES_WIN32
            closesocket(q_child_tty_fd);
#else
            close(q_child_tty_fd);
#endif
            q_child_tty_fd = -1;
            qlog(_("Connection closed.\n"));
            break;
#ifndef Q_NO_SERIAL
#ifdef Q_PDCURSES_WIN32
        case Q_HOST_TYPE_MODEM:
            CloseHandle(q_serial_handle);
            q_serial_handle = NULL;
            qlog(_("Connection closed.\n"));
            break;

        case Q_HOST_TYPE_SERIAL:
            CloseHandle(q_serial_handle);
            q_serial_handle = NULL;
            qlog(_("Connection closed.\n"));
            break;
#else
        case Q_HOST_TYPE_MODEM:
            close(q_child_tty_fd);
            q_child_tty_fd = -1;
            qlog(_("Connection closed.\n"));
            break;

        case Q_HOST_TYPE_SERIAL:
            close(q_child_tty_fd);
            q_child_tty_fd = -1;
            qlog(_("Connection closed.\n"));
            break;
#endif /* Q_PDCURSES_WIN32 */
#endif
        }

    } else {
        /*
         * Call the appropriate close function.
         */
        assert(close_function != NULL);
        close_function();

        /* Increment stats */
        if (q_current_dial_entry != NULL) {
            q_current_dial_entry->times_on++;
            time(&q_current_dial_entry->last_call);
        }
    }

    /* Offline now */
    q_status.online = Q_FALSE;

    /* See if the user wanted to disconnect */
    if (q_status.exit_on_disconnect == Q_TRUE) {
        q_program_state = Q_STATE_EXIT;
    }
}

/**
 * Close remote connection, dispatching to the appropriate
 * connection-specific close function.
 */
void close_connection() {

    DLOG(("close_connection()\n"));

    /* How to close depends on the connection method */
    if (net_is_connected() == Q_TRUE) {
        /*
         * Telnet, Rlogin, and SSH read() functions have set connected to
         * false.  Socket does not, so treat it like host mode.
         */
        if ((q_program_state != Q_STATE_HOST) &&
            (q_status.dial_method == Q_DIAL_METHOD_SOCKET)
#ifdef Q_SSH_CRYPTLIB
            /*
             * Cryptlib does not return 0 on the next read() after
             * cryptDestroySession(), so treat it like socket.
             */
            || (q_status.dial_method == Q_DIAL_METHOD_SSH)
#endif
        ) {
            cleanup_connection();
            net_force_close();
        } else {
            net_close();
        }
        if (q_program_state == Q_STATE_HOST) {
            /*
             * Host mode has called host_stop().  Cleanup the connection
             * immediately, don't wait on a read of 0 that may never come.
             */
            cleanup_connection();
            net_force_close();
        }
        return;
    }

#ifdef Q_PDCURSES_WIN32
    /* Win32 case */
    /* Terminate process */
    assert(q_child_process != NULL);
    TerminateProcess(q_child_process, -1);
#else
    /* Killing -1 kills EVERYTHING.  Not good! */
    assert(q_child_pid != -1);
    kill(q_child_pid, SIGHUP);
#endif /* Q_PDCURSES_WIN32 */

}

/**
 * See if the fd is readable.
 *
 * @return true if the fd is readable
 */
static Q_BOOL is_readable(int fd) {

#ifdef Q_PDCURSES_WIN32
    char notify_message[DIALOG_MESSAGE_SIZE];
#endif

    if (FD_ISSET(fd, &readfds)) {
        return Q_TRUE;
    }
    /* Rlogin special case: look for OOB data */
    if ((q_status.dial_method == Q_DIAL_METHOD_RLOGIN) &&
        (net_is_connected() == Q_TRUE)
    ) {
        if (FD_ISSET(fd, &exceptfds)) {
            return Q_TRUE;
        }
    }

#ifdef Q_SSH_CRYPTLIB
    /* SSH special case: see if we should read again anyway */
    if (((q_status.dial_method == Q_DIAL_METHOD_SSH) &&
            (net_is_connected() == Q_TRUE)) ||
        (((q_program_state == Q_STATE_HOST) || (q_host_active == Q_TRUE)) &&
            (q_host_type == Q_HOST_TYPE_SSHD))
    ) {
        if (fd == q_child_tty_fd) {
            if (ssh_maybe_readable() == Q_TRUE) {
                return Q_TRUE;
            }
            /*
             * ALWAYS try to read after 0.25 seconds, even if there is
             * "nothing" on the socket itself.
             */
            gettimeofday(&ssh_tv, NULL);
            if ((ssh_tv.tv_usec < ssh_last_time) ||
                (ssh_tv.tv_usec - ssh_last_time > 250000)
            ) {
                DLOG(("SSH OVERRIDE: check socket anyway\n"));
                return Q_TRUE;
            }
        }
    }
#endif

#ifdef Q_PDCURSES_WIN32

    if ((q_status.online == Q_TRUE) &&
        (q_program_state != Q_STATE_HOST) &&
        ((q_status.dial_method == Q_DIAL_METHOD_SHELL) ||
            (q_status.dial_method == Q_DIAL_METHOD_COMMANDLINE))
    ) {
        /*
         * Check for data on child process.  If we have some, set have_data
         * to true so we can skip the select() call.
         */
        DWORD actual_bytes = 0;
        if (PeekNamedPipe(q_child_stdout, NULL, 0, NULL, &actual_bytes,
                NULL) == 0) {
            /* Error peeking */
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                /*
                 * This is EOF.  Say that it's readable so that qodem_read()
                 * can return the 0.
                 */
                set_errno(EIO);
                return Q_TRUE;
            }
            snprintf(notify_message, sizeof(notify_message),
                _("Call to PeekNamedPipe() failed: %d (%s)"), GetLastError(),
                strerror(GetLastError()));
            notify_form(notify_message, 0);
        } else {
            DLOG(("is_readable() PeekNamedPipe: %d bytes available\n",
                    actual_bytes));
            if (actual_bytes > 0) {
                /*
                 * There's something there, go after it.
                 */
                return Q_TRUE;
            } else {
                return Q_FALSE;
            }
        }
    }

#endif /* Q_PDCURSES_WIN32 */

    return Q_FALSE;
}

/**
 * Read data from the remote side, dispatch it to the correct data handling
 * function, and write data to the remote side.
 */
static void process_incoming_data() {
    int rc;
    int n;
    int unprocessed_n;
    char time_string[SHORT_TIME_SIZE];
    time_t current_time;
    int hours, minutes, seconds;
    time_t connect_time;
    int i;
    char notify_message[DIALOG_MESSAGE_SIZE];

    Q_BOOL wait_on_script = Q_FALSE;

    /*
     * For scripts: don't read any more data from the remote side if there is
     * no more room in the print buffer side.
     */
    if (q_program_state == Q_STATE_SCRIPT_EXECUTE) {
        if (q_running_script.print_buffer_full == Q_TRUE) {
            wait_on_script = Q_TRUE;
        }
    }

#ifdef Q_NO_SERIAL
    DLOG(("IF CHECK: %s %s %s %s\n",
            "N/A",
            (q_status.online == Q_TRUE ? "true" : "false"),
            (is_readable(q_child_tty_fd) == Q_TRUE ? "true" : "false"),
            (wait_on_script == Q_FALSE ? "true" : "false")));
#else
    DLOG(("IF CHECK: %s %s %s %s\n",
            (q_status.serial_open == Q_TRUE ? "true" : "false"),
            (q_status.online == Q_TRUE ? "true" : "false"),
            (is_readable(q_child_tty_fd) == Q_TRUE ? "true" : "false"),
            (wait_on_script == Q_FALSE ? "true" : "false")));
#endif

    if ((Q_SERIAL_OPEN || (q_status.online == Q_TRUE)) &&
#if defined(Q_PDCURSES_WIN32) && !defined(Q_NO_SERIAL)
        ((q_serial_readable == Q_TRUE) ||
            (is_readable(q_child_tty_fd) == Q_TRUE)) &&
#else
        (is_readable(q_child_tty_fd) == Q_TRUE) &&
#endif
        (wait_on_script == Q_FALSE)
    ) {

#ifdef Q_SSH_CRYPTLIB
        ssh_last_time = ssh_tv.tv_usec;
#endif

        /*
         * There is something to read.
         */
        n = Q_BUFFER_SIZE - q_buffer_raw_n;

        DLOG(("before qodem_read(), n = %d\n", n));

        if (n > 0) {
            int error;

            /* Clear errno */
            set_errno(0);
            rc = qodem_read(q_child_tty_fd, q_buffer_raw + q_buffer_raw_n, n);
            error = get_errno();

            DLOG(("qodem_read() : rc = %d errno=%d\n", rc, error));

            if (rc < 0) {
                if (error == EIO) {
                    /* This is EOF. */
                    rc = 0;
#ifdef Q_PDCURSES_WIN32
                } else if ((error == WSAEWOULDBLOCK) &&
#else
                } else if ((error == EAGAIN) &&
#endif
                    (((q_status.dial_method == Q_DIAL_METHOD_TELNET) &&
                        (net_is_connected() == Q_TRUE)) ||
                        ((q_status.dial_method == Q_DIAL_METHOD_SOCKET) &&
                            (net_is_connected() == Q_TRUE)) ||
                        ((q_status.dial_method == Q_DIAL_METHOD_RLOGIN) &&
                            (net_is_connected() == Q_TRUE)) ||
#ifdef Q_SSH_CRYPTLIB
                        ((q_status.dial_method == Q_DIAL_METHOD_SSH) &&
                            (net_is_connected() == Q_TRUE)) ||
#endif
                        ((q_host_active == Q_TRUE) &&
                            (q_host_type == Q_HOST_TYPE_TELNETD)) ||
#ifdef Q_SSH_CRYPTLIB
                        ((q_host_active == Q_TRUE) &&
                            (q_host_type == Q_HOST_TYPE_SSHD)) ||
#endif
                        ((q_host_active == Q_TRUE) &&
                            (q_host_type == Q_HOST_TYPE_SOCKET)) ||
                        (q_status.hanging_up == Q_TRUE)
                    )
                ) {
                    /*
                     * All of the bytes available were for a telnet / rlogin
                     * / ssh / etc. layer, nothing for us here.
                     */
                    goto no_data;

#if defined(Q_PDCURSES_WIN32) && !defined(Q_NO_SERIAL)
                } else if ((error == EAGAIN) && Q_SERIAL_OPEN) {
                    /*
                     * We called qodem_read() for the serial port, but there
                     * was no data waiting to be read.
                     */
                    goto no_data;

#endif

#ifdef Q_PDCURSES_WIN32
                } else if (error == WSAECONNRESET) {
#else
                } else if (errno == ECONNRESET) {
#endif
                    /* "Connection reset by peer".  This is EOF. */
                    rc = 0;

#ifdef Q_PDCURSES_WIN32
                } else if (error == WSAECONNABORTED) {
                    /*
                     * "Connection aborted".  Host mode called
                     * shutdown(BOTH).  Treat this as EOF.
                     */
                    rc = 0;
#endif

#ifdef Q_PDCURSES_WIN32
                } else if (error == 0) {
                    /* No idea why this is happening.  Treat it as EOF. */
                    rc = 0;
#endif
                } else {
                    DLOG(("Call to read() failed: %d %s\n",
                            error, get_strerror(error)));

                    snprintf(notify_message, sizeof(notify_message),
                        _("Call to read() failed: %d (%s)"),
                        error, get_strerror(error));
                    notify_form(notify_message, 0);
                    /*
                     * Treat it like EOF.  This will terminate the
                     * connection.
                     */
                    rc = 0;
                }
            } /* if (rc < 0) */

            if (rc == 0) {
                /* EOF */
#ifndef Q_NO_SERIAL
                if (Q_SERIAL_OPEN) {
                    /* Modem/serial */
                    close_serial_port();
                } else {
                    /* All other */
                    cleanup_connection();
                }
#else
                /* All other */
                cleanup_connection();
#endif /* Q_NO_SERIAL */

                /* Kill quicklearn script */
                stop_quicklearn();

                /* Kill running script */
                script_stop();

                /* Compute time */
                /* time_string needs to be hours/minutes/seconds CONNECTED */
                time(&current_time);
                connect_time = (time_t) difftime(current_time,
                                                 q_status.connect_time);
                hours   = (int)  (connect_time / 3600);
                minutes = (int) ((connect_time % 3600) / 60);
                seconds = (int)  (connect_time % 60);
                snprintf(time_string, sizeof(time_string), "%02u:%02u:%02u",
                    hours, minutes, seconds);

                qlog(_("CONNECTION CLOSED. Total time online: %s\n"),
                    time_string);

                /*
                 * If we died before switching out of DIALING into CONNECTED,
                 * (e.g. at Q_DIAL_CONNECTED in phonebook_refresh()), then
                 * switch back to phonebook mode.
                 */
                if (q_program_state == Q_STATE_DIALER) {
                    switch_state(Q_STATE_PHONEBOOK);
                    q_screen_dirty = Q_TRUE;
                    /*
                     * We need to explicitly call refresh_hanlder() because
                     * phonebook_keyboard_handler() blocks.
                     */
                    refresh_handler();
                }

                /* Wipe out current dial entry */
                q_current_dial_entry = NULL;

                /* Not in the middle of a hangup sequence */
                q_status.hanging_up = Q_FALSE;
                return;
            }

            time(&data_time);

#if !defined(Q_NO_SERIAL) && !defined(Q_PDCURSES_WIN32)
            /* Mark/space parity */
            if (Q_SERIAL_OPEN && (q_serial_port.parity == Q_PARITY_MARK)) {
                /* Incoming data as MARK parity:  strip the 8th bit */
                for (i = 0; i < rc; i++) {
                    q_buffer_raw[q_buffer_raw_n + i] &= 0x7F;
                }
            }
#endif

#ifdef LINE_NOISE
            for (i = 0; i < rc; i++) {
                int do_noise = random() % line_noise_per_bytes;
                if ((do_noise == 1) && (noise_stop == Q_FALSE)) {
                    q_buffer_raw[i] = random() % 0xFF;
                    noise_stop = Q_TRUE;
                    break;
                }
            }
#endif

            /* Record # of new bytes in */
            q_buffer_raw_n += rc;

            if (DLOGNAME != NULL) {
                DLOG(("INPUT bytes: "));
                for (i = 0; i < q_buffer_raw_n; i++) {
                    DLOG2(("%02x ", q_buffer_raw[i] & 0xFF));
                }
                DLOG2(("\n"));
                DLOG(("INPUT bytes (ASCII): "));
                for (i = 0; i < q_buffer_raw_n; i++) {
                    DLOG2(("%c ", q_buffer_raw[i] & 0xFF));
                }
                DLOG2(("\n"));
            }
        } /* if (n > 0) */
    } /* if (FD_ISSET(q_child_tty_fd, &readfds)) */

no_data:

    if (DLOGNAME != NULL) {
        DLOG(("\n"));
        DLOG(("q_program_state: "));
        switch (q_program_state) {
        case Q_STATE_DIALER:
            DLOG2(("Q_STATE_DIALER"));
            break;
        case Q_STATE_HOST:
            DLOG2(("Q_STATE_HOST"));
            break;
        case Q_STATE_CONSOLE:
            DLOG2(("Q_STATE_CONSOLE"));
            break;
        case Q_STATE_UPLOAD:
            DLOG2(("Q_STATE_UPLOAD"));
            break;
        case Q_STATE_DOWNLOAD:
            DLOG2(("Q_STATE_DOWNLOAD"));
            break;
        case Q_STATE_UPLOAD_BATCH:
            DLOG2(("Q_STATE_UPLOAD_BATCH"));
            break;
        case Q_STATE_PHONEBOOK:
            DLOG2(("Q_STATE_PHONEBOOK"));
            break;
        case Q_STATE_SCRIPT_EXECUTE:
            DLOG2(("Q_STATE_SCRIPT_EXECUTE"));
            break;
        default:
            DLOG2(("%d", q_program_state));
            break;
        }
        DLOG2((" q_transfer_buffer_raw_n %d\n", q_program_state, q_transfer_buffer_raw_n));
        if (q_transfer_buffer_raw_n > 0) {
            DLOG(("LEFTOVER OUTPUT\n"));
        }
        DLOG(("\n"));
    }

    unprocessed_n = q_buffer_raw_n;

    /*
     * Modem dialer - allow everything to be sent first before looking for
     * more data.
     */
    if ((q_program_state == Q_STATE_DIALER) && (q_transfer_buffer_raw_n == 0)) {

#ifndef Q_NO_SERIAL
        assert(q_current_dial_entry != NULL);
        if (q_current_dial_entry->method != Q_DIAL_METHOD_MODEM) {
            /*
             * We're doing a network connection, do NOT consume the data.
             * Leave it in the buffer for the console to see later.
             */

            /* Do nothing */
        } else {
            /*
             * We're talking to the modem.
             */
            dialer_process_data(q_buffer_raw, q_buffer_raw_n, &unprocessed_n,
                q_transfer_buffer_raw, &q_transfer_buffer_raw_n,
                sizeof(q_transfer_buffer_raw));
        }

#endif /* Q_NO_SERIAL */

    }

    if ((q_program_state == Q_STATE_UPLOAD) ||
        (q_program_state == Q_STATE_UPLOAD_BATCH) ||
        (q_program_state == Q_STATE_DOWNLOAD) ||
        (q_program_state == Q_STATE_SCRIPT_EXECUTE) ||
        (q_program_state == Q_STATE_HOST)
    ) {
        /*
         * File transfers, scripts, and host mode: run
         * protocol_process_data() until old_q_transfer_buffer_raw_n ==
         * q_transfer_buffer_raw_n .
         *
         * Every time we come through process_incoming_data() we call
         * protocol_process_data() at least once.
         */

        int old_q_transfer_buffer_raw_n = -1;
        DLOG(("ENTER TRANSFER LOOP\n"));

        while (old_q_transfer_buffer_raw_n != q_transfer_buffer_raw_n) {
            unprocessed_n = q_buffer_raw_n;
            old_q_transfer_buffer_raw_n = q_transfer_buffer_raw_n;

            DLOG(("2 old_q_transfer_buffer_raw_n %d q_transfer_buffer_raw_n %d unprocessed_n %d\n",
                    old_q_transfer_buffer_raw_n, q_transfer_buffer_raw_n,
                    unprocessed_n));

            if ((q_program_state == Q_STATE_UPLOAD) ||
                (q_program_state == Q_STATE_UPLOAD_BATCH) ||
                (q_program_state == Q_STATE_DOWNLOAD)
            ) {
                /* File transfer protocol data handler */
                protocol_process_data(q_buffer_raw, q_buffer_raw_n,
                    &unprocessed_n, q_transfer_buffer_raw,
                    &q_transfer_buffer_raw_n, sizeof(q_transfer_buffer_raw));
            } else if (q_program_state == Q_STATE_SCRIPT_EXECUTE) {
                /* Script data handler */
                script_process_data(q_buffer_raw, q_buffer_raw_n,
                    &unprocessed_n, q_transfer_buffer_raw,
                    &q_transfer_buffer_raw_n,
                    sizeof(q_transfer_buffer_raw));
                /*
                 * Reset the flags so the second call is a timeout type.
                 */
                q_running_script.stdout_readable = Q_FALSE;
                q_running_script.stdin_writeable = Q_FALSE;
            } else if (q_program_state == Q_STATE_HOST) {
                /* Host mode data handler */
                host_process_data(q_buffer_raw, q_buffer_raw_n, &unprocessed_n,
                    q_transfer_buffer_raw, &q_transfer_buffer_raw_n,
                    sizeof(q_transfer_buffer_raw));
            }

            DLOG(("3 old_q_transfer_buffer_raw_n %d q_transfer_buffer_raw_n %d unprocessed_n %d\n",
                    old_q_transfer_buffer_raw_n, q_transfer_buffer_raw_n,
                    unprocessed_n));

            /* Hang onto whatever was unprocessed */
            if (unprocessed_n > 0) {
                memmove(q_buffer_raw, q_buffer_raw +
                    q_buffer_raw_n - unprocessed_n, unprocessed_n);
            }
            q_buffer_raw_n = unprocessed_n;

            DLOG(("4 old_q_transfer_buffer_raw_n %d q_transfer_buffer_raw_n %d unprocessed_n %d\n",
                    old_q_transfer_buffer_raw_n, q_transfer_buffer_raw_n,
                    unprocessed_n));

            /*
             * The bytes between old_q_transfer_buffer_raw_n and
             * q_transfer_buffer_raw_n needs to be run ONCE through the 8-bit
             * translate table.
             */
            if (old_q_transfer_buffer_raw_n < 0) {
                for (i = 0; i < q_transfer_buffer_raw_n; i++) {
                    q_transfer_buffer_raw[i] =
                        translate_8bit_out(q_transfer_buffer_raw[i]);
                }
            } else {
                for (i = old_q_transfer_buffer_raw_n;
                     i < q_transfer_buffer_raw_n; i++) {
                    q_transfer_buffer_raw[i] =
                        translate_8bit_out(q_transfer_buffer_raw[i]);
                }
            }

        }

        DLOG(("EXIT TRANSFER LOOP\n"));

    }

    /* Terminal mode */
    if (q_program_state == Q_STATE_CONSOLE) {
        DLOG(("console_process_incoming_data: > q_buffer_raw_n %d unprocessed_n %d\n",
                q_buffer_raw_n, unprocessed_n));

        /*
         * Usability issue: if we're in the middle of a very fast but botched
         * download, the keyboard will become hard to use if we keep reading
         * stuff and processing through the console.  So flag a potential
         * console flood.
         */
        if (q_program_state == Q_STATE_CONSOLE) {
            if (q_buffer_raw_n >= sizeof(q_buffer_raw) / 2) {
                q_console_flood = Q_TRUE;
            } else {
                q_console_flood = Q_FALSE;
            }
        }

        /* Let the console process the data */
        console_process_incoming_data(q_buffer_raw, q_buffer_raw_n,
            &unprocessed_n);

        DLOG(("console_process_incoming_data: < q_buffer_raw_n %d unprocessed_n %d\n",
                q_buffer_raw_n, unprocessed_n));
    }

    assert(q_transfer_buffer_raw_n >= 0);
    assert(unprocessed_n >= 0);

    /* Hang onto whatever was unprocessed */
    if (unprocessed_n > 0) {
        memmove(q_buffer_raw, q_buffer_raw + q_buffer_raw_n - unprocessed_n,
            unprocessed_n);
    }
    q_buffer_raw_n = unprocessed_n;

#ifdef Q_NO_SERIAL
    DLOG(("serial_open = %s online = %s q_transfer_buffer_raw_n = %d\n",
            "N/A",
            (q_status.online == Q_TRUE ? "true" : "false"),
            q_transfer_buffer_raw_n));
#else
    DLOG(("serial_open = %s online = %s q_transfer_buffer_raw_n = %d\n",
            (q_status.serial_open == Q_TRUE ? "true" : "false"),
            (q_status.online == Q_TRUE ? "true" : "false"),
            q_transfer_buffer_raw_n));
#endif

    /* Write the data in the output buffer to q_child_tty_fd */
    if ((Q_SERIAL_OPEN || (q_status.online == Q_TRUE)) &&
        (q_transfer_buffer_raw_n > 0)
    ) {

#ifdef LINE_NOISE
        for (i = 0; i < q_transfer_buffer_raw_n; i++) {
            int do_noise = random() % line_noise_per_bytes;
            if ((do_noise == 1) && (noise_stop == Q_FALSE)) {
                q_transfer_buffer_raw[i] = random() % 0xFF;
                noise_stop = Q_TRUE;
                break;
            }
        }
#endif

        /*
         * Write the data to q_child_tty_fd.
         */
#ifndef Q_NO_SERIAL
        if (q_program_state == Q_STATE_DIALER) {
            assert(q_current_dial_entry != NULL);
            if (q_current_dial_entry->method == Q_DIAL_METHOD_MODEM) {
                /*
                 * During the dialing sequence, force sync on every write.
                 * This is potentially MUCH slower due to the tcdrain()
                 * calls.
                 */
                rc = qodem_write(q_child_tty_fd,
                    (char *) q_transfer_buffer_raw, q_transfer_buffer_raw_n,
                    Q_TRUE);
            } else {
                /*
                 * Dialing, but not modem: buffer output.
                 */
                rc = qodem_write(q_child_tty_fd,
                    (char *) q_transfer_buffer_raw, q_transfer_buffer_raw_n,
                    Q_FALSE);
            }
        } else {
            /*
             * Normal case: buffer output.
             */
            rc = qodem_write(q_child_tty_fd, (char *) q_transfer_buffer_raw,
                q_transfer_buffer_raw_n, Q_FALSE);
        }
#else
        /*
         * No serial support: buffer output.
         */
        rc = qodem_write(q_child_tty_fd, (char *) q_transfer_buffer_raw,
            q_transfer_buffer_raw_n, Q_FALSE);

#endif /* Q_NO_SERIAL */

        if (rc < 0) {
            int error = get_errno();

            switch (error) {

            case EAGAIN:
#ifdef Q_PDCURSES_WIN32
            case WSAEWOULDBLOCK:
#endif
                /*
                 * Outgoing buffer is full, wait for the next round
                 */
                break;
#ifdef Q_PDCURSES_WIN32
            case WSAEBADF:
                /*
                 * I'm not seeing the read 0 --> EOF sometimes.  Ignore this
                 * for now, the read() call will set its "error 0" to EOF.
                 */
                break;
#endif
            default:
#ifdef Q_PDCURSES_WIN32
#ifndef Q_NO_SERIAL
                if (q_serial_handle != NULL) {
                    DWORD serial_error = GetLastError();
                    DLOG(("Call to write() failed: %d %s\n",
                            serial_error, strerror(serial_error)));

                    /* Uh-oh, error */
                    snprintf(notify_message, sizeof(notify_message),
                        _("Call to write() failed: %s"),
                        strerror(serial_error));
                    notify_form(notify_message, 0);
                } else {
#endif
                    DLOG(("Call to write() failed: %d %s\n", error,
                            get_strerror(error)));

                    /* Uh-oh, error */
                    snprintf(notify_message, sizeof(notify_message),
                        _("Call to write() failed: %s"), get_strerror(error));
                    notify_form(notify_message, 0);
#ifndef Q_NO_SERIAL
                }
#endif

#else

                DLOG(("Call to write() failed: %d %s\n",
                        error, get_strerror(error)));

                /* Uh-oh, error */
                snprintf(notify_message, sizeof(notify_message),
                    _("Call to write() failed: %s"), get_strerror(error));
                notify_form(notify_message, 0);
#endif
                return;
            }
        } else {

            DLOG(("%d bytes written\n", rc));

            /* Hang onto the difference for the next round */
            assert(rc <= q_transfer_buffer_raw_n);

            if (rc < q_transfer_buffer_raw_n) {
                memmove(q_transfer_buffer_raw, q_transfer_buffer_raw + rc,
                    q_transfer_buffer_raw_n - rc);
            }
            q_transfer_buffer_raw_n -= rc;
        }
    }
}

/**
 * See if a child process has exited.  For non-shell connections, this
 * returns false.
 *
 * @return true if the child process has exited
 */
static Q_BOOL child_is_dead() {

#ifdef Q_PDCURSES_WIN32
    DWORD status;
#endif

#ifdef Q_PDCURSES_WIN32

    /* Win32 case */
    if (q_child_process == NULL) {
        /*
         * This is not a shell connection.
         */
        return Q_FALSE;
    }

    if (GetExitCodeProcess(q_child_process, &status) == TRUE) {
        /* Got return code */
        if (status == STILL_ACTIVE) {
            /*
             * Process is still running.
             */
            return Q_FALSE;
        } else {
            /*
             * Process has died.
             */
            return Q_TRUE;
        }
    } else {
        /*
         * Can't get process exit code, assume it is dead.
         */
        return Q_TRUE;
    }

#else

    /* POSIX case */
    if (q_child_pid == -1) {
        /*
         * This is not a shell connection.
         */
        return Q_FALSE;
    }

    /* See if SIGCHLD was received before. */
    return q_child_exited;

#endif /* Q_PDCURSES_WIN32 */

}

/**
 * Check various data sources and sinks for data, and dispatch to appropriate
 * handlers.
 */
static void data_handler() {
    int rc;
    int error;
    int select_fd_max;
    struct timeval listen_timeout;
    time_t current_time;
    int default_timeout;
    char notify_message[DIALOG_MESSAGE_SIZE];
    Q_BOOL have_data = Q_FALSE;
#ifndef Q_NO_SERIAL
    char time_string[SHORT_TIME_SIZE];
    int hours, minutes, seconds;
    time_t connect_time;
#endif

#ifdef Q_PDCURSES_WIN32
    Q_BOOL check_net_data = Q_FALSE;
#ifndef Q_NO_SERIAL
    DWORD serial_event_mask = 0;
#endif
#endif

    /* Flush curses */
    screen_flush();

#ifdef Q_PDCURSES_WIN32
    /*
     * Win32 doesn't support select() on stdin or on sub-process pipe
     * handles.  So we call different functions to check for data depending
     * on what we're connected to.  We still use the select() as a general
     * idle call.
     */

    if ((net_is_connected() == Q_FALSE) &&
        (net_connect_pending() == Q_FALSE) &&
        (net_is_listening() == Q_FALSE)
    ) {
        if (is_readable(q_child_tty_fd) == Q_TRUE) {
            /*
             * The socket was readable on the last select() call, so make
             * sure to check it again.  This is needed to ensure that
             * lower-level layers can establish their session between
             * net_connect_start() and net_connect_finish().
             */
            have_data = Q_TRUE;
        }

        if (q_child_tty_fd != -1) {
            switch (q_status.dial_method) {
            case Q_DIAL_METHOD_SOCKET:
            case Q_DIAL_METHOD_TELNET:
            case Q_DIAL_METHOD_RLOGIN:
            case Q_DIAL_METHOD_SSH:
                /*
                 * Network connections: always include in select() call.
                 * This is to catch the EOF after calling shutdown().
                 */
                check_net_data = Q_TRUE;
                break;
            case Q_DIAL_METHOD_COMMANDLINE:
            case Q_DIAL_METHOD_SHELL:
#ifndef Q_NO_SERIAL
            case Q_DIAL_METHOD_MODEM:
#endif
                break;
            }
        }
    } else {
        check_net_data = Q_TRUE;
    }

    DLOG(("data_handler() have_data %s check_net_data %s\n",
            (have_data == Q_TRUE ? "true" : "false"),
            (check_net_data == Q_TRUE ? "true" : "false")));

    if (q_program_state == Q_STATE_SCRIPT_EXECUTE) {
        /* Check for readability on stdout */
        DWORD actual_bytes = 0;
        if (PeekNamedPipe(q_script_stdout, NULL, 0, NULL, &actual_bytes,
                NULL) == 0) {
            /*
             * Error peeking.  Go after data anyway to catch a process
             * termination.
             */
            q_running_script.stdout_readable = Q_TRUE;
        } else {
            if (actual_bytes > 0) {
                /* Data is available for reading */
                q_running_script.stdout_readable = Q_TRUE;
            } else {
                /* Do not access stdout */
                q_running_script.stdout_readable = Q_FALSE;
            }
        }
        /*
         * We always assume the script is writeable and look for EAGAIN when
         * we write 0.
         */
        q_running_script.stdin_writeable = Q_TRUE;
    }
#else

    /*
     * For network connections through the dialer, we might have read data
     * before we got to STATE_CONSOLE.  So look at q_buffer_raw_n and set
     * have_data appropriately.
     */
    if (q_buffer_raw_n > 0) {
        have_data = Q_TRUE;
    }

#endif /* Q_PDCURSES_WIN32 */

    /*
     * Default is to block 20 milliseconds (50Hz).
     */
    default_timeout = 20000;

    /* Initialize select() structures */
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);

#if !defined(Q_PDCURSES) && !defined(Q_PDCURSES_WIN32)
    /* Add stdin */
    select_fd_max = STDIN_FILENO;
    FD_SET(STDIN_FILENO, &readfds);
#else
#  if defined(Q_PDCURSES) && !defined(Q_PDCURSES_WIN32)
    /* X11 PDCurses case: select on xc_key_sock just like it was stdin */
    assert(xc_key_sock > 2);

    select_fd_max = xc_key_sock;
    FD_SET(xc_key_sock, &readfds);
#  else
    /* Win32 PDCurses case: don't select on stdin */
    select_fd_max = 0;
#  endif
#endif

    /* Add the child tty */
    if (q_child_tty_fd != -1) {
        /*
         * Do not read data while in some program states.
         */
        switch (q_program_state) {
        case Q_STATE_DIALER:
            if (net_connect_pending() == Q_TRUE) {
                DLOG(("CHECK NET connect()\n"));
                FD_SET(q_child_tty_fd, &writefds);
            }
            /* Fall through... */
        case Q_STATE_HOST:
        case Q_STATE_UPLOAD:
        case Q_STATE_UPLOAD_BATCH:
        case Q_STATE_DOWNLOAD:
        case Q_STATE_SCRIPT_EXECUTE:
        case Q_STATE_CONSOLE:

#ifdef Q_PDCURSES_WIN32
            if (check_net_data == Q_TRUE) {
#endif
                DLOG(("select on q_child_tty_fd = %d\n", q_child_tty_fd));

                /* These states are OK to read() */
                FD_SET(q_child_tty_fd, &readfds);

                if ((q_status.dial_method == Q_DIAL_METHOD_RLOGIN) &&
                    (net_is_connected() == Q_TRUE)
                ) {
                    /* rlogin needs to look for OOB data */
                    FD_SET(q_child_tty_fd, &exceptfds);
                }

                /* Flag if we need to send data out to the child tty */
                if (q_transfer_buffer_raw_n > 0) {
                    FD_SET(q_child_tty_fd, &writefds);
                }
                if (q_child_tty_fd > select_fd_max) {
                    select_fd_max = q_child_tty_fd;
                }
#ifdef Q_PDCURSES_WIN32
            }
#endif
#if defined(__linux) && defined(Q_ENABLE_GPM)
            /*
             * If we have GPM running, select on its descriptor.
             */
            if ((q_gpm_mouse == Q_TRUE) && (gpm_fd > 2)) {
                DLOG(("GPM FD = %d\n", gpm_fd));
                FD_SET(gpm_fd, &readfds);
                if (gpm_fd > select_fd_max) {
                    select_fd_max = gpm_fd;
                }
            }
#endif
            break;
        case Q_STATE_DOWNLOAD_MENU:
        case Q_STATE_UPLOAD_MENU:
        case Q_STATE_DOWNLOAD_PATHDIALOG:
        case Q_STATE_UPLOAD_PATHDIALOG:
        case Q_STATE_EMULATION_MENU:
        case Q_STATE_TRANSLATE_MENU:
        case Q_STATE_INITIALIZATION:
        case Q_STATE_UPLOAD_BATCH_DIALOG:
        case Q_STATE_CODEPAGE:
        case Q_STATE_SCROLLBACK:
        case Q_STATE_CONSOLE_MENU:
        case Q_STATE_INFO:
        case Q_STATE_FUNCTION_KEY_EDITOR:
#ifndef Q_NO_SERIAL
        case Q_STATE_MODEM_CONFIG:
#endif
        case Q_STATE_PHONEBOOK:
        case Q_STATE_TRANSLATE_EDITOR_8BIT:
        case Q_STATE_TRANSLATE_EDITOR_UNICODE:
        case Q_STATE_EXIT:
        case Q_STATE_SCREENSAVER:
            /* For these states, do NOT read() */
#ifdef Q_PDCURSES_WIN32
            check_net_data = Q_FALSE;
#endif
            break;
        }
    }

    if (q_program_state == Q_STATE_SCRIPT_EXECUTE) {
#ifndef Q_PDCURSES_WIN32
        /* Add the script tty fd */
        if ((q_running_script.script_tty_fd != -1) &&
            (q_running_script.paused == Q_FALSE)
        ) {
            FD_SET(q_running_script.script_tty_fd, &readfds);

            /* Only check for writeability if we have something to send */
            if (q_running_script.print_buffer_empty == Q_FALSE) {
                FD_SET(q_running_script.script_tty_fd, &writefds);
            }

            if (q_running_script.script_tty_fd > select_fd_max) {
                select_fd_max = q_running_script.script_tty_fd;
            }
        }
#endif
    }

    /* select() needs 1 + MAX */
    select_fd_max++;

    DLOG(("call select(): select_fd_max = %d\n", select_fd_max));

    /* Set the timeout */
    listen_timeout.tv_sec = default_timeout / 1000000;
    listen_timeout.tv_usec = default_timeout % 1000000;
#ifdef Q_PDCURSES_WIN32
    if ((have_data == Q_FALSE) && (check_net_data == Q_TRUE) &&
        (select_fd_max > 1)) {

        rc = select(select_fd_max, &readfds, &writefds, &exceptfds, &listen_timeout);

#ifndef Q_NO_SERIAL
    } else if (q_serial_handle != NULL) {
        /*
         * Use Win32 overlapped I/O to see if we have an empty buffer to
         * write to, data to read, or a ring indication.
         */
        DWORD millis = listen_timeout.tv_sec * 1000 +
        listen_timeout.tv_usec / 1000;
        DWORD comm_mask = EV_RXCHAR | EV_RING;
        OVERLAPPED serial_overlapped;

        DLOG(("Check serial port for data\n"));

        if (q_transfer_buffer_raw_n > 0) {
            /*
             * See when the buffer is empty for more data to write to it.
             */
            comm_mask |= EV_TXEMPTY;
        }
        if (!SetCommMask(q_serial_handle, comm_mask)) {
            error = GetLastError();

            DLOG(("Call to SetCommMask() failed: %d %s\n",
                    error, get_strerror(error)));

            snprintf(notify_message, sizeof(notify_message),
                _("Call to SetCommMask() failed: %d %s"),
                error, get_strerror(error));
            notify_form(notify_message, 0);
            exit(EXIT_ERROR_SERIAL_FAILED);
        }
        q_serial_readable = Q_FALSE;
        DLOG(("comm_mask: %d 0x%x\n", comm_mask, comm_mask));

        ZeroMemory(&serial_overlapped, sizeof(serial_overlapped));
        DLOG(("BEFORE serial_event_mask %d 0x%x\n", serial_event_mask,
                serial_event_mask));
        if (WaitCommEvent(q_serial_handle, &serial_event_mask,
                &serial_overlapped) == FALSE) {
            if (GetLastError() == ERROR_IO_PENDING) {
                DWORD wait_rc;

                DLOG(("WaitCommEvent() returned ERROR_IO_PENDING\n"));

                /*
                 * We don't have an event ready immediately.  Wait until we
                 * do.
                 */
                wait_rc = WaitForSingleObject(q_serial_handle,
                    millis);
                if (wait_rc == WAIT_FAILED) {
                    error = GetLastError();

                    DLOG(("Call to WaitForSingleObject() failed: %d %s\n",
                            error, get_strerror(error)));

                    snprintf(notify_message, sizeof(notify_message),
                        _("Call to WaitForSingleObject() failed: %d %s"),
                        error, get_strerror(error));
                    notify_form(notify_message, 0);
                    exit(EXIT_ERROR_SERIAL_FAILED);
                } else if (wait_rc == WAIT_TIMEOUT) {
                    DLOG(("WaitForSingleObject() WAIT_TIMEOUT\n"));
                    /*
                     * There is no data to process.  Go to the timeout case
                     * at the bottom of the normal POSIX select() code.
                     */
                    rc = 0;
                } else {
                    DLOG(("WaitForSingleObject() WAIT_ABANDONED or WAIT_OBJECT_0\n"));
                    DLOG(("AFTER serial_event_mask %d 0x%x\n",
                            serial_event_mask, serial_event_mask));
                    /*
                     * This is either WAIT_ABANDONED or WAIT_OBJECT_0.  Treat
                     * it the same way, as though some data came in.
                     */
                    rc = 1;
                }

            } else {
                error = GetLastError();

                /*
                 * Something strange happened.  Bail out.
                 */
                DLOG(("Call to WaitCommEvent() failed: %d %s\n",
                        error, get_strerror(error)));

                snprintf(notify_message, sizeof(notify_message),
                    _("Call to WaitCommEvent() failed: %d %s"),
                    error, get_strerror(error));
                notify_form(notify_message, 0);
                exit(EXIT_ERROR_SERIAL_FAILED);
            }
        } else {
            DLOG(("WaitCommEvent() returned TRUE\n"));

            /*
             * Data is ready somewhere on the serial port.  This is
             * equivalent to select() returning > 0.
             */
            rc = 1;
        }

        if ((serial_event_mask & EV_RXCHAR) != 0) {
            DLOG(("q_serial_readable set to TRUE - EV_RXCHAR\n"));
            q_serial_readable = Q_TRUE;
        } else {
            COMSTAT com_stat;
            ClearCommError(q_serial_handle, NULL, &com_stat);
            if (com_stat.cbInQue > 0) {
                DLOG(("q_serial_readable set to TRUE - cbInQue > 0\n"));
                q_serial_readable = Q_TRUE;
            }
        }

#endif /* Q_NO_SERIAL */

    } else { /* if (q_serial_handle != NULL) */

        /*
         * There is no data to process.  We are either on the console or not
         * connected.  Go to the timeout case at the bottom of the normal
         * POSIX select() code.
         */
        rc = 0;
    }

#else

    rc = select(select_fd_max, &readfds, &writefds, &exceptfds,
                &listen_timeout);

#endif /* Q_PDCURSES_WIN32 */

    /*
    DLOG(("q_program_state = %d select() returned %d\n", q_program_state, rc));
    */

    switch (rc) {

    case -1:
        /* ERROR */
        error = get_errno();

        switch (error) {
        case EINTR:
            /* Interrupted system call, say from a SIGWINCH */
            break;
        default:
            DLOG(("Call to select() failed: %d %s\n",
                    error, get_strerror(error)));

            snprintf(notify_message, sizeof(notify_message),
                _("Call to select() failed: %d %s"),
                error, get_strerror(error));
            notify_form(notify_message, 0);
            exit(EXIT_ERROR_SELECT_FAILED);
        }
        break;

    case 0:
        /*
         * We timed out looking for data.  See if other things need to run
         * during this idle period.
         */

        /* Flush capture file if necessary */
        if (q_status.capture == Q_TRUE) {
            if (q_status.capture_flush_time < time(NULL)) {
                fflush(q_status.capture_file);
                q_status.capture_flush_time = time(NULL);
            }
        }

#ifndef Q_NO_SERIAL

        /*
         * Check for DCD drop, but NOT if the host is running in serial or
         * modem mode.
         */
        if ((q_serial_port.ignore_dcd == Q_FALSE) &&
            (q_status.online == Q_TRUE) && Q_SERIAL_OPEN &&
            !((q_program_state == Q_STATE_HOST) &&
                ((q_host_type == Q_HOST_TYPE_SERIAL) ||
                 (q_host_type == Q_HOST_TYPE_MODEM)))
        ) {
            query_serial_port();
            if (q_serial_port.rs232.DCD == Q_FALSE) {
                qlog(_("OFFLINE: modem DCD line went down, lost carrier\n"));

                time(&current_time);
                connect_time = (time_t) difftime(current_time,
                                                 q_status.connect_time);
                hours   = (int)  (connect_time / 3600);
                minutes = (int) ((connect_time % 3600) / 60);
                seconds = (int)  (connect_time % 60);
                snprintf(time_string, sizeof(time_string), "%02u:%02u:%02u",
                    hours, minutes, seconds);

                /* Kill quicklearn script */
                stop_quicklearn();

                /* Kill running script */
                script_stop();

                qlog(_("CONNECTION CLOSED. Total time online: %s\n"),
                    time_string);

                /* Modem/serial */
                close_serial_port();
            }
        }

#endif /* Q_NO_SERIAL */

        /*
         * See if an idle timeout was reached, and if so close the connection.
         */
        if ((q_child_tty_fd != -1) && (q_status.idle_timeout > 0)) {
            time(&current_time);
            if ((difftime(current_time, data_time) > q_status.idle_timeout) &&
                (difftime(current_time, q_data_sent_time) > q_status.idle_timeout)
            ) {
                qlog(_("Connection IDLE timeout exceeded, closing...\n"));

                /* Kill quicklearn script */
                stop_quicklearn();

                /* Kill running script */
                script_stop();

                if (Q_SERIAL_OPEN) {
#ifndef Q_NO_SERIAL
                    /* Modem/serial */
                    close_serial_port();
#endif
                } else {
                    /*
                     * All other cases.  We send the kill now, and the rest
                     * will be handled in process_incoming_data() .
                     */
                    close_connection();
                }
            }
        }

        /*
         * See if the child process died.  It's possible for the child
         * process to be defunct but the fd still be active, e.g. if a shell
         * has a blocked background process with an open handle to its
         * stdout.
         */
        if (child_is_dead() == Q_TRUE) {
            qlog(_("Child process has exited, closing...\n"));
            close_connection();

            /*
             * We need to cleanup immediately, because read() will never
             * return 0.
             */
            cleanup_connection();

            /*
             * Don't allow the loop to enter process_incoming_data().
             */
            break;
        }

        /*
         * See if a keepalive timeout was specified, and if so send the
         * keepalive bytes to the remote side.
         */
        if ((q_child_tty_fd != -1) && (q_keepalive_timeout > 0) &&
            (q_program_state != Q_STATE_DIALER)
        ) {
            /* See if keepalive timeout was specified */
            time(&current_time);
            if ((difftime(current_time, data_time) > q_keepalive_timeout) &&
                (difftime(current_time, q_data_sent_time) > q_keepalive_timeout)) {
                /* Send keepalive bytes */
                if (q_keepalive_bytes_n > 0) {
                    qodem_write(q_child_tty_fd, q_keepalive_bytes,
                        q_keepalive_bytes_n, Q_TRUE);
                }
            }
        }

        /*
         * See if there are any file transfers, scripts, or host mode to run.
         */
        if ((q_program_state == Q_STATE_DOWNLOAD) ||
            (q_program_state == Q_STATE_UPLOAD) ||
            (q_program_state == Q_STATE_UPLOAD_BATCH) ||
            (q_program_state == Q_STATE_DIALER) ||
            (q_program_state == Q_STATE_SCRIPT_EXECUTE) ||
            (q_program_state == Q_STATE_HOST)
#ifdef Q_SSH_CRYPTLIB
            || ((q_status.dial_method == Q_DIAL_METHOD_SSH) &&
                (net_is_connected() == Q_TRUE) &&
                (is_readable(q_child_tty_fd)))
#endif
            || (have_data == Q_TRUE)
        ) {

#ifndef Q_PDCURSES_WIN32
            /*
             * For scripts: this is timeout, so don't try to move data to the
             * pty/pipe.
             */
            if (q_program_state == Q_STATE_SCRIPT_EXECUTE) {
                q_running_script.stdout_readable = Q_FALSE;
                q_running_script.stdin_writeable = Q_FALSE;

            }
#endif

            /* Process incoming data */
            process_incoming_data();
        }
        break;

    default:
        /*
         * At least one descriptor is readable or writeable.
         */

        DLOG(("q_child_tty %s %s %s\n",
                (FD_ISSET(q_child_tty_fd, &readfds) ? "READ" : ""),
                (FD_ISSET(q_child_tty_fd, &writefds) ? "WRITE" : ""),
                (FD_ISSET(q_child_tty_fd, &exceptfds) ? "EXCEPT" : "")));

        /*
         * For scripts: see if stdout/stderr are readable and set flag as
         * needed.
         */
        if (q_program_state == Q_STATE_SCRIPT_EXECUTE) {
#ifndef Q_PDCURSES_WIN32
            if (q_running_script.script_tty_fd != -1) {
                if (FD_ISSET(q_running_script.script_tty_fd, &readfds)) {
                    q_running_script.stdout_readable = Q_TRUE;
                } else {
                    q_running_script.stdout_readable = Q_FALSE;
                }
                if (FD_ISSET(q_running_script.script_tty_fd, &writefds)) {
                    q_running_script.stdin_writeable = Q_TRUE;
                } else {
                    q_running_script.stdin_writeable = Q_FALSE;
                }
            }
#endif
        }

        if ((net_connect_pending() == Q_TRUE) &&
            ((FD_ISSET(q_child_tty_fd, &readfds)) ||
                (FD_ISSET(q_child_tty_fd, &writefds)))
        ) {
            DLOG(("net_connect_finish()\n"));

            /* Our connect() call has completed, go deal with it */
            net_connect_finish();
        }

        /*
         * Data is present somewhere, go process it.
         */
        if (((q_child_tty_fd > 0) && (is_readable(q_child_tty_fd))) ||
            ((q_child_tty_fd > 0) && (FD_ISSET(q_child_tty_fd, &writefds))) ||
#if defined(Q_PDCURSES_WIN32) && !defined(Q_NO_SERIAL)
            ((q_serial_handle != NULL) && (q_serial_readable == Q_TRUE)) ||
            ((q_serial_handle != NULL) &&
                ((serial_event_mask & EV_TXEMPTY) != 0)) ||
#endif
            (q_program_state == Q_STATE_SCRIPT_EXECUTE) ||
            (q_program_state == Q_STATE_HOST)
        ) {
            /* Process incoming data */
            process_incoming_data();
        }

#if defined(__linux) && defined(Q_ENABLE_GPM)
        /*
         * If GPM has data, process it.
         */
        if ((q_gpm_mouse == Q_TRUE) && FD_ISSET(gpm_fd, &readfds)) {
            DLOG(("GPM is readable, check it...\n"));
            Gpm_Event event;
            if (Gpm_GetEvent(&event) > 0) {
                DLOG(("Gpm_GetEvent() > 0, call gpm_handle_mouse()\n"));
                gpm_handle_mouse(&event, 0);
            }
        }
#endif
        break;
    }

}

/**
 * Open a file in the working directory.  It will be opened in "a" mode
 * (opened for appending, created if it does not exist).  This is used for
 * capture file, log file, screen/scrollback dump, and phonebook save files.
 *
 * @param filename the filename to open.  It can be a relative or absolute
 * path.  If absolute, then new_filename will point to a strdup()d copy of
 * filename.
 * @param new_filename this will point to a newly-allocated string containing
 * the full pathname of the opened file, usually
 * /home/username/.qodem/filename.
 * @return the opened file handle
 */
FILE * open_workingdir_file(const char * filename, char ** new_filename) {

    *new_filename = NULL;

    if (strlen(filename) == 0) {
        return NULL;
    }

    if (filename[0] != '/') {
        /* Relative path, prefix working directory */
        *new_filename = (char *)Xmalloc(strlen(filename) +
            strlen(get_option(Q_OPTION_WORKING_DIR)) + 2, __FILE__, __LINE__);
        memset(*new_filename, 0, strlen(filename) +
            strlen(get_option(Q_OPTION_WORKING_DIR)) + 2);

        strncpy(*new_filename, get_option(Q_OPTION_WORKING_DIR),
            strlen(get_option(Q_OPTION_WORKING_DIR)));
        (*new_filename)[strlen(*new_filename)] = '/';
        strncpy(*new_filename + strlen(*new_filename), filename,
            strlen(filename));
    } else {
        /* Duplicate the passed-in filename */
        *new_filename = Xstrdup(filename, __FILE__, __LINE__);
    }

    return fopen(*new_filename, "a");
}


/**
 * Get the full path to a filename in the data directory.  Note that the
 * string returned is a single static buffer, i.e. this is NOT thread-safe.
 *
 * @param filename a relative filename
 * @return the full path to the filename (usually ~/qodem/filename or My
 * Documents\\qodem\\filename).
 */
char * get_datadir_filename(const char * filename) {
    assert(q_home_directory != NULL);
    sprintf(datadir_filename, "%s/%s", q_home_directory, filename);
    return datadir_filename;
}

/**
 * Get the full path to a filename in the working directory.  Note that the
 * string returned is a single static buffer, i.e. this is NOT thread-safe.
 *
 * @param filename a relative filename
 * @return the full path to the filename (usually ~/.qodem/filename or My
 * Documents\\qodem\\prefs\\filename).
 */
char * get_workingdir_filename(const char * filename) {
    sprintf(datadir_filename, "%s/%s",
        get_option(Q_OPTION_WORKING_DIR), filename);

    return datadir_filename;
}

/**
 * Get the full path to a filename in the scripts directory.  Note that the
 * string returned is a single static buffer, i.e. this is NOT thread-safe.
 *
 * @param filename a relative filename
 * @return the full path to the filename (usually ~/.qodem/scripts/filename
 * or My Documents\\qodem\\scripts\\filename).
 */
char * get_scriptdir_filename(const char * filename) {
    sprintf(datadir_filename, "%s/%s",
        get_option(Q_OPTION_SCRIPTS_DIR), filename);

    return datadir_filename;
}

/**
 * Open a file in the data directory.
 *
 * @param filename the filename to open.  It can be a relative or absolute
 * path.  If absolute, then new_filename will point to a strdup()d copy of
 * filename.
 * @param new_filename this will point to a newly-allocated string containing
 * the full pathname of the opened file, usually
 * /home/username/qodem/filename.
 * @param mode the fopen mode to use
 * @return the opened file handle
 */
FILE * open_datadir_file(const char * filename, char ** new_filename,
                         const char * mode) {

    assert(q_home_directory != NULL);

    *new_filename = NULL;

    if (strlen(filename) == 0) {
        return NULL;
    }

    if (filename[0] != '/') {
        /* Relative path, prefix data directory */
        *new_filename = (char *)Xmalloc(strlen(filename) +
            strlen(q_home_directory) + 2, __FILE__, __LINE__);
        memset(*new_filename, 0, strlen(filename) +
            strlen(q_home_directory) + 2);

        strncpy(*new_filename, q_home_directory, strlen(q_home_directory));
        (*new_filename)[strlen(*new_filename)] = '/';
        strncpy(*new_filename + strlen(*new_filename), filename,
            strlen(filename));
    } else {
        /* Duplicate the passed-in filename */
        *new_filename = Xstrdup(filename, __FILE__, __LINE__);
    }

    return fopen(*new_filename, mode);
}

/**
 * Spawn a command in an external terminal.  This is used for the mail reader
 * and external file editors.
 *
 * @param command the command line to execute
 */
void spawn_terminal(const char * command) {

#ifdef Q_PDCURSES
    int i;
    char * substituted_string;
    char * wait_msg;
    substituted_string = substitute_string(get_option(Q_OPTION_X11_TERMINAL),
        "$COMMAND", command);

    /* Clear with background */
    for (i = 0; i < HEIGHT; i++) {
        screen_put_color_hline_yx(i, 0, ' ', WIDTH, Q_COLOR_CONSOLE);
    }
#ifdef Q_PDCURSES_WIN32
    wait_msg = _("Waiting On Command Shell To Exit...");
#else
    wait_msg = _("Waiting On X11 Terminal To Exit...");
#endif

    screen_put_color_str_yx(HEIGHT / 2, (WIDTH - strlen(wait_msg)) / 2,
        wait_msg, Q_COLOR_CONSOLE);
    screen_flush();

    system(substituted_string);
    Xfree(substituted_string, __FILE__, __LINE__);
    screen_clear();
    q_screen_dirty = Q_TRUE;

#else

    reset_shell_mode();
    system(command);
    reset_prog_mode();
    screen_really_clear();
    q_screen_dirty = Q_TRUE;

#endif /* Q_PDCURSES */

}

/**
 * Reset the global status and variables to their default state.
 */
static void reset_global_state() {

    /* Initial program state */
    q_program_state = Q_STATE_INITIALIZATION;

    /*
     * Read only flag.  When set, things that can write to disk are disabled.
     */
    q_status.read_only              = Q_FALSE;

    /* Default to VT102 as the most common denominator */
    q_status.emulation              = Q_EMUL_VT102;
    q_status.codepage               = default_codepage(q_status.emulation);
    q_status.doorway_mode           = Q_DOORWAY_MODE_OFF;
    q_status.zmodem_autostart       = Q_TRUE;
    q_status.zmodem_escape_ctrl     = Q_FALSE;
    q_status.zmodem_zchallenge      = Q_FALSE;

    q_status.kermit_autostart               = Q_TRUE;
    q_status.kermit_robust_filename         = Q_FALSE;
    q_status.kermit_streaming               = Q_TRUE;
    q_status.kermit_long_packets            = Q_TRUE;
    q_status.kermit_uploads_force_binary    = Q_TRUE;
    q_status.kermit_downloads_convert_text  = Q_TRUE;

    q_status.external_telnet        = Q_FALSE;
    q_status.external_rlogin        = Q_TRUE;
    q_status.external_ssh           = Q_TRUE;
    q_status.xterm_double           = Q_TRUE;
    q_status.xterm_mouse_reporting  = Q_TRUE;
    q_status.vt100_color            = Q_TRUE;
    q_status.vt52_color             = Q_TRUE;

    /*
     * Due to Avatar's ANSI fallback, in practice this flag is just an
     * imperceptible performance enhancement.  Before ANSI fallback, it
     * used to really control whether or not Avatar would handle SGR.
     */
    q_status.avatar_color           = Q_TRUE;
    q_status.avatar_ansi_fallback   = Q_TRUE;

    /*
     * I don't think there are any PETSCII emulators that also speak ANSI,
     * but it comes for mostly free so I will leave it in.
     */
    q_status.petscii_color          = Q_TRUE;
    q_status.petscii_ansi_fallback  = Q_TRUE;
    q_status.petscii_has_wide_font  = Q_TRUE;
    q_status.petscii_use_unicode    = Q_FALSE;
    q_status.petscii_is_c64         = Q_TRUE;

    q_status.atascii_has_wide_font  = Q_FALSE;

#ifndef Q_NO_SERIAL
    q_status.serial_open            = Q_FALSE;
#endif /* Q_NO_SERIAL */
    q_status.online                 = Q_FALSE;
    q_status.hanging_up             = Q_FALSE;
    q_status.split_screen           = Q_FALSE;
    q_status.sound                  = Q_FALSE;
    q_status.beeps                  = Q_FALSE;
    q_status.ansi_music             = Q_FALSE;
    q_status.strip_8th_bit          = Q_FALSE;
    q_status.full_duplex            = Q_TRUE;
    q_status.line_feed_on_cr        = Q_FALSE;
    q_status.guard_hangup           = Q_TRUE;
    q_status.capture                = Q_FALSE;
    q_status.capture_file           = NULL;
    q_status.capture_type           = Q_CAPTURE_TYPE_NORMAL;
    q_status.screen_dump_type       = Q_CAPTURE_TYPE_NORMAL;
    q_status.scrollback_save_type   = Q_CAPTURE_TYPE_NORMAL;
    q_status.capture_x              = 0;
    q_status.logging                = Q_FALSE;
    q_status.logging_file           = NULL;
    q_status.scrollback_enabled     = Q_TRUE;
    q_status.scrollback_lines       = 0;
    q_status.status_visible         = Q_TRUE;
    q_status.status_line_info       = Q_FALSE;
    q_status.xterm_mode             = Q_FALSE;
    q_status.bracketed_paste_mode   = Q_FALSE;
    q_status.hard_backspace         = Q_TRUE;
    /*
     * Every console assumes line wrap, so turn it on by default.
     */
    q_status.line_wrap              = Q_TRUE;
    /*
     * BBS-like emulations usually assume 80 columns, so turn it on by
     * default.
     */
    q_status.assume_80_columns      = Q_TRUE;
    q_status.ansi_animate           = Q_FALSE;
    q_status.display_null           = Q_FALSE;
    q_status.reverse_video          = Q_FALSE;
    q_status.origin_mode            = Q_FALSE;
    q_status.insert_mode            = Q_FALSE;
    q_status.hold_screen_mode       = Q_FALSE;
    q_status.led_1                  = Q_FALSE;
    q_status.led_2                  = Q_FALSE;
    q_status.led_3                  = Q_FALSE;
    q_status.led_4                  = Q_FALSE;
    q_status.current_username       = NULL;
    q_status.current_password       = NULL;
    q_status.remote_address         = NULL;
    q_status.remote_port            = NULL;
    q_status.remote_phonebook_name  = NULL;
#ifdef Q_NO_SERIAL
    q_status.dial_method            = Q_DIAL_METHOD_TELNET;
#else
    q_status.dial_method            = Q_DIAL_METHOD_MODEM;
#endif /* Q_NO_SERIAL */
    q_status.idle_timeout           = 0;
    q_status.quicklearn             = Q_FALSE;
    q_screensaver_timeout           = 0;
    q_keepalive_timeout             = 0;
    q_current_dial_entry            = NULL;
    q_status.exit_on_disconnect     = Q_FALSE;

    set_status_line(Q_TRUE);
}

/**
 * Program main entry point.
 *
 * @param argc command-line argument count
 * @param argv command-line arguments
 * @return the final program return code
 */
int qodem_main(int argc, char * const argv[]) {
    int option_index = 0;
    int rc;
    char * env_string;
    char * substituted_filename;
    Q_BOOL first = Q_TRUE;
#ifdef Q_PDCURSES_WIN32
    TCHAR windows_user_name[65];
    DWORD windows_user_name_n;
#else
    wchar_t value_wchar[128];
    char * username;
#endif

    /* Internationalization */
    if (setlocale(LC_ALL, "") == NULL) {
        fprintf(stderr, "setlocale returned NULL: %s\n",
            strerror(errno));
        exit(EXIT_ERROR_SETLOCALE);
    }

/*
 * Bug #3528357 - The "proper" solution is to add LIBINTL to LDFLAGS and have
 * configure build the intl directory.  But until we get actual non-English
 * translations it doesn't matter.  We may as well just disable gettext().
 */
#if defined(ENABLE_NLS) && defined(HAVE_GETTEXT)
#ifdef Q_PDCURSES
    bindtextdomain("qodem-x11", LOCALEDIR);
    textdomain("qodem-x11");
#else
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);
#endif

    /*
    fprintf(stderr, "LANG: %s\n", getenv("LANG"));
    fprintf(stderr, "%s\n", bindtextdomain(PACKAGE, LOCALEDIR));
    fprintf(stderr, "%s\n", textdomain(PACKAGE));
     */
#endif /* ENABLE_NLS && HAVE_GETTEXT */

    /*
     * We reduce ESCDELAY on the assumption that local console is VERY fast.
     * However, if the user has already set ESCDELAY, we don't want to change
     * it.
     */
    if (getenv("ESCDELAY") == NULL) {
        putenv("ESCDELAY=20");
    }

    /*
     * If the user asked for help or version, do that and bail out before
     * doing anything else like reading or writing to disk.
     */
    rc = check_for_help(argc, argv);
    if (rc != 0) {
        exit(rc);
    }

    /*
     * Obtain the user name.
     */
#ifdef Q_PDCURSES_WIN32

    memset(windows_user_name, 0, sizeof(windows_user_name));
    windows_user_name_n = 64;

    if (!GetUserName(windows_user_name, &windows_user_name_n)) {
        /* Error getting username, just make it blank */
        initial_call.username = Xwcsdup(L"", __FILE__, __LINE__);
    } else {
        if (sizeof(TCHAR) == sizeof(char)) {
            /* Convert from char to wchar_t */
            initial_call.username = Xstring_to_wcsdup((char *)windows_user_name,
                __FILE__, __LINE__);
        } else {
            /* TCHAR is wchar_t */
            initial_call.username = Xwcsdup((wchar_t *)windows_user_name,
                __FILE__, __LINE__);
        }
    }
#else
    username = getpwuid(geteuid())->pw_name;
    mbstowcs(value_wchar, username, strlen(username) + 1);
    initial_call.username = Xwcsdup(value_wchar, __FILE__, __LINE__);
#endif /* Q_PDCURSES_WIN32 */

    /*
     * Set the global status to its defaults.
     */
    reset_global_state();

    /*
     * Reset the screensaver clock, otherwise the very first keystroke will
     * activate it.
     */
    time(&screensaver_time);

    /* Initialize the music "engine" :-) */
    music_init();

    /*
     * Setup an initial call state to support the --connect or --dial command
     * line options.
     */
    initial_call.address        = NULL;
    initial_call.port           = "22";
    initial_call.password       = L"";
    initial_call.emulation      = Q_EMUL_XTERM_UTF8;
    initial_call.method         = Q_DIAL_METHOD_SSH;
    initial_call.codepage       = default_codepage(initial_call.emulation);
    initial_call.notes          = NULL;
    initial_call.script_filename            = "";
    initial_call.keybindings_filename       = "";
    initial_call.capture_filename           = "";
    initial_call.translate_8bit_filename    = "";
    initial_call.translate_unicode_filename = "";
    initial_call.doorway                    = Q_DOORWAY_CONFIG;
    initial_call.use_default_toggles        = Q_TRUE;
    dial_phonebook_entry_n                  = -1;

    /* Process options */
    for (;;) {
        rc = getopt_long(argc, argv, "xh?", q_getopt_long_options,
            &option_index);
        if (rc == -1) {
            /* Fall out of the for loop, we're done calling getopt_long */
            break;
        }

        /* See which new option was specified */
        switch (rc) {
        case 0:
            process_command_line_option(
                q_getopt_long_options[option_index].name, optarg);
            break;

        case 'x':
            q_exit_on_disconnect = Q_TRUE;
            break;

        default:
            break;
        }
    } /* for (;;) */

    if (q_program_state == Q_STATE_EXIT) {
        /*
         * --help or --version or somesting similar was on the command
         * line.  Bail out now.
         */
        exit(0);
    }

    /*
     * Set q_home_directory.  load_options() will create the default key
     * binding files and needs to use open_datadir_file().
     */

    if (q_dotqodem_dir != NULL) {
        /* The user supplied a .qodem directory, use that. */
        q_home_directory = q_dotqodem_dir;
    } else {
        /* Sustitute for $HOME */
        env_string = get_home_directory();
#ifdef Q_PDCURSES_WIN32
        q_home_directory = substitute_string("$HOME\\qodem\\prefs", "$HOME",
            env_string);
#else
        q_home_directory = substitute_string("$HOME/.qodem", "$HOME",
            env_string);
#endif
    }

#if !defined(Q_PDCURSES) && !defined(Q_PDCURSES_WIN32)
    /*
     * Xterm: send the private sequence to select metaSendsEscape and
     * bracketed paste mode.
     */
    fprintf(stdout, "\033[?1036;2004h");
    fflush(stdout);
#endif

    /* Load the options. */
    load_options();

    /* Initialize curses. */
    screen_setup(q_rows_arg, q_cols_arg);

    /* Now that colors are known, use them. */
    q_setup_colors();
    q_current_color = scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);

    /*
     * Modify q_status based on command line options.  Do this AFTER
     * load_options() has set defaults.
     */
    resolve_command_line_options();

    /* Setup MIXED mode doorway */
    setup_doorway_handling();

    /*
     * Initialize the keyboard here.  It will newterm() each supported
     * emulation, but restore things before it leaves.
     */
    initialize_keyboard();
    if (q_keyfile != NULL) {
        switch_current_keyboard(q_keyfile);
    }

    /*
     * Set the translation tables to do nothing.
     */
    initialize_translate_tables();
    if (q_xl8file != NULL) {
        use_translate_table_8bit(q_xl8file);
    }
    if (q_xlufile != NULL) {
        use_translate_table_unicode(q_xlufile);
    }

#ifndef Q_NO_SERIAL
    /*
     * Load the modem configuration
     */
    load_modem_config();
#endif

    /*
     * Setup the help system
     */
    setup_help();

    /*
     * See if the user wants automatic capture/logging enabled.
     */
    if (strcasecmp(get_option(Q_OPTION_CAPTURE), "true") == 0) {
        start_capture(get_option(Q_OPTION_CAPTURE_FILE));
    }
    if (strcasecmp(get_option(Q_OPTION_LOG), "true") == 0) {
        start_logging(get_option(Q_OPTION_LOG_FILE));
    }

    /* Default scrolling region needs HEIGHT which is set by curses. */
    q_status.scroll_region_top      = 0;
    q_status.scroll_region_bottom   = HEIGHT - STATUS_HEIGHT - 1;

#ifndef Q_PDCURSES_WIN32
    /* Ignore SIGPIPE */
    signal(SIGPIPE, SIG_IGN);

    /* Catch SIGCHLD */
    signal(SIGCHLD, handle_sigchld);
#endif

    if (q_status.xterm_mode == Q_TRUE) {
        /*
         * We need empty strings for address and name to spawn the local
         * shell.
         */
        initial_call.method = Q_DIAL_METHOD_SHELL;
        initial_call.address = (char *)Xmalloc(sizeof(char) * 1, __FILE__,
            __LINE__);
        initial_call.address[0] = '\0';
        initial_call.name = Xstring_to_wcsdup(initial_call.address,
            __FILE__, __LINE__);
        goto no_initial_call;
    }

    /*
     * If anything else remains, turn it into a command line.
     */
    if (optind < argc) {
        if ((strlen(argv[optind]) == 2) && (strcmp(argv[optind], "--") == 0)) {
            /*
             * Special case: strip out the "--" argument used to pass the
             * remaining arguments to the connect program.
             */
            optind++;
        }

        if (initial_call.address != NULL) {
            /* Error: --connect was specified along with a command line */
            screen_put_color_str_yx(0, 0,
                _("Error: The --connect argument cannot be used when a command"),
                Q_COLOR_CONSOLE_TEXT);
            screen_put_color_str_yx(1, 0,
                _("line is also specified."),  Q_COLOR_CONSOLE_TEXT);

            /*
             * Some X11 emulators (like GNOME-Terminal) will drop directly
             * down to a command-line without leaving the usage info up.
             * Force a keystroke so the user can actually see it.
             */
            screen_put_color_str_yx(3, 0, _("Press any key to continue...\n"),
                Q_COLOR_CONSOLE_TEXT);
            screen_flush();
            discarding_getch();

            q_exitrc = EXIT_ERROR_COMMANDLINE;
            q_program_state = Q_STATE_EXIT;
            /*
             * I'll let it finish constructing initial_call, but it'll never
             * be used.
             */
        }

        /* Set the dial method */
        initial_call.method = Q_DIAL_METHOD_COMMANDLINE;

        /* Build the command line */
        initial_call.address = (char *)Xmalloc(sizeof(char) * 1, __FILE__,
            __LINE__);
        initial_call.address[0] = '\0';
        for (;optind < argc; optind++) {
            /*
             * Expand the buffer to include the next argument + one space +
             * null terminator.
             */
            initial_call.address = (char *)Xrealloc(initial_call.address,
                sizeof(char) * (strlen(initial_call.address) +
                    strlen(argv[optind]) + 2), __FILE__, __LINE__);
            /* Zero out the new space */
            memset(initial_call.address + strlen(initial_call.address), 0,
                strlen(argv[optind]) + 1);
            if (first == Q_TRUE) {
                /* Don't precede the entire command line with a space. */
                first = Q_FALSE;
            } else {
                /* Add the space before the next argument */
                initial_call.address[strlen(initial_call.address)] = ' ';
            }
            /* Copy the next argument + the null terminator over */
            memcpy(initial_call.address + strlen(initial_call.address),
                argv[optind], strlen(argv[optind]) + 1);
        }
        initial_call.name = Xstring_to_wcsdup(initial_call.address,
            __FILE__, __LINE__);

    } /* if (optind < argc) */

no_initial_call:

    /* See if we need to --play something */
    if (play_music_string != NULL) {
        play_ansi_music(play_music_string, strlen((char *)play_music_string),
            Q_TRUE);
        Xfree(play_music_string, __FILE__, __LINE__);
        play_music_string = NULL;
        if (play_music_exit == Q_TRUE) {
            q_program_state = Q_STATE_EXIT;
        }
    }

    if (q_program_state != Q_STATE_EXIT) {

        /*
         * Load the phonebook
         */
        if (q_dotqodem_dir != NULL) {
            /* The user supplied a .qodem directory, use that. */
            substituted_filename = substitute_string("$HOME/"
                DEFAULT_PHONEBOOK, "$HOME", q_dotqodem_dir);
        } else {
            /* Sustitute for $HOME */
            env_string = get_home_directory();
#ifdef Q_PDCURSES_WIN32
            substituted_filename = substitute_string("$HOME\\qodem\\prefs\\"
                DEFAULT_PHONEBOOK, "$HOME", env_string);
#else
            substituted_filename = substitute_string("$HOME/.qodem/"
                DEFAULT_PHONEBOOK, "$HOME", env_string);
#endif
        }

#ifdef Q_PDCURSES_WIN32
        if (file_exists(substituted_filename) == Q_FALSE) {
#else
        if (access(substituted_filename, F_OK) != 0) {
#endif
            /*
             * The default phonebook does not exist.  Try to create it.
             */
            FILE * file;
            if ((file = fopen(substituted_filename, "w")) == NULL) {
                screen_put_color_printf_yx(0, 0, Q_COLOR_CONSOLE_TEXT,
                    _("Error creating file \"%s\": %s\n"),
                    substituted_filename, strerror(errno));
                screen_put_color_printf_yx(3, 0, Q_COLOR_CONSOLE_TEXT,
                    _("Press any key to continue...\n"));
                screen_flush();
                discarding_getch();
            } else {
                fclose(file);
                /* Create the default phonebook */
                q_phonebook.filename = substituted_filename;
                create_phonebook();
            }
        }
        q_phonebook.filename = substituted_filename;

        /* Now load it */
        load_phonebook(Q_FALSE);

        /*
         * Explicitly call console_refresh() so that the scrollback will be
         * set up.
         */
        console_refresh(Q_FALSE);

        /* Reset all emulations */
        reset_emulation();

        if (dial_phonebook_entry_n != -1) {
            q_current_dial_entry = q_phonebook.entries;
            while ((dial_phonebook_entry_n > 1) &&
                (q_current_dial_entry != NULL)
            ) {
                q_current_dial_entry = q_current_dial_entry->next;
                dial_phonebook_entry_n--;
            }
            if (q_current_dial_entry != NULL) {
                q_phonebook.selected_entry = q_current_dial_entry;
                phonebook_normalize();
                do_dialer();
            }
        } else if (initial_call.address != NULL) {
            q_keyboard_blocks = Q_TRUE;
            q_current_dial_entry = &initial_call;
            do_dialer();
        } else if ((strncmp(get_option(Q_OPTION_START_PHONEBOOK),
                "true", 4) == 0) && (q_status.xterm_mode == Q_FALSE)) {
            switch_state(Q_STATE_PHONEBOOK);
        } else if (q_status.xterm_mode == Q_TRUE) {
            /*
             * Spawn a the local shell.
             */
            q_keyboard_blocks = Q_TRUE;
            q_current_dial_entry = &initial_call;
            do_dialer();
        } else {
            switch_state(Q_STATE_CONSOLE);
        }

        if ((strncmp(get_option(Q_OPTION_STATUS_LINE_VISIBLE),
                "true", 4) == 0) &&
            (q_status.xterm_mode == Q_FALSE) &&
            (status_line_disabled == Q_FALSE)
        ) {
            set_status_line(Q_TRUE);
        } else {
            set_status_line(Q_FALSE);
        }

#ifdef Q_SSH_CRYPTLIB
        if ((cryptStatusError(cryptInit()) != CRYPT_OK) ||
            (cryptStatusError(cryptAddRandom(NULL,
                    CRYPT_RANDOM_SLOWPOLL)) != CRYPT_OK)
        ) {
            screen_put_color_printf_yx(0, 0, Q_COLOR_CONSOLE_TEXT,
                _("Error initializing cryptlib\n"));
            screen_put_color_printf_yx(3, 0, Q_COLOR_CONSOLE_TEXT,
                _("Press any key to continue...\n"));
            screen_flush();
            discarding_getch();
        } else {
            ssh_create_server_key();
        }
#endif

        /* Enter main loop */
        for (;;) {
            /* Window size checks, refresh, etc. */
            refresh_handler();

            /* Grab data */
            data_handler();

            keyboard_handler();
            if (q_program_state == Q_STATE_EXIT) {
                break;
            }
        }

    } /* if (q_program_state != Q_STATE_EXIT) */

    /* Close any open files */
    stop_capture();
    stop_quicklearn();
    script_stop();
#ifndef Q_NO_SERIAL
    if (Q_SERIAL_OPEN) {
        close_serial_port();
    }
#endif

    /* Log our exit */
    qlog(_("Qodem exiting...\n"));
    stop_logging();

    /* Clear the screen */
    screen_clear();

    /* Shutdown curses */
    screen_teardown();

    /* Shutdown the music "engine" :-) */
    music_teardown();

#ifdef Q_SSH_CRYPTLIB
    cryptEnd();
#endif

#ifdef Q_PDCURSES_WIN32
    /* Shutdown winsock */
    stop_winsock();
#endif

#if !defined(Q_PDCURSES) && !defined(Q_PDCURSES_WIN32)
    /*
     * Xterm: send the private sequence to disable bracketed paste mode.
     */
    fprintf(stdout, "\033[?2004l");
    fflush(stdout);
#endif

    /* Exit */
    exit(q_exitrc);

    /* Should never get here. */
    return (q_exitrc);
}

/**
 * Program main entry point.
 *
 * @param argc command-line argument count
 * @param argv command-line arguments
 * @return the final program return code
 */
int main(int argc, char * const argv[]) {
    return qodem_main(argc, argv);
}

#ifdef Q_PDCURSES_WIN32

/**
 * Windows main entry point.
 *
 * @param hInstance the current application instance
 * @param hPrevInstance the previous application instance
 * @param lpszCmdLine the command line
 * @param nCmdShow flags for how the window is shown
 */
int APIENTRY WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance,
    LPSTR lpszCmdLine, int nCmdShow) {

    char * argv[30];
    int i;
    int argc = 1;

    argv[0] = "qodem";
    for (i = 0; lpszCmdLine[i]; i++) {
        if ((lpszCmdLine[i] != ' ') &&
            ((i == 0) || lpszCmdLine[i - 1] == ' ')
        ) {
            argv[argc++] = lpszCmdLine + i;
        }
    }

    for (i = 0; lpszCmdLine[i]; i++) {
        if (lpszCmdLine[i] == ' ') {
            lpszCmdLine[i] = '\0';
        }
    }
    return qodem_main(argc, (char **)argv);
}

#endif
