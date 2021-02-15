/*
 * options.h
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

#ifndef __OPTIONS_H__
#define __OPTIONS_H__

/* Includes --------------------------------------------------------------- */

#include <wchar.h>
#include "common.h"             /* Q_BOOL */

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

/* One of the locations to look for in load_options() */
#ifndef INSTALL_DIR
#define INSTALL_DIR "/usr/local/qodem"
#endif

/**
 * The option types.  See option.c for detailed descriptions.
 */
typedef enum Q_OPTIONS {
    Q_OPTION_NULL,

    Q_OPTION_HOST_USERNAME,
    Q_OPTION_HOST_PASSWORD,
    Q_OPTION_WORKING_DIR,
    Q_OPTION_HOST_DIR,
    Q_OPTION_DOWNLOAD_DIR,
    Q_OPTION_UPLOAD_DIR,
    Q_OPTION_SCRIPTS_DIR,
    Q_OPTION_SCRIPTS_STDERR_FIFO,
    Q_OPTION_BATCH_ENTRY_FILE,
    Q_OPTION_SHELL,
    Q_OPTION_EDITOR,
    Q_OPTION_X11_TERMINAL,
    Q_OPTION_MAIL_READER,
    Q_OPTION_ISO8859_LANG,
    Q_OPTION_UTF8_LANG,
    Q_OPTION_SOUNDS_ENABLED,
    Q_OPTION_XTERM_DOUBLE,
    Q_OPTION_X11_FONT,
    Q_OPTION_START_PHONEBOOK,
    Q_OPTION_STATUS_LINE_VISIBLE,
    Q_OPTION_DIAL_CONNECT_TIME,
    Q_OPTION_DIAL_BETWEEN_TIME,
    Q_OPTION_EXIT_ON_DISCONNECT,
    Q_OPTION_IDLE_TIMEOUT,
    Q_OPTION_BRACKETED_PASTE,
    Q_OPTION_CAPTURE,
    Q_OPTION_CAPTURE_FILE,
    Q_OPTION_CAPTURE_TYPE,
    Q_OPTION_SCREEN_DUMP_TYPE,
    Q_OPTION_SCROLLBACK_LINES,
    Q_OPTION_SCROLLBACK_SAVE_TYPE,
    Q_OPTION_LOG,
    Q_OPTION_LOG_FILE,
    Q_OPTION_CONNECT_DOORWAY,
    Q_OPTION_DOORWAY_MIXED_KEYS,
    Q_OPTION_KEEPALIVE_TIMEOUT,
    Q_OPTION_KEEPALIVE_BYTES,
    Q_OPTION_SCREENSAVER_TIMEOUT,
    Q_OPTION_SCREENSAVER_PASSWORD,
    Q_OPTION_MUSIC_CONNECT,
    Q_OPTION_MUSIC_CONNECT_MODEM,
    Q_OPTION_MUSIC_UPLOAD,
    Q_OPTION_MUSIC_DOWNLOAD,
    Q_OPTION_MUSIC_PAGE_SYSOP,
    Q_OPTION_80_COLUMNS,
    Q_OPTION_ENQ_ANSWERBACK,
    Q_OPTION_ANSI_MUSIC,
    Q_OPTION_ANSI_ANIMATE,
    Q_OPTION_AVATAR_COLOR,
    Q_OPTION_AVATAR_ANSI_FALLBACK,
    Q_OPTION_PETSCII_C64,
    Q_OPTION_PETSCII_COLOR,
    Q_OPTION_PETSCII_ANSI_FALLBACK,
    Q_OPTION_PETSCII_WIDE_FONT,
    Q_OPTION_PETSCII_UNICODE,
    Q_OPTION_ATASCII_WIDE_FONT,
    Q_OPTION_VT52_COLOR,
    Q_OPTION_VT100_COLOR,
    Q_OPTION_XTERM_MOUSE_REPORTING,
    Q_OPTION_SSH_EXTERNAL,
    Q_OPTION_SSH,
    Q_OPTION_SSH_USER,
    Q_OPTION_SSH_KNOWNHOSTS,
    Q_OPTION_RLOGIN_EXTERNAL,
    Q_OPTION_RLOGIN,
    Q_OPTION_RLOGIN_USER,
    Q_OPTION_TELNET_EXTERNAL,
    Q_OPTION_TELNET,
    Q_OPTION_ASCII_UPLOAD_USE_TRANSLATE_TABLE,
    Q_OPTION_ASCII_UPLOAD_CR_POLICY,
    Q_OPTION_ASCII_UPLOAD_LF_POLICY,
    Q_OPTION_ASCII_DOWNLOAD_USE_TRANSLATE_TABLE,
    Q_OPTION_ASCII_DOWNLOAD_CR_POLICY,
    Q_OPTION_ASCII_DOWNLOAD_LF_POLICY,
    Q_OPTION_ZMODEM_AUTOSTART,
    Q_OPTION_ZMODEM_ZCHALLENGE,
    Q_OPTION_ZMODEM_ESCAPE_CTRL,
    Q_OPTION_KERMIT_AUTOSTART,
    Q_OPTION_KERMIT_ROBUST_FILENAME,
    Q_OPTION_KERMIT_STREAMING,
    Q_OPTION_KERMIT_UPLOADS_FORCE_BINARY,
    Q_OPTION_KERMIT_DOWNLOADS_CONVERT_TEXT,
    Q_OPTION_KERMIT_RESEND,
    Q_OPTION_KERMIT_LONG_PACKETS,

    Q_OPTION_MAX
} Q_OPTION;

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

/**
 * Get an option value.  Note that the string returned is not
 * newly-allocated, i.e. do not free it later.
 *
 * @param option the option
 * @return the option value from the config file
 */
extern char * get_option(const Q_OPTION option);

/**
 * Reset options to default state.
 */
extern void reset_options();

/**
 * This must be called to initialize the options list from the config file.
 */
extern void load_options();

/**
 * Save options to a file.
 *
 * @param filename file to save to
 * @return true if successful
 */
extern Q_BOOL save_options(const char * filename);

/**
 * Replace all instances of "pattern" in "original" with "new_string",
 * returning a newly-allocated string.
 *
 * @param original the original string
 * @param pattern the pattern in original string to replace
 * @param new_string the string to replace pattern with
 * @return a newly-allocated string
 */
extern char * substitute_string(const char * original, const char * pattern,
                                const char * new_string);

/**
 * Replace all instances of "pattern" in "original" with "new_string",
 * returning a newly-allocated string.
 *
 * @param original the original string
 * @param pattern the pattern in original string to replace
 * @param new_string the string to replace pattern with.  It will be
 * converted to UTF-8.
 * @return a newly-allocated string
 */
extern char * substitute_wcs_half(const char * original, const char * pattern,
                                  const wchar_t * new_string);

/**
 * Replace all instances of "pattern" in "original" with "new_string",
 * returning a newly-allocated string.
 *
 * @param original the original string
 * @param pattern the pattern in original string to replace
 * @param new_string the string to replace pattern with
 * @return a newly-allocated string
 */
extern wchar_t * substitute_wcs(const wchar_t * original,
                                const wchar_t * pattern,
                                const wchar_t * new_string);

/**
 * Get the full path to the options config file.
 *
 * @return the full path to qodemrc (usually ~/.qodem/qodemrc or My
 * Documents\\qodem\\prefs\\qodemrc.txt).
 */
extern char * get_options_filename();

/**
 * Set q_status.capture_type to whatever is defined in the options file.
 */
extern void reset_capture_type();

/**
 * Set q_status.screen_dump_type to whatever is defined in the options file.
 */
extern void reset_screen_dump_type();

/**
 * Set q_status.scrollback_save_type to whatever is defined in the options
 * file.
 */
extern void reset_scrollback_save_type();

/**
 * Get the key for an option.  The help system uses this to automatically
 * generate a help screen out of the options descriptions.
 *
 * @param option the option
 * @return the option key
 */
extern const char * get_option_key(const Q_OPTION option);

/**
 * Get the long description for an option.  The help system uses this to
 * automatically generate a help screen out of the options descriptions.
 *
 * @param option the option
 * @return the option description
 */
extern const char * get_option_description(const Q_OPTION option);

/**
 * Get the default value for an option.  The help system uses this to
 * automatically generate a help screen out of the options descriptions.
 *
 * @param option the option
 * @return the option default value
 */
extern const char * get_option_default(const Q_OPTION option);

#ifdef __cplusplus
}
#endif

#endif /* __OPTIONS_H__ */
