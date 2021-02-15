/*
 * dialer.c
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

#ifdef Q_PDCURSES_WIN32
#  include <winsock2.h>
#  include <windows.h>
#else
/* Find the right header for forkpty() */
#  ifdef __APPLE__
#    include <util.h>
#  else
#    if defined(__FreeBSD__) || \
        defined(__OpenBSD__) || \
        defined(__NetBSD__)
#      include <sys/types.h>
#      include <util.h>
#    else
#      include <pty.h>
#    endif
#  endif
#  include <termios.h>
#  include <unistd.h>
#endif /* Q_PDCURSES_WIN32 */

#ifndef Q_PDCURSES_WIN32
#include <sys/ioctl.h>
#endif
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <assert.h>
#include <fcntl.h>
#include "qodem.h"
#include "music.h"
#include "states.h"
#include "screen.h"
#include "options.h"
#include "script.h"
#include "netclient.h"
#include "dialer.h"

#ifdef Q_PDCURSES_WIN32

/*
 * Win32 case: we have to create pipes that are connected to the child
 * process' streams.  These are initialized in spawn_process().
 */
HANDLE q_child_stdin = NULL;
HANDLE q_child_stdout = NULL;
HANDLE q_child_process = NULL;
HANDLE q_child_thread = NULL;

/* If set, errors in spawn_process() will be emitted to stderr. */
/* #define DEBUG_SPAWNPROCESS 1 */

#else

/*
 * POSIX case: we need to reset the child exited flag before forkpty().
 */
extern Q_BOOL q_child_exited;

#endif

/**
 * Our current dialing state.
 */
Q_DIAL_STATE q_dial_state;

/**
 * When we started dialing.
 */
time_t q_dialer_start_time;

/**
 * How much time is left (in seconds) on the cycle clock.
 */
time_t q_dialer_cycle_time;

/**
 * When the cycle clock started.
 */
time_t q_dialer_cycle_start_time;

/**
 * How many calls have been attempted.
 */
int q_dialer_attempts;

/**
 * The status line to report on the redialer screen.
 */
char q_dialer_status_message[DIALOG_MESSAGE_SIZE];

/**
 * The modem line to report on the redialer screen.
 */
char q_dialer_modem_message[DIALOG_MESSAGE_SIZE];

/**
 * Convert a command line string with spaces into a NULL-terminated array of
 * strings appropriate to passing to the execvp().
 *
 * @param argv the command line string
 * @return the array
 */
char ** tokenize_command(const char * argv) {
    int i, j;
    int argv_start;
    char ** new_argv;
    char * old_argv;
    int n;

    old_argv = Xstrdup(argv, __FILE__, __LINE__);
    n = strlen(argv);

    /*
     * Trim beginning whitespace
     */
    argv_start = 0;
    while (q_isspace(argv[argv_start])) {
        argv_start++;
    }

    for (i = 0, j = argv_start; j < n; i++) {
        while ((j < n) && !q_isspace(argv[j])) {
            j++;
        }
        while ((j < n) && q_isspace(argv[j])) {
            j++;
        }
    }

    /*
     * i is the number of tokens.
     */
    new_argv = (char **) Xmalloc(sizeof(char *) * (i + 1), __FILE__, __LINE__);
    for (i = 0, j = argv_start; j < n; i++) {
        new_argv[i] = old_argv + j;
        while ((j < n) && !q_isspace(old_argv[j])) {
            j++;
        }
        while ((j < n) && q_isspace(old_argv[j])) {
            old_argv[j] = 0;
            j++;
        }
    }

    /*
     * Why can't I free this?  Because new_argv is pointers into old_argv.
     * Freeing it kills the child process.
     */
    /*
     * Xfree(old_argv, __FILE__, __LINE__);
     */

    new_argv[i] = NULL;
    return new_argv;
}

/**
 * Construct the appropriate command line for a particular phonebook entry.
 *
 * @param target the phonebook entry to connect to
 * @return the command line
 */
static char * connect_command(const struct q_phone_struct * target) {
    char connect_string[COMMAND_LINE_SIZE];
    char * substituted_string;

    switch (target->method) {

    case Q_DIAL_METHOD_SHELL:
        snprintf(connect_string, sizeof(connect_string), "%s",
                 get_option(Q_OPTION_SHELL));
        break;
    case Q_DIAL_METHOD_SSH:
        if ((q_status.current_username != NULL)
            && (wcslen(q_status.current_username) > 0)) {
            snprintf(connect_string, sizeof(connect_string), "%s",
                     get_option(Q_OPTION_SSH_USER));
        } else {
            snprintf(connect_string, sizeof(connect_string), "%s",
                     get_option(Q_OPTION_SSH));
        }
        break;
    case Q_DIAL_METHOD_RLOGIN:
        if ((q_status.current_username != NULL)
            && (wcslen(q_status.current_username) > 0)) {
            snprintf(connect_string, sizeof(connect_string), "%s",
                     get_option(Q_OPTION_RLOGIN_USER));
        } else {
            snprintf(connect_string, sizeof(connect_string), "%s",
                     get_option(Q_OPTION_RLOGIN));
        }
        break;
    case Q_DIAL_METHOD_TELNET:
        snprintf(connect_string, sizeof(connect_string), "%s",
                 get_option(Q_OPTION_TELNET));
        break;
    case Q_DIAL_METHOD_COMMANDLINE:
        snprintf(connect_string, sizeof(connect_string), "%s", target->address);
        break;

#ifndef Q_NO_SERIAL
    case Q_DIAL_METHOD_MODEM:
#endif
    case Q_DIAL_METHOD_SOCKET:
        /*
         * NOP
         */
        return NULL;
    }

    /*
     * Substitute for $USERNAME.
     */
    if (q_status.current_username != NULL) {
        substituted_string =
            substitute_wcs_half(connect_string, "$USERNAME",
                                q_status.current_username);
        strncpy(connect_string, substituted_string, sizeof(connect_string));
        Xfree(substituted_string, __FILE__, __LINE__);
    }

    /*
     * Substitute for $REMOTEHOST.
     */
    if (q_status.remote_address != NULL) {
        substituted_string =
            substitute_string(connect_string, "$REMOTEHOST",
                              q_status.remote_address);
        strncpy(connect_string, substituted_string, sizeof(connect_string));
        Xfree(substituted_string, __FILE__, __LINE__);
    }

    /*
     * Substitute for $REMOTEPORT.
     */
    if (q_status.remote_port != NULL) {
        substituted_string =
            substitute_string(connect_string, "$REMOTEPORT",
                              q_status.remote_port);
        strncpy(connect_string, substituted_string, sizeof(connect_string));
        Xfree(substituted_string, __FILE__, __LINE__);
    }

    return Xstrdup(connect_string, __FILE__, __LINE__);
}

/**
 * Spawn a sub-process.  NOTE: ANY CHANGES TO BEHAVIOR HERE MUST BE CHECKED
 * IN script_start() ALSO!!!
 *
 * @param command_line the command line.  Note that it will be free()'d in
 * this function.
 * @param emulation Q_EMUL_TTY, Q_EMUL_ANSI, etc.
 */
static void spawn_process(char * command_line, Q_EMULATION emulation) {

#ifdef Q_PDCURSES_WIN32
    /*
     * Modeled after example on MSDN:
     * http://msdn.microsoft.com/en-us/library/ms682499%28v=VS.85%29.aspx
     */
    PROCESS_INFORMATION process_info;
    STARTUPINFOA startup_info;
    HANDLE q_child_stdin_2 = NULL;
    HANDLE q_child_stdout_2 = NULL;
    SECURITY_ATTRIBUTES security_attr;
    char buffer[32];
    int columns;

    security_attr.nLength = sizeof(SECURITY_ATTRIBUTES);
    security_attr.bInheritHandle = TRUE;
    security_attr.lpSecurityDescriptor = NULL;

    /*
     * Create pipes as needed to communicate with child process.
     */
    if (!CreatePipe(&q_child_stdout, &q_child_stdout_2, &security_attr, 0)) {
#ifdef DEBUG_SPAWNPROCESS
        fprintf(stderr, "CreatePipe() 1 failed: %d %s\n", GetLastError(),
                strerror(GetLastError()));
        fflush(stderr);
#endif
        /*
         * Error, bail out.
         */
        return;
    }
    if (!SetHandleInformation(q_child_stdout, HANDLE_FLAG_INHERIT, 0)) {
        /*
         * Error, bail out.
         */
        CloseHandle(q_child_stdout);
        CloseHandle(q_child_stdout_2);
        q_child_stdout = NULL;
        q_child_stdout_2 = NULL;
#ifdef DEBUG_SPAWNPROCESS
        fprintf(stderr, "SetHandleInformation() 1 failed: %d %s\n",
                GetLastError(), strerror(GetLastError()));
        fflush(stderr);
#endif
        return;
    }
    if (!CreatePipe(&q_child_stdin_2, &q_child_stdin, &security_attr, 0)) {
#ifdef DEBUG_SPAWNPROCESS
        fprintf(stderr, "CreatePipe() 2 failed: %d %s\n",
                GetLastError(), strerror(GetLastError()));
        fflush(stderr);
#endif
        /*
         * Error, bail out.
         */
        return;
    }
    if (!SetHandleInformation(q_child_stdin, HANDLE_FLAG_INHERIT, 0)) {
        /*
         * Error, bail out.
         */
        CloseHandle(q_child_stdout);
        CloseHandle(q_child_stdout_2);
        q_child_stdout = NULL;
        q_child_stdout_2 = NULL;
        CloseHandle(q_child_stdin);
        CloseHandle(q_child_stdin_2);
        q_child_stdin = NULL;
        q_child_stdin_2 = NULL;
#ifdef DEBUG_SPAWNPROCESS
        fprintf(stderr, "SetHandleInformation() 2 failed: %d %s\n",
                GetLastError(), strerror(GetLastError()));
        fflush(stderr);
#endif
        return;
    }

    /*
     * Set TERM, LINES, COLUMNS, and LANG.
     */
    if (strlen(emulation_term(q_status.emulation)) > 0) {
        SetEnvironmentVariableA("TERM", emulation_term(q_status.emulation));
    }
    snprintf(buffer, sizeof(buffer), "%u", HEIGHT - STATUS_HEIGHT);
    SetEnvironmentVariableA("LINES", buffer);

    switch (emulation) {
    case Q_EMUL_ANSI:
    case Q_EMUL_AVATAR:
    case Q_EMUL_TTY:
        /*
         * BBS-ish emulations:  check the assume_80_columns flag.
         */
        if (q_status.assume_80_columns == Q_TRUE) {
            columns = 80;
        } else {
            columns = WIDTH;
        }
        break;
    case Q_EMUL_PETSCII:
    case Q_EMUL_ATASCII:
        /*
         * PETSCII and ATASCII are always 40 columns.
         */
        columns = 40;
        break;
    default:
        columns = WIDTH;
        break;
    }
    snprintf(buffer, sizeof(buffer), "%u", columns);
    SetEnvironmentVariableA("COLUMNS", buffer);

    SetEnvironmentVariableA("LANG", emulation_lang(q_status.emulation));

    /*
     * Create child process itself.
     */
    memset(&process_info, 0, sizeof(PROCESS_INFORMATION));
    memset(&startup_info, 0, sizeof(STARTUPINFO));
    startup_info.cb = sizeof(STARTUPINFO);
    startup_info.hStdInput = q_child_stdin_2;
    startup_info.hStdOutput = q_child_stdout_2;
    startup_info.hStdError = q_child_stdout_2;
    startup_info.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup_info.wShowWindow = SW_HIDE;
    if (!CreateProcessA(NULL,   /* Use command line */
                        command_line,   /* Command line */
                        NULL,   /*
                                 * No inherited security attributes for
                                 * process
                                 */
                        NULL,   /*
                                 * No inherited security attributes for
                                 * thread
                                 */
                        TRUE,   /* Inherit handles */
                        0,      /* No special creation flags */
                        NULL,   /* Inherit environment block */
                        NULL,   /* Inherit starting directory */
                        &startup_info,  /* STARTUPINFO to fill */
                        &process_info   /* PROCESS_INFORMATION to fill */
        )) {
        /*
         * Error, bail out.
         */
#ifdef DEBUG_SPAWNPROCESS
        fprintf(stderr, "CreateProcess() failed: %d %s\n", GetLastError(),
                strerror(GetLastError()));
        fflush(stderr);
#endif
        CloseHandle(process_info.hProcess);
        CloseHandle(process_info.hThread);
        CloseHandle(q_child_stdout);
        CloseHandle(q_child_stdout_2);
        q_child_stdout = NULL;
        q_child_stdout_2 = NULL;
        CloseHandle(q_child_stdin);
        CloseHandle(q_child_stdin_2);
        q_child_stdin = NULL;
        q_child_stdin_2 = NULL;
        return;
    }
    q_child_process = process_info.hProcess;
    q_child_thread = process_info.hThread;

    /*
     * The child has these, not us, so close them.
     */
    CloseHandle(q_child_stdin_2);
    CloseHandle(q_child_stdout_2);

    /*
     * At this point, we should have a running child process that writes to
     * the other ends of q_child_stdout and q_child_stderr, and reads from
     * the other end of q_child_stdin.  We have to use ReadFile() and
     * WriteFile() on our end of these handles.
     */
#ifdef DEBUG_SPAWNPROCESS
    fprintf(stderr, "spawn_process() OK: PID %d TID %d\n",
            process_info.dwProcessId, process_info.dwThreadId);
    fflush(stderr);
#endif
    return;

#else
    /*
     * POSIX case: fork and execute
     */

    /*
     * Assume the child has not yet exited.
     */
    q_child_exited = Q_FALSE;

    /*
     * Fork and put the child on a new tty
     */
    char ** target_argv;
    char ttyname_buffer[FILENAME_SIZE];
    pid_t child_pid = forkpty(&q_child_tty_fd, ttyname_buffer, NULL, NULL);
    q_child_ttyname = Xstrdup(ttyname_buffer, __FILE__, __LINE__);

    if (child_pid == 0) {
        /*
         * Child process, will become the connection program.
         */

        struct q_scrolline_struct * line;
        struct q_scrolline_struct * line_next;

        /*
         * This just has to be long enough for LINES=blah and COLUMNS=blah
         */
        char buffer[32];
        int columns;

        /*
         * Restore signal handlers
         */
        signal(SIGPIPE, SIG_DFL);

        /*
         * Free scrollback memory
         */
        line = q_scrollback_buffer;
        while (line != NULL) {
            line_next = line->next;
            Xfree(line, __FILE__, __LINE__);
            line = line_next;
        }

        /*
         * Set TERM, LANG, LINES, and COLUMNS.
         */
        if (strlen(emulation_term(q_status.emulation)) == 0) {
            unsetenv("TERM");
        } else {
            setenv("TERM", emulation_term(q_status.emulation), 1);
        }
        snprintf(buffer, sizeof(buffer), "LANG=%s",
                 emulation_lang(q_status.emulation));
        putenv(strdup(buffer));

        memset(buffer, 0, sizeof(buffer));
        snprintf(buffer, sizeof(buffer), "LINES=%u", HEIGHT - STATUS_HEIGHT);
        putenv(strdup(buffer));
        memset(buffer, 0, sizeof(buffer));

        switch (emulation) {
        case Q_EMUL_ANSI:
        case Q_EMUL_AVATAR:
        case Q_EMUL_TTY:
            /*
             * BBS-ish emulations:  check the assume_80_columns flag.
             */
            if (q_status.assume_80_columns == Q_TRUE) {
                columns = 80;
            } else {
                columns = WIDTH;
            }
            break;
        case Q_EMUL_PETSCII:
        case Q_EMUL_ATASCII:
            /*
             * PETSCII and ATASCII are always 40 columns.
             */
            columns = 40;
            break;
        default:
            columns = WIDTH;
            break;
        }

        snprintf(buffer, sizeof(buffer), "COLUMNS=%u", columns);
        putenv(strdup(buffer));

        /*
         * Set the TTY cols and rows.  This handles the case for those
         * programs that don't propogate LINES and COLUMNS.
         *
         * I use perror() here because it will make its way back to the
         * parent (I hope).  This child process doesn't have control of the
         * terminal anymore so I can't use ncurses functions.
         */
        struct winsize console_size;
        if (ioctl(STDIN_FILENO, TIOCGWINSZ, &console_size) < 0) {
            perror("ioctl(TIOCGWINSZ)");
        } else {
            console_size.ws_row = HEIGHT - STATUS_HEIGHT;
            console_size.ws_col = columns;
            if (ioctl(STDIN_FILENO, TIOCSWINSZ, &console_size) < 0) {
                perror("ioctl(TIOCSWINSZ)");
            }
        }

        /*
         * Separate target into arguments.
         */
        target_argv = tokenize_command(command_line);
        Xfree(command_line, __FILE__, __LINE__);
        execvp(target_argv[0], target_argv);
        /*
         * If we got here, then we failed to spawn.  Emit the error message
         * and crap out.
         */
        perror("execvp()");
        exit(-1);
    } else {

        /*
         * Free leak
         */
        Xfree(command_line, __FILE__, __LINE__);

    } /* if (child_pid == 0) */

    if (q_child_tty_fd != -1) {
        q_child_pid = child_pid;
    }
#endif /* Q_PDCURSES_WIN32 */

}

#ifdef Q_PDCURSES_WIN32

/**
 * Set a file descriptor or Winsock socket handle to non-blocking mode.
 *
 * @param fd the descriptor
 */
void set_nonblock(const int fd) {
    u_long non_block_mode = 1;

    if ((net_is_connected() == Q_FALSE) &&
        (net_connect_pending == Q_FALSE) &&
        (net_is_listening == Q_FALSE)
    ) {
        /*
         * Do nothing for a not-socket case.
         */
        return;
    }

    ioctlsocket(fd, FIONBIO, &non_block_mode);
}

/**
 * Set a file descriptor or Winsock socket handle to blocking mode.
 *
 * @param fd the descriptor
 */
void set_blocking(const int fd) {
    u_long non_block_mode = 0;

    /*
     * The only code that calls this is ssh, which will always be a socket.
     */

    /*
     * Clear the event object BEFORE switching the mode, as per the
     * documentation for ioctlsocket().
     */
    WSAEventSelect(fd, NULL, 0);
    ioctlsocket(fd, FIONBIO, &non_block_mode);
}

#else

/**
 * Set a file descriptor or Winsock socket handle to non-blocking mode.
 *
 * @param fd the descriptor
 */
void set_nonblock(const int fd) {
    int flags;

    flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/**
 * Set a file descriptor or Winsock socket handle to blocking mode.
 *
 * @param fd the descriptor
 */
void set_blocking(const int fd) {
    int flags;

    flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
}

/**
 * Set a tty into raw mode.
 *
 * @param tty_fd the tty descriptor
 */
void set_raw_termios(const int tty_fd) {
    struct termios old_termios;
    struct termios new_termios;

    if (tcgetattr(tty_fd, &old_termios) < 0) {
        /*
         * Error, bail out.
         */
        return;
    }
    memcpy(&new_termios, &old_termios, sizeof(struct termios));
    cfmakeraw(&new_termios);
    if (tcsetattr(tty_fd, TCSANOW, &new_termios) < 0) {
        /*
         * Error, bail out.
         */
        return;
    }

    /*
     * All OK
     */
    return;
}

#endif /* Q_PDCURSES_WIN32 */

/**
 * Set the variables used by bottom dialer section of the phonebook screen.
 */
static void setup_dial_screen() {
    time(&q_dialer_cycle_start_time);
    q_dialer_cycle_time = atoi(get_option(Q_OPTION_DIAL_CONNECT_TIME));
    q_dialer_attempts++;
    q_dial_state = Q_DIAL_DIALING;

    /*
     * Set the message
     */
    sprintf(q_dialer_status_message, _("%-3d Seconds remain until Cycle"),
            (int) q_dialer_cycle_time);

    switch_state(Q_STATE_DIALER);
    refresh_handler();
}

/**
 * See if a dialup script was specified, and if so execute it.
 */
void check_for_dialup_script() {
    /*
     * Execute script if supplied.
     */
    if (q_scrfile != NULL) {
        if (file_exists(get_scriptdir_filename(q_scrfile)) == Q_TRUE) {
            if (q_status.quicklearn == Q_FALSE) {
                /*
                 * Execute script if supplied
                 */
                if (q_status.read_only == Q_FALSE) {
                    script_start(q_scrfile);
                }
            }
        }
        Xfree(q_scrfile, __FILE__, __LINE__);
        q_scrfile = NULL;
    } else {
        if (q_current_dial_entry->script_filename != NULL) {
            if ((strlen(q_current_dial_entry->script_filename) > 0) &&
                (file_exists(get_scriptdir_filename(
                    q_current_dial_entry->script_filename)) == Q_TRUE)
            ) {
                if ((q_status.quicklearn == Q_FALSE) &&
                    (q_current_dial_entry->quicklearn == Q_FALSE)
                ) {
                    /*
                     * We're not quicklearning, start the script
                     */
                    if (q_status.read_only == Q_FALSE) {
                        script_start(
                        q_current_dial_entry->script_filename);
                    }
                }
            }
        }
    }
}

/**
 * Called upon the completion of a successful connection.
 */
void dial_success() {
    q_dial_state = Q_DIAL_CONNECTED;
    time(&q_dialer_cycle_start_time);
    time(&q_status.connect_time);
    q_status.online = Q_TRUE;

    /*
     * Switch to doorway mode if requested.
     */
    if (q_current_dial_entry->doorway == Q_DOORWAY_CONFIG) {
        if (strcasecmp(get_option(Q_OPTION_CONNECT_DOORWAY), "doorway") == 0) {
            q_status.doorway_mode = Q_DOORWAY_MODE_FULL;
        }
        if (strcasecmp(get_option(Q_OPTION_CONNECT_DOORWAY), "mixed") == 0) {
            q_status.doorway_mode = Q_DOORWAY_MODE_MIXED;
        }
    } else if (q_current_dial_entry->doorway == Q_DOORWAY_ALWAYS_DOORWAY) {
        q_status.doorway_mode = Q_DOORWAY_MODE_FULL;
    } else if (q_current_dial_entry->doorway == Q_DOORWAY_ALWAYS_MIXED) {
        q_status.doorway_mode = Q_DOORWAY_MODE_MIXED;
    } else if (q_current_dial_entry->doorway == Q_DOORWAY_NEVER) {
        q_status.doorway_mode = Q_DOORWAY_MODE_OFF;
    }

    /*
     * Set the non-default phonebook entry toggles.
     */
    if (q_current_dial_entry->use_default_toggles == Q_FALSE) {
        set_dial_out_toggles(q_current_dial_entry->toggles);
    }

    /*
     * Untag it in the phonebook on the assumption we connected.
     */
    if (q_current_dial_entry->tagged == Q_TRUE) {
        q_current_dial_entry->tagged = Q_FALSE;
        q_phonebook.tagged--;
    }

#ifndef Q_NO_SERIAL
    if (q_current_dial_entry->method != Q_DIAL_METHOD_MODEM) {
#endif
        /*
         * Log the connection.
         */
        qlog(_("CONNECTION ESTABLISHED: %ls\n"), q_current_dial_entry->name);

        /*
         * Play the connection sequence.
         */
        if (q_status.beeps == Q_TRUE) {
            play_sequence(Q_MUSIC_CONNECT);
        }

        if (net_connect_pending() == Q_FALSE) {

            /*
             * Non-network connections: go immediately to console.
             */
            switch_state(Q_STATE_CONSOLE);

            /*
             * Execute script if supplied.
             */
            check_for_dialup_script();
            screen_flush();
        }
#ifndef Q_NO_SERIAL
    }
#endif

}

/**
 * Connect to a remote system.
 *
 * @param number the phonebook entry to connect to
 */
void dial_out(struct q_phone_struct * target) {
    char * command;
    Q_BOOL do_network_connect = Q_FALSE;

    /*
     * Assert that the connection does not already exist.
     */
    assert(q_child_tty_fd == -1);

    /*
     * Reset clock for keepalive/idle timeouts.
     */
    time(&q_data_sent_time);

    /*
     * Reset the terminal emulation state.
     */
    q_status.emulation = target->emulation;
    q_status.codepage = target->codepage;
    reset_emulation();

#ifndef Q_NO_SERIAL
    if (target->method == Q_DIAL_METHOD_MODEM) {
        close_function = NULL;
        /*
         * Modem dialup: just open the serial port and switch to
         * Q_STATE_DIALER.
         */
        qlog(_("Dialing %ls (%s)...\n"), target->name, target->address);
        if (open_serial_port() == Q_TRUE) {
            /*
             * Set the modem setting based on the target settings.
             */
            if (target->use_modem_cfg == Q_TRUE) {
                q_serial_port.rtscts = q_modem_config.rtscts;
                q_serial_port.xonxoff = q_modem_config.xonxoff;
                q_serial_port.baud = q_modem_config.default_baud;
                q_serial_port.data_bits = q_modem_config.default_data_bits;
                q_serial_port.stop_bits = q_modem_config.default_stop_bits;
                q_serial_port.parity = q_modem_config.default_parity;
                q_serial_port.lock_dte_baud = q_modem_config.lock_dte_baud;
            } else {
                q_serial_port.rtscts = target->rtscts;
                q_serial_port.xonxoff = target->xonxoff;
                q_serial_port.baud = target->baud;
                q_serial_port.data_bits = target->data_bits;
                q_serial_port.stop_bits = target->stop_bits;
                q_serial_port.parity = target->parity;
                q_serial_port.lock_dte_baud = target->lock_dte_baud;
            }
            configure_serial_port();

            setup_dial_screen();
        }

    }
#endif /* Q_NO_SERIAL */

    if ((target->method == Q_DIAL_METHOD_TELNET) &&
        (q_status.external_telnet == Q_FALSE)) {

        /*
         * Telnet, requesting internal code.
         */
        do_network_connect = Q_TRUE;
        setup_dial_screen();
        q_child_tty_fd = net_connect_start(target->address, target->port);
        close_function = close_network_connection;
    } else if ((target->method == Q_DIAL_METHOD_RLOGIN) &&
               (q_status.external_rlogin == Q_FALSE)) {
        /*
         * Rlogin, requesting internal code.  Rlogin is always connected to
         * port 513.
         */
        do_network_connect = Q_TRUE;
        setup_dial_screen();
        q_child_tty_fd = net_connect_start(target->address, "513");
        close_function = close_network_connection;
    } else if (target->method == Q_DIAL_METHOD_SOCKET) {
        /*
         * Socket, requesting internal code.
         */
        do_network_connect = Q_TRUE;
        setup_dial_screen();
        q_child_tty_fd = net_connect_start(target->address, target->port);
        close_function = close_network_connection;
#ifdef Q_SSH_CRYPTLIB
    } else if ((target->method == Q_DIAL_METHOD_SSH) &&
               (q_status.external_ssh == Q_FALSE)) {
        /*
         * SSH, requesting internal code.
         */
        do_network_connect = Q_TRUE;
        setup_dial_screen();
        q_child_tty_fd = net_connect_start(target->address, target->port);
        close_function = close_network_connection;
#endif

#ifdef Q_NO_SERIAL
    } else {
#else
    } else if (target->method != Q_DIAL_METHOD_MODEM) {
#endif

        /*
         * Other connection methods: fork and execute.
         */
        close_function = close_shell_connection;

        /*
         * Push all the data out to clear the soon-to-be child process's
         * output buffer.
         */
        screen_flush();

        command = connect_command(target);

        /*
         * Log the command line.  This should be in the child branch but
         * putting it there corrupts the session log output (two file handles
         * pointing to the same file).
         */
        qlog(_("[child] Connecting with command line '%s'...\n"), command);

        /*
         * Spawn and go.
         */
        spawn_process(command, target->emulation);

    } /* if (target->method != Q_DIAL_METHOD_MODEM) */

    /*
     * At this point, q_child_tty_fd is set to something, either through the
     * forkpty() in spawn_process() (and this is where the parent process
     * continues on), or by way of an open serial port, or through the
     * network connect() call.
     */

    /*
     * Reset the bytes connection bytes counter.
     */
    q_connection_bytes_received = 0;

#ifdef Q_PDCURSES_WIN32
    if (((q_child_tty_fd != -1) && (do_network_connect == Q_FALSE))
        || (q_child_stdout != NULL)
    ) {
#else
    if ((q_child_tty_fd != -1) && (do_network_connect == Q_FALSE)) {
#endif

        /*
         * This was a serial port, modem dial, or spawned process.  The
         * connection is already open and ready to go.  We need to make it
         * non-blocking.
         */
        set_nonblock(q_child_tty_fd);

#ifndef Q_NO_SERIAL
        if (target->method != Q_DIAL_METHOD_MODEM) {
#endif
            /*
             * Non-modem connections can call dial_success() immediately.
             */
            dial_success();

#ifndef Q_NO_SERIAL
        }
#endif
    }
}
