/*
 * options.c
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

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <ctype.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef Q_PDCURSES_WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#include <shlobj.h>
#else
#include <unistd.h>
#endif

#include "keyboard.h"
#include "translate.h"
#include "qodem.h"
#include "scrollback.h"
#include "forms.h"
#include "options.h"

/*
 * One option from the config file.
 */
struct option_struct {
    Q_OPTION option;
    char * value;
    char * name;
    char * default_value;
    char * comment;
};

/* The full path to the options file. */
static char home_directory_options_filename[FILENAME_SIZE];

/**
 * The --config command line argument, stored in qodem.c.
 */
extern char * q_config_filename;

/**
 * The --dotqodem-dir command line argument, stored in qodem.c.
 */
extern char * q_dotqodem_dir;

/* Options list */
static struct option_struct options[] = {
/* Host mode username/password */

        {Q_OPTION_HOST_USERNAME, NULL, "host_username", "guest", ""
"### HOST MODE OPTIONS -----------------------------------------------------\n"
"\n"
"### The username to require for host mode logins.  Maximum length is 64\n"
"### bytes."},

        {Q_OPTION_HOST_PASSWORD, NULL, "host_password", "let me in please", ""
"### The password to require for host mode logins.  Maximum length is 64\n"
"### bytes."},

/* Directories */

#ifdef Q_PDCURSES_WIN32
        {Q_OPTION_WORKING_DIR, NULL, "working_dir", "$HOME\\qodem", ""
#else
        {Q_OPTION_WORKING_DIR, NULL, "working_dir", "$HOME/qodem", ""
#endif
"### DIRECTORIES -----------------------------------------------------------\n"
"\n"
"### The default working directory.  The $HOME environment variable will\n"
"### be substituted if specified."},

#ifdef Q_PDCURSES_WIN32
        {Q_OPTION_HOST_DIR, NULL, "host_mode_dir", "$HOME\\qodem\\host", ""
#else
        {Q_OPTION_HOST_DIR, NULL, "host_mode_dir", "$HOME/qodem/host", ""
#endif
"### The default working directory for host mode.  The $HOME environment\n"
"### variable will be substituted if specified."},

#ifdef Q_PDCURSES_WIN32
        {Q_OPTION_DOWNLOAD_DIR, NULL, "download_dir", "$HOME\\qodem", ""
#else
        {Q_OPTION_DOWNLOAD_DIR, NULL, "download_dir", "$HOME/qodem", ""
#endif
"### The default directory to store downloaded files.  The $HOME\n"
"### environment variable will be substituted if specified."},

#ifdef Q_PDCURSES_WIN32
        {Q_OPTION_UPLOAD_DIR, NULL, "upload_dir", "$HOME\\qodem", ""
#else
        {Q_OPTION_UPLOAD_DIR, NULL, "upload_dir", "$HOME/qodem", ""
#endif
"### The default directory to look for files to upload.  The $HOME\n"
"### environment variable will be substituted if specified."},

#ifdef Q_PDCURSES_WIN32
        {Q_OPTION_SCRIPTS_DIR, NULL, "scripts_dir", "$HOME\\qodem\\scripts", ""
#else
        {Q_OPTION_SCRIPTS_DIR, NULL, "scripts_dir", "$HOME/.qodem/scripts", ""
#endif
"### The default directory to look for scripts.  The $HOME environment\n"
"### variable will be substituted if specified."},

#ifdef Q_PDCURSES_WIN32
        {Q_OPTION_SCRIPTS_STDERR_FIFO, NULL, "scripts_fifo", "unused", ""
#else
        {Q_OPTION_SCRIPTS_STDERR_FIFO, NULL, "scripts_fifo",
         "$HOME/.qodem/scripts/script.stderr", ""
#endif
"### The fifo (named pipe) used to capture the stderr stream from scripts.\n"
"### The $HOME environment  variable will be substituted if specified.\n"
"### This is not used by the Windows version."},

#ifdef Q_PDCURSES_WIN32
        {Q_OPTION_BATCH_ENTRY_FILE, NULL, "bew_file",
         "$HOME\\qodem\\batch_upload.txt", ""
#else
        {Q_OPTION_BATCH_ENTRY_FILE, NULL, "bew_file",
         "$HOME/qodem/batch_upload.txt", ""
#endif
"### Where to store the Batch Entry Window entries."},

/* Spawned programs (not connection protocols) */

#ifdef Q_PDCURSES_WIN32
        /* Win32: use cmd.exe */
        {Q_OPTION_SHELL, NULL, "shell", "cmd.exe", ""
#else
        /* Normal: use bash */
        {Q_OPTION_SHELL, NULL, "shell", "$SHELL", ""
#endif
"### LOCAL PROGRAMS (NOT CONNECTION PROTOCOLS) ----------------------------\n"
"\n"
"### The OS shell program.  The $SHELL environment  variable will be\n"
"### substituted if specified.  Examples: /bin/bash /bin/tcsh my_shell"},

#ifdef Q_PDCURSES_WIN32
        {Q_OPTION_EDITOR, NULL, "editor", "notepad.exe", ""
#else
        {Q_OPTION_EDITOR, NULL, "editor", "vi", ""
#endif
"### The editor program.  The $EDITOR environment variable will be\n"
"### substituted if specified."},

#ifdef Q_PDCURSES_WIN32
        {Q_OPTION_X11_TERMINAL, NULL, "x11_terminal",
         "cmd.exe /c start /wait $COMMAND", ""
"### The command shell to spawn for executing OS commands.  This is used\n"
#else
        {Q_OPTION_X11_TERMINAL, NULL, "x11_terminal",
         "x-terminal-emulator -e \'$COMMAND\'", ""
"### The X11 terminal to spawn for executing OS commands.  This is used\n"
#endif
"### for the following functions: Alt-R OS Shell, Alt-M Mail Reader,\n"
"### Alt-L Log View, Alt-N Configuration, Alt-V View File, and editing\n"
"### attached notes and linked scripts in the phonebook.\n"
"###\n"
"### This is only used by the X11 build.  Note that qodem will wait on this\n"
"### program to exit before resuming, just like the text-only build waits\n"
"### when it shells to the OS.\n"
"###\n"
"### $COMMAND will be replaced with the program to execute."},

        {Q_OPTION_MAIL_READER, NULL, "mail_reader", "mm", ""
"### The QWK/SOUP/etc. mail reader program.  Default is multimail (mm)"},


/* LANG flags */

        {Q_OPTION_ISO8859_LANG, NULL, "iso8859_lang", "C", ""
"### LANG ENVIRONMENT VARIABLE TO SEND ------------------------------------\n"
"\n"
"### The LANG environment variable to specify for the remote\n"
"### connection for non-Unicode emulations."},
        {Q_OPTION_UTF8_LANG, NULL, "utf8_lang", "en_US.UTF-8", ""
"### The LANG environment variable to specify for the remote\n"
"### connection for LINUX UTF-8 and XTERM UTF-8 emulations."},

/* General flags */

        {Q_OPTION_SOUNDS_ENABLED, NULL, "sounds", "true", ""
"### GENERAL FLAGS --------------------------------------------------------\n"
"\n"
"### Whether or not to support sounds.  This overrides ANSI music.  Value\n"
"### is 'true' or 'false'."},

        {Q_OPTION_X11_FONT, NULL, "x11_font", "-misc-fixed-medium-r-normal--20-200-75-75-c-100-iso10646-1", ""
"### The font to use in the X11 version of Qodem.  Some good font choices\n"
"### are listed below.  Currently only bitmap fonts (those that can be seen\n"
"### in 'xfontsel' and/or 'xterm -fn font-name') are supported.\n"
"###\n"
"###   PDCurses default:\n"
"###        -misc-fixed-medium-r-normal--20-200-75-75-c-100-iso10646-1\n"
"###   Terminus:\n"
"###        -*-terminus-medium-r-normal--16-200-*-*-c-*-iso10646-1\n"
"###        -*-terminus-medium-r-normal--20-200-*-*-c-*-iso10646-1\n"
"###        -*-terminus-medium-r-normal--24-240-*-*-c-*-iso10646-1\n"
"###        -*-terminus-medium-r-normal--32-320-*-*-c-*-iso10646-1\n"
"###   uni_vga:\n"
"###        -bolkhov-vga-medium-r-normal--16-160-75-75-c-80-iso10646-1\n"
"###\n"
"### This is only used by the X11 (PDCurses) build."},

         {Q_OPTION_XTERM_DOUBLE, NULL, "xterm_double_width", "true", ""
"### Qodem can display true double-width / double-height characters\n"
"### when run under an xterm that supports it.  Examples of xterms\n"
"### that can do so are PuTTY, Terminal.app on OS X, and of course\n"
"### the genuine XFree86 xterm ('xterm-new').\n"
"###\n"
"### Some programs known NOT to work are konsole, gnome-terminal,\n"
"### and rxvt.\n"
"###\n"
"### This is only used by the text (ncurses) build."},

        {Q_OPTION_START_PHONEBOOK, NULL, "start_in_phonebook", "true", ""
"### Whether to startup in the phonebook.  Value is 'true' or\n"
"### 'false'."},

         {Q_OPTION_STATUS_LINE_VISIBLE, NULL, "status_line", "true", ""
"### Whether the status line is visible on startup.  Value is 'true' or\n"
"### 'false'."},

        {Q_OPTION_DIAL_CONNECT_TIME, NULL, "dial_connect_time", "60", ""
"### How many seconds to wait when dialing to receive a successful\n"
"### connection."},

        {Q_OPTION_DIAL_BETWEEN_TIME, NULL, "dial_between_time", "5", ""
"### How many seconds to wait after a busy signal before dialing\n"
"### the next number."},

        {Q_OPTION_EXIT_ON_DISCONNECT, NULL, "exit_on_disconnect", "false", ""
"### Whether to exit Qodem when the connection closes.  Value is 'true' or\n"
"### 'false'."},

        {Q_OPTION_IDLE_TIMEOUT, NULL, "idle_timeout", "0", ""
"### The number of idle seconds to wait before automatically closing\n"
"### the connection.  A value of 0 means never disconnect."},

         {Q_OPTION_BRACKETED_PASTE, NULL, "bracketed_paste_mode", "false", ""
"### Whether or not bracketed paste mode is enabled.  Bracketed paste mode\n"
"### is a security feature that permits applications to distinguish between\n"
"### normal keyboard text and pasted text.  Qodem will automatically honor\n"
"### bracketed paste mode for XTERM and X_UTF8 emulations.  Other\n"
"### emulations will only use bracketed paste mode if this value is\n"
"### 'true'."},

/* Capture file */

        {Q_OPTION_CAPTURE, NULL, "capture_enabled", "false", ""
"### CAPTURE FILE ---------------------------------------------------------\n"
"\n"
"### Whether or not capture is enabled on startup.  Value is\n"
"### 'true' or 'false'."},

        {Q_OPTION_CAPTURE_FILE, NULL, "capture_file", "capture.txt", ""
"### The default capture file name.  When enabled, all transmitted and\n"
"### received bytes (minus color) are appended to this file.  This file\n"
"### is stored in the working directory if a relative path is specified."},

        {Q_OPTION_CAPTURE_TYPE, NULL, "capture_type", "normal", ""
"### The default capture format.  Value is 'normal', 'raw', 'html', or\n"
"### 'ask'."},

/* Screen dump */

        {Q_OPTION_SCREEN_DUMP_TYPE, NULL, "screen_dump_type", "normal", ""
"### SCREEN DUMP ----------------------------------------------------------\n"
"\n"
"### The default screen dump format.  Value is 'normal', 'html', or\n"
"### 'ask'."},

/* Scrollback */

        {Q_OPTION_SCROLLBACK_LINES, NULL, "scrollback_max_lines", "20000", ""
"### SCROLLBACK BUFFER ----------------------------------------------------\n"
"\n"
"### The maximum number of lines to save in the scrollback buffer.  0 means\n"
"### unlimited scrollback."},

        {Q_OPTION_SCROLLBACK_SAVE_TYPE, NULL, "scrollback_save_type",
         "normal", ""
"### The default capture format.  Value is 'normal', 'html', or\n"
"### 'ask'."},

/* Logfile options */

        {Q_OPTION_LOG, NULL, "log_enabled", "false", ""
"### LOG FILE -------------------------------------------------------------\n"
"\n"
"### Whether or not session logging is enabled on startup.  Value is\n"
"### 'true' or 'false'."},

        {Q_OPTION_LOG_FILE, NULL, "log_file", "session_log.txt", ""
"### The default session log file name.  When enabled, an entry is appended\n"
"### to this file for one of the following events:\n"
"###     connect\n"
"###     disconnect\n"
"###     program start\n"
"###     program exit\n"
"###     file upload\n"
"###     file download\n"
"###     OS shell\n"
"###     Scripted timestamp message\n"
"### This file is stored in the working directory if a relative path is\n"
"### specified."},

/* Doorway flags */

        {Q_OPTION_CONNECT_DOORWAY, NULL, "doorway_mode_on_connect", "off", ""
"### DOORWAY MODE ---------------------------------------------------------\n"
"\n"
"### Whether to automatically switch to DOORWAY or MIXED mode after\n"
"### connecting.  Value is 'doorway', 'mixed', or 'off'."},

        {Q_OPTION_DOORWAY_MIXED_KEYS, NULL, "doorway_mixed_mode_commands",
         "D P T Y Z / PgUp PgDn", ""
"### A space-separated list of command keys that will be honored when in\n"
"### MIXED doorway mode.  Each key is one of the Alt-key combos on the Alt-Z\n"
"### Command menu, except for 'PgUp' and 'PgDn'.  Listing 'PgUp' or 'PgDn'\n"
"### here means to allow the unmodified 'PgUp' and 'PgDn' keys to go to the\n"
"### remote side but still honor ALT- and CTRL- 'PgUp' and 'PgDn'.\n"
"### The default commands to honor are:\n"
"###     Alt-D Phonebook\n"
"###     Alt-P Capture\n"
"###     Alt-T Screen Dump\n"
"###     Alt-Y COM Parameters\n"
"###     Alt-Z Menu\n"
"###     Alt-/ Scrollback\n"
"###     Alt-PgUp or Ctrl-PgUp Upload Files\n"
"###     Alt-PgDn or Ctrl-PgDn Download Files"
        },

/* Keepalive feature */

        {Q_OPTION_KEEPALIVE_TIMEOUT, NULL, "keepalive_timeout", "0", ""
"### KEEPALIVE ------------------------------------------------------------\n"
"\n"
"### The number of idle seconds to wait before automatically sending\n"
"### the keepalive bytes.  A value of 0 disables the keepalive feature."},

        {Q_OPTION_KEEPALIVE_BYTES, NULL, "keepalive_bytes", "\\x00", ""
"### The bytes to every 'keepalive_timeout' seconds.  Use C-style\n"
"### hex notation with 2 hex digits to embed raw bytes, e.g. '\\x00' to\n"
"### mean ASCII NUL, '\\x32' is converted to '2', etc.\n"
"###\n"
"### The maximum string size is 128 bytes."},

/* Screensaver flags */

        {Q_OPTION_SCREENSAVER_TIMEOUT, NULL, "screensaver_timeout", "0", ""
"### SCREENSAVER ----------------------------------------------------------\n"
"\n"
"### The number of idle seconds to wait before automatically locking\n"
"### the screen.  A value of 0 means never lock the screen."},

        {Q_OPTION_SCREENSAVER_PASSWORD, NULL, "screensaver_password",
         "password", ""
"### The password required to unlock the screen when the screen saver\n"
"### is active.  The maximum length is 64 bytes."},

/* Music sequences */

        {Q_OPTION_MUSIC_CONNECT, NULL, "music_on_connect", "none", ""
"### MUSIC / BEEPS AND BELLS ----------------------------------------------\n"
"\n"
"### If sounds are enabled, the music sequence to play after\n"
"### successfully connected.  The string is the same format used\n"
"### by the GWBASIC PLAY statement (ANSI Music), or 'none'."},

        {Q_OPTION_MUSIC_CONNECT_MODEM, NULL, "music_on_modem_connect",
         "MN L16 T120 O4 AB>CAB>CAB>C", ""
"### If sounds are enabled, the music sequence to play after\n"
"### successfully connected via modem.  The string is the same format\n"
"### used by the GWBASIC PLAY statement (ANSI Music), or 'none'."},

        {Q_OPTION_MUSIC_UPLOAD, NULL, "music_on_upload_complete",
         "MS L8 T120 O5 EEEEE", ""
"### If sounds are enabled, the music sequence to play after\n"
"### a successful upload.  The string is the same format used\n"
"### by the GWBASIC PLAY statement (ANSI Music), or 'none'."},

        {Q_OPTION_MUSIC_DOWNLOAD, NULL, "music_on_download_complete",
         "MS L8 T120 O5 CCCCC", ""
"### If sounds are enabled, the music sequence to play after\n"
"### a successful download.  The string is the same format used\n"
"### by the GWBASIC PLAY statement (ANSI Music), or 'none'."},

        {Q_OPTION_MUSIC_PAGE_SYSOP, NULL, "music_on_download_complete",
         "MS T120 O4 L8 C L16 DEFGAB L8 >C L16 BAGFED L8 C", ""
"### If sounds are enabled, the music sequence to play when\n"
"### pagin the sysop in host mode.  The string is the same format\n"
"### used by the GWBASIC PLAY statement (ANSI Music), or 'none'."},

/* Emulation: general */

        {Q_OPTION_80_COLUMNS, NULL, "80_columns", "true", ""
"### EMULATION: GENERAL ---------------------------------------------------\n"
"\n"
"### Whether or not ANSI, AVATAR, and TTY emulations assume 80 columns.\n"
"### Value is 'true' or 'false'.\n"
"###\n"
"### 'true' means lines will wrap properly (if line wrap is enabled) at\n"
"### column 80.  This is often needed when connecting to text-based BBSes\n"
"### with classic ANSI art screens."},

        {Q_OPTION_ENQ_ANSWERBACK, NULL, "enq_response", "", ""
"### The string to respond with after receiving the ASCII ENQ (0x05, ^E).\n"
"### Value is a string.\n"
"###\n"
"### Many terminals can respond to a received ENQ with a user-provided\n"
"### string.  This was typically used for logging terminal identity and\n"
"### determining if it is still present.  Very few modern applications make\n"
"### use of this function, so most emulators return nothing (e.g. empty\n"
"### string)."},

/* Emulation: ANSI */

        {Q_OPTION_ANSI_MUSIC, NULL, "ansi_music", "true", ""
"### EMULATION: ANSI ------------------------------------------------------\n"
"\n"
"### Whether or not ANSI music is enabled on startup.  Value is 'true'\n"
"### or 'false'."},

        {Q_OPTION_ANSI_ANIMATE, NULL, "ansi_animate", "false", ""
"### Whether or not ANSI should update the screen quickly to support\n"
"### animation.  Value is 'true' or 'false'.\n"
"###\n"
"### 'true' means that ANSI emulation will update the screen much more often,\n"
"### resulting in better animation sequences at a high performance penalty.\n"
"### 'false' means buffer ANSI output like all other emulations."},

/* Emulation: AVATAR */

        {Q_OPTION_AVATAR_COLOR, NULL, "avatar_ansi_color", "true", ""
"### EMULATION: AVATAR ----------------------------------------------------\n"
"\n"
"### Whether or not ANSI.SYS-style color selection commands will be\n"
"### supported with the AVATAR emulation.  Value is 'true' or 'false'.\n"
"###\n"
"### Avatar emulation has its own color selection command, but some\n"
"### programs (like 'ls') send it ANSY.SYS-style color commands\n"
"### instead.  If this value is set to true the AVATAR emulation will\n"
"### honor the ANSI.SYS-style color selection codes.  If this value is\n"
"### false the color selection codes will be visible in the output, as a\n"
"### real Avatar-only emulator would do."},

         {Q_OPTION_AVATAR_ANSI_FALLBACK, NULL,
          "avatar_ansi_fallback", "true", ""
"### Whether or not AVATAR will fallback to ANSI.SYS for anything it does not\n"
"### recognize.  Value is 'true' or 'false'.\n"
"###\n"
"### The Avatar emulation standard does not require fallback to ANSI, but\n"
"### most BBS clients either supported fallback or emitted unknown sequences\n"
"### to the DOS console where ANSI.SYS would act on them.\n"
"###\n"
"### If this value is set to true the AVATAR emulation will recognize both\n"
"### Avatar and ANSI.  If this value is false then unknown ANSI sequences\n"
"### will be visible in the output, as a real Avatar-only emulator would do."},

/* Emulation: PETSCII */

         {Q_OPTION_PETSCII_C64, NULL, "petscii_c64", "true", ""
"### EMULATION: PETSCII ---------------------------------------------------\n"
"\n"
"### PETSCII supports two modes: Commodore 64, and 40-column Commodore 128.\n"
"### This option allows one to select between these modes.  True means behave\n"
"### as a Commodore 64; false means behave like a 40-column Commodore 128."},

        {Q_OPTION_PETSCII_COLOR, NULL, "petscii_ansi_color", "true", ""
"### Whether or not ANSI.SYS-style color selection commands will be\n"
"### supported with the PETSCII emulation.  Value is 'true' or 'false'.\n"
"###\n"
"### PETSCII emulation has its own color selection command, but some\n"
"### programs (like 'ls') may send it ANSY.SYS-style color commands\n"
"### instead.  If this value is set to true the PETSCII emulation will\n"
"### honor the ANSI.SYS-style color selection codes.  If this value is\n"
"### false the color selection codes will be visible in the output, as a\n"
"### real Commodore would do."},

         {Q_OPTION_PETSCII_ANSI_FALLBACK, NULL,
          "petscii_ansi_fallback", "true", ""
"### Whether or not PETSCII will fallback to ANSI.SYS for anything it does\n"
"### not recognize.  Value is 'true' or 'false'.\n"
"###\n"
"### Real Commodore machines do not recognize ANSI style escape sequences,\n"
"### but many BBS systems send them anyway.  This feature enables PETSCII to\n"
"### use both the Commodore and ANSI color and cursor movement commands,\n"
"### which for most people will lead to a smoother experience.\n"
"###\n"
"### If this value is set to true the PETSCII emulation will recognize both\n"
"### PETSCII and ANSI.  If this value is false then unknown ANSI sequences\n"
"### will be visible in the output, as a real Commodore would do."},

         {Q_OPTION_PETSCII_WIDE_FONT, NULL, "petscii_has_wide_font", "true", ""
"### Whether or not PETSCII is being used with a 40-column font (e.g. C64\n"
"### Pro Mono).  Value is 'true' or 'false'.\n"
"###\n"
"### Qodem's PETSCII emulation is intended to work with C64 Pro Mono,\n"
"### available at http://style64.org/c64-truetype/ .  (Run Qodem inside\n"
"### xterm via 'xterm -fn \"C64 Pro Mono\"', or on Windows switch font, to\n"
"### see this.)\n"
"###\n"
"### This font is already at the 40-column aspect ratio (square glyphs), so\n"
"### Qodem does not need to enable double-width lines.\n"
"###\n"
"### If you see either large gaps between characters, or very narrow\n"
"### characters squished onto the left side, try changing this option."},

         {Q_OPTION_PETSCII_UNICODE, NULL, "petscii_use_unicode", "false", ""
"### Whether or not PETSCII is being used with an 80-column Unicode font.\n"
"### Value is 'true' or 'false'.\n"
"###\n"
"### Qodem's PETSCII emulation is intended to work with C64 Pro Mono,\n"
"### available at http://style64.org/c64-truetype/ .  However, it can be\n"
"### inconvenient to switch to this font just for PETSCII systems.  This\n"
"### option permits Qodem to map byte to the relatively few PETSCII glyphs\n"
"### that are defined in Unicode, enabling the use of a typical Unicode font\n"
"### like Terminus or uni_vga.\n"
"###\n"
"### Note that enabling this option also disables the 'petscii_has_wide_font'\n"
"### option above.\n"},

/* Emulation: ATASCII */

         {Q_OPTION_ATASCII_WIDE_FONT, NULL, "atascii_has_wide_font", "false", ""
"### EMULATION: ATASCII ---------------------------------------------------\n"
"\n"
"### Whether or not ATASCII is being used with a 40-column font.  Value is\n"
"### 'true' or 'false'.\n"
"###\n"
"### If you see either large gaps between characters, or very narrow\n"
"### characters squished onto the left side, try changing this option."},

/* Emulation: VT52 */

        {Q_OPTION_VT52_COLOR, NULL, "vt52_ansi_color", "true", ""
"### EMULATION: VT52 ------------------------------------------------------\n"
"\n"
"### Whether or not ANSI.SYS-style color selection commands will be "
"supported\n"
"### with the VT52 emulation.  Value is 'true' or 'false'.\n"
"###\n"
"### Real VT52 applications are in black and white only.  However, some\n"
"### host application send color selection commands despite the fact the\n"
"### VT52 terminfo/terminfo entry lacks these codes.  ('ls' is one notable\n"
"### example.)  If this value is set to true the VT52 emulator will honor\n"
"### the color selection codes.  If this value is false the VT52 emulator\n"
"### will show the broken escape codes on the screen as (presumably) a real\n"
"### VT52 would do."},

/* Emulation: VT100 */

        {Q_OPTION_VT100_COLOR, NULL, "vt100_ansi_color", "true", ""
"### EMULATION: VT100 -----------------------------------------------------\n"
"\n"
"### Whether or not ANSI.SYS-style color selection commands will be\n"
"### supported with the VT100, VT102, and VT220 emulations.  Value is\n"
"### 'true' or 'false'.\n"
"###\n"
"### Real VT100, VT102, and VT220 applications are in black and white\n"
"### only.  However, some host applications send color selection commands\n"
"### despite the fact the termcap/terminfo entry lacks these codes.\n"
"### If this value is set to true the VT100, VT102, and VT220 emulation\n"
"### will honor the color selection codes.  If this value is false the\n"
"### color selection codes will be quietly consumed, as a real VT100-ish\n"
"### terminal would do."},

         {Q_OPTION_XTERM_MOUSE_REPORTING, NULL, "xterm_mouse_reporting", "true", ""
"### EMULATION: XTERM -----------------------------------------------------\n"
"\n"
"### Whether to support xterm mouse reporting at all.  Note that this only\n"
"### affects XTERM and X_UTF8 emulations.  Mouse reporting is never sent to\n"
"### the remote side for non-xterm emulations.  Value is 'true' or 'false'."},

/* Communication protocol: SSH */

#ifdef Q_PDCURSES_WIN32
        /* Win32: use internal ssh by default */
        {Q_OPTION_SSH_EXTERNAL, NULL, "use_external_ssh", "false", ""
#else
        /* Normal: use external ssh by default */
        {Q_OPTION_SSH_EXTERNAL, NULL, "use_external_ssh", "true", ""
#endif
"### COMMUNICATION PROTOCOL: SSH ------------------------------------------\n"
"\n"
"### Whether or not to use an external ssh connection program.\n"
"### 'true' means use an external ssh command, 'false' means use our\n"
"### own internal ssh code.  The default on Win32 is 'false' because\n"
"### Windows does not have its own ssh client.  However, for all\n"
"### other systems the default is 'true' because those systems\n"
"### already provide a client that has regular security updates."},

        {Q_OPTION_SSH, NULL, "ssh", "ssh -e none $REMOTEHOST -p $REMOTEPORT", ""
"### The ssh connection program.  Examples: /bin/ssh /usr/local/bin/ssh2\n"
"###\n"
"### The default value includes the -e none option to disable the escape\n"
"### character.  This arguments works for the Debian Linux OpenSSH 3.8\n"
"### client.  You may have to change it for your client.\n"
"###\n"
"### $REMOTEHOST will be replaced with the phonebook address,\n"
"### $REMOTEPORT will be replaced with the phonebook port."},

        {Q_OPTION_SSH_USER, NULL, "ssh_user",
         "ssh -e none -l $USERNAME -p $REMOTEPORT $REMOTEHOST", ""
"### The ssh connection program when the phonebook username is set.\n"
"###\n"
"### The default value includes the -e none option to disable the escape\n"
"### character.  This arguments works for the Debian Linux OpenSSH 3.8\n"
"### client.  You may have to change it for your client.\n"
"###\n"
"### $USERNAME will be replaced with the phonebook username, $REMOTEHOST\n"
"### will be replaced with the phonebook address, and $REMOTEPORT\n"
"### will be replaced with the phonebook port."},

#ifdef Q_PDCURSES_WIN32
        {Q_OPTION_SSH_KNOWNHOSTS, NULL, "knownhosts_file",
         "$HOME\\qodem\\prefs\\known_hosts", ""
#else
        {Q_OPTION_SSH_KNOWNHOSTS, NULL, "knownhosts_file",
         "$HOME/.qodem/known_hosts", ""
#endif
"### The location of the SSH known_hosts file.  The $HOME environment\n"
"### variable will be substituted if specified."},

/* Communication protocol: RLOGIN */

#ifdef Q_PDCURSES_WIN32
        /* Win32: use internal rlogin by default */
        {Q_OPTION_RLOGIN_EXTERNAL, NULL, "use_external_rlogin", "false", ""
#else
        /* Normal: use external rlogin by default */
        {Q_OPTION_RLOGIN_EXTERNAL, NULL, "use_external_rlogin", "true", ""
#endif
"### COMMUNICATION PROTOCOL: RLOGIN ---------------------------------------\n"
"\n"
"### Whether or not to use an external rlogin connection program.\n"
"### 'true' means use an external rlogin command, 'false' means use our\n"
"### own internal rlogin code.  The default on Win32 is 'false' because\n"
"### Windows does not have its own rlogin client.  However, for all\n"
"### other systems the default is 'true' because rlogin must originate\n"
"### from a privileged port, something only a root user can do."},

        {Q_OPTION_RLOGIN, NULL, "rlogin", "rlogin $REMOTEHOST", ""
"### The rlogin connection program.  Examples: /bin/rlogin\n"
"### /usr/local/bin/rlogin\n"
"###\n"
"### $REMOTEHOST will be replaced with the phonebook address."},

        {Q_OPTION_RLOGIN_USER, NULL, "rlogin_user",
         "rlogin -l $USERNAME $REMOTEHOST", ""
"### The rlogin connection program to use when the phonebook username is set.\n"
"###\n"
"### $USERNAME will be replaced with the phonebook username and $REMOTEHOST\n"
"### will be replaced with the phonebook address."},

/* Communication protocol: TELNET */

        {Q_OPTION_TELNET_EXTERNAL, NULL, "use_external_telnet", "false", ""
"### COMMUNICATION PROTOCOL: TELNET ---------------------------------------\n"
"\n"
"### Whether or not to use an external telnet connection program.\n"
"### 'true' means use an external telnet command, 'false' means use our\n"
"### own internal telnet code."},

        {Q_OPTION_TELNET, NULL, "telnet",
         "telnet -E -8 $REMOTEHOST $REMOTEPORT", ""
"### The external telnet connection program.  Examples:\n"
"### /bin/telnet /usr/local/bin/telnet\n"
"###\n"
"### The default value includes the -E option to disable the escape\n"
"### character and the -8 option to negotiate an 8-bit connection.\n"
"### These arguments work for the Debian Linux telnet client.  You may\n"
"### have to change it for more traditional Unix-like operating system\n"
"### clients.\n"
"###\n"
"### $REMOTEHOST will be replaced with the phonebook address,\n"
"### $REMOTEPORT will be replaced with the phonebook port."},

/* File transfer protocol: ASCII */

        {Q_OPTION_ASCII_UPLOAD_USE_TRANSLATE_TABLE, NULL,
         "ascii_upload_use_xlate_table", "true", ""
"### FILE TRANSFER PROTOCOL: ASCII ----------------------------------------\n"
"\n"
"### Whether or not the ASCII translate table function should be used\n"
"### during ASCII file uploads.  Value is 'true' or 'false'.\n"
"###\n"
"### When true, outgoing bytes will first be translated according to the\n"
"### table and then sent to the remote system."},

        {Q_OPTION_ASCII_UPLOAD_CR_POLICY, NULL, "ascii_upload_cr_policy",
         "none", ""
"### How to handle outgoing carriage-return characters (0x0D)\n"
"### during ASCII file uploads.  Value is 'none', 'strip', or 'add'.\n"
"###\n"
"### 'none' means do nothing to change the bytes sent.\n"
"### 'strip' means remove carriage-returns while sending the file.\n"
"### 'add' means add a linefeed character (0x0A) after each carriage-"
"return\n"
"### while sending the file."},

        {Q_OPTION_ASCII_UPLOAD_LF_POLICY, NULL, "ascii_upload_lf_policy",
         "none", ""
"### How to handle outgoing linefeed characters (0x0A) during ASCII file\n"
"### uploads.  Value is 'none', 'strip', or 'add'.\n"
"###\n"
"### 'none' means do nothing to change the bytes sent.\n"
"### 'strip' means remove linefeeds while sending the file.\n"
"### 'add' means add a carriage-return character (0x0D) before each "
"linefeed\n"
"### while sending the file."},

        {Q_OPTION_ASCII_DOWNLOAD_USE_TRANSLATE_TABLE, NULL,
         "ascii_download_use_xlate_table", "true", ""
"### Whether or not the ASCII translate table function should be used\n"
"### during ASCII file downloads.  Value is 'true' or 'false'.\n"
"###\n"
"### When true, incoming bytes will be translated according to the table\n"
"### before being saved to file."},

        {Q_OPTION_ASCII_DOWNLOAD_CR_POLICY, NULL, "ascii_download_cr_policy",
         "none", ""
"### How to handle incoming carriage-return characters (0x0D)\n"
"### during ASCII file downloads.  Value is 'none', 'strip', or 'add'.\n"
"###\n"
"### 'none' means do nothing to change the bytes saved.\n"
"### 'strip' means remove carriage-returns while saving the file.\n"
"### 'add' means add a linefeed character (0x0A) after each carriage-"
"return\n"
"### while saving the file."},

        {Q_OPTION_ASCII_DOWNLOAD_LF_POLICY, NULL, "ascii_download_lf_policy",
         "none", ""
"### How to handle incoming linefeed characters (0x0A) during ASCII file\n"
"### downloads.  Value is 'none', 'strip', or 'add'.\n"
"###\n"
"### 'none' means do nothing to change the bytes saved.\n"
"### 'strip' means remove linefeeds while saving the file.\n"
"### 'add' means add a carriage-return character (0x0D) before each "
"linefeed\n"
"### while saving the file."},

/* File transfer protocol: ZMODEM */

        {Q_OPTION_ZMODEM_AUTOSTART, NULL, "zmodem_autostart", "true", ""
"### FILE TRANSFER PROTOCOL: ZMODEM ---------------------------------------\n"
"\n"
"### Whether or not Zmodem autostart should be used.\n"
"### Value is 'true' or 'false'.\n"
"###\n"
"### 'true' means Zmodem autostart will be enabled.\n"
"### 'false' means Zmodem autostart will not be enabled."},

        {Q_OPTION_ZMODEM_ZCHALLENGE, NULL, "zmodem_zchallenge", "false", ""
"### Whether or not Zmodem will issue ZCHALLENGE at the beginning.\n"
"### of each transfer.  ZCHALLENGE was meant to improve security\n"
"### but some Zmodem clients do not support it.  Its security\n"
"### benefits are dubious.\n"
"### Value is 'true' or 'false'.\n"
"###\n"
"### 'true' means Zmodem will issue a ZCHALLENGE.\n"
"### 'false' means Zmodem will not issue a ZCHALLENGE."},

        {Q_OPTION_ZMODEM_ESCAPE_CTRL, NULL, "zmodem_escape_control_chars",
         "false", ""
"### Whether or not Zmodem should escape control characters by default.\n"
"### Value is 'true' or 'false'.\n"
"###\n"
"### 'true' means Zmodem will escape control characters, which will\n"
"### make file transfers slower but may be necessary for Zmodem to\n"
"### work at all over the link.\n"
"### 'false' means Zmodem will not escape control characters.\n"
"### \n"
"### In both cases, Zmodem will honor the encoding requested at the\n"
"### other end.\n"
"### \n"
"### The Zmodem protocol defines this as an available option, and Qodem\n"
"### supports it for completeness.  However, (l)rzsz never used it, and\n"
"### enabling this option causes uploads to (l)rzsz to fail.\n"
"### \n"
"### DO NOT SET THIS TO 'true' UNLESS YOU ARE TESTING ANOTHER ZMODEM\n"
"### IMPLEMENTATION."},

/* File transfer protocol: KERMIT */

        {Q_OPTION_KERMIT_AUTOSTART, NULL, "kermit_autostart", "true", ""
"### FILE TRANSFER PROTOCOL: KERMIT ---------------------------------------\n"
"\n"
"### Whether or not Kermit autostart should be enabled by default.\n"
"### Value is 'true' or 'false'.\n"
"###\n"
"### 'true' means Kermit autostart will be enabled on startup.\n"
"### 'false' means Kermit autostart will not be enabled on startup."},

        {Q_OPTION_KERMIT_ROBUST_FILENAME, NULL, "kermit_robust_filename",
         "false", ""
"### Whether or not Kermit should use common form filenames.\n"
"### Value is 'true' or 'false'.\n"
"###\n"
"### 'true' means Kermit uploads will convert filenames to uppercase,\n"
"### remove all but one period, and remove many punctuation characters.\n"
"### 'false' means Kermit uplods will use the literal filename."},

        {Q_OPTION_KERMIT_STREAMING, NULL, "kermit_streaming", "true", ""
"### Whether or not Kermit should use streaming (sending all file data\n"
"### packets continuously without waiting for ACKs).\n"
"### Value is 'true' or 'false'.\n"
"###\n"
"### 'true' means Kermit will use streaming, resulting in a significant\n"
"### performance improvement in most cases, especially over TCP links.\n"
"### 'false' means Kermit will not use streaming."},

        {Q_OPTION_KERMIT_UPLOADS_FORCE_BINARY, NULL,
         "kermit_uploads_force_binary", "true", ""
"### Whether or not Kermit uploads will transfer files as 8-bit binary files.\n"
"### Value is 'true' or 'false'.\n"
"###\n"
"### 'true' means Kermit uploads will transfer all files (including text\n"
"### files) in binary.\n"
"### 'false' means Kermit will convert text files to CRLF format, but\n"
"### leave binary files as-is.  Note that Qodem's kermit checks the first\n"
"### 1024 bytes of the file, and if it sees only ASCII characters assumes\n"
"### the file is text; this heuristic might occasionally mis-identify files."},

        {Q_OPTION_KERMIT_DOWNLOADS_CONVERT_TEXT, NULL,
         "kermit_downloads_convert_text", "false", ""
"### Whether or not Kermit downloads will convert text files to the local\n"
"### end-of-line convention (e.g. CRLF -> LF).\n"
"### Value is 'true' or 'false'.\n"
"###\n"
"### 'true' means Kermit downloads will convert CRLF to LF.\n"
"### 'false' means Kermit will leave text files in the format sent, usually\n"
"### CRLF."},

        {Q_OPTION_KERMIT_RESEND, NULL, "kermit_resend", "true", ""
"### Whether or not Kermit uploads should RESEND by default.  The RESEND\n"
"### option appends data to existing files.  Most of the time this results\n"
"### file transfers resuming where they left off, similar to Zmodem crash\n"
"### recovery.\n"
"### Value is 'true' or 'false'.\n"
"###\n"
"### 'true' means all Kermit uploads will use RESEND.\n"
"### 'false' means Kermit uploads will use SEND."},

         {Q_OPTION_KERMIT_LONG_PACKETS, NULL, "kermit_long_packets", "true", ""
"### Whether or not Kermit should use long packets.  On very noisy channels,\n"
"### Kermit may need to use short packets to get through.\n"
"### Value is 'true' or 'false'.\n"
"###\n"
"### 'true' means Kermit will use long packets, up to 1k.\n"
"### 'false' means Kermit will use short packets, up to 96 bytes."},

    {Q_OPTION_NULL, NULL, NULL, NULL}
};

/**
 * Replace all instances of "pattern" in "original" with "new_string",
 * returning a newly-allocated string.
 *
 * @param original the original string
 * @param pattern the pattern in original string to replace
 * @param new_string the string to replace pattern with
 * @return a newly-allocated string
 */
char * substitute_string(const char * original, const char * pattern,
                         const char * new_string) {
    int i;
    /*
     * Cast to avoid compiler warning
     */
    char * current_place = (char *) original;
    char * old_place;
    char * return_string;
    int return_string_place;

    /*
     * Count all occurrences
     */
    i = 0;
    while (current_place[0] != '\0') {
        current_place = strstr(current_place, pattern);
        if (current_place == NULL) {
            break;
        } else {
            i++;
            current_place += strlen(pattern);
        }
    }

    /*
     * i contains the # of occurrences of pattern.
     */
    return_string =
        (char *) Xmalloc(1 + strlen(original) +
                         (i * (strlen(new_string) - strlen(pattern))), __FILE__,
                         __LINE__);
    memset(return_string, 0,
           1 + strlen(original) + (i * (strlen(new_string) - strlen(pattern))));
    /*
     * Cast to avoid compiler warning
     */
    current_place = (char *) original;
    old_place = current_place;
    return_string_place = 0;
    while (i > 0) {
        current_place = strstr(old_place, pattern);
        /*
         * Copy part before pattern
         */
        strncpy(return_string + return_string_place, old_place,
                current_place - old_place);
        return_string_place += current_place - old_place;
        old_place = current_place;
        /*
         * Copy new string
         */
        strncpy(return_string + return_string_place, new_string,
                strlen(new_string));
        return_string_place += strlen(new_string);
        old_place += strlen(pattern);
        current_place = old_place;
        i--;
    }
    /*
     * Copy terminating non-pattern part
     */
    strncpy(return_string + return_string_place, current_place,
            original + strlen(original) - current_place);

    return return_string;
}

/**
 * Replace all instances of "pattern" in "original" with "new_string",
 * returning a newly-allocated string.
 *
 * @param original the original string
 * @param pattern the pattern in original string to replace
 * @param new_string the string to replace pattern with
 * @return a newly-allocated string
 */
wchar_t * substitute_wcs(const wchar_t * original, const wchar_t * pattern,
                         const wchar_t * new_string) {
    int i;
    /*
     * Cast to avoid compiler warning
     */
    wchar_t * current_place = (wchar_t *) original;
    wchar_t * old_place;
    wchar_t * return_string;
    int return_string_place;

    /*
     * Count all occurrences
     */
    i = 0;
    while (current_place[0] != L'\0') {
        current_place = wcsstr(current_place, pattern);
        if (current_place == NULL) {
            break;
        } else {
            i++;
            current_place += wcslen(pattern);
        }
    }

    /*
     * i contains the # of occurrences of pattern.
     */
    return_string =
        (wchar_t *) Xmalloc((sizeof(wchar_t)) *
                            (1 + wcslen(original) +
                             (i * (wcslen(new_string) - wcslen(pattern)))),
                            __FILE__, __LINE__);
    wmemset(return_string, 0,
            1 + wcslen(original) +
            (i * (wcslen(new_string) - wcslen(pattern))));
    /*
     * Cast to avoid compiler warning
     */
    current_place = (wchar_t *) original;
    old_place = current_place;
    return_string_place = 0;
    while (i > 0) {
        current_place = wcsstr(old_place, pattern);
        /*
         * Copy part before pattern
         */
        wcsncpy(return_string + return_string_place, old_place,
                current_place - old_place);
        return_string_place += current_place - old_place;
        old_place = current_place;
        /*
         * Copy new string
         */
        wcsncpy(return_string + return_string_place, new_string,
                wcslen(new_string));
        return_string_place += wcslen(new_string);
        old_place += wcslen(pattern);
        current_place = old_place;
        i--;
    }
    /*
     * Copy terminating non-pattern part
     */
    wcsncpy(return_string + return_string_place, current_place,
            original + wcslen(original) - current_place);

    return return_string;
}

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
char * substitute_wcs_half(const char * original, const char * pattern,
                           const wchar_t * new_string) {
    int i;
    /*
     * Cast to avoid compiler warning
     */
    char * current_place = (char *) original;
    char * old_place;
    char * return_string;
    int return_string_place;
    char new_str[256];

    /*
     * Convert to UTF-8
     */
    memset(new_str, 0, sizeof(new_str));
    wcstombs(new_str, new_string, wcslen(new_string));

    /*
     * Count all occurrences
     */
    i = 0;
    while (current_place[0] != '\0') {
        current_place = strstr(current_place, pattern);
        if (current_place == NULL) {
            break;
        } else {
            i++;
            current_place++;
        }
    }

    /*
     * i contains the # of occurrences of pattern.
     */
    return_string =
        (char *) Xmalloc(1 + strlen(original) +
                         (i * (strlen(new_str) - strlen(pattern))), __FILE__,
                         __LINE__);
    memset(return_string, 0,
           1 + strlen(original) + (i * (strlen(new_str) - strlen(pattern))));
    /*
     * Cast to avoid compiler warning
     */
    current_place = (char *) original;
    old_place = current_place;
    return_string_place = 0;
    while (i > 0) {
        current_place = strstr(old_place, pattern);
        /*
         * Copy part before pattern
         */
        strncpy(return_string + return_string_place, old_place,
                current_place - old_place);
        return_string_place += current_place - old_place;
        old_place = current_place;
        /*
         * Copy new string
         */
        strncpy(return_string + return_string_place, new_str, strlen(new_str));
        return_string_place += strlen(new_str);
        old_place += strlen(pattern);
        current_place = old_place;
        i--;
    }
    /*
     * Copy terminating non-pattern part
     */
    strncpy(return_string + return_string_place, current_place,
            original + strlen(original) - current_place);

    return return_string;
}

/**
 * Get an option value.  Note that the string returned is not
 * newly-allocated, i.e. do not free it later.
 *
 * @param option the option
 * @return the option value from the config file
 */
char * get_option(const Q_OPTION option) {
    struct option_struct * current_option = options;
    while (current_option->option != Q_OPTION_NULL) {
        if (current_option->option == option) {
            return current_option->value;
        }
        current_option++;
    }
    return "";
}

/**
 * Get the long description for an option.  The help system uses this to
 * automatically generate a help screen out of the options descriptions.
 *
 * @param option the option
 * @return the option description
 */
const char * get_option_description(const Q_OPTION option) {
    struct option_struct * current_option = options;
    while (current_option->option != Q_OPTION_NULL) {
        if (current_option->option == option) {
            return current_option->comment;
        }
        current_option++;
    }
    return "";
}

/**
 * Get the key for an option.  The help system uses this to automatically
 * generate a help screen out of the options descriptions.
 *
 * @param option the option
 * @return the option key
 */
const char * get_option_key(const Q_OPTION option) {
    struct option_struct * current_option = options;
    while (current_option->option != Q_OPTION_NULL) {
        if (current_option->option == option) {
            return current_option->name;
        }
        current_option++;
    }
    return "";
}

/**
 * Get the default value for an option.  The help system uses this to
 * automatically generate a help screen out of the options descriptions.
 *
 * @param option the option
 * @return the option default value
 */
const char * get_option_default(const Q_OPTION option) {
    struct option_struct * current_option = options;
    while (current_option->option != Q_OPTION_NULL) {
        if (current_option->option == option) {
            return current_option->default_value;
        }
        current_option++;
    }
    return "";
}

/**
 * Save options to a file.
 *
 * @param filename file to save to
 * @return true if successful
 */
Q_BOOL save_options(const char * filename) {
    struct option_struct * current_option;
    FILE * file;
    int rc;
    char time_string[TIME_STRING_LENGTH];
    time_t current_time;

    if (q_status.read_only == Q_TRUE) {
        return Q_FALSE;
    }

    file = fopen(filename, "w");
    if (file == NULL) {
        fprintf(stderr, _("Error opening file \"%s\" for writing: %s"),
                filename, strerror(errno));
        return Q_FALSE;
    }

    /*
     * Standard header.
     */
    time(&current_time);
    strftime(time_string, sizeof(time_string), _("%a, %d %b %Y %H:%M:%S %z"),
        localtime(&current_time));
    fprintf(file, _("### Qodem " Q_VERSION " options file generated %s\n"),
        time_string);
    fprintf(file, "###\n");
#ifdef Q_PDCURSES_WIN32
    fprintf(file, "### This file is typically at My Documents\\qodem\\prefs\\qodemrc.txt\n");
#else
    fprintf(file, "### This file is typically at ~/.qodem/qodemrc\n");
#endif
    fprintf(file, "### ------------------------------------------------------"
        "-----------------");
    fprintf(file, "\n\n");

    /*
     * Emit each option, its description and default value.
     */
    current_option = options;
    while (current_option->option != Q_OPTION_NULL) {
        rc = fwrite(current_option->comment, strlen(current_option->comment), 1,
                    file);
        if (rc == -1) {
            fprintf(stderr, _("Error writing to file \"%s\": %s"), filename,
                    strerror(errno));
            return Q_FALSE;
        }
        rc = fwrite("\n###", 4, 1, file);
        if (rc == -1) {
            fprintf(stderr, _("Error writing to file \"%s\": %s"), filename,
                    strerror(errno));
            return Q_FALSE;
        }
        rc = fprintf(file, _("\n### Default value: "));
        if (rc == -1) {
            fprintf(stderr, _("Error writing to file \"%s\": %s"), filename,
                    strerror(errno));
            return Q_FALSE;
        }
        rc = fwrite(current_option->default_value,
                    strlen(current_option->default_value), 1, file);
        if (rc == -1) {
            fprintf(stderr, _("Error writing to file \"%s\": %s"), filename,
                    strerror(errno));
            return Q_FALSE;
        }
        if (strncmp
            (current_option->value, current_option->default_value,
             strlen(current_option->default_value)) == 0) {
            rc = fwrite("\n### ", 5, 1, file);
            if (rc == -1) {
                fprintf(stderr, _("Error writing to file \"%s\": %s"), filename,
                        strerror(errno));
                return Q_FALSE;
            }
        } else {
            rc = fwrite("\n", 1, 1, file);
            if (rc == -1) {
                fprintf(stderr, _("Error writing to file \"%s\": %s"), filename,
                        strerror(errno));
                return Q_FALSE;
            }
        }
        rc = fwrite(current_option->name, strlen(current_option->name), 1,
                    file);
        if (rc == -1) {
            fprintf(stderr, _("Error writing to file \"%s\": %s"), filename,
                    strerror(errno));
            return Q_FALSE;
        }
        rc = fwrite(" = ", 3, 1, file);
        if (rc == -1) {
            fprintf(stderr, _("Error writing to file \"%s\": %s"), filename,
                    strerror(errno));
            return Q_FALSE;
        }
        rc = fwrite(current_option->value, strlen(current_option->value), 1,
                    file);
        if (rc == -1) {
            fprintf(stderr, _("Error writing to file \"%s\": %s"), filename,
                    strerror(errno));
            return Q_FALSE;
        }
        rc = fwrite("\n\n\n", 3, 1, file);
        if (rc == -1) {
            fprintf(stderr, _("Error writing to file \"%s\": %s"), filename,
                    strerror(errno));
            return Q_FALSE;
        }

        current_option++;
    }

    fclose(file);

    return Q_TRUE;
}

/**
 * Set an option's value.  This allocates a new copy of value.
 *
 * @param option the option to set
 * @param value the new value
 */
static void set_option(struct option_struct * option, char * value) {
    char * new_value = value;
    while (((q_isspace(new_value[0])) || (new_value[0] == '='))
           && (strlen(new_value) > 0)) {
        new_value++;
    }
    new_value = Xstrdup(new_value, __FILE__, __LINE__);;
    if (option->value != NULL) {
        Xfree(option->value, __FILE__, __LINE__);
    }
    option->value = new_value;
}

/**
 * Perform option-specific substitutions for $HOME and $EDITOR.
 *
 * @param option the option to check
 */
static void check_option(struct option_struct * option) {
    char * env_string;
    char * new_value;

    /*
     * Set a default value to empty string so our calls to
     * substitute_string() can have an Xfree() call.
     */
    if (option->value == NULL) {
        option->value = Xstrdup("", __FILE__, __LINE__);
    }

    switch (option->option) {

    case Q_OPTION_WORKING_DIR:
    case Q_OPTION_HOST_DIR:
    case Q_OPTION_SCRIPTS_DIR:
    case Q_OPTION_BATCH_ENTRY_FILE:
    case Q_OPTION_UPLOAD_DIR:
    case Q_OPTION_DOWNLOAD_DIR:
    case Q_OPTION_SSH_KNOWNHOSTS:
        env_string = get_home_directory();
        if (env_string == NULL) {
            env_string = "";
        }
        /*
         * Sustitute for $HOME
         */
        new_value = substitute_string(option->value, "$HOME", env_string);
        Xfree(option->value, __FILE__, __LINE__);
        option->value = new_value;
        break;
    case Q_OPTION_EDITOR:
        env_string = getenv("EDITOR");
        if (env_string == NULL) {
            env_string = "";
        }
        /*
         * Sustitute for $EDITOR
         */
        new_value = substitute_string(option->value, "$EDITOR", env_string);
        Xfree(option->value, __FILE__, __LINE__);
        option->value = new_value;
        break;
    case Q_OPTION_SHELL:
        env_string = getenv("SHELL");
        if (env_string == NULL) {
            env_string = "";
        }
        /*
         * Sustitute for $EDITOR
         */
        new_value = substitute_string(option->value, "$SHELL", env_string);
        Xfree(option->value, __FILE__, __LINE__);
        option->value = new_value;
        break;
    default:
        break;
    }
}

/**
 * Load options from a file.
 *
 * @param filename file to read from
 */
static void load_options_from_file(const char * filename) {
    struct option_struct * current_option;
    FILE * file;
    char line[OPTIONS_LINE_SIZE];
    char * line_begin;

    file = fopen(filename, "r");
    if (file == NULL) {
        fprintf(stderr, _("Error opening file \"%s\" for reading: %s"),
                filename, strerror(errno));
        return;
    }

    while (!feof(file)) {
        memset(line, 0, sizeof(line));
        if (fgets(line, sizeof(line), file) == NULL) {
            if (!feof(file)) {
                fprintf(stderr, _("Error reading from file \"%s\": %s"),
                        filename, strerror(errno));
            }
            break;
        }

        line_begin = line;

        /*
         * Trim whitespace from line
         */
        while ((strlen(line_begin) > 0)
               && q_isspace(line_begin[strlen(line_begin) - 1])) {
            line_begin[strlen(line_begin) - 1] = '\0';
        }
        while ((q_isspace(*line_begin)) && (strlen(line_begin) > 0)) {
            line_begin++;
        }

        if (*line_begin == '#') {
            /*
             * Skip comment lines
             */
            continue;
        }

        /*
         * Look for option this line changes
         */
        current_option = options;
        while (current_option->option != Q_OPTION_NULL) {
            if (strstr(line, current_option->name) == line) {
                if ((line[strlen(current_option->name)] == '=') ||
                    (q_isspace(line[strlen(current_option->name)]))) {

                    /*
                     * Valid option
                     */
                    set_option(current_option,
                               line + strlen(current_option->name));
                }
            }
            check_option(current_option);
            current_option++;
        }
    }

    fclose(file);
}

/**
 * Get the full path to the options config file.
 *
 * @return the full path to qodemrc (usually ~/.qodem/qodemrc or My
 * Documents\\qodem\\prefs\\qodemrc.txt).
 */
char * get_options_filename() {
    return home_directory_options_filename;
}

/**
 * Find the option_struct from the Q_OPTION value.
 *
 * @param option the option enum
 * @return the options struct
 */
static struct option_struct * find_option(const Q_OPTION option) {
    struct option_struct * current_option;

    current_option = options;
    while (current_option->option != Q_OPTION_NULL) {
        if (current_option->option == option) {
            return current_option;
        }
        current_option++;
    }
    return NULL;
}

/**
 * Create a directory, using either Windows or POSIX calls.  This will also
 * create any directories that are missing in the middle.
 *
 * @param path the directory path name
 * @return true if successful
 */
static Q_BOOL create_directory(const char * path) {
    char * path_copy;
    char * parent_dir;
#ifdef Q_PDCURSES_WIN32
        BOOL rc;
#else
        int i;
#endif

    if (q_status.read_only == Q_TRUE) {
        return Q_FALSE;
    }

    assert(directory_exists(path) == Q_FALSE);
    if (file_exists(path) == Q_TRUE) {
        /*
         * We cannot create a directory when a file already exists there.
         */
        return Q_FALSE;
    }

    path_copy = Xstrdup(path, __FILE__, __LINE__);
    parent_dir = Xstrdup(dirname(path_copy), __FILE__, __LINE__);
    if (directory_exists(parent_dir) == Q_FALSE) {
        create_directory(parent_dir);
    }
    Xfree(parent_dir, __FILE__, __LINE__);
    Xfree(path_copy, __FILE__, __LINE__);

#ifdef Q_PDCURSES_WIN32
    rc = CreateDirectoryA(path, NULL);
    if (rc == TRUE) {
        return Q_TRUE;
    } else {
        return Q_FALSE;
    }
#else
    i = mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR);
    if (i == 0) {
        return Q_TRUE;
    } else {
        return Q_FALSE;
    }
#endif
}

/**
 * Create everything that needs to be in ~/.qodem.
 */
static void populate_dotqodem() {
    /*
     * Create the key bindings files
     */
    create_keybindings_files();

#ifndef Q_NO_SERIAL
    /*
     * Create the modem config file
     */
    create_modem_config_file();
#endif
}

/**
 * Create everything that needs to be in ~/qodem.
 */
static void check_and_create_directories() {
    Q_BOOL rc;
    char * working_dir;
    char notify_message[DIALOG_MESSAGE_SIZE];
    char * message_lines[10];
    char * env_string;
    char * substituted_filename;

    /*
     * We will use $HOME several times now, hang onto it.
     */
    env_string = get_home_directory();

    /*
     * Check for working directory
     */
    working_dir =
        substitute_string(get_option(Q_OPTION_WORKING_DIR), "$HOME",
                          env_string);

    if (directory_exists(working_dir) == Q_FALSE) {
        rc = create_directory(working_dir);
        if (rc == Q_FALSE) {
            snprintf(notify_message, sizeof(notify_message), _(""
                    "Could not create the directory %s."), working_dir);
            message_lines[0] = notify_message;
            message_lines[1] = _("You may have to specify full paths when you");
            message_lines[2] = _("download files, enable capture/log, etc."),
            notify_form_long(message_lines, 0, 3);
        }
    }
    /*
     * Free leak
     */
    Xfree(working_dir, __FILE__, __LINE__);

    /*
     * Check for host mode working directory
     */
    working_dir =
        substitute_string(get_option(Q_OPTION_HOST_DIR), "$HOME", env_string);
    if (directory_exists(working_dir) == Q_FALSE) {
        rc = create_directory(working_dir);
        if (rc == Q_FALSE) {
            snprintf(notify_message, sizeof(notify_message), _(""
                    "Could not create the directory %s."), working_dir);
            message_lines[0] = notify_message;
            message_lines[1] = _("Host mode may be unable to perform");
            message_lines[2] = _("uploads and downloads or messages"),
            notify_form_long(message_lines, 0, 3);
        }
    }
    /*
     * Free leak
     */
    Xfree(working_dir, __FILE__, __LINE__);

    /*
     * Check for scripts directory
     */
    working_dir =
        substitute_string(get_option(Q_OPTION_SCRIPTS_DIR), "$HOME",
                          env_string);
    if (directory_exists(working_dir) == Q_FALSE) {
        rc = create_directory(working_dir);
        if (rc == Q_FALSE) {
            snprintf(notify_message, sizeof(notify_message), _(""
"Could not create the directory %s.  Scripts might not work correctly."),
                working_dir);
            notify_form(notify_message, 0);
        }
    }
    /*
     * Free leak
     */
    Xfree(working_dir, __FILE__, __LINE__);

#ifndef Q_PDCURSES_WIN32
    /*
     * Check for the scripts stderr fifo.
     */
    substituted_filename =
        substitute_string(get_option(Q_OPTION_SCRIPTS_STDERR_FIFO), "$HOME",
                          env_string);
    if (access(substituted_filename, F_OK) != 0) {
        /*
         * Try to create it
         */
        if (q_status.read_only == Q_FALSE) {
            mkfifo(substituted_filename, S_IRUSR | S_IWUSR);
        }
    }
    /*
     * Free leak
     */
    Xfree(substituted_filename, __FILE__, __LINE__);
#endif

}

/**
 * Reset options to default state.
 */
void reset_options() {
    struct option_struct * current_option;
    current_option = options;
    while (current_option->option != Q_OPTION_NULL) {
        if (current_option->value != NULL) {
            Xfree(current_option->value, __FILE__, __LINE__);
        }
        current_option->value =
            Xstrdup(current_option->default_value, __FILE__, __LINE__);

        /*
         * Translate option help text to local language
         */
        current_option->comment = _(current_option->comment);
        current_option++;
    }
}

/**
 * This must be called to initialize the options list from the config file.
 *
 * Load options from all the files.  We search the following:
 *     $HOME/.qodemrc
 *     INSTALL_DIR/qodemrc
 *     /etc/qodemrc
 *     /usr/lib/qodem/qodemrc
 *     /usr/local/lib/qodem/qodemrc
 */
void load_options() {
    int i;
    Q_BOOL rc;
    int state;
    char * begin;
    unsigned char ch = 0;
    char * filenames[] = {
        INSTALL_DIR "/qodemrc",
        "/etc/qodemrc",
        "/usr/lib/qodem/qodemrc",
        "/usr/local/lib/qodem/qodemrc",

        /*
         * List $HOME last so that it overrides everything else.
         */
#ifdef Q_PDCURSES_WIN32
        "$HOME/qodem/prefs/qodemrc.txt",
#else
        "$HOME/.qodem/qodemrc",
#endif
        NULL
    };
    char * current_filename;
    char * env_string;
    char * substituted_filename;
    char * lang_default;
    char notify_message[DIALOG_MESSAGE_SIZE];
    char * message_lines[10];

    /*
     * Set default values.
     */
    reset_options();

    /*
     * It is main()'s job to set q_home_directory before calling
     * load_options().  Make sure that was done.
     */
    assert(q_home_directory != NULL);

    /*
     * Check for .qodem directory
     */
    if (directory_exists(q_home_directory) == Q_FALSE) {
        rc = create_directory(q_home_directory);
        if (rc == Q_FALSE) {
            snprintf(notify_message, sizeof(notify_message), _(""
                    "Could not create the directory %s."), q_home_directory);
            message_lines[0] = notify_message;
            message_lines[1] = _("You may have to specify full paths when");
            message_lines[2] = _("you load key bindings, phone books, etc."),
            notify_form_long(message_lines, 0, 3);
        } else {
            /*
             * Since we just created .qodem, we can now create everything in
             * it.
             */
            populate_dotqodem();
        }
    }

    /*
     * Special check: $HOME/.qodem/qodemrc.  If this doesn't exist, AND we
     * just created its directory, then we create it here before moving on.
     */
#ifdef Q_PDCURSES_WIN32
    substituted_filename =
    substitute_string("$HOME/qodemrc.txt", "$HOME", q_home_directory);
#else
    substituted_filename =
    substitute_string("$HOME/qodemrc", "$HOME", q_home_directory);
#endif
    if (file_exists(substituted_filename) == Q_FALSE) {
        /*
         * Set the ISO8859 and UTF8 lang variables in the new qodemrc based
         * on the LANG environment variable.
         */
        lang_default = getenv("LANG");
        if (lang_default == NULL) {
            /*
             * User doesn't have it set, so we use the defaults.
             */
        } else {
            if (strstr(lang_default, "UTF-8") == NULL) {
                /*
                 * User has LANG set, but it isn't UTF-8.  Use LANG for
                 * 8-bit, and default for UTF-8.
                 */
                set_option(find_option(Q_OPTION_ISO8859_LANG), lang_default);
            } else {
                /*
                 * User has LANG set, and it is UTF-8.  Use LANG for UTF-8,
                 * and default for 8-bit.
                 */
                set_option(find_option(Q_OPTION_UTF8_LANG), lang_default);
            }
        }

        /*
         * Save options
         */
        if (save_options(substituted_filename) == Q_FALSE) {
            snprintf(notify_message, sizeof(notify_message),
                _("Error saving default options to %s"), substituted_filename);
            notify_form(notify_message, 0);
        }
    }
    /*
     * Free leak
     */
    Xfree(substituted_filename, __FILE__, __LINE__);

    /*
     * We will use $HOME several times now, hang onto it.
     */
    env_string = get_home_directory();

    if (q_config_filename != NULL) {
        /*
         * The user specified --config on the command line.  Only read that
         * file, don't read any of the others.
         */
#ifdef Q_PDCURSES_WIN32
        if (file_exists(q_config_filename) == Q_TRUE) {
#else
        if (access(q_config_filename, F_OK | R_OK) == 0) {
#endif
            load_options_from_file(q_config_filename);
        }
        goto no_more_config_files;
    }

    /*
     * Read options from all the available files, overwriting them with each
     * read.
     */
    i = 0;
    for (current_filename = filenames[i]; current_filename != NULL;
         i++, current_filename = filenames[i]) {
        /*
         * Sustitute for $HOME
         */
        substituted_filename =
            substitute_string(current_filename, "$HOME", env_string);

        /*
         * Save that last filename so we can use it for Alt-N Configuration
         * later.
         */
        if (filenames[i + 1] == NULL) {
            sprintf(home_directory_options_filename, "%s",
                    substituted_filename);
        }

        /*
         * Check existence of file.  For the Windows case, assume that the
         * file is readable.  For everyone else, explicitly check for
         * readability.
         */
#ifdef Q_PDCURSES_WIN32
        if (file_exists(substituted_filename) == Q_TRUE) {
#else
        if (access(substituted_filename, F_OK | R_OK) == 0) {
#endif
            load_options_from_file(substituted_filename);
        }
        /*
         * Free leak
         */
        Xfree(substituted_filename, __FILE__, __LINE__);

    }

no_more_config_files:

    /*
     * ----------------------------------------------------------------------
     *
     * At this point, we have a ~/.qodem created, a ~/.qodem/qodemrc created,
     * and we have read all of the options between INSTALL_DIR/qodemrc and
     * $HOME/.qodem/qodemrc.  The working, host mode, and script directory
     * names are now known.
     *
     * Create the directories if needed.  Then wrap up the options handling
     * and be done.
     *
     * ----------------------------------------------------------------------
     */
    check_and_create_directories();

    /*
     * Special-case options.  For each one, reset to default and then re-load
     * from file.
     */
    q_status.idle_timeout = 0;
    if (get_option(Q_OPTION_IDLE_TIMEOUT) != NULL) {
        q_status.idle_timeout = atoi(get_option(Q_OPTION_IDLE_TIMEOUT));
    }

    q_status.bracketed_paste_mode = Q_FALSE;
    if (strcasecmp(get_option(Q_OPTION_BRACKETED_PASTE), "true") == 0) {
        q_status.bracketed_paste_mode = Q_TRUE;
    }

    q_screensaver_timeout = 0;
    if (get_option(Q_OPTION_SCREENSAVER_TIMEOUT) != NULL) {
        q_screensaver_timeout = atoi(get_option(Q_OPTION_SCREENSAVER_TIMEOUT));
    }

    q_scrollback_max = atoi(get_option_default(Q_OPTION_SCROLLBACK_LINES));
    if (get_option(Q_OPTION_SCROLLBACK_LINES) != NULL) {
        q_scrollback_max = atoi(get_option(Q_OPTION_SCROLLBACK_LINES));
    }
    if (q_scrollback_max < 0) {
        q_scrollback_max = 0;
    }

    q_keepalive_timeout = 0;
    if (get_option(Q_OPTION_KEEPALIVE_TIMEOUT) != NULL) {
        q_keepalive_timeout = atoi(get_option(Q_OPTION_KEEPALIVE_TIMEOUT));
    }
    if (strlen(get_option(Q_OPTION_KEEPALIVE_BYTES)) > 0) {
        memset(q_keepalive_bytes, 0, sizeof(q_keepalive_bytes));
        state = 0;
        begin = get_option(Q_OPTION_KEEPALIVE_BYTES);
        for (i = 0; i < strlen(get_option(Q_OPTION_KEEPALIVE_BYTES)); i++) {
            if (q_keepalive_bytes_n == sizeof(q_keepalive_bytes)) {
                /*
                 * Max length
                 */
                break;
            }
            switch (state) {
            case 0:
                if (begin[i] == '\\') {
                    /*
                     * Embedded hex chracter
                     */
                    ch = 0;
                    state = 1;
                    continue;
                }
                q_keepalive_bytes[q_keepalive_bytes_n] = begin[i];
                q_keepalive_bytes_n++;
                break;
            case 1:
                if (tolower(begin[i]) == 'x') {
                    /*
                     * Embedded hex chracter
                     */
                    ch = 0;
                    state = 2;
                    continue;
                }
                q_keepalive_bytes[q_keepalive_bytes_n] = '\\';
                q_keepalive_bytes_n++;
                state = 0;
                break;
            case 2:
                ch *= 16;
                if ((tolower(begin[i]) >= 'a') && (tolower(begin[i]) <= 'f')) {
                    ch += begin[i] - 'a' + 16;
                } else if ((begin[i] >= '0') && (begin[i] <= '9')) {
                    ch += begin[i] - '0';
                } else {
                    q_keepalive_bytes[q_keepalive_bytes_n] = '\\';
                    q_keepalive_bytes_n++;
                    if (q_keepalive_bytes_n < sizeof(q_keepalive_bytes)) {
                        q_keepalive_bytes[q_keepalive_bytes_n] = begin[i - 1];
                        q_keepalive_bytes_n++;
                    }
                    state = 0;
                    i--;
                    continue;
                }
                state = 3;
                continue;

            case 3:
                ch *= 16;
                if ((tolower(begin[i]) >= 'a') && (tolower(begin[i]) <= 'f')) {
                    ch += begin[i] - 'a' + 16;
                } else if ((begin[i] >= '0') && (begin[i] <= '9')) {
                    ch += begin[i] - '0';
                } else {
                    i--;
                }
                q_keepalive_bytes[q_keepalive_bytes_n] = ch;
                q_keepalive_bytes_n++;
                state = 0;
                continue;
            }
        }
    }

    /*
     * Capture types
     */
    reset_capture_type();
    reset_screen_dump_type();
    reset_scrollback_save_type();

    q_status.sound = Q_FALSE;
    q_status.beeps = Q_FALSE;
    q_status.ansi_music = Q_FALSE;
    if (strcasecmp(get_option(Q_OPTION_SOUNDS_ENABLED), "true") == 0) {
        q_status.sound = Q_TRUE;
        q_status.beeps = Q_TRUE;

        if (strcasecmp(get_option(Q_OPTION_ANSI_MUSIC), "true") == 0) {
            q_status.ansi_music = Q_TRUE;
        }
    }

    q_status.zmodem_autostart = Q_TRUE;
    if (strcasecmp(get_option(Q_OPTION_ZMODEM_AUTOSTART), "false") == 0) {
        q_status.zmodem_autostart = Q_FALSE;
    }
    q_status.zmodem_zchallenge = Q_FALSE;
    if (strcasecmp(get_option(Q_OPTION_ZMODEM_ZCHALLENGE), "true") == 0) {
        q_status.zmodem_zchallenge = Q_TRUE;
    }
    q_status.zmodem_escape_ctrl = Q_FALSE;
    if (strcasecmp(get_option(Q_OPTION_ZMODEM_ESCAPE_CTRL), "true") == 0) {
        q_status.zmodem_escape_ctrl = Q_TRUE;
    }

    q_status.kermit_autostart = Q_TRUE;
    if (strcasecmp(get_option(Q_OPTION_KERMIT_AUTOSTART), "false") == 0) {
        q_status.kermit_autostart = Q_FALSE;
    }
    q_status.kermit_robust_filename = Q_FALSE;
    if (strcasecmp(get_option(Q_OPTION_KERMIT_ROBUST_FILENAME), "true") == 0) {
        q_status.kermit_robust_filename = Q_TRUE;
    }
    q_status.kermit_streaming = Q_TRUE;
    if (strcasecmp(get_option(Q_OPTION_KERMIT_STREAMING), "false") == 0) {
        q_status.kermit_streaming = Q_FALSE;
    }
    q_status.kermit_long_packets = Q_TRUE;
    if (strcasecmp(get_option(Q_OPTION_KERMIT_LONG_PACKETS), "false") == 0) {
        q_status.kermit_long_packets = Q_FALSE;
    }
    q_status.kermit_uploads_force_binary = Q_TRUE;
    if (strcasecmp(get_option(Q_OPTION_KERMIT_UPLOADS_FORCE_BINARY), "false") ==
        0) {
        q_status.kermit_uploads_force_binary = Q_FALSE;
    }
    q_status.kermit_downloads_convert_text = Q_FALSE;
    if (strcasecmp(get_option(Q_OPTION_KERMIT_DOWNLOADS_CONVERT_TEXT), "true")
        == 0) {
        q_status.kermit_downloads_convert_text = Q_TRUE;
    }
    q_status.kermit_resend = Q_TRUE;
    if (strcasecmp(get_option(Q_OPTION_KERMIT_RESEND), "false") == 0) {
        q_status.kermit_resend = Q_FALSE;
    }

    q_status.assume_80_columns = Q_TRUE;
    if (strcasecmp(get_option(Q_OPTION_80_COLUMNS), "false") == 0) {
        q_status.assume_80_columns = Q_FALSE;
    }

    q_status.ansi_animate = Q_FALSE;
    if (strcasecmp(get_option(Q_OPTION_ANSI_ANIMATE), "true") == 0) {
        q_status.ansi_animate = Q_TRUE;
    }

    q_status.exit_on_disconnect = Q_FALSE;
    if (strcasecmp(get_option(Q_OPTION_EXIT_ON_DISCONNECT), "true") == 0) {
        q_status.exit_on_disconnect = Q_TRUE;
    }

    q_status.external_telnet = Q_FALSE;
    if (strcasecmp(get_option(Q_OPTION_TELNET_EXTERNAL), "true") == 0) {
        q_status.external_telnet = Q_TRUE;
    }
    q_status.external_rlogin = Q_TRUE;
    if (strcasecmp(get_option(Q_OPTION_RLOGIN_EXTERNAL), "false") == 0) {
        q_status.external_rlogin = Q_FALSE;
    }
    q_status.external_ssh = Q_TRUE;
    if (strcasecmp(get_option(Q_OPTION_SSH_EXTERNAL), "false") == 0) {
        q_status.external_ssh = Q_FALSE;
    }
    q_status.xterm_double = Q_TRUE;
    if (strcasecmp(get_option(Q_OPTION_XTERM_DOUBLE), "false") == 0) {
        q_status.xterm_double = Q_FALSE;
    }
    q_status.xterm_mouse_reporting = Q_TRUE;
    if (strcasecmp(get_option(Q_OPTION_XTERM_MOUSE_REPORTING), "false") == 0) {
        q_status.xterm_mouse_reporting = Q_FALSE;
    }
    q_status.vt100_color = Q_TRUE;
    if (strcasecmp(get_option(Q_OPTION_VT100_COLOR), "false") == 0) {
        q_status.vt100_color = Q_FALSE;
    }
    q_status.vt52_color = Q_TRUE;
    if (strcasecmp(get_option(Q_OPTION_VT52_COLOR), "false") == 0) {
        q_status.vt52_color = Q_FALSE;
    }
    q_status.avatar_color = Q_TRUE;
    if (strcasecmp(get_option(Q_OPTION_AVATAR_COLOR), "false") == 0) {
        q_status.avatar_color = Q_FALSE;
    }
    q_status.avatar_ansi_fallback = Q_TRUE;
    if (strcasecmp(get_option(Q_OPTION_AVATAR_ANSI_FALLBACK), "false") == 0) {
        q_status.avatar_ansi_fallback = Q_FALSE;
    }
    q_status.petscii_is_c64 = Q_TRUE;
    if (strcasecmp(get_option(Q_OPTION_PETSCII_C64), "false") == 0) {
        q_status.petscii_is_c64 = Q_FALSE;
    }
    q_status.petscii_color = Q_TRUE;
    if (strcasecmp(get_option(Q_OPTION_PETSCII_COLOR), "false") == 0) {
        q_status.petscii_color = Q_FALSE;
    }
    q_status.petscii_ansi_fallback = Q_TRUE;
    if (strcasecmp(get_option(Q_OPTION_PETSCII_ANSI_FALLBACK), "false") == 0) {
        q_status.petscii_ansi_fallback = Q_FALSE;
    }
    q_status.petscii_has_wide_font = Q_TRUE;
    if (strcasecmp(get_option(Q_OPTION_PETSCII_WIDE_FONT), "false") == 0) {
        q_status.petscii_has_wide_font = Q_FALSE;
    }
    q_status.petscii_use_unicode = Q_FALSE;
    if (strcasecmp(get_option(Q_OPTION_PETSCII_UNICODE), "true") == 0) {
        q_status.petscii_use_unicode = Q_TRUE;
        q_status.petscii_has_wide_font = Q_FALSE;
    }
    q_status.atascii_has_wide_font = Q_FALSE;
    if (strcasecmp(get_option(Q_OPTION_ATASCII_WIDE_FONT), "true") == 0) {
        q_status.atascii_has_wide_font = Q_TRUE;
    }

}

/**
 * Set q_status.capture_type to whatever is defined in the options file.
 */
void reset_capture_type() {
    q_status.capture_type = Q_CAPTURE_TYPE_NORMAL;
    if (strcasecmp(get_option(Q_OPTION_CAPTURE_TYPE), "raw") == 0) {
        q_status.capture_type = Q_CAPTURE_TYPE_RAW;
    }
    if (strcasecmp(get_option(Q_OPTION_CAPTURE_TYPE), "html") == 0) {
        q_status.capture_type = Q_CAPTURE_TYPE_HTML;
    }
    if (strcasecmp(get_option(Q_OPTION_CAPTURE_TYPE), "ask") == 0) {
        q_status.capture_type = Q_CAPTURE_TYPE_ASK;
    }
}

/**
 * Set q_status.screen_dump_type to whatever is defined in the options file.
 */
void reset_screen_dump_type() {
    q_status.screen_dump_type = Q_CAPTURE_TYPE_NORMAL;
    if (strcasecmp(get_option(Q_OPTION_SCREEN_DUMP_TYPE), "html") == 0) {
        q_status.screen_dump_type = Q_CAPTURE_TYPE_HTML;
    }
    if (strcasecmp(get_option(Q_OPTION_SCREEN_DUMP_TYPE), "ask") == 0) {
        q_status.screen_dump_type = Q_CAPTURE_TYPE_ASK;
    }
}

/**
 * Set q_status.scrollback_save_type to whatever is defined in the options
 * file.
 */
void reset_scrollback_save_type() {
    q_status.scrollback_save_type = Q_CAPTURE_TYPE_NORMAL;
    if (strcasecmp(get_option(Q_OPTION_SCROLLBACK_SAVE_TYPE), "html") == 0) {
        q_status.scrollback_save_type = Q_CAPTURE_TYPE_HTML;
    }
    if (strcasecmp(get_option(Q_OPTION_SCROLLBACK_SAVE_TYPE), "ask") == 0) {
        q_status.scrollback_save_type = Q_CAPTURE_TYPE_ASK;
    }
}
