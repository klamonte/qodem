/*
 * script.c
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
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>

#ifdef Q_PDCURSES_WIN32
#  include <windows.h>
#else
#  include <signal.h>
#  include <sys/ioctl.h>
#  include <sys/poll.h>
#  include <sys/wait.h>
#  include <unistd.h>
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
#endif /* Q_PDCURSES_WIN32 */

#include <ctype.h>
#include "screen.h"
#include "qodem.h"
#include "options.h"
#include "states.h"
#include "dialer.h"
#include "console.h"
#include "forms.h"
#include "translate.h"
#include "keyboard.h"
#include "script.h"

/* Set this to a not-NULL value to enable debug log. */
/* static const char * DLOGNAME = "script"; */
static const char * DLOGNAME = NULL;

/**
 * Status of the running script.
 */
Q_SCRIPT q_running_script;

/*
 * Buffer of UTF-8 encoded printable characters to send to the script's
 * stdin.  This is space for at least 128 Unicode code points that could each
 * take up to 4 bytes to encode.
 */
static char print_buffer[128 * 4];
static int print_buffer_n;

#ifdef Q_PDCURSES_WIN32

/*
 * Win32 case: we have to create pipes that are connected to the script
 * process' streams.  These are initialized in spawn_process().
 */
HANDLE q_script_stdin = NULL;
HANDLE q_script_stdout = NULL;
HANDLE q_script_stderr = NULL;
HANDLE q_script_process = NULL;
HANDLE q_script_thread = NULL;

#endif

/*
 * Time when the script process was spawned.
 */
static time_t script_start_time;

/*
 * Script stdout buffer.
 */
static unsigned char stdout_buffer[Q_BUFFER_SIZE];
static int stdout_buffer_n = 0;

/*
 * Script stderr buffer, both in the raw byte stream form and in the decoded
 * wide char form.
 */
static unsigned char stderr_buffer[Q_BUFFER_SIZE];
static int stderr_buffer_n = 0;
static wchar_t stderr_utf8_buffer[Q_BUFFER_SIZE];
static int stderr_utf8_buffer_n = 0;

static struct q_scrolline_struct * stderr_lines = NULL;
static struct q_scrolline_struct * stderr_last = NULL;

/* UTF-8 decoder support */
uint32_t stdout_utf8_state;
uint32_t stderr_utf8_state;

/* The final return code retrieved when the script exited. */
int script_rc = -1;

/**
 * Figure out the appropriate full and empty print buffer state exposed to
 * the global script status.
 */
static void update_print_buffer_flags() {
    if (print_buffer_n >= sizeof(print_buffer) - 4) {
        /*
         * No more room for another character, we are full
         */
        q_running_script.print_buffer_full = Q_TRUE;
    } else {
        q_running_script.print_buffer_full = Q_FALSE;
    }
    if (print_buffer_n == 0) {
        q_running_script.print_buffer_empty = Q_TRUE;
    } else {
        q_running_script.print_buffer_empty = Q_FALSE;
    }
}

/**
 * Called by print_character() in scrollback.c to pass printable characters
 * to the running script's stdin.
 *
 * @param ch the character
 */
void script_print_character(const wchar_t ch) {
    int rc;

    if (q_running_script.paused == Q_TRUE) {
        /*
         * Drop characters when the script is paused.
         */
        return;
    }

    if (q_running_script.running == Q_FALSE) {
        /*
         * Drop characters when the script is dead.
         */
        print_buffer_n = 0;
        update_print_buffer_flags();
        return;
    }

    if (q_running_script.print_buffer_full == Q_TRUE) {
        /*
         * Drop characters when the print buffer is full.
         */
        return;
    }

    /*
     * Encode the character to UTF-8.
     */
    rc = utf8_encode(ch, &print_buffer[print_buffer_n]);
    print_buffer_n += rc;

    /*
     * Fix the full/empty flags
     */
    update_print_buffer_flags();
}

/**
 * Send a script message (a line from its stderr) to the log file.
 *
 * @param line the message from the script
 */
static void log_line(struct q_scrolline_struct * line) {
    char buffer[Q_MAX_LINE_LENGTH + 1];
    int n;

    /*
     * Terminate wcs string
     */
    if (stderr_last->length == Q_MAX_LINE_LENGTH) {
        stderr_last->length--;
    }
    stderr_last->chars[stderr_last->length] = 0;

    n = wcstombs(NULL, stderr_last->chars, 0) + 1;
    wcstombs(buffer, stderr_last->chars, n);

    qlog(_("Script message: %s\n"), buffer);
}

/**
 * Record whatever the script emitted to its stderr to our variable
 * stderr_lines.
 */
static void print_stderr() {
    struct q_scrolline_struct * new_line;
    int i, j;

    if (stderr_lines == NULL) {
        /*
         * Allocate first line
         */
        new_line =
            (struct q_scrolline_struct *)
            Xmalloc(sizeof(struct q_scrolline_struct), __FILE__, __LINE__);
        memset(new_line, 0, sizeof(struct q_scrolline_struct));
        for (i = 0; i < Q_MAX_LINE_LENGTH; i++) {
            new_line->chars[i] = ' ';
        }
        new_line->prev = NULL;
        new_line->next = NULL;
        stderr_lines = new_line;
        stderr_last = stderr_lines;
    }

    for (i = 0; i < stderr_utf8_buffer_n; i++) {
        if (((stderr_utf8_buffer[i] == '\r') ||
             (stderr_utf8_buffer[i] == '\n') ||
             (stderr_last->length == WIDTH)) && (stderr_last->length > 0)
            ) {
            /*
             * New line
             */
            new_line =
                (struct q_scrolline_struct *)
                Xmalloc(sizeof(struct q_scrolline_struct), __FILE__, __LINE__);
            memset(new_line, 0, sizeof(struct q_scrolline_struct));
            for (j = 0; j < Q_MAX_LINE_LENGTH; j++) {
                new_line->chars[j] = ' ';
            }
            new_line->prev = stderr_last;
            new_line->next = NULL;
            new_line->length = 0;
            stderr_last->next = new_line;
            log_line(stderr_last);
            stderr_last = new_line;
            /*
             * Fall through...
             */
        }
        if ((stderr_utf8_buffer[i] != '\r') &&
            (stderr_utf8_buffer[i] != '\n')) {

            stderr_last->chars[stderr_last->length] = stderr_utf8_buffer[i];
            stderr_last->length++;
        }
    }
    /*
     * Clear pending data
     */
    stderr_utf8_buffer_n = 0;

    /*
     * Refresh
     */
    q_screen_dirty = Q_TRUE;
}

/**
 * Process raw bytes from the remote side through the script.  See also
 * console_process_incoming_data().
 *
 * @param input the bytes from the remote side
 * @param input_n the number of bytes in input_n
 * @param remaining the number of un-processed bytes that should be sent
 * through a future invocation of script_process_data()
 * @param output a buffer to contain the bytes to send to the remote side
 * @param output_n the number of bytes that this function wrote to output
 * @param output_max the maximum number of bytes this function may write to
 * output
 */
void script_process_data(unsigned char * input, const unsigned int input_n,
                         int * remaining, unsigned char * output,
                         unsigned int * output_n,
                         const unsigned int output_max) {

    char notify_message[DIALOG_MESSAGE_SIZE];
    int rc = 0;
    int n;
    int i;
    uint32_t utf8_char;
    uint32_t last_utf8_state;
#ifdef Q_PDCURSES_WIN32
    DWORD actual_bytes = 0;
    DWORD bytes_read = 0;
#else
    struct pollfd pfd;
#endif
    Q_BOOL check_stderr = Q_FALSE;

    DLOG(("script.c: buffer_full %s buffer_empty %s running %s paused %s\n",
         (q_running_script.print_buffer_full == Q_TRUE ? "true" : "false"),
         (q_running_script.print_buffer_empty == Q_TRUE ? "true" : "false"),
         (q_running_script.running == Q_TRUE ? "true" : "false"),
         (q_running_script.paused == Q_TRUE ? "true" : "false")));

    DLOG(("script.c: stdin %s stdout %s buffer_n %d\n",
         (q_running_script.stdin_writeable == Q_TRUE ? "true" : "false"),
         (q_running_script.stdout_readable == Q_TRUE ? "true" : "false"),
         print_buffer_n));

    /*
     * -----------------------------------------------------------------
     * Dispatch things in print_buffer to script stdin
     * -----------------------------------------------------------------
     */
    if (q_running_script.stdin_writeable == Q_TRUE) {

        /*
         * Write the data to q_running_script.script_tty_fd
         */
#ifdef Q_PDCURSES_WIN32
        if (print_buffer_n > 0) {
            DWORD bytes_written = 0;
            if (WriteFile(q_script_stdin, print_buffer,
                          print_buffer_n, &bytes_written, NULL) == TRUE) {
                rc = bytes_written;

                /*
                 * Force this sucker to flush
                 */
                FlushFileBuffers(q_script_stdin);
            } else {
                errno = GetLastError();
                rc = -1;
            }
        } else {
            /*
             * NOP, pretend it's EAGAIN
             */
            errno = EAGAIN;
            rc = -1;
        }
#else
        rc = write(q_running_script.script_tty_fd, print_buffer,
                   print_buffer_n);
#endif

        if (rc < 0) {
            switch (errno) {

#ifdef Q_PDCURSES_WIN32
            case ERROR_NO_DATA:
                /*
                 * Other side is closing, give up here and the stdout read
                 * should return EOF.
                 */
                break;
#endif
            case EAGAIN:
                /*
                 * Outgoing buffer is full, wait for the next round.
                 */
                break;
            default:
                /*
                 * Uh-oh, error
                 */
                snprintf(notify_message, sizeof(notify_message),
                         _("Call to write() failed: %d %s"), errno,
                         strerror(errno));
                notify_form(notify_message, 0);
                return;
            }
        } else {
            /*
             * Hang onto the difference for the next round.
             */
            assert(rc <= print_buffer_n);

            if (rc < print_buffer_n) {
                memmove(print_buffer, print_buffer + rc, print_buffer_n - rc);
            }
            print_buffer_n -= rc;
            update_print_buffer_flags();
        }
    }

    /*
     * -----------------------------------------------------------------
     * Read bytes from remote side, and pass through
     * console_process_incoming_data()
     * -----------------------------------------------------------------
     */
    if ((input_n > 0) &&
        (((q_running_script.print_buffer_full == Q_FALSE) &&
          (q_running_script.running == Q_TRUE) &&
          (q_running_script.paused == Q_FALSE)) ||
         (q_running_script.running == Q_FALSE) ||
         (q_running_script.paused == Q_TRUE))
    ) {
        console_process_incoming_data(input, input_n, remaining);
    }

    /*
     * -----------------------------------------------------------------
     * Read bytes from script stderr, decode from UTF-8, and display on
     * screen.
     *
     * Since stderr is a named pipe, poll it explicitly.  select() and named
     * pipes are broken on some platforms.
     * -----------------------------------------------------------------
     */
#ifdef Q_PDCURSES_WIN32

    if (PeekNamedPipe(q_script_stderr, NULL, 0, NULL, &actual_bytes,
                      NULL) == 0) {
        /*
         * Error peeking.  It's either EOF or something else.  Ignore it, the
         * stdout check will catch the EOF.
         */
        check_stderr = Q_FALSE;
    } else {
        if (actual_bytes > 0) {
            /*
             * Data is available for reading
             */
            check_stderr = Q_TRUE;
        } else {
            /*
             * Do not access stderr
             */
            check_stderr = Q_FALSE;
        }
    }

#else

    if (q_running_script.script_stderr_fd != -1) {
        pfd.fd = q_running_script.script_stderr_fd;
        pfd.events = POLLIN;
        rc = poll(&pfd, 1, 0);
        if (rc > 0) {
            /*
             * Data is available for reading
             */
            check_stderr = Q_TRUE;
        } else {
            /*
             * Do not access stderr
             */
            check_stderr = Q_FALSE;
        }
    } else {
        check_stderr = Q_FALSE;
    }

#endif /* Q_PDCURSES_WIN32 */

    if (check_stderr == Q_TRUE) {
        n = Q_BUFFER_SIZE - stderr_buffer_n;
        /*
         * Make sure at least 4 bytes are available for 1 UTF-8 character
         */
        if ((n > 0) && (output_max - *output_n > 4)) {
            /*
             * Clear errno
             */
            errno = 0;

#ifdef Q_PDCURSES_WIN32

            if (PeekNamedPipe(q_script_stderr, NULL, 0, NULL, &actual_bytes,
                              NULL) == 0) {

                /*
                 * Error peeking
                 */
                rc = 0;
            } else {
                if (actual_bytes == 0) {
                    rc = 0;
                } else if (actual_bytes > n) {
                    actual_bytes = n;
                }
                if (ReadFile(q_script_stderr,
                             stderr_buffer + stderr_buffer_n,
                             actual_bytes, &bytes_read, NULL) == TRUE) {
                    rc = bytes_read;
                } else {
                    /*
                     * Error in read
                     */
                    rc = 0;
                }
            }
#else

            rc = read(q_running_script.script_stderr_fd,
                      stderr_buffer + stderr_buffer_n, n);

#endif /* Q_PDCURSES_WIN32 */

            if (rc < 0) {
                if (errno == EIO) {
                    /*
                     * This is EOF
                     */
                    rc = 0;
                } else {
                    qlog(_("Script stderr read() failed: %s\n"),
                         strerror(errno));
                }
            }
            if (rc == 0) {
                /*
                 * EOF - close stderr.  Only the stdout EOF officially kills
                 * the script.
                 */
#ifdef Q_PDCURSES_WIN32
                CloseHandle(q_script_stderr);
                q_script_stderr = NULL;
#else
                close(q_running_script.script_stderr_fd);
                q_running_script.script_stderr_fd = -1;
#endif
            }

            if (rc > 0) {
                /*
                 * Record # of new bytes in
                 */
                stderr_buffer_n += rc;
            }

        } /* if ((n > 0) && (output_max - *output_n > 4)) */

        last_utf8_state = stderr_utf8_state;
        n = 0;
        for (i = 0; i < stderr_buffer_n; i++) {
            utf8_decode(&stderr_utf8_state, &utf8_char, stderr_buffer[i]);
            n++;

            if ((last_utf8_state == stderr_utf8_state) &&
                (stderr_utf8_state != UTF8_ACCEPT)) {

                /*
                 * Bad character, reset UTF8 decoder state
                 */
                stderr_utf8_state = 0;

                /*
                 * Discard character
                 */
                continue;
            }
            if (stderr_utf8_state != UTF8_ACCEPT) {
                /*
                 * Not enough characters to convert yet
                 */
                continue;
            }
            stderr_utf8_buffer[stderr_utf8_buffer_n] = (wchar_t) utf8_char;
            stderr_utf8_buffer_n++;
            if (stderr_utf8_buffer_n == Q_BUFFER_SIZE) {
                break;
            }
        }
        /*
         * Discard processed bytes
         */
        if (n < stderr_buffer_n) {
            memmove(stderr_buffer, stderr_buffer + n, stderr_buffer_n - n);
        }
        stderr_buffer_n -= n;
        print_stderr();

    } /* if (check_stderr == Q_TRUE) */

    /*
     * -----------------------------------------------------------------
     * Read bytes from script stdout, decode from UTF-8, and send to remote
     * side
     * -----------------------------------------------------------------
     */
    if ((q_running_script.stdout_readable == Q_TRUE) &&
        (q_running_script.paused == Q_FALSE)) {

        n = Q_BUFFER_SIZE - stdout_buffer_n;

        /*
         * Make sure at least 4 bytes are available for 1 UTF-8 character
         */
        if ((n > 0) && (output_max - *output_n > 4)) {
            /*
             * Clear errno
             */
            errno = 0;

#ifdef Q_PDCURSES_WIN32

            if (PeekNamedPipe(q_script_stdout, NULL, 0, NULL, &actual_bytes,
                              NULL) == 0) {

                /*
                 * Error peeking
                 */
                rc = 0;
            } else {
                if (actual_bytes == 0) {
                    rc = 0;
                } else if (actual_bytes > n) {
                    actual_bytes = n;
                }
                if (ReadFile(q_script_stdout,
                             stdout_buffer + stdout_buffer_n,
                             actual_bytes, &bytes_read, NULL) == TRUE) {
                    rc = bytes_read;
                } else {
                    /*
                     * Error in read
                     */
                    rc = 0;
                }
            }

#else

            rc = read(q_running_script.script_tty_fd,
                      stdout_buffer + stdout_buffer_n, n);

#endif /* Q_PDCURSES_WIN32 */

            DLOG(("read() rc: %d\n", rc));
            for (i = 0; i < rc; i++) {
                DLOG((" %c\n", stdout_buffer[stdout_buffer_n + i]));
            }

            if (rc < 0) {
                if (errno == EIO) {
                    /*
                     * This is EOF
                     */
                    rc = 0;
                } else {
                    qlog(_("Script stdout read() failed: %s"), strerror(errno));
                }
            }
            if (rc == 0) {
                /*
                 * EOF
                 */
                script_stop();
                return;
            }

            if (rc > 0) {
                /*
                 * Record # of new bytes in
                 */
                stdout_buffer_n += rc;
            }

        } /* if ((n > 0) && (output_max - *output_n > 4)) */

        /*
         * Decode UTF-8 and post to remote side
         */
        last_utf8_state = stdout_utf8_state;
        n = 0;
        for (i = 0; i < stdout_buffer_n; i++) {
            utf8_decode(&stdout_utf8_state, &utf8_char, stdout_buffer[i]);
            n++;

            if ((last_utf8_state == stdout_utf8_state)
                && (stdout_utf8_state != UTF8_ACCEPT)) {
                /*
                 * Bad character, reset UTF8 decoder state
                 */
                stdout_utf8_state = 0;

                /*
                 * Discard character
                 */
                continue;
            }

            if (stdout_utf8_state != UTF8_ACCEPT) {
                /*
                 * Not enough characters to convert yet
                 */
                continue;
            }

            switch (q_status.emulation) {
            case Q_EMUL_TTY:
            case Q_EMUL_ANSI:
            case Q_EMUL_AVATAR:
            case Q_EMUL_PETSCII:
            case Q_EMUL_ATASCII:
            case Q_EMUL_DEBUG:
            case Q_EMUL_VT52:
            case Q_EMUL_VT100:
            case Q_EMUL_VT102:
            case Q_EMUL_VT220:
            case Q_EMUL_LINUX:
            case Q_EMUL_XTERM:
                /*
                 * 8-bit emulations: try to backmap to the codepage,
                 * including allowing Unicode synonyms.
                 */
                output[*output_n] = translate_unicode_to_8bit(
                                        (wchar_t) utf8_char, q_status.codepage);
                /*
                 * 8-bit emulations
                 */
                output[*output_n] = (unsigned char) (utf8_char & 0xFF);
                rc = 1;
                break;
            case Q_EMUL_LINUX_UTF8:
            case Q_EMUL_XTERM_UTF8:
                /*
                 * UTF-8 emulations: encode outbound "keystroke", after
                 * running it through the direct Unicode translation map.
                 */
                utf8_char = translate_unicode_out((wchar_t) utf8_char);

                /*
                 * Now re-encode on the wire.
                 */
                rc = utf8_encode((wchar_t) utf8_char,
                                 (char *) &output[*output_n]);
                break;
            }
            *output_n += rc;

            if (output_max - *output_n < 4) {
                /*
                 * No room for more characters
                 */
                break;
            }
        }
        /*
         * Discard processed bytes
         */
        if (n < stdout_buffer_n) {
            memmove(stdout_buffer, stdout_buffer + n, stdout_buffer_n - n);
        }
        stdout_buffer_n -= n;
    }

}

/**
 * Spawn a new script process and start it.
 *
 * @param script_filename the filename to execute
 */
void script_start(const char * script_filename) {

#ifdef Q_PDCURSES_WIN32
    PROCESS_INFORMATION process_info;
    STARTUPINFOA startup_info;
    HANDLE q_script_stdin_2 = NULL;
    HANDLE q_script_stdout_2 = NULL;
    HANDLE q_script_stderr_2 = NULL;
    SECURITY_ATTRIBUTES security_attr;
    char buffer[32];
    int columns;
    DWORD pipe_flags = PIPE_NOWAIT;
#else
    char path_string[COMMAND_LINE_SIZE];
    Q_BOOL use_stderr = Q_TRUE;
    char * stderr_filename;
    char * env_string = getenv("HOME");
    pid_t child_pid = -1;
    char ** target_argv;
    char ttyname_buffer[FILENAME_SIZE];
#endif

    char command_line[COMMAND_LINE_SIZE];
    struct q_scrolline_struct * line;
    struct q_scrolline_struct * line2;

    assert(q_status.read_only == Q_FALSE);

    qlog(_("Executing script %s...\n"), script_filename);

    /*
     * Initial state
     */
    q_running_script.running = Q_FALSE;
    q_running_script.paused = Q_FALSE;
#ifdef Q_PDCURSES_WIN32
    assert(q_script_stdin == NULL);
    assert(q_script_stdout == NULL);
    assert(q_script_stderr == NULL);
    assert(q_script_process == NULL);
    assert(q_script_thread == NULL);
#else
    q_running_script.script_pid = -1;
    q_running_script.script_tty_fd = -1;
    q_running_script.script_tty_name = NULL;
    q_running_script.script_stderr_fd = -1;
#endif

    /*
     * Script filename needs to be on the display even after script_stop()
     * has been called, so reset it here.
     */
    if (q_running_script.filename != NULL) {
        Xfree(q_running_script.filename, __FILE__, __LINE__);
    }
    q_running_script.filename = Xstrdup(script_filename, __FILE__, __LINE__);
    q_running_script.stdin_writeable = Q_FALSE;
    q_running_script.stdout_readable = Q_FALSE;

    memset(print_buffer, 0, sizeof(print_buffer));
    print_buffer_n = 0;
    update_print_buffer_flags();

#ifndef Q_PDCURSES_WIN32

    /*
     * Open stderr
     */
    stderr_filename =
        substitute_string(get_option(Q_OPTION_SCRIPTS_STDERR_FIFO), "$HOME",
                          env_string);

    if (access(stderr_filename, F_OK) != 0) {
        use_stderr = Q_FALSE;
    }

#endif

    /*
     * Clear stderr output window lines
     */
    line = stderr_lines;
    while (line != NULL) {
        line2 = line;
        line = line2->next;
        Xfree(line2, __FILE__, __LINE__);
    }
    stderr_lines = line;
    stderr_last = stderr_lines;

#ifdef Q_PDCURSES_WIN32
    /*
     * Modeled after example on MSDN:
     * http://msdn.microsoft.com/en-us/library/ms682499%28v=VS.85%29.aspx
     */

    security_attr.nLength = sizeof(SECURITY_ATTRIBUTES);
    security_attr.bInheritHandle = TRUE;
    security_attr.lpSecurityDescriptor = NULL;

    /*
     * Create pipes as needed to communicate with script process
     */
    if (!CreatePipe(&q_script_stdout, &q_script_stdout_2, &security_attr, 0)) {
#ifdef DEBUG_SPAWNPROCESS
        fprintf(stderr, "CreatePipe() 1 failed: %d %sn",
                GetLastError(), strerror(GetLastError()));
        fflush(stderr);
#endif
        /*
         * Error, bail out
         */
        return;
    }
    if (!SetHandleInformation(q_script_stdout, HANDLE_FLAG_INHERIT, 0)) {
        /*
         * Error, bail out
         */
        CloseHandle(q_script_stdout);
        CloseHandle(q_script_stdout_2);
        q_script_stdout = NULL;
        q_script_stdout_2 = NULL;
#ifdef DEBUG_SPAWNPROCESS
        fprintf(stderr, "SetHandleInformation() 1 failed: %d %sn",
                GetLastError(), strerror(GetLastError()));
        fflush(stderr);
#endif
        return;
    }
    if (!CreatePipe(&q_script_stderr, &q_script_stderr_2, &security_attr, 0)) {
#ifdef DEBUG_SPAWNPROCESS
        fprintf(stderr, "CreatePipe() 2 failed: %d %sn",
                GetLastError(), strerror(GetLastError()));
        fflush(stderr);
#endif
        /*
         * Error, bail out
         */
        return;
    }
    if (!SetHandleInformation(q_script_stderr, HANDLE_FLAG_INHERIT, 0)) {
        /*
         * Error, bail out
         */
        CloseHandle(q_script_stdout);
        CloseHandle(q_script_stdout_2);
        q_script_stdout = NULL;
        q_script_stdout_2 = NULL;
        CloseHandle(q_script_stderr);
        CloseHandle(q_script_stderr_2);
        q_script_stderr = NULL;
        q_script_stderr_2 = NULL;
#ifdef DEBUG_SPAWNPROCESS
        fprintf(stderr, "SetHandleInformation() 2 failed: %d %sn",
                GetLastError(), strerror(GetLastError()));
        fflush(stderr);
#endif
        return;
    }
    /*
     * This stdin must NOT be bufferred.
     */
    if (!CreatePipe(&q_script_stdin_2, &q_script_stdin, &security_attr, 0)) {
#ifdef DEBUG_SPAWNPROCESS
        fprintf(stderr, "CreatePipe() 3 failed: %d %sn",
                GetLastError(), strerror(GetLastError()));
        fflush(stderr);
#endif
        /*
         * Error, bail out
         */
        return;
    }
    if (!SetHandleInformation(q_script_stdin, HANDLE_FLAG_INHERIT, 0)) {
        /*
         * Error, bail out
         */
        CloseHandle(q_script_stdout);
        CloseHandle(q_script_stdout_2);
        q_script_stdout = NULL;
        q_script_stdout_2 = NULL;
        CloseHandle(q_script_stderr);
        CloseHandle(q_script_stderr_2);
        q_script_stderr = NULL;
        q_script_stderr_2 = NULL;
        CloseHandle(q_script_stdin);
        CloseHandle(q_script_stdin_2);
        q_script_stdin = NULL;
        q_script_stdin_2 = NULL;
#ifdef DEBUG_SPAWNPROCESS
        fprintf(stderr, "SetHandleInformation() 3 failed: %d %sn",
                GetLastError(), strerror(GetLastError()));
        fflush(stderr);
#endif
        return;
    }
    /*
     * Don't block on writes to q_script_stdin
     */
    if (SetNamedPipeHandleState(q_script_stdin, &pipe_flags, NULL, NULL) ==
        FALSE) {
        /*
         * Error, bail out
         */
        CloseHandle(q_script_stdout);
        CloseHandle(q_script_stdout_2);
        q_script_stdout = NULL;
        q_script_stdout_2 = NULL;
        CloseHandle(q_script_stderr);
        CloseHandle(q_script_stderr_2);
        q_script_stderr = NULL;
        q_script_stderr_2 = NULL;
        CloseHandle(q_script_stdin);
        CloseHandle(q_script_stdin_2);
        q_script_stdin = NULL;
        q_script_stdin_2 = NULL;
#ifdef DEBUG_SPAWNPROCESS
        fprintf(stderr, "SetNamedPipeHandleState() 3 failed: %d %sn",
                GetLastError(), strerror(GetLastError()));
        fflush(stderr);
#endif
        return;
    }

    /*
     * Set my TERM variable
     */
    if (strlen(emulation_term(q_status.emulation)) > 0) {
        SetEnvironmentVariableA("TERM", emulation_term(q_status.emulation));
    }

    /*
     * Set LINES and COLUMNS
     */
    snprintf(buffer, sizeof(buffer), "%u", HEIGHT - STATUS_HEIGHT);
    SetEnvironmentVariableA("LINES", buffer);

    switch (q_status.emulation) {
    case Q_EMUL_ANSI:
    case Q_EMUL_AVATAR:
    case Q_EMUL_TTY:
        /*
         * BBS-ish emulations: check the assume_80_columns flag
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

    /*
     * Set the LANG.  For scripts, it is ALWAYS the UTF-8 LANG.
     */
    SetEnvironmentVariableA("LANG", get_option(Q_OPTION_UTF8_LANG));

    /*
     * Create child process itself
     */
    memset(&process_info, 0, sizeof(PROCESS_INFORMATION));
    memset(&startup_info, 0, sizeof(STARTUPINFO));
    startup_info.cb = sizeof(STARTUPINFO);
    startup_info.hStdInput = q_script_stdin_2;
    startup_info.hStdOutput = q_script_stdout_2;
    startup_info.hStdError = q_script_stderr_2;
    startup_info.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup_info.wShowWindow = SW_HIDE;

    /*
     * The POSIX code can run the script by prepending the scripts dir to
     * PATH.  Win32 cannot do this, so the script must be called by a
     * fully-qualified name.  The downside is scripts can't call each other
     * as easily.
     */
    snprintf(command_line, sizeof(command_line) - 1,
             "perl -w \"%s\\%s\"", get_option(Q_OPTION_SCRIPTS_DIR),
             script_filename);

    /*
     * Log the command line.
     */
    qlog(_("Spawning script with command line '%s'...\n"), command_line);
    qlog(_("Will record messages from script\n"));

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
         * Error, bail out
         */
#ifdef DEBUG_SPAWNPROCESS
        fprintf(stderr, "CreateProcess() failed: %d %sn",
                GetLastError(), strerror(GetLastError()));
        fflush(stderr);
#endif
        CloseHandle(process_info.hProcess);
        CloseHandle(process_info.hThread);
        CloseHandle(q_script_stdout);
        CloseHandle(q_script_stdout_2);
        q_script_stdout = NULL;
        q_script_stdout_2 = NULL;
        CloseHandle(q_script_stderr);
        CloseHandle(q_script_stderr_2);
        q_script_stderr = NULL;
        q_script_stderr_2 = NULL;
        CloseHandle(q_script_stdin);
        CloseHandle(q_script_stdin_2);
        q_script_stdin = NULL;
        q_script_stdin_2 = NULL;
        return;
    }
    q_script_process = process_info.hProcess;
    q_script_thread = process_info.hThread;

    /*
     * The child has these, not us, so close them
     */
    CloseHandle(q_script_stdin_2);
    CloseHandle(q_script_stdout_2);
    CloseHandle(q_script_stderr_2);

    /*
     * Record start time
     */
    time(&script_start_time);

    /*
     * At this point, we should have a running script process that writes to
     * the other ends of q_script_stdout and q_script_stderr, and reads from
     * the other end of q_script_stdin.  We have to use ReadFile() and
     * WriteFile() on our end of these handles.
     */
#ifdef DEBUG_SPAWNPROCESS
    fprintf(stderr, "script_start() OK: PID %d TID %d\n",
            process_info.dwProcessId, process_info.dwThreadId);
    fflush(stderr);
#endif

#else

    /*
     * POSIX case: call forkpty() and execXX() to spawn a new script in
     * another process.
     */

    /*
     * Assert that we closed the script TTY fd correctly from the last script
     * executed.
     */
    assert(q_running_script.script_tty_fd == -1);

    /*
     * Push all the data out to clear the soon-to-be child process's output
     * buffer.
     */
    screen_flush();

    if (use_stderr == Q_TRUE) {
        snprintf(command_line, sizeof(command_line) - 1, "exec %s 2>%s",
                 script_filename, stderr_filename);
    } else {
        snprintf(command_line, sizeof(command_line) - 1, "exec %s 2>/dev/null",
                 script_filename);
    }

    /*
     * Log the command line.  This should be in the child branch but putting
     * it there corrupts the session log output (two file handles pointing to
     * the same file).
     */
    qlog(_("[child] Spawning with command line '/bin/sh -c \"%s\"'...\n"),
         command_line);

    /*
     * Fork and put the child on a new tty
     */
    child_pid =
        forkpty(&q_running_script.script_tty_fd, ttyname_buffer, NULL, NULL);
    q_running_script.script_tty_name =
        Xstrdup(ttyname_buffer, __FILE__, __LINE__);

    if (child_pid == 0) {
        /*
         * Child process, will become the spawned script.
         */

        /*
         * We re-create the same environment conditions that dial_out() sets
         * up for the connection program.  Scripts can trust that LANG,
         * LINES, and COLUMNS match what the remote side knows about.
         *
         * If we make any changes to dial_out(), we need to verify those over
         * here too.
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
         * Set my TERM variable
         */
        if (strlen(emulation_term(q_status.emulation)) == 0) {
            unsetenv("TERM");
        } else {
            setenv("TERM", emulation_term(q_status.emulation), 1);
        }

        /*
         * Set LINES and COLUMNS
         */
        memset(buffer, 0, sizeof(buffer));
        snprintf(buffer, sizeof(buffer), "LINES=%u", HEIGHT - STATUS_HEIGHT);
        putenv(strdup(buffer));
        memset(buffer, 0, sizeof(buffer));

        switch (q_status.emulation) {
        case Q_EMUL_ANSI:
        case Q_EMUL_AVATAR:
        case Q_EMUL_TTY:
            /*
             * BBS-ish emulations: check the assume_80_columns flag.
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
         * Set the LANG.  For scripts, it is ALWAYS the UTF-8 LANG.
         */
        snprintf(buffer, sizeof(buffer), "LANG=%s",
                 get_option(Q_OPTION_UTF8_LANG));
        putenv(strdup(buffer));

        /*
         * This part is unique to script_start() : prepend SCRIPTS_DIR to
         * PATH.
         */
        snprintf(path_string, sizeof(path_string) - 1, "PATH=%s:%s",
                 get_option(Q_OPTION_SCRIPTS_DIR), getenv("PATH"));
        putenv(strdup(path_string));

        /*
         * Build exec arguments.  This needs to be done in a different way
         * from dial_out() because of the redirection arguments to the
         * subshell.
         */
        target_argv = (char **) Xmalloc(sizeof(char *) * 4, __FILE__, __LINE__);
        target_argv[0] = "/bin/sh";
        target_argv[1] = "-c";
        target_argv[2] = Xstrdup(command_line, __FILE__, __LINE__);
        target_argv[3] = NULL;
        execvp(target_argv[0], target_argv);
    }

    /*
     * Parent qodem process
     */
    q_running_script.script_pid = child_pid;
    qlog(_("[child] PID is %d\n"), child_pid);

    /*
     * Record start time
     */
    time(&script_start_time);

    /*
     * Open the stderr file for reading.
     */
    if (use_stderr == Q_TRUE) {
        q_running_script.script_stderr_fd = open(stderr_filename, O_RDONLY);
        if (q_running_script.script_stderr_fd == -1) {
            qlog(_("Error capturing script stderr: %d (%s)\n"), errno,
                 strerror(errno));
        } else {
            qlog(_("Will record messages from script (fd = %d)\n"),
                 q_running_script.script_stderr_fd);
        }
    }

    /*
     * Don't block the port
     */
    set_nonblock(q_running_script.script_tty_fd);

    /*
     * Put the PTY in raw mode
     */
    set_raw_termios(q_running_script.script_tty_fd);

    /*
     * Free leak
     */
    Xfree(stderr_filename, __FILE__, __LINE__);

#endif /* Q_PDCURSES_WIN32 */

    /*
     * Flag as running
     */
    q_running_script.running = Q_TRUE;
    q_running_script.paused = Q_FALSE;

    /*
     * Reset UTF-8 state
     */
    stdout_utf8_state = 0;
    stderr_utf8_state = 0;

    /*
     * Done
     */
    switch_state(Q_STATE_SCRIPT_EXECUTE);
}

/**
 * Terminate the script process.  We try to do it nicely with SIGHUP on POSIX
 * systems, which has the minor risk that we might hang waiting on its exit
 * if it ignores that signal.  For Windows we brutally murder it with
 * TerminateProcess if it hasn't already exited before script_stop() is
 * called.
 */
void script_stop() {
#ifdef Q_PDCURSES_WIN32
    DWORD status;
#else
    int status;
#endif

    char time_string[SHORT_TIME_SIZE];
    time_t current_time;
    int hours, minutes, seconds;
    time_t connect_time;

    if (q_running_script.running == Q_FALSE) {
        return;
    }

    /*
     * Flush whatever is in stderr
     */
    if (stderr_last != NULL) {
        if (stderr_last->length > 0) {
            log_line(stderr_last);
            q_screen_dirty = Q_TRUE;
        }
    }

    /*
     * Throw away the remaining print buffer
     */
    print_buffer_n = 0;
    update_print_buffer_flags();

#ifdef Q_PDCURSES_WIN32

    if (GetExitCodeProcess(q_script_process, &status) == TRUE) {
        /*
         * Got return code
         */
        if (status == STILL_ACTIVE) {
            /*
             * Process thinks it's still running, DIE!
             */
            TerminateProcess(q_script_process, -1);
            status = -1;
            qlog(_("Script forcibly terminated: still thinks it is alive.\n"));
        } else {
            qlog(_("Script exited with RC=%u\n"), status);
        }
    } else {
        /*
         * Can't get process exit code
         */
        TerminateProcess(q_script_process, -1);
        qlog(_("Script forcibly terminated: unable to get exit code.\n"));
    }

    /*
     * Close pipes
     */
    CloseHandle(q_script_stdin);
    q_script_stdin = NULL;
    CloseHandle(q_script_stdout);
    q_script_stdout = NULL;
    if (q_script_stderr != NULL) {
        CloseHandle(q_script_stderr);
        q_script_stderr = NULL;
    }
    CloseHandle(q_script_process);
    q_script_process = NULL;
    CloseHandle(q_script_thread);
    q_script_thread = NULL;

#else

    /*
     * Kill the child process and reap it
     */
    if (q_running_script.script_pid != -1) {
        kill(q_running_script.script_pid, SIGHUP);
    }

    /*
     * Close pty
     */
    if (q_running_script.script_tty_fd != -1) {
        close(q_running_script.script_tty_fd);
        q_running_script.script_tty_fd = -1;
    }

    /*
     * Close stderr
     */
    if (q_running_script.script_stderr_fd != -1) {
        close(q_running_script.script_stderr_fd);
        q_running_script.script_stderr_fd = -1;
    }

#endif /* Q_PDCURSES_WIN32 */

    q_running_script.running = Q_FALSE;
    q_running_script.paused = Q_FALSE;

    /*
     * Compute time
     */

    /*
     * time_string needs to be hours/minutes/seconds since script began
     */
    time(&current_time);
    connect_time = (time_t)difftime(current_time, script_start_time);
    hours = connect_time / 3600;
    minutes = (connect_time % 3600) / 60;
    seconds = connect_time % 60;
    snprintf(time_string, sizeof(time_string), "%02u:%02u:%02u", hours, minutes,
             seconds);
    qlog(_("Script exiting, total script time: %s\n"), time_string);

#ifndef Q_PDCURSES_WIN32

    /*
     * Reap process
     */
    if (q_running_script.script_pid != -1) {
        if (wait4(q_running_script.script_pid, &status, WNOHANG, NULL) !=
            q_running_script.script_pid) {

            qlog(_("Script failed to start! (errno = %d: %s)\n"), errno,
                strerror(errno));
            status = -1;
        }

        if (WIFEXITED(status)) {
            qlog(_("Script exited with RC=%u\n"), (WEXITSTATUS(status) & 0xFF));
            script_rc = (WEXITSTATUS(status) & 0xFF);
        } else if (WIFSIGNALED(status)) {
            qlog(_("Script exited with signal=%u\n"), WTERMSIG(status));
            script_rc = WTERMSIG(status) + 128;
        }
        q_running_script.script_pid = -1;
    }

    /*
     * No leaks
     */
    if (q_running_script.script_tty_name != NULL) {
        Xfree(q_running_script.script_tty_name, __FILE__, __LINE__);
        q_running_script.script_tty_name = NULL;
    }

#endif

    /*
     * Refresh
     */
    q_screen_dirty = Q_TRUE;
}

/**
 * Stop sending I/O to the script process.
 */
void script_pause() {
    if (q_running_script.paused == Q_FALSE) {
        q_running_script.paused = Q_TRUE;
    }
}

/**
 * Resume sending I/O to the script process.
 */
void script_resume() {
    if (q_running_script.paused == Q_TRUE) {
        q_running_script.paused = Q_FALSE;
    }
}

/**
 * Keyboard handler for script running mode.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
void script_keyboard_handler(const int keystroke, const int flags) {

    switch (keystroke) {
    case 'P':
    case 'p':
        if (flags & KEY_FLAG_ALT) {
            if (q_running_script.paused == Q_TRUE) {
                script_resume();
            } else {
                script_pause();
            }
            q_screen_dirty = Q_TRUE;
        }
        break;

    case '`':
        /*
         * Backtick works too
         */
    case Q_KEY_ESCAPE:
        if (q_running_script.paused == Q_FALSE) {
            /*
             * Kill the script
             */
            script_stop();

            /*
             * Return to TERMINAL mode
             */
            switch_state(Q_STATE_CONSOLE);
            return;
        }
        break;

    default:
        /*
         * Ignore keystroke
         */
        break;
    }

    if ((flags & KEY_FLAG_ALT) == 0) {
        if (q_running_script.paused == Q_TRUE) {
            /*
             * Pass keystroke
             */
            post_keystroke(keystroke, 0);
        }
    }

}

/**
 * Draw screen for script running mode.
 */
void script_refresh() {
    char * status_string;
    int status_left_stop;
    char * title;
    int left_stop;
    int i;
    int row;
    int stderr_top;
    struct q_scrolline_struct * line;
    const int stderr_height = 5;

    if (q_screen_dirty == Q_FALSE) {
        return;
    }

    /*
     * Steal some lines from the scrollback buffer display: stderr_height
     * lines of script stderr area + 1 line for the stderr status bar.
     */
    render_scrollback(stderr_height + 1);

    /*
     * Put up the script stderr line
     */
    title = _(" Script User Output ");
    left_stop = WIDTH - strlen(title);
    if (left_stop < 0) {
        left_stop = 0;
    } else {
        left_stop /= 2;
    }
    screen_put_color_hline_yx(HEIGHT - 1 - STATUS_HEIGHT - stderr_height, 0,
                              cp437_chars[DOUBLE_BAR], WIDTH,
                              Q_COLOR_WINDOW_BORDER);
    /*
     * Script filename
     */
    screen_put_color_char_yx(HEIGHT - 1 - STATUS_HEIGHT - stderr_height, 3, '[',
                             Q_COLOR_WINDOW_BORDER);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, _(" File: "));
    screen_put_color_printf(Q_COLOR_MENU_COMMAND,
                            _("%s "), q_running_script.filename);
    screen_put_color_char(']', Q_COLOR_WINDOW_BORDER);
    /*
     * Title
     */
    screen_put_color_char_yx(HEIGHT - 1 - STATUS_HEIGHT - stderr_height,
                             left_stop - 1, '[', Q_COLOR_WINDOW_BORDER);
    screen_put_color_str(title, Q_COLOR_MENU_COMMAND);
    screen_put_color_char(']', Q_COLOR_WINDOW_BORDER);
    /*
     * Script state text
     */
    screen_put_color_char_yx(HEIGHT - 1 - STATUS_HEIGHT - stderr_height,
                             WIDTH - 3 - 25, '[', Q_COLOR_WINDOW_BORDER);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, _(" Status: "));
    if (q_running_script.running == Q_TRUE) {
        screen_put_color_printf(Q_COLOR_SCRIPT_RUNNING, _("Running "));
    } else {
        if (script_rc == 0) {
            screen_put_color_printf(Q_COLOR_SCRIPT_FINISHED_OK,
                                    _("Finished OK "));
        } else {
            screen_put_color_printf(Q_COLOR_SCRIPT_FINISHED,
                                    _("Error code %d "), script_rc);
        }
    }
    screen_put_color_char(']', Q_COLOR_WINDOW_BORDER);

    /*
     * Clear the bottom lines.  Start stderr_height lines above the bottom.
     */
    stderr_top = (HEIGHT - STATUS_HEIGHT - stderr_height);
    for (i = stderr_top; i < (HEIGHT - STATUS_HEIGHT); i++) {
        screen_put_color_hline_yx(i, 0, ' ', WIDTH, Q_COLOR_CONSOLE_TEXT);
    }

    line = stderr_last;
    /*
     * Don't show the last line if it's still blank.
     */
    if (line != NULL) {
        if (line->length == 0) {
            line = line->prev;
        }
    }
    row = 0;
    if (line != NULL) {
        while (line->prev != NULL) {
            row++;
            line = line->prev;
            if (row == stderr_height - 1) {
                break;
            }
        }
    }

    for (row = 0; line != NULL; line = line->next) {
        for (i = 0; i < line->length; i++) {
            if (i >= WIDTH) {
                break;
            }
            /*
             * Don't run the script's stderr through the Unicode translate
             * tables.
             */
            screen_put_scrollback_char_yx(row + stderr_top, i, line->chars[i],
                                          scrollback_full_attr
                                          (Q_COLOR_CONSOLE_TEXT));
        }
        row++;
    }

    /*
     * Status line
     */
    if (q_running_script.paused == Q_FALSE) {
        status_string =
            _(" Script Executing   Alt-P-Pause    ESC/`-Stop Script ");
    } else {
        status_string =
            _(" Script PAUSED      Alt-P-Resume                     ");
    }
    screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH,
                              Q_COLOR_STATUS);
    status_left_stop = WIDTH - strlen(status_string);
    if (status_left_stop <= 0) {
        status_left_stop = 0;
    } else {
        status_left_stop /= 2;
    }
    screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string,
                            Q_COLOR_STATUS);

    screen_flush();
    q_screen_dirty = Q_FALSE;
}
