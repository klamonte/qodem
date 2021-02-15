/*
 * qodem.h
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

#ifndef __QODEM_H__
#define __QODEM_H__

/* Includes --------------------------------------------------------------- */

#include <time.h>               /* time_t */
#include <stdio.h>              /* FILE */
#include <sys/types.h>
#include "emulation.h"          /* Q_EMULATION */
#include "codepage.h"           /* Q_CODEPAGE */
#include "phonebook.h"          /* Q_DIAL_METHOD */
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

/* The network buffer size. */
#define Q_BUFFER_SIZE 4096

/**
 * Available capture types.
 */
typedef enum Q_CAPTURE_TYPES {
    Q_CAPTURE_TYPE_NORMAL,      /* normal */
    Q_CAPTURE_TYPE_RAW,         /* raw */
    Q_CAPTURE_TYPE_HTML,        /* html */
    Q_CAPTURE_TYPE_ASK          /* ask - prompt every time */
} Q_CAPTURE_TYPE;

/**
 * Available doorway modes
 */
typedef enum Q_DOORWAY_MODE {
    Q_DOORWAY_MODE_OFF,         /* No doorway */
    Q_DOORWAY_MODE_MIXED,       /* Mixed mode */
    Q_DOORWAY_MODE_FULL         /* Full doorway */
} Q_DOORWAY_MODE;

#ifndef Q_NO_SERIAL
#define Q_SERIAL_OPEN (q_status.serial_open == Q_TRUE)
#else
#define Q_SERIAL_OPEN (Q_FALSE)
#endif

/**
 * There is lots of global state in qodem, needed to control features and
 * feed various UI elements.  The stuff that is used by more than two modules
 * we try to keep here.
 */
struct q_status_struct {

    /**
     * If true, do not write anything to disk.
     */
    Q_BOOL read_only;

    /**
     * Current emulation mode.
     */
    Q_EMULATION emulation;

    /**
     * Current codepage.
     */
    Q_CODEPAGE codepage;

    /**
     * Doorway mode.
     */
    Q_DOORWAY_MODE doorway_mode;

    /**
     * When true, we are "online".
     */
    Q_BOOL online;

    /**
     * When true, the user has requested a hangup with Alt-H.  For network
     * connections it may take a cycle or two through data_handler() before
     * the EOF is detected.
     */
    Q_BOOL hanging_up;

#ifndef Q_NO_SERIAL
    /**
     * When true, the serial port is open.
     */
    Q_BOOL serial_open;
#endif

    /**
     * When true, the console is in split screen mode.
     */
    Q_BOOL split_screen;

    /**
     * The moment that q_status.online became true.
     */
    time_t connect_time;

    /**
     * When true, sound is enabled.  Beeps and bells and ANSI music can be
     * enabled separately.
     */
    Q_BOOL sound;

    /**
     * When true, beeps and bells are enabled.
     */
    Q_BOOL beeps;

    /**
     * When true, ANSI music is enabled.
     */
    Q_BOOL ansi_music;

    /**
     * When true, the session capture is enabled.  Bytes or characters are
     * being written to the capture file handle.
     */
    Q_BOOL capture;

    /**
     * The capture file handle.
     */
    FILE * capture_file;

    /**
     * The capture type (normal/raw/html/ask).
     */
    Q_CAPTURE_TYPE capture_type;

    /**
     * The screen dump type (normal/html/ask).
     */
    Q_CAPTURE_TYPE screen_dump_type;

    /**
     * The scrollback save type (normal/html/ask).
     */
    Q_CAPTURE_TYPE scrollback_save_type;

    /**
     * The time that fflush() was last called on the capture file handle.
     * This is used to allow buffered writes but also flush at least once a
     * second.
     */
    time_t capture_flush_time;

    /**
     * The current column number for the capture file.  This can differ from
     * the screen cursor_x due to cursor position commands in the emulation.
     */
    int capture_x;

    /**
     * When true, the session log is enabled.  Major events are being written
     * to the logging file handle.
     */
    Q_BOOL logging;

    /**
     * The logging file handle.
     */
    FILE * logging_file;

    /**
     * The number of lines in scrollback buffer.
     */
    unsigned int scrollback_lines;

    /**
     * The current screen cursor position X.
     */
    int cursor_x;

    /**
     * The current screen cursor position Y.
     */
    int cursor_y;

    /**
     * When true, strip the high bit.  Note that this happens on the raw byte
     * stream sent to the console, before emulation; UTF-8 emulations will
     * see garbage.  This does not affect file transfers.
     */
    Q_BOOL strip_8th_bit;

    /**
     * When true, use full duplex.  When false, perform a local echo of
     * keystrokes sent to the remote side.
     */
    Q_BOOL full_duplex;

    /**
     * When true, add a linefeed for every carriage return (CR) received.
     */
    Q_BOOL line_feed_on_cr;

    /**
     * When true, prompt the user for confirmation on Alt-H hangup.
     */
    Q_BOOL guard_hangup;

    /**
     * When true, lines that would scroll off screen are instead recorded to
     * the scrollback buffer.
     */
    Q_BOOL scrollback_enabled;

    /**
     * When true, the status line is visible.
     */
    Q_BOOL status_visible;

    /**
     * When true, the status line has the "alternate" info (address, current
     * time).  When false, it has the "regular" info (online, flags, connect
     * time).
     */
    Q_BOOL status_line_info;

    /**
     * When true, Qodem is operating in "X11 terminal mode": no phonebook, no
     * serial port, status line starts turned off, and disconnect on exit.
     * The only way to enable xterm_mode is to pass the --xterm command line
     * option.
     */
    Q_BOOL xterm_mode;

    /**
     * When true, bracketed paste mode is enabled.
     */
    Q_BOOL bracketed_paste_mode;

    /**
     * When true, backspace sends the C0 backspace control character ^H
     * (0x08).  When false, backspace sends the DEL character (0x7F).  VT220
     * emulation does not honor this flag, because backspace is defined by
     * its standard as DEL.
     */
    Q_BOOL hard_backspace;

    /**
     * When true, wrap lines at the right-most column.
     */
    Q_BOOL line_wrap;

    /**
     * When true, display the NUL (0x00) as a space ' '.  When false, strip
     * NUL from the input.
     */
    Q_BOOL display_null;

    /**
     * The amount of time to wait before disconnecting.
     */
    int idle_timeout;

    /**
     * When true, exit qodem on the next disconnect.
     */
    Q_BOOL exit_on_disconnect;

    /**
     * When true, the terminal in is quicklearn mode.
     */
    Q_BOOL quicklearn;

    /**
     * When true, do a trick to show actual double-width characters.  This is
     * only tried when TERM contains "xterm".
     */
    Q_BOOL xterm_double;

    /**
     * When true, enable xterm mouse reporting.
     */
    Q_BOOL xterm_mouse_reporting;

    /* Session variables */

    /**
     * The method used to obtain the current connection.
     */
    Q_DIAL_METHOD dial_method;

    /**
     * The 8-bit translate table that converts incoming bytes to something
     * else.
     */
    void * translate_8bit_in;

    /**
     * The 8-bit translate table that converts bytes to something
     * else before being written to the remote side.
     */
    void * translate_8bit_out;

    /**
     * The Unicode translate table that converts incoming wchar_t's to
     * something else.
     */
    void * translate_unicode_in;

    /**
     * The Unicode translate table that converts wchar_t's to something else
     * before being encoded to UTF-8 for eventual writing to the remote side.
     */
    void * translate_unicode_out;

    /**
     * The username for the current connection.
     */
    wchar_t * current_username;

    /**
     * The password for the current connection.
     */
    wchar_t * current_password;

    /**
     * The remote IP address for the current connection.
     */
    char * remote_address;

    /**
     * The remote IP port for the current connection.
     */
    char * remote_port;

    /**
     * The phonebook entry name for the current connection.
     */
    wchar_t * remote_phonebook_name;

    /* Zmodem */

    /**
     * When true, autostart a Zmodem download when ZRQINIT is seen.
     */
    Q_BOOL zmodem_autostart;

    /**
     * When true, escape control characters in Zmodem transfers.
     */
    Q_BOOL zmodem_escape_ctrl;

    /**
     * When true, issue a ZCHALLENGE in Zmodem transfers.
     */
    Q_BOOL zmodem_zchallenge;

    /* Kermit */

    /**
     * When true, autostart a Kermit download when SEND-INIT is seen.
     */
    Q_BOOL kermit_autostart;

    /**
     * When true, squish filenames to the Kermit "common form" definition.
     */
    Q_BOOL kermit_robust_filename;

    /**
     * When true, use streaming mode (don't send NAKs) for Kermit transfers.
     * This is generally the right thing to do for every kind of connection
     * except serial port and modem connections.
     */
    Q_BOOL kermit_streaming;

    /**
     * When true, force binary uploads on all file types (binary or text).
     * When false, try to detect if files are text only and if so use a text
     * upload (the other side will convert the text to its local equivalent,
     * i.e. do LF/CRLF conversion).
     */
    Q_BOOL kermit_uploads_force_binary;

    /**
     * When true, convert CRLF to LF on text file downloads.  When false,
     * treat text files like binary.
     */
    Q_BOOL kermit_downloads_convert_text;

    /**
     * When true, use the RESEND feature to attempt to resume Kermit uploads.
     When false, use SEND instead which will re-upload the entire file.
     */
    Q_BOOL kermit_resend;

    /**
     * When true, use long packets in Kermit transfers.  This is nearly
     * always the right thing to do.
     */
    Q_BOOL kermit_long_packets;

    /* Network connections */

    /**
     * When true, spawn an external program for outbound telnet connections.
     * When false, use the telnet support in netclient.c.
     */
    Q_BOOL external_telnet;

    /**
     * When true, spawn an external program for outbound rlogin connections.
     * When false, use the rlogin support in netclient.c.
     */
    Q_BOOL external_rlogin;

    /**
     * When true, spawn an external program for outbound ssh connections.
     * When false, use the ssh support in netclient.c.
     */
    Q_BOOL external_ssh;

    /* Avatar features */

    /**
     * When true, support color ANSI codes for Avatar which doesn't
     * officially support that.  Do this even when avatar_ansi_fallback is
     * false.
     */
    Q_BOOL avatar_color;

    /**
     * When true, send anything Avatar doesn't understand through the ANSI
     * emulator.
     */
    Q_BOOL avatar_ansi_fallback;

    /* PETSCII features */

    /**
     * When true, support color ANSI codes for PETSCII which doesn't
     * officially support that.  Do this even when petscii_ansi_fallback is
     * false.
     */
    Q_BOOL petscii_color;

    /**
     * When true, send anything PETSCII doesn't understand through the ANSI
     * emulator.
     */
    Q_BOOL petscii_ansi_fallback;

    /**
     * When true, PETSCII can assume that it has a wide font and does not
     * need to set every line to double-width.
     */
    Q_BOOL petscii_has_wide_font;

    /**
     * When true, PETSCII uses Commodore 64 control codes.  When false, it
     * uses Commodore 128 control codes.
     */
    Q_BOOL petscii_is_c64;

    /**
     * When true, PETSCII will try to map to Unicode rather than use the C64
     * Pro Mono font.
     */
    Q_BOOL petscii_use_unicode;

    /* ATASCII features */

    /**
     * When true, ATASCII can assume that it has a wide font and does not
     * need to set every line to double-width.
     */
    Q_BOOL atascii_has_wide_font;

    /* VT100 features */

    /**
     * When true, support color ANSI codes even for VT100 which doesn't
     * officially support that.
     */
    Q_BOOL vt100_color;

    /**
     * When true, cursor position is relative to the scrolling region.
     */
    Q_BOOL origin_mode;

    /**
     * When true, new printed characters shift the row to right.
     */
    Q_BOOL insert_mode;

    /**
     * Top margin of the scrolling region.
     */
    int scroll_region_top;

    /**
     * Bottom margin of the scrolling region.
     */
    int scroll_region_bottom;

    /**
     * When true, foreground and background colors are reversed for the
     * entire screen.
     */
    Q_BOOL reverse_video;

    /**
     * DECLL led 1.
     */
    Q_BOOL led_1;

    /**
     * DECLL led 2.
     */
    Q_BOOL led_2;

    /**
     * DECLL led 3.
     */
    Q_BOOL led_3;

    /**
     * DECLL led 4.
     */
    Q_BOOL led_4;

    /* VT220 features */

    /**
     * When true, the cursor is visible in terminal mode.
     */
    Q_BOOL visible_cursor;

    /* VT52 features */

    /**
     * When true, support color ANSI codes even for VT52 which doesn't
     * officially support that.
     */
    Q_BOOL vt52_color;

    /**
     * When true, HOLD SCREEN mode has been requested.  We do not yet
     * actually do it though.
     */
    Q_BOOL hold_screen_mode;

    /* ANSI features */

    /**
     * When true, flush screen to show ANSI animation ASAP.  When false,
     * buffer as normal, even if ANSI animates oddly.
     */
    Q_BOOL ansi_animate;

    /* ANSI, Avatar, and TTY features */

    /**
     * When true, wrap at column 80.  When false, wrap at the screen right
     * margin.
     */
    Q_BOOL assume_80_columns;

};

/* Globals ---------------------------------------------------------------- */

/**
 * Global status struct.
 */
extern struct q_status_struct q_status;

/**
 * The TTY name of the child TTY.
 */
extern char * q_child_ttyname;

/**
 * The child TTY descriptor.  For POSIX, this is the same descriptor for
 * command line programs, network connections, and serial port.  For Windows,
 * this is only for network connections.
 */
extern int q_child_tty_fd;

/**
 * The child process ID.
 */
extern pid_t q_child_pid;

/**
 * The physical screen width.
 */
extern int WIDTH;

/**
 * The physical screen height.
 */
extern int HEIGHT;

/**
 * The height of the status bar.  Currently this is either 0 or 1, but in the
 * future it could become several lines.
 */
extern int STATUS_HEIGHT;

/**
 * The base working directory where qodem stores its config files and
 * phonebook.  For POSIX this is usually ~/.qodem, for Windows it is My
 * Documents\\qodem.
 */
extern char * q_home_directory;

/**
 * The screensaver timeout in seconds.
 */
extern int q_screensaver_timeout;

/**
 * The keepalive timeout in seconds.
 */
extern int q_keepalive_timeout;

/**
 * The bytes to send to the remote side when the keepalive timeout is
 * reached.
 */
extern char q_keepalive_bytes[128];

/**
 * The number of bytes in the q_keepalive_bytes buffer.
 */
extern unsigned int q_keepalive_bytes_n;

/**
 * The last time we sent data, used by the keepalive feature.
 */
extern time_t q_data_sent_time;

/**
 * The --keyfile command line argument.
 */
extern char * q_keyfile;

/**
 * The --scrfile command line argument.
 */
extern char * q_scrfile;

/**
 * The --xl8file command line argument.
 */
extern char * q_xl8file;

/**
 * The --xlufile command line argument.
 */
extern char * q_xlufile;

/* Functions -------------------------------------------------------------- */

/**
 * Emit a message to the log file.
 *
 * @param format a printf-style format string
 */
extern void qlog(const char * format, ...);

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
extern FILE * open_workingdir_file(const char * filename, char ** new_filename);

/**
 * Get the full path to a filename in the data directory.  Note that the
 * string returned is a single static buffer, i.e. this is NOT thread-safe.
 *
 * @param filename a relative filename
 * @return the full path to the filename (usually ~/qodem/filename or My
 * Documents\\qodem\\filename).
 */
extern char * get_datadir_filename(const char * filename);

/**
 * Get the full path to a filename in the working directory.  Note that the
 * string returned is a single static buffer, i.e. this is NOT thread-safe.
 *
 * @param filename a relative filename
 * @return the full path to the filename (usually ~/.qodem/filename or My
 * Documents\\qodem\\prefs\\filename).
 */
extern char * get_workingdir_filename(const char * filename);

/**
 * Get the full path to a filename in the scripts directory.  Note that the
 * string returned is a single static buffer, i.e. this is NOT thread-safe.
 *
 * @param filename a relative filename
 * @return the full path to the filename (usually ~/.qodem/scripts/filename
 * or My Documents\\qodem\\scripts\\filename).
 */
extern char * get_scriptdir_filename(const char * filename);

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
extern FILE * open_datadir_file(const char * filename, char ** new_filename,
                                const char * mode);

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
extern int qodem_write(const int fd, const char * data, const int data_n,
                       const Q_BOOL sync);

/**
 * Buffer up data to write to the remote system.
 *
 * @param data the buffer to read from
 * @param data_n the number of bytes to write to the remote side
 */
extern void qodem_buffered_write(const char * data, const int data_n);

/**
 * Write data from the buffer of qodem_buffered_write() to the remote system,
 * dispatching to the appropriate connection-specific write function.
 *
 * @param fd the socket descriptor
 */
extern void qodem_buffered_write_flush(const int fd);

/**
 * Spawn a command in an external terminal.  This is used for the mail reader
 * and external file editors.
 *
 * @param command the command line to execute
 */
extern void spawn_terminal(const char * command);

/**
 * Close remote connection, dispatching to the appropriate
 * connection-specific close function.
 */
extern void close_connection();

/**
 * Close a remote network connection.
 */
extern void close_network_connection();

/**
 * Close a wrapped shell connection.
 */
extern void close_shell_connection();

/*
 * A function pointer containing the appropriate network close connection to
 * call, set by dial_out().
 */
extern void (*close_function)();

#ifdef __cplusplus
}
#endif

#endif /* __QODEM_H__ */
