/*
 * phonebook.c
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
#include <ctype.h>
#ifdef Q_PDCURSES_WIN32
#  include <stdio.h>
#else
#  include <wctype.h>
#  include <unistd.h>
#endif
#ifdef _MSC_VER
#  include <windows.h>          /* GetCurrentProcessId() */
#endif
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "qodem.h"
#include "screen.h"
#include "forms.h"
#include "keyboard.h"
#include "console.h"
#include "states.h"
#include "options.h"
#include "music.h"
#include "field.h"
#include "script.h"
#include "dialer.h"
#include "help.h"
#include "netclient.h"
#include "translate.h"
#include "phonebook.h"

#ifdef __clang__
/*
 * Disable the "tautological enum compare" check, because we actually do
 * assign -1 to enums sometimes, which is legal C.
 */
#pragma clang diagnostic ignored "-Wtautological-constant-out-of-range-compare"
#endif

/* Modem dialer ----------------------------------------------------------- */

/* Set this to a not-NULL value to enable debug log. */
/* static const char * DLOGNAME = "dialer"; */
static const char * DLOGNAME = NULL;

#define VIEW_MODE_MAX 5

typedef enum {
    DIAL_MODEM_INIT,
    DIAL_MODEM_SENT_AT,
    DIAL_MODEM_SENT_DIAL_STRING,
    DIAL_MODEM_CONNECTED
} DIAL_MODEM_STATE;

#ifndef Q_NO_SERIAL
/* Current modem state */
static DIAL_MODEM_STATE modem_state;
#endif

/* ------------------------------------------------------------------------ */

/* Max size of a displayed emulation string name */
#define EMULATION_STRING_SIZE   32

/* Max size of a displayed codepage string name */
#define CODEPAGE_STRING_SIZE    32

/* Max size of a displayed connection method string name */
#define METHOD_STRING_SIZE      32

/**
 * The currently-connected entry.
 */
struct q_phone_struct * q_current_dial_entry = NULL;

/**
 * The phonebook.
 */
struct q_phonebook_struct q_phonebook = {
    "fonebook.txt",             /* filename */
    0,                          /* tagged */
    0,                          /* last_saved_time */
    0,                          /* view_mode */
    NULL,                       /* entries */
    0,                          /* entry_count */
    NULL                        /* selected_entry */
};

/* The currently selected entry in the phonebook */
static int phonebook_entry_i = 0;

/* The currently visible "page" in the phonebook */
static int phonebook_page = 0;

/* Sort field choices */
typedef enum {
    SORT_METHOD_NAME_ASC,       /* Sort by name ascending */
    SORT_METHOD_ADDRESS_ASC,    /* Sort by address ascending */
    SORT_METHOD_TOTAL_CALLS_DESC,       /* Sort by total calls descending */
    SORT_METHOD_METHOD_ASC,     /* Sort by method ascending */
    SORT_METHOD_LAST_CALL_DESC, /* Sort by method last call descending */
    SORT_METHOD_REVERSE,        /* Reverse existing order */

    SORT_METHOD_MAX
} SORT_METHOD;

/* If the phonebook is printed to this "file", run it through cat instead */
#define LPR_FILE_NAME "|lpr"

/*
 * When true, phonebook_refresh() pops up a notification that find/find again
 * found the text in a note.
 */
static Q_BOOL found_note_flag = Q_FALSE;

/* The available per-entry dial out toggles as a bitmask */
#define TOGGLE_SESSION_LOG      0x0001
#define TOGGLE_XONXOFF          0x0002
#define TOGGLE_HARD_BACKSPACE   0x0004
#define TOGGLE_LINEWRAP         0x0008
#define TOGGLE_DISPLAY_NULL     0x0010
#define TOGGLE_STATUS_LINE_INFO 0x0020
#define TOGGLE_STRIP_8TH        0x0040
#define TOGGLE_BEEPS            0x0080
#define TOGGLE_HALF_DUPLEX      0x0100
#define TOGGLE_SCROLLBACK       0x0200
#define TOGGLE_STATUS_LINE      0x0400
#define TOGGLE_CRLF             0x0800
#define TOGGLE_ANSI_MUSIC       0x1000

/* The buffer returned by toggles_to_string() */
static char toggles_string_buffer[32];

/* Close the currently dialed entry */
static void close_dial_entry() {
    if (q_current_dial_entry != NULL) {
#ifndef Q_NO_SERIAL
        if (q_current_dial_entry->method == Q_DIAL_METHOD_MODEM) {
            close_serial_port();
        } else {
#endif
            net_close();
            q_child_tty_fd = -1;
#ifndef Q_NO_SERIAL
        }
#endif
    }
}

/**
 * Reset the phonebook selection display.  This is called when the screen is
 * resized.
 */
void phonebook_reset() {
    phonebook_entry_i = 0;
    phonebook_page = 0;
    q_phonebook.selected_entry = q_phonebook.entries;
}

/**
 * Fix the internal page and entry indices so that the current selected entry
 * is visible in the phonebook display screen.
 */
void phonebook_normalize() {
    int visible_entries_n = HEIGHT - 1 - 14;
    struct q_phone_struct *p;

    phonebook_entry_i = 0;
    phonebook_page = 0;

    if ((q_phonebook.selected_entry == NULL) || (q_phonebook.entries == NULL)) {
        return;
    }

    p = q_phonebook.entries;
    while (p != NULL) {
        if (p == q_phonebook.selected_entry) {
            return;
        }
        phonebook_entry_i++;
        if (phonebook_entry_i % visible_entries_n == 0) {
            phonebook_page++;
        }
        p = p->next;
    }
}

/**
 * See if the phonebook was last modified by this instance.
 *
 * @param backup_version if true, check the backup copy
 * @return true if the phonebook on disk was last modified by this instance,
 * or the user overrode ownership
 */
static Q_BOOL phonebook_is_mine(Q_BOOL backup_version) {
    struct stat fstats;
    char * filename;
    Q_BOOL rc = Q_FALSE;
    int keystroke;
    char * message_lines[4];

    if (backup_version == Q_FALSE) {
        filename = q_phonebook.filename;
    } else {
        filename =
            (char *) Xmalloc(sizeof(char) * (strlen(q_phonebook.filename) + 5),
                             __FILE__, __LINE__);
        snprintf(filename, strlen(q_phonebook.filename) + 5, "%s.bak",
                 q_phonebook.filename);
    }

    /*
     * Get the last modified time, so that we can decide if someone else has
     * modified it.
     */
    if (stat(filename, &fstats) == 0) {
        if (q_phonebook.last_save_time != fstats.st_mtime) {
            /*
             * Someone else has modified this phonebook.  Ask the user if
             * they want to overwrite it or not.
             */
            message_lines[0] = _("It appears another instance may have");
            message_lines[1] = _("modified this phonebook file.");
            message_lines[2] = "";
            message_lines[3] = _("     Save anyway? [Y/n] "),
            keystroke = q_tolower(notify_prompt_form_long(message_lines,
                _("Overwrite Phonebook"),
                _(" Y-Overwrite The Phonebook File   N-Do Not Save Changes "),
                    Q_TRUE, 0.0, "YyNn\r", 4));

            if ((keystroke == 'y') || (keystroke == Q_KEY_ENTER)) {
                rc = Q_TRUE;
            } else {
                rc = Q_FALSE;
            }
        } else {
            /*
             * Default outcome: it is ours.
             */
            rc = Q_TRUE;
        }
    } else {
        /*
         * We couldn't check the time, so assume it is ours.
         */
        rc = Q_TRUE;
    }

    if (backup_version == Q_TRUE) {
        Xfree(filename, __FILE__, __LINE__);
    }
    return rc;
}

/**
 * Print the phonebook in 80-column mode.
 *
 * @param dest either a filename, or the special value LPR_FILE_NAME to send
 * to the printer
 */
static void print_phonebook_80(const char * dest) {
    char notify_message[DIALOG_MESSAGE_SIZE];
    char command[COMMAND_LINE_SIZE];
    char line[80 + 1];
    FILE * file;
    char * filename;
    char * full_filename;
    Q_BOOL lpr;
    struct q_phone_struct * entry;
    unsigned int left_stop;
    unsigned int i;
    unsigned int entry_i;
    unsigned int page = 1;
    Q_BOOL page_header = Q_TRUE;
    unsigned int lines_per_page = 60;
    unsigned int lines = 0;

    if (strstr(dest, LPR_FILE_NAME) == dest) {
        /*
         * This one is going to lpr
         */
        lpr = Q_TRUE;
        filename = "savefon.txt";
    } else {
        lpr = Q_FALSE;
        filename = (char *) dest;
    }

    /*
     * Print everything to file first
     */
    file = open_workingdir_file(filename, &full_filename);
    if (file == NULL) {
        snprintf(notify_message, sizeof(notify_message),
                 _("Error opening file \"%s\" for writing: %s"), full_filename,
                 strerror(errno));
        notify_form(notify_message, 0);
        if (full_filename != NULL) {
            Xfree(full_filename, __FILE__, __LINE__);
        }
        return;
    }

    /*
     * Start at the top
     */
    entry = q_phonebook.entries;

    sprintf(line, "%s %s %s", _("Qodem Version"), Q_VERSION, _("Phone Book"));
    left_stop = (80 - strlen(line)) / 2;
    for (i = 0; i < left_stop; i++) {
        fprintf(file, " ");
    }
    fprintf(file, "%s\n", line);
    for (i = 0; i < left_stop; i++) {
        fprintf(file, " ");
    }
    for (i = 0; i < strlen(line); i++) {
        fprintf(file, "=");
    }
    fprintf(file, "\n\n");
    lines += 3;

    /*
     * Loop through the entries
     */
    entry_i = 1;
    while (entry != NULL) {
        if (page_header == Q_TRUE) {
            fprintf(file, _("Page %2d   File : %s"), page, full_filename);
            fprintf(file, "\n");
            fprintf(file, "\n");
            fprintf(file,
                _("              Name                        Address/Number    Method  Com Settings"));
            fprintf(file, "\n");
            for (i = 0; i < 80; i++) {
                fprintf(file, "-");
            }
            fprintf(file, "\n");
            lines += 4;

            page++;
            page_header = Q_FALSE;
        }
#ifndef Q_NO_SERIAL
        if (entry->use_modem_cfg == Q_TRUE) {
            snprintf(line, sizeof(line) - 1, _(" Modem Cfg"));
        } else {
            snprintf(line, sizeof(line) - 1,
                     "%6.6s %s-%s-%s",
                     baud_string(entry->baud),
                     data_bits_string(entry->data_bits),
                     parity_string(entry->parity, Q_TRUE),
                     stop_bits_string(entry->stop_bits));
        }
#endif

        fprintf(file, _("%3d %-28.28ls %23.23s %9s %9s"),
                entry_i, entry->name, entry->address,
                method_string(entry->method), line);
        fprintf(file, "\n");
        lines++;

        if (lines == lines_per_page) {
            /*
             * Send form feed
             */
            fprintf(file, "\014\n");

            /*
             * Reset for next page
             */
            lines = 0;
            page_header = Q_TRUE;
        }

        /*
         * Next entry
         */
        entry = entry->next;
        entry_i++;
    }

    /*
     * Send form feed
     */
    fprintf(file, "\014");

    fclose(file);
    if (full_filename != filename) {
        Xfree(full_filename, __FILE__, __LINE__);
    }

    if (lpr == Q_TRUE) {
        /*
         * Send the file to lpr
         */
        memset(command, 0, sizeof(command));
        snprintf(command, sizeof(command) - 1, "cat %s | lpr", full_filename);
        system(command);
    }
}

/**
 * Print the phonebook in 132-column mode.
 *
 * @param dest either a filename, or the special value LPR_FILE_NAME to send
 * to the printer
 */
static void print_phonebook_132(const char * dest) {
    char notify_message[DIALOG_MESSAGE_SIZE];
    char command[COMMAND_LINE_SIZE];
    char line[132 + 1];
    FILE * file;
    char * filename;
    char * full_filename;
    Q_BOOL lpr;
    struct q_phone_struct * entry;
    unsigned int left_stop;
    unsigned int i;
    unsigned int entry_i;
    unsigned int page = 1;
    Q_BOOL page_header = Q_TRUE;
    unsigned int lines_per_page = 60;
    unsigned int lines = 0;

    if (strstr(dest, LPR_FILE_NAME) == dest) {
        /*
         * This one is going to lpr
         */
        lpr = Q_TRUE;
        filename = "savefon.txt";
    } else {
        lpr = Q_FALSE;
        filename = (char *) dest;
    }

    /*
     * Print everything to file first
     */
    file = open_workingdir_file(filename, &full_filename);
    if (file == NULL) {
        snprintf(notify_message, sizeof(notify_message),
                 _("Error opening file \"%s\" for writing: %s"), full_filename,
                 strerror(errno));
        notify_form(notify_message, 0);
        if (full_filename != NULL) {
            Xfree(full_filename, __FILE__, __LINE__);
        }
        return;
    }

    /*
     * Start at the top
     */
    entry = q_phonebook.entries;

    sprintf(line, "%s %s %s", _("Qodem Version"), Q_VERSION, _("Phone Book"));
    left_stop = (132 - strlen(line)) / 2;
    for (i = 0; i < left_stop; i++) {
        fprintf(file, " ");
    }
    fprintf(file, "%s\n", line);
    for (i = 0; i < left_stop; i++) {
        fprintf(file, " ");
    }
    for (i = 0; i < strlen(line); i++) {
        fprintf(file, "=");
    }
    fprintf(file, "\n\n");
    lines += 3;

    /*
     * Loop through the entries
     */
    entry_i = 1;
    while (entry != NULL) {
        if (page_header == Q_TRUE) {
            fprintf(file, _("Page %2d   File : %s"), page, full_filename);
            fprintf(file, "\n");
            fprintf(file, "\n");
            fprintf(file,
                _("              Name                        Address/Number  Port   Method  Com Settings  Emulation         Username           Password"));
            fprintf(file, "\n");
            for (i = 0; i < 132; i++) {
                fprintf(file, "-");
            }
            fprintf(file, "\n");
            lines += 4;

            page++;
            page_header = Q_FALSE;
        }
#ifndef Q_NO_SERIAL
        if (entry->use_modem_cfg == Q_TRUE) {
            snprintf(line, sizeof(line) - 1, _(" Modem Cfg"));
        } else {
            snprintf(line, sizeof(line) - 1,
                     "%6.6s %s-%s-%s",
                     baud_string(entry->baud),
                     data_bits_string(entry->data_bits),
                     parity_string(entry->parity, Q_TRUE),
                     stop_bits_string(entry->stop_bits));
        }
#endif

        fprintf(file,
            _("%3d %-28.28ls %23.23s %5s %7s %13.13s %8.8s %19.19ls %18.18ls"),
            entry_i, entry->name, entry->address, entry->port,
            method_string(entry->method), line,
            emulation_string(entry->emulation), entry->username,
            entry->password);
        fprintf(file, "\n");
        lines++;

        if (lines == lines_per_page) {
            /*
             * Send form feed
             */
            fprintf(file, "\014\n");

            /*
             * Reset for next page
             */
            lines = 0;
            page_header = Q_TRUE;
        }

        /*
         * Next entry
         */
        entry = entry->next;
        entry_i++;
    }

    /*
     * Send form feed
     */
    fprintf(file, "\014");

    fclose(file);
    if (full_filename != filename) {
        Xfree(full_filename, __FILE__, __LINE__);
    }

    if (lpr == Q_TRUE) {
        /*
         * Send the file to lpr
         */
        memset(command, 0, sizeof(command));
        snprintf(command, sizeof(command) - 1, "cat %s | lpr", full_filename);
        system(command);
    }
}

/**
 * Get the default port number for a particular connection method as a
 * newly-allocated string.
 *
 * @param method the dialing method enum
 * @return a newly-allocated string
 */
static char * default_port(const Q_DIAL_METHOD method) {

    switch (method) {
    case Q_DIAL_METHOD_SOCKET:
        /*
         * Default on the telnet port.
         */
        return Xstrdup("23", __FILE__, __LINE__);
    case Q_DIAL_METHOD_TELNET:
        return Xstrdup("23", __FILE__, __LINE__);
    case Q_DIAL_METHOD_SSH:
        return Xstrdup("22", __FILE__, __LINE__);
    case Q_DIAL_METHOD_RLOGIN:
        return Xstrdup("", __FILE__, __LINE__);
    case Q_DIAL_METHOD_SHELL:
        return Xstrdup("", __FILE__, __LINE__);
    case Q_DIAL_METHOD_COMMANDLINE:
        return Xstrdup("", __FILE__, __LINE__);
#ifndef Q_NO_SERIAL
    case Q_DIAL_METHOD_MODEM:
        return Xstrdup("", __FILE__, __LINE__);
#endif
    }
    return NULL;
}

/**
 * Get a string representing a doorway option to expose in the phonebook
 * display.
 *
 * @param doorway a doorway mode enum
 * @return a human-readable string
 */
static char * doorway_string(const Q_DOORWAY doorway) {

    switch (doorway) {

    case Q_DOORWAY_CONFIG:
        return _("Use Global Option");

    case Q_DOORWAY_ALWAYS_DOORWAY:
        return _("Always DOORWAY");

    case Q_DOORWAY_ALWAYS_MIXED:
        return _("Always MIXED");

    case Q_DOORWAY_NEVER:
        return _("Never");
    }
    return NULL;
}

/**
 * Get a doorway enum from the human-readable string.
 * display.
 *
 * @param string a human-readable string
 * @return a doorway mode enum
 */
static Q_DOORWAY doorway_from_string(const char * string) {

    if (strncmp(string, _("Use Global Option"),
            strlen(_("Use Global Option"))) == 0) {
        return Q_DOORWAY_CONFIG;
    } else if (strncmp(string, _("Always DOORWAY"),
            strlen(_("Always DOORWAY"))) == 0) {
        return Q_DOORWAY_ALWAYS_DOORWAY;
    } else if (strncmp(string, _("Always MIXED"),
            strlen(_("Always MIXED"))) == 0) {
        return Q_DOORWAY_ALWAYS_MIXED;
    } else if (strncmp(string, _("Never"), strlen(_("Never"))) == 0) {
        return Q_DOORWAY_NEVER;
    }

    return Q_DOORWAY_NEVER;
}

/**
 * Return a string for a Q_DIAL_METHOD enum.
 *
 * @param baud Q_DIAL_METHOD_TELNET etc.
 * @return "TELNET" etc.
 */
char * method_string(const Q_DIAL_METHOD method) {

    switch (method) {
    case Q_DIAL_METHOD_SHELL:
        return "LOCAL";
#ifndef Q_NO_SERIAL
    case Q_DIAL_METHOD_MODEM:
        return "MODEM";
#endif
    case Q_DIAL_METHOD_SSH:
        return "SSH";
    case Q_DIAL_METHOD_RLOGIN:
        return "RLOGIN";
    case Q_DIAL_METHOD_TELNET:
        return "TELNET";
    case Q_DIAL_METHOD_SOCKET:
        return "SOCKET";
    case Q_DIAL_METHOD_COMMANDLINE:
        return "CMDLINE";
    }

    return NULL;
}

/**
 * Return a Q_DIAL_METHOD enum from a string.
 *
 * @param string "TELNET" etc.
 * @return Q_DIAL_METHOD_TELNET etc.
 */
static Q_DIAL_METHOD method_from_string(const char *string) {

    if (strncmp(string, "LOCAL", strlen("LOCAL")) == 0) {
        return Q_DIAL_METHOD_SHELL;
    } else if (strncmp(string, "SSH", strlen("SSH")) == 0) {
        return Q_DIAL_METHOD_SSH;
#ifndef Q_NO_SERIAL
    } else if (strncmp(string, "MODEM", strlen("MODEM")) == 0) {
        return Q_DIAL_METHOD_MODEM;
#endif
    } else if (strncmp(string, "RLOGIN", strlen("RLOGIN")) == 0) {
        return Q_DIAL_METHOD_RLOGIN;
    } else if (strncmp(string, "TELNET", strlen("TELNET")) == 0) {
        return Q_DIAL_METHOD_TELNET;
    } else if (strncmp(string, "SOCKET", strlen("SOCKET")) == 0) {
        return Q_DIAL_METHOD_SOCKET;
    } else if (strncmp(string, "CMDLINE", strlen("CMDLINE")) == 0) {
        return Q_DIAL_METHOD_COMMANDLINE;
    }

    return Q_DIAL_METHOD_SHELL;
}

/**
 * Set the global state based on a phonebook entry toggles.  This is used to
 * do things like selectively enable linefeed-after-cr, session log, beeps,
 * etc.
 *
 * @param toggles a bitmask of the options to change
 */
void set_dial_out_toggles(int toggles) {
    if (toggles & TOGGLE_SESSION_LOG) {
        if (q_status.logging == Q_FALSE) {
            start_logging(get_option(Q_OPTION_LOG_FILE));
        }
    }
#ifndef Q_NO_SERIAL
    if (toggles & TOGGLE_XONXOFF) {
        q_serial_port.xonxoff = Q_TRUE;
    } else {
        q_serial_port.xonxoff = Q_FALSE;
    }
    /*
     * Reconfigure the port if it's open
     */
    if (Q_SERIAL_OPEN) {
        if (configure_serial_port() == Q_FALSE) {
            /*
             * Oops, we couldn't change a setting.  Do nothing.
             */
        }
    }
#endif

    if (toggles & TOGGLE_HARD_BACKSPACE) {
        q_status.hard_backspace = Q_TRUE;
    } else {
        q_status.hard_backspace = Q_FALSE;
    }

    if (toggles & TOGGLE_LINEWRAP) {
        q_status.line_wrap = Q_FALSE;
    } else {
        q_status.line_wrap = Q_TRUE;
    }

    if (toggles & TOGGLE_DISPLAY_NULL) {
        q_status.display_null = Q_TRUE;
    } else {
        q_status.display_null = Q_FALSE;
    }

    if (toggles & TOGGLE_STATUS_LINE_INFO) {
        q_status.status_line_info = Q_TRUE;
    } else {
        q_status.status_line_info = Q_FALSE;
    }

    if (toggles & TOGGLE_STRIP_8TH) {
        q_status.strip_8th_bit = Q_TRUE;
    } else {
        q_status.strip_8th_bit = Q_FALSE;
    }

    if (toggles & TOGGLE_BEEPS) {
        q_status.beeps = Q_FALSE;
    } else {
        q_status.beeps = Q_TRUE;
    }

    if (toggles & TOGGLE_HALF_DUPLEX) {
        q_status.full_duplex = Q_FALSE;
    } else {
        q_status.full_duplex = Q_TRUE;
    }

    if (toggles & TOGGLE_SCROLLBACK) {
        q_status.scrollback_enabled = Q_FALSE;
    } else {
        q_status.scrollback_enabled = Q_TRUE;
    }

    if (toggles & TOGGLE_STATUS_LINE) {
        set_status_line(Q_FALSE);
    } else {
        set_status_line(Q_TRUE);
    }

    if (toggles & TOGGLE_CRLF) {
        q_status.line_feed_on_cr = Q_TRUE;
    } else {
        q_status.line_feed_on_cr = Q_FALSE;
    }

    if (toggles & TOGGLE_ANSI_MUSIC) {
        q_status.ansi_music = Q_FALSE;
    } else {
        q_status.ansi_music = Q_TRUE;
    }

}

/**
 * Generate the human-readable string from a toggles bitmask.
 *
 * @param toggles a bitmask of toggles
 * @return a string representing which toggles are opposite of the default
 */
static char * toggles_to_string(int toggles) {
    memset(toggles_string_buffer, 0, sizeof(toggles_string_buffer));

    if (toggles & TOGGLE_SESSION_LOG) {
        toggles_string_buffer[strlen(toggles_string_buffer)] = '0';
    }
    if (toggles & TOGGLE_XONXOFF) {
        toggles_string_buffer[strlen(toggles_string_buffer)] = '1';
    }
    if (toggles & TOGGLE_HARD_BACKSPACE) {
        toggles_string_buffer[strlen(toggles_string_buffer)] = '2';
    }
    if (toggles & TOGGLE_LINEWRAP) {
        toggles_string_buffer[strlen(toggles_string_buffer)] = '3';
    }
    if (toggles & TOGGLE_DISPLAY_NULL) {
        toggles_string_buffer[strlen(toggles_string_buffer)] = '4';
    }
    if (toggles & TOGGLE_STATUS_LINE_INFO) {
        toggles_string_buffer[strlen(toggles_string_buffer)] = '7';
    }
    if (toggles & TOGGLE_STRIP_8TH) {
        toggles_string_buffer[strlen(toggles_string_buffer)] = '8';
    }
    if (toggles & TOGGLE_BEEPS) {
        toggles_string_buffer[strlen(toggles_string_buffer)] = 'B';
    }
    if (toggles & TOGGLE_HALF_DUPLEX) {
        toggles_string_buffer[strlen(toggles_string_buffer)] = 'E';
    }
    if (toggles & TOGGLE_SCROLLBACK) {
        toggles_string_buffer[strlen(toggles_string_buffer)] = 'U';
    }
    if (toggles & TOGGLE_STATUS_LINE) {
        toggles_string_buffer[strlen(toggles_string_buffer)] = '-';
    }
    if (toggles & TOGGLE_CRLF) {
        toggles_string_buffer[strlen(toggles_string_buffer)] = '+';
    }
    if (toggles & TOGGLE_ANSI_MUSIC) {
        toggles_string_buffer[strlen(toggles_string_buffer)] = ',';
    }

    return toggles_string_buffer;
}

/**
 * Load the phonebook from file.
 *
 * @param backup_version if true, load from the backup copy
 */
void load_phonebook(const Q_BOOL backup_version) {
    FILE * file;
    char * begin;
    char * end;
    char * filename;
    int i;
    char line[PHONEBOOK_LINE_SIZE];
    char buffer[PHONEBOOK_LINE_SIZE];
    wchar_t value_wchar[PHONEBOOK_LINE_SIZE];
    struct q_phone_struct * old_entry;
    struct q_phone_struct * new_entry;

    enum SCAN_STATES {
        SCAN_STATE_NONE,        /* Between entries */
        SCAN_STATE_ENTRY,       /* Scanning for single-line fields */
        SCAN_STATE_NOTES        /* Reading the notes */
    };

    int scan_state;
    int notes_length = 0;
    struct stat fstats;

    DLOG(("load_phonebook()\n"));

    if (backup_version == Q_FALSE) {
        filename = q_phonebook.filename;
    } else {
        filename =
            (char *) Xmalloc(sizeof(char) * (strlen(q_phonebook.filename) + 5),
                             __FILE__, __LINE__);
        snprintf(filename, strlen(q_phonebook.filename) + 5, "%s.bak",
                 q_phonebook.filename);
    }

    file = fopen(filename, "r");
    if (file == NULL) {
        snprintf(line, sizeof(line),
                 _("Error opening file \"%s\" for reading: %s"), filename,
                 strerror(errno));
        notify_form(line, 0);
        if (backup_version == Q_TRUE) {
            Xfree(filename, __FILE__, __LINE__);
        }
        return;
    }

    /*
     * Free old phonebook
     */
    old_entry = q_phonebook.entries;
    while (old_entry != NULL) {
        Xfree(old_entry->name, __FILE__, __LINE__);
        Xfree(old_entry->address, __FILE__, __LINE__);
        Xfree(old_entry->port, __FILE__, __LINE__);
        Xfree(old_entry->username, __FILE__, __LINE__);
        Xfree(old_entry->password, __FILE__, __LINE__);
        if (old_entry->notes != NULL) {
            for (i = 0; old_entry->notes[i] != NULL; i++) {
                Xfree(old_entry->notes[i], __FILE__, __LINE__);
            }
            Xfree(old_entry->notes, __FILE__, __LINE__);
        }
        Xfree(old_entry->script_filename, __FILE__, __LINE__);
        Xfree(old_entry->capture_filename, __FILE__, __LINE__);
        Xfree(old_entry->translate_8bit_filename, __FILE__, __LINE__);
        Xfree(old_entry->translate_unicode_filename, __FILE__, __LINE__);
        Xfree(old_entry->keybindings_filename, __FILE__, __LINE__);
        new_entry = old_entry;
        old_entry = old_entry->next;
        Xfree(new_entry, __FILE__, __LINE__);
    }

    /*
     * Reset for new phonebook
     */
    q_phonebook.tagged = 0;
    q_phonebook.last_save_time = 0;
    q_phonebook.view_mode = 0;
    q_phonebook.entries = 0;
    q_phonebook.entry_count = 0;
    q_phonebook.selected_entry = NULL;

    old_entry = NULL;
    new_entry = NULL;
    scan_state = SCAN_STATE_NONE;
    while (!feof(file)) {

        if (fgets(line, sizeof(line), file) == NULL) {
            /*
             * This will cause the outer while's feof() check to fail and
             * smoothly exit the while loop.
             */
            continue;
        }
        begin = line;

        DLOG(("  scan_state = %d\n", scan_state));

        if (scan_state == SCAN_STATE_NONE) {
            while ((strlen(line) > 0) && q_isspace(line[strlen(line) - 1])) {
                /*
                 * Trim trailing whitespace
                 */
                line[strlen(line) - 1] = '\0';
            }
            while (q_isspace(*begin)) {
                /*
                 * Trim leading whitespace
                 */
                begin++;
            }
            DLOG(("    line = '%s'\n", line));
            DLOG(("    begin = '%s'\n", begin));

            if ((strlen(begin) == 0) || (*begin == '#')) {
                /*
                 * Ignore blank lines and commented lines between entries
                 */
                continue;
            }

            if (strncmp(begin, "[entry]", strlen("[entry]")) == 0) {
                /*
                 * Beginning of an entry found
                 */
                scan_state = SCAN_STATE_ENTRY;
                new_entry =
                    (struct q_phone_struct *)
                    Xmalloc(sizeof(struct q_phone_struct), __FILE__, __LINE__);
                memset(new_entry, 0, sizeof(struct q_phone_struct));
                if (q_phonebook.entry_count == 0) {
                    q_phonebook.entries = new_entry;
                    new_entry->prev = NULL;
                } else {
                    old_entry->next = new_entry;
                    new_entry->prev = old_entry;

                    /*
                     * Fixup any missing fields
                     */
                    if (old_entry->script_filename == NULL) {
                        old_entry->script_filename =
                            Xstrdup("", __FILE__, __LINE__);
                    }
                    if (old_entry->keybindings_filename == NULL) {
                        old_entry->keybindings_filename =
                            Xstrdup("", __FILE__, __LINE__);
                    }
                    if (old_entry->capture_filename == NULL) {
                        old_entry->capture_filename =
                            Xstrdup("", __FILE__, __LINE__);
                    }
                    if (old_entry->translate_8bit_filename == NULL) {
                        old_entry->translate_8bit_filename =
                            Xstrdup("", __FILE__, __LINE__);
                    }
                    if (old_entry->translate_unicode_filename == NULL) {
                        old_entry->translate_unicode_filename =
                            Xstrdup("", __FILE__, __LINE__);
                    }
                    if (old_entry->username == NULL) {
                        old_entry->username = Xwcsdup(L"", __FILE__, __LINE__);
                    }
                    if (old_entry->password == NULL) {
                        old_entry->password = Xwcsdup(L"", __FILE__, __LINE__);
                    }
                    if (old_entry->name == NULL) {
                        old_entry->name = Xwcsdup(L"", __FILE__, __LINE__);
                    }
                    if (old_entry->address == NULL) {
                        old_entry->address = Xstrdup("", __FILE__, __LINE__);
                    }
                    if (old_entry->port == NULL) {
                        old_entry->port = Xstrdup("", __FILE__, __LINE__);
                    }
                }

                q_phonebook.entry_count++;
                new_entry->port = NULL;
                new_entry->notes = NULL;
                new_entry->tagged = Q_FALSE;
                new_entry->doorway = Q_DOORWAY_CONFIG;
                new_entry->emulation = Q_EMUL_XTERM_UTF8;
                new_entry->codepage = default_codepage(new_entry->emulation);

#ifndef Q_NO_SERIAL
                new_entry->use_modem_cfg = Q_TRUE;
                new_entry->baud = Q_BAUD_115200;
                new_entry->data_bits = Q_DATA_BITS_8;
                new_entry->parity = Q_PARITY_NONE;
                new_entry->stop_bits = Q_STOP_BITS_1;
                new_entry->xonxoff = Q_FALSE;
                new_entry->rtscts = Q_TRUE;
                new_entry->lock_dte_baud = Q_TRUE;
#endif

                new_entry->name = NULL;
                new_entry->address = NULL;
                new_entry->port = NULL;
                new_entry->username = NULL;
                new_entry->password = NULL;
                new_entry->script_filename = NULL;
                new_entry->capture_filename = NULL;
                new_entry->translate_8bit_filename = NULL;
                new_entry->translate_unicode_filename = NULL;
                new_entry->keybindings_filename = NULL;
                new_entry->use_default_toggles = Q_TRUE;
                new_entry->toggles = 0;
                new_entry->quicklearn = Q_FALSE;

                new_entry->next = NULL;

                /*
                 * Save entry
                 */
                old_entry = new_entry;
                continue;
            }
        }

        if (scan_state == SCAN_STATE_ENTRY) {
            while ((strlen(line) > 0) && q_isspace(line[strlen(line) - 1])) {
                /*
                 * Trim trailing whitespace
                 */
                line[strlen(line) - 1] = '\0';
            }
            while (q_isspace(*begin)) {
                /*
                 * Trim leading whitespace
                 */
                begin++;
            }

            if ((strlen(begin) == 0) || (*begin == '#')) {
                /*
                 * Ignore blank lines and commented lines between entries
                 */
                continue;
            }

            end = strchr(begin, '=');
            if (end == NULL) {
                /*
                 * Ignore this line.
                 */
                continue;
            }
            memset(buffer, 0, sizeof(buffer));
            strncpy(buffer, begin, end - begin);
            begin = end + 1;

            mbstowcs(value_wchar, begin, strlen(begin) + 1);

            if (strncmp(buffer, "name", strlen("name")) == 0) {
                /*
                 * NAME
                 */
                new_entry->name = Xwcsdup(value_wchar, __FILE__, __LINE__);
            } else if (strncmp(buffer, "address", strlen("address")) == 0) {
                /*
                 * ADDRESS
                 */
                new_entry->address = Xstrdup(begin, __FILE__, __LINE__);
            } else if (strncmp(buffer, "port", strlen("port")) == 0) {
                /*
                 * PORT
                 */
                new_entry->port = Xstrdup(begin, __FILE__, __LINE__);
            } else if (strncmp(buffer, "username", strlen("password")) == 0) {
                /*
                 * USERNAME
                 */
                new_entry->username = Xwcsdup(value_wchar, __FILE__, __LINE__);
            } else if (strncmp(buffer, "password", strlen("password")) == 0) {
                /*
                 * PASSWORD
                 */
                new_entry->password = Xwcsdup(value_wchar, __FILE__, __LINE__);
            } else if (strncmp(buffer, "tagged", strlen("tagged")) == 0) {
                /*
                 * TAGGED
                 */
                if (strncasecmp(begin, "true", strlen("true")) == 0) {
                    new_entry->tagged = Q_TRUE;
                    q_phonebook.tagged++;
                }
            } else if (strncmp(buffer, "doorway", strlen("doorway")) == 0) {
                /*
                 * DOORWAY
                 */
                if (strncasecmp(begin, "doorway", strlen("doorway")) == 0) {
                    new_entry->doorway = Q_DOORWAY_ALWAYS_DOORWAY;
                } else if (strncasecmp(begin, "always",
                        strlen("always")) == 0) {
                    /*
                     * Allow "always" to mean always doorway too, for upgrading
                     */
                    new_entry->doorway = Q_DOORWAY_ALWAYS_DOORWAY;
                } else if (strncasecmp(begin, "mixed", strlen("mixed")) == 0) {
                    new_entry->doorway = Q_DOORWAY_ALWAYS_MIXED;
                } else if (strncasecmp(begin, "never", strlen("never")) == 0) {
                    new_entry->doorway = Q_DOORWAY_NEVER;
                } else {
                    new_entry->doorway = Q_DOORWAY_CONFIG;
                }
            } else if (strncmp(buffer, "method", strlen("method")) == 0) {
                /*
                 * METHOD
                 */
                new_entry->method = method_from_string(begin);
                if (new_entry->port == NULL) {
                    new_entry->port = default_port(new_entry->method);
                }
            } else if (strncmp(buffer, "emulation", strlen("emulation")) == 0) {
                /*
                 * EMULATION
                 */
                new_entry->emulation = emulation_from_string(begin);
                /*
                 * Set codepage right now in case it's not declared in the
                 * file (upgrade case).
                 */
                new_entry->codepage = default_codepage(new_entry->emulation);
            } else if (strncmp(buffer, "codepage", strlen("codepage")) == 0) {
                /*
                 * CODEPAGE
                 */
                new_entry->codepage = codepage_from_string(begin);
            } else if (strncmp(buffer, "quicklearn",
                    strlen("quicklearn")) == 0) {
                /*
                 * QUICKLEARN
                 */
                if (strncmp(begin, "true", strlen("true")) == 0) {
                    new_entry->quicklearn = Q_TRUE;
                }
#ifndef Q_NO_SERIAL
            } else if (strncmp(buffer, "use_modem_cfg",
                    strlen("use_modem_cfg")) == 0) {
                /*
                 * USE_MODEM_CFG
                 */
                if (strncmp(begin, "false", strlen("false")) == 0) {
                    new_entry->use_modem_cfg = Q_FALSE;
                }
#endif
            } else if (strncmp(buffer, "use_default_toggles",
                    strlen("use_default_toggles")) == 0) {
                /*
                 * USE_DEFAULT_TOGGLES
                 */
                if (strncmp(begin, "false", strlen("false")) == 0) {
                    new_entry->use_default_toggles = Q_FALSE;
                }
            } else if (strncmp(buffer, "toggles", strlen("toggles")) == 0) {
                /*
                 * TOGGLES
                 */
                new_entry->toggles = atoi(begin);
#ifndef Q_NO_SERIAL
            } else if (strncmp(buffer, "xonxoff", strlen("xonxoff")) == 0) {
                /*
                 * XONXOFF
                 */
                if (strncmp(begin, "true", strlen("true")) == 0) {
                    new_entry->xonxoff = Q_TRUE;
                }
            } else if (strncmp(buffer, "rtscts", strlen("rtscts")) == 0) {
                /*
                 * RTSCTS
                 */
                if (strncmp(begin, "false", strlen("false")) == 0) {
                    new_entry->rtscts = Q_FALSE;
                }
            } else if (strncmp(buffer, "baud", strlen("baud")) == 0) {
                /*
                 * BAUD
                 */
                if (strcmp(begin, "300") == 0) {
                    new_entry->baud = Q_BAUD_300;
                } else if (strcmp(begin, "1200") == 0) {
                    new_entry->baud = Q_BAUD_1200;
                } else if (strcmp(begin, "2400") == 0) {
                    new_entry->baud = Q_BAUD_2400;
                } else if (strcmp(begin, "4800") == 0) {
                    new_entry->baud = Q_BAUD_4800;
                } else if (strcmp(begin, "9600") == 0) {
                    new_entry->baud = Q_BAUD_9600;
                } else if (strcmp(begin, "19200") == 0) {
                    new_entry->baud = Q_BAUD_19200;
                } else if (strcmp(begin, "38400") == 0) {
                    new_entry->baud = Q_BAUD_38400;
                } else if (strcmp(begin, "57600") == 0) {
                    new_entry->baud = Q_BAUD_57600;
                } else if (strcmp(begin, "115200") == 0) {
                    new_entry->baud = Q_BAUD_115200;
                } else if (strcmp(begin, "230400") == 0) {
                    new_entry->baud = Q_BAUD_230400;
                }

            } else if (strncmp(buffer, "data_bits", strlen("data_bits")) == 0) {
                if (strcmp(begin, "8") == 0) {
                    new_entry->data_bits = Q_DATA_BITS_8;
                } else if (strcmp(begin, "7") == 0) {
                    new_entry->data_bits = Q_DATA_BITS_7;
                } else if (strcmp(begin, "6") == 0) {
                    new_entry->data_bits = Q_DATA_BITS_6;
                } else if (strcmp(begin, "5") == 0) {
                    new_entry->data_bits = Q_DATA_BITS_5;
                }
            } else if (strncmp(buffer, "parity", strlen("parity")) == 0) {
                if (strcmp(begin, "none") == 0) {
                    new_entry->parity = Q_PARITY_NONE;
                } else if (strcmp(begin, "even") == 0) {
                    new_entry->parity = Q_PARITY_EVEN;
                } else if (strcmp(begin, "odd") == 0) {
                    new_entry->parity = Q_PARITY_ODD;
                } else if (strcmp(begin, "mark") == 0) {
                    new_entry->parity = Q_PARITY_MARK;
                } else if (strcmp(begin, "space") == 0) {
                    new_entry->parity = Q_PARITY_SPACE;
                }
            } else if (strncmp(buffer, "stop_bits", strlen("stop_bits")) == 0) {
                if (strcmp(begin, "1") == 0) {
                    new_entry->stop_bits = Q_STOP_BITS_1;
                } else if (strcmp(begin, "2") == 0) {
                    new_entry->stop_bits = Q_STOP_BITS_2;
                }
            } else if (strncmp(buffer, "lock_dte_baud", strlen("lock_dte_baud"))
                       == 0) {
                if (strcmp(begin, "true") == 0) {
                    new_entry->lock_dte_baud = Q_TRUE;
                } else if (strcmp(begin, "false") == 0) {
                    new_entry->lock_dte_baud = Q_FALSE;
                }
#endif /* Q_NO_SERIAL */

            } else if (strncmp(buffer, "times_on", strlen("times_on")) == 0) {
                /*
                 * TIMES ON
                 */
                new_entry->times_on = atol(begin);
            } else if (strncmp(buffer, "last_call", strlen("last_call")) == 0) {
                /*
                 * LAST CALL
                 */
                new_entry->last_call = atol(begin);
            } else if (strncmp(buffer, "notes", strlen("notes")) == 0) {
                /*
                 * Switch state to reading Notes
                 */
                new_entry->notes =
                    (wchar_t **) Xmalloc(sizeof(wchar_t *), __FILE__, __LINE__);
                notes_length = 0;
                new_entry->notes[notes_length] = NULL;
                scan_state = SCAN_STATE_NOTES;
                continue;
            } else if (strncmp(buffer, "script_filename",
                    strlen("script_filename")) == 0) {
                /*
                 * SCRIPT FILENAME
                 */
                new_entry->script_filename = Xstrdup(begin, __FILE__, __LINE__);
            } else if (strncmp(buffer, "capture_filename",
                    strlen("capture_filename")) == 0) {
                /*
                 * CAPTURE FILENAME
                 */
                new_entry->capture_filename =
                    Xstrdup(begin, __FILE__, __LINE__);
            } else if (strncmp(buffer, "translate_8bit_filename",
                    strlen("translate_8bit_filename")) == 0) {
                /*
                 * 8-BIT TRANSLATE FILENAME
                 */
                new_entry->translate_8bit_filename =
                    Xstrdup(begin, __FILE__, __LINE__);
            } else if (strncmp(buffer, "translate_unicode_filename",
                    strlen("translate_unicode_filename")) == 0) {
                /*
                 * UNICODE TRANSLATE FILENAME
                 */
                new_entry->translate_unicode_filename =
                    Xstrdup(begin, __FILE__, __LINE__);
            } else if (strncmp(buffer, "keybindings_filename",
                    strlen("keybindings_filename")) == 0) {
                /*
                 * KEY BINDINGS
                 */
                new_entry->keybindings_filename =
                    Xstrdup(begin, __FILE__, __LINE__);

                /*
                 * -------------------
                 * ---- LAST ITEM ----
                 * -------------------
                 *
                 * There are no more supported options, switch state.
                 */
                scan_state = SCAN_STATE_NONE;
            }
        } /* (scan_state == SCAN_STATE_ENTRY) */

        if (scan_state == SCAN_STATE_NOTES) {
            while ((strlen(line) > 0) && q_isspace(line[strlen(line) - 1])) {
                /*
                 * Trim trailing whitespace
                 */
                line[strlen(line) - 1] = '\0';
            }
            if ((strlen(line) > 0) &&
                (strncmp(line, "END", strlen(line)) == 0)) {

                /*
                 * Done, back to entry scanning
                 */
                scan_state = SCAN_STATE_ENTRY;
                continue;
            }
            notes_length++;
            new_entry->notes =
                (wchar_t **) Xrealloc(new_entry->notes,
                                      (notes_length + 1) * sizeof(wchar_t *),
                                      __FILE__, __LINE__);
            /*
             * Ensure line is terminated
             */
            line[sizeof(line) - 1] = 0;
            mbstowcs(value_wchar, line, strlen(line) + 1);
            new_entry->notes[notes_length - 1] =
                Xwcsdup(value_wchar, __FILE__, __LINE__);
            new_entry->notes[notes_length] = NULL;
        }

    } /* while (!feof(file)) */

    /*
     * Fixup any missing fields
     */
    if (new_entry->script_filename == NULL) {
        new_entry->script_filename = Xstrdup("", __FILE__, __LINE__);
    }
    if (new_entry->capture_filename == NULL) {
        new_entry->capture_filename = Xstrdup("", __FILE__, __LINE__);
    }
    if (new_entry->translate_8bit_filename == NULL) {
        new_entry->translate_8bit_filename = Xstrdup("", __FILE__, __LINE__);
    }
    if (new_entry->translate_unicode_filename == NULL) {
        new_entry->translate_unicode_filename = Xstrdup("", __FILE__, __LINE__);
    }
    if (new_entry->keybindings_filename == NULL) {
        new_entry->keybindings_filename = Xstrdup("", __FILE__, __LINE__);
    }

    q_phonebook.selected_entry = q_phonebook.entries;
    fclose(file);
    phonebook_page = 0;
    phonebook_entry_i = 0;

    /*
     * Save the last modified time, so that we can decide if someone else has
     * modified it.
     */
    if (stat(filename, &fstats) == 0) {
        q_phonebook.last_save_time = fstats.st_mtime;
    }

}

/**
 * Save the phonebook to file.
 *
 * @param backup_version if true, save to the backup copy
 */
static void save_phonebook(const Q_BOOL backup_version) {
    FILE * file;
    char * filename;
    struct q_phone_struct * entry;
    char notify_message[DIALOG_MESSAGE_SIZE];
    wchar_t * notes_line;
    int current_notes_idx;
    struct stat fstats;

    if (q_status.read_only == Q_TRUE) {
        return;
    }

    if (backup_version == Q_FALSE) {
        filename = q_phonebook.filename;
    } else {
        filename =
            (char *) Xmalloc(sizeof(char) * (strlen(q_phonebook.filename) + 5),
                             __FILE__, __LINE__);
        snprintf(filename, strlen(q_phonebook.filename) + 5, "%s.bak",
                 q_phonebook.filename);
    }

    file = fopen(filename, "w");
    if (file == NULL) {
        snprintf(notify_message, sizeof(notify_message),
                 _("Error opening file \"%s\" for writing: %s"), filename,
                 strerror(errno));
        notify_form(notify_message, 0);
        if (backup_version == Q_TRUE) {
            Xfree(filename, __FILE__, __LINE__);
        }
        return;
    }

    fprintf(file, "# Qodem Phonebook\n");
    fprintf(file, "#\n");

    for (entry = q_phonebook.entries; entry != NULL; entry = entry->next) {
        fprintf(file, "[entry]\n");
        fprintf(file, "name=%ls\n", entry->name);
        fprintf(file, "address=%s\n", entry->address);
        fprintf(file, "port=%s\n", entry->port);
        fprintf(file, "username=%ls\n", entry->username);
        fprintf(file, "password=%ls\n", entry->password);
        fprintf(file, "tagged=%s\n",
                (entry->tagged == Q_TRUE) ? "true" : "false");
        switch (entry->doorway) {
        case Q_DOORWAY_ALWAYS_DOORWAY:
            fprintf(file, "doorway=always\n");
            break;
        case Q_DOORWAY_ALWAYS_MIXED:
            fprintf(file, "doorway=mixed\n");
            break;
        case Q_DOORWAY_NEVER:
            fprintf(file, "doorway=never\n");
            break;
        case Q_DOORWAY_CONFIG:
            fprintf(file, "doorway=config\n");
            break;
        }
        fprintf(file, "method=%s\n", method_string(entry->method));
        fprintf(file, "emulation=%s\n", emulation_string(entry->emulation));
        fprintf(file, "codepage=%s\n", codepage_string(entry->codepage));
        fprintf(file, "quicklearn=%s\n",
                (entry->quicklearn == Q_TRUE ? "true" : "false"));
#ifndef Q_NO_SERIAL
        fprintf(file, "use_modem_cfg=%s\n",
                (entry->use_modem_cfg == Q_TRUE ? "true" : "false"));
        fprintf(file, "baud=%s\n", baud_string(entry->baud));
        fprintf(file, "data_bits=%s\n", data_bits_string(entry->data_bits));
        fprintf(file, "parity=%s\n", parity_string(entry->parity, Q_FALSE));
        fprintf(file, "stop_bits=%s\n", stop_bits_string(entry->stop_bits));
        fprintf(file, "xonxoff=%s\n",
                (entry->xonxoff == Q_TRUE ? "true" : "false"));
        fprintf(file, "rtscts=%s\n",
                (entry->rtscts == Q_TRUE ? "true" : "false"));
        fprintf(file, "lock_dte_baud=%s\n",
                (entry->lock_dte_baud == Q_TRUE ? "true" : "false"));
#endif
        fprintf(file, "times_on=%u\n", entry->times_on);
        fprintf(file, "use_default_toggles=%s\n",
                (entry->use_default_toggles == Q_TRUE ? "true" : "false"));
        fprintf(file, "toggles=%u\n", entry->toggles);
        fprintf(file, "last_call=%lu\n", entry->last_call);
        if (entry->notes != NULL) {
            fprintf(file, "notes=<<<END\n");
            current_notes_idx = 0;
            for (notes_line = entry->notes[current_notes_idx];
                 notes_line != NULL;
                 current_notes_idx++, notes_line =
                 entry->notes[current_notes_idx]) {
                fprintf(file, "%ls\n", notes_line);
            }
            fprintf(file, "END\n");
        }
        fprintf(file, "script_filename=%s\n", entry->script_filename);
        fprintf(file, "capture_filename=%s\n", entry->capture_filename);
        fprintf(file, "translate_8bit_filename=%s\n",
            entry->translate_8bit_filename);
        fprintf(file, "translate_unicode_filename=%s\n",
            entry->translate_unicode_filename);
        fprintf(file, "keybindings_filename=%s\n", entry->keybindings_filename);
        fprintf(file, "\n");
    }
    fclose(file);

    /*
     * Save the last modified time, so that we can decide if someone else has
     * modified it.
     */
    if ((stat(filename, &fstats) == 0) && (backup_version == Q_FALSE)) {
        q_phonebook.last_save_time = fstats.st_mtime;
    }

    if (backup_version == Q_TRUE) {
        Xfree(filename, __FILE__, __LINE__);
    }
}

/**
 * Create the initial default phonebook.
 */
void create_phonebook() {
    /*
     * We will create the following entries:
     *
     * 1) A local shell using X_UTF8 emulation
     * 2) Vertrauen - Home of Synchronet
     * 3) SDF.org - Unix shell accounts
     * 4) rainmaker.wunderground.com - get the weather
     * 5) Zapto - Home of Ethral BBS
     * 6) OSUNY - the resurrected phreaking BBS
     * 7) Mono - a big U.K. BBS
     * 8) Electronic Chicken - BBS telnet
     * 9) Electronic Chicken - BBS dial-up
     */
    struct q_phone_struct * new_entry;
    struct q_phone_struct * old_entry = NULL;

    assert(q_phonebook.entries == NULL);

    q_phonebook.entry_count = 0;
    new_entry =
        (struct q_phone_struct *) Xmalloc(sizeof(struct q_phone_struct),
                                          __FILE__, __LINE__);
    if (q_phonebook.entry_count == 0) {
        q_phonebook.entries = new_entry;
        new_entry->prev = NULL;
    } else {
        old_entry->next = new_entry;
        new_entry->prev = old_entry;
    }
    new_entry->name             = Xstring_to_wcsdup(_("Local shell"),
        __FILE__, __LINE__);
    new_entry->method           = Q_DIAL_METHOD_SHELL;
    new_entry->address          = Xstrdup("", __FILE__, __LINE__);
    new_entry->port             = Xstrdup("", __FILE__, __LINE__);
    new_entry->username         = Xwcsdup(L"", __FILE__, __LINE__);
    new_entry->password         = Xwcsdup(L"", __FILE__, __LINE__);
    new_entry->notes            = NULL;
    new_entry->tagged           = Q_FALSE;
    new_entry->doorway          = Q_DOORWAY_CONFIG;
    new_entry->emulation        = Q_EMUL_XTERM_UTF8;
    new_entry->codepage         = default_codepage(new_entry->emulation);
#ifndef Q_NO_SERIAL
    new_entry->use_modem_cfg    = Q_TRUE;
    new_entry->baud             = Q_BAUD_115200;
    new_entry->data_bits        = Q_DATA_BITS_8;
    new_entry->parity           = Q_PARITY_NONE;
    new_entry->stop_bits        = Q_STOP_BITS_1;
    new_entry->xonxoff          = Q_FALSE;
    new_entry->rtscts           = Q_TRUE;
    new_entry->lock_dte_baud    = Q_TRUE;
#endif
    new_entry->script_filename            = Xstrdup("", __FILE__, __LINE__);
    new_entry->capture_filename           = Xstrdup("", __FILE__, __LINE__);
    new_entry->translate_8bit_filename    = Xstrdup("", __FILE__, __LINE__);
    new_entry->translate_unicode_filename = Xstrdup("", __FILE__, __LINE__);
    new_entry->keybindings_filename       = Xstrdup("", __FILE__, __LINE__);
    new_entry->use_default_toggles        = Q_TRUE;
    new_entry->toggles          = 0;
    new_entry->last_call        = 0;
    new_entry->times_on         = 0;
    new_entry->quicklearn       = Q_FALSE;
    new_entry->next             = NULL;
    q_phonebook.entry_count++;

    /* Vertrauen - home of Synchronet -------------------------------------- */

    old_entry = new_entry;
    new_entry =
        (struct q_phone_struct *) Xmalloc(sizeof(struct q_phone_struct),
                                          __FILE__, __LINE__);
    if (q_phonebook.entry_count == 0) {
        q_phonebook.entries = new_entry;
        new_entry->prev = NULL;
    } else {
        old_entry->next = new_entry;
        new_entry->prev = old_entry;
    }
    new_entry->name             = Xwcsdup(L"Vertrauen BBS - The home of Synchronet",
        __FILE__, __LINE__);
    new_entry->method           = Q_DIAL_METHOD_TELNET;
    new_entry->address          = Xstrdup("vert.synchro.net",
        __FILE__, __LINE__);
    new_entry->port             = Xstrdup("23", __FILE__, __LINE__);
    new_entry->username         = Xwcsdup(L"", __FILE__, __LINE__);
    new_entry->password         = Xwcsdup(L"", __FILE__, __LINE__);
    new_entry->notes            = NULL;
    new_entry->tagged           = Q_FALSE;
    new_entry->doorway          = Q_DOORWAY_CONFIG;
    new_entry->emulation        = Q_EMUL_ANSI;
    new_entry->codepage         = default_codepage(new_entry->emulation);
#ifndef Q_NO_SERIAL
    new_entry->use_modem_cfg    = Q_TRUE;
    new_entry->baud             = Q_BAUD_115200;
    new_entry->data_bits        = Q_DATA_BITS_8;
    new_entry->parity           = Q_PARITY_NONE;
    new_entry->stop_bits        = Q_STOP_BITS_1;
    new_entry->xonxoff          = Q_FALSE;
    new_entry->rtscts           = Q_TRUE;
    new_entry->lock_dte_baud    = Q_TRUE;
#endif
    new_entry->script_filename            = Xstrdup("", __FILE__, __LINE__);
    new_entry->capture_filename           = Xstrdup("", __FILE__, __LINE__);
    new_entry->translate_8bit_filename    = Xstrdup("", __FILE__, __LINE__);
    new_entry->translate_unicode_filename = Xstrdup("", __FILE__, __LINE__);
    new_entry->keybindings_filename       = Xstrdup("", __FILE__, __LINE__);
    new_entry->use_default_toggles        = Q_TRUE;
    new_entry->toggles          = 0;
    new_entry->last_call        = 0;
    new_entry->times_on         = 0;
    new_entry->quicklearn       = Q_FALSE;
    new_entry->next             = NULL;
    q_phonebook.entry_count++;

    /* SDF.org ------------------------------------------------------------- */

    old_entry = new_entry;
    new_entry =
        (struct q_phone_struct *) Xmalloc(sizeof(struct q_phone_struct),
                                          __FILE__, __LINE__);
    if (q_phonebook.entry_count == 0) {
        q_phonebook.entries = new_entry;
        new_entry->prev = NULL;
    } else {
        old_entry->next = new_entry;
        new_entry->prev = old_entry;
    }
    new_entry->name             = Xwcsdup(L"SDF.org - Free public access Unix systems",
        __FILE__, __LINE__);
    new_entry->method           = Q_DIAL_METHOD_SSH;
    new_entry->address          = Xstrdup("sdf.org", __FILE__, __LINE__);
    new_entry->port             = Xstrdup("22", __FILE__, __LINE__);
    new_entry->username         = Xwcsdup(L"new", __FILE__, __LINE__);
    new_entry->password         = Xwcsdup(L"", __FILE__, __LINE__);
    new_entry->notes            = NULL;
    new_entry->tagged           = Q_FALSE;
    new_entry->doorway          = Q_DOORWAY_CONFIG;
    new_entry->emulation        = Q_EMUL_XTERM_UTF8;
    new_entry->codepage         = default_codepage(new_entry->emulation);
#ifndef Q_NO_SERIAL
    new_entry->use_modem_cfg    = Q_TRUE;
    new_entry->baud             = Q_BAUD_115200;
    new_entry->data_bits        = Q_DATA_BITS_8;
    new_entry->parity           = Q_PARITY_NONE;
    new_entry->stop_bits        = Q_STOP_BITS_1;
    new_entry->xonxoff          = Q_FALSE;
    new_entry->rtscts           = Q_TRUE;
    new_entry->lock_dte_baud    = Q_TRUE;
#endif
    new_entry->script_filename            = Xstrdup("", __FILE__, __LINE__);
    new_entry->capture_filename           = Xstrdup("", __FILE__, __LINE__);
    new_entry->translate_8bit_filename    = Xstrdup("", __FILE__, __LINE__);
    new_entry->translate_unicode_filename = Xstrdup("", __FILE__, __LINE__);
    new_entry->keybindings_filename       = Xstrdup("", __FILE__, __LINE__);
    new_entry->use_default_toggles        = Q_TRUE;
    new_entry->toggles          = 0;
    new_entry->last_call        = 0;
    new_entry->times_on         = 0;
    new_entry->quicklearn       = Q_FALSE;
    new_entry->next             = NULL;
    q_phonebook.entry_count++;

    /* rainmaker.wunderground.com ------------------------------------------ */

    old_entry = new_entry;
    new_entry =
        (struct q_phone_struct *) Xmalloc(sizeof(struct q_phone_struct),
                                          __FILE__, __LINE__);
    if (q_phonebook.entry_count == 0) {
        q_phonebook.entries = new_entry;
        new_entry->prev = NULL;
    } else {
        old_entry->next = new_entry;
        new_entry->prev = old_entry;
    }
    new_entry->name             = Xwcsdup(L"The Weather Underground",
        __FILE__, __LINE__);
    new_entry->method           = Q_DIAL_METHOD_TELNET;
    new_entry->address          = Xstrdup("rainmaker.wunderground.com",
        __FILE__, __LINE__);
    new_entry->port             = Xstrdup("23", __FILE__, __LINE__);
    new_entry->username         = Xwcsdup(L"", __FILE__, __LINE__);
    new_entry->password         = Xwcsdup(L"", __FILE__, __LINE__);
    new_entry->notes            = NULL;
    new_entry->tagged           = Q_FALSE;
    new_entry->doorway          = Q_DOORWAY_CONFIG;
    new_entry->emulation        = Q_EMUL_VT102;
    new_entry->codepage         = default_codepage(new_entry->emulation);
#ifndef Q_NO_SERIAL
    new_entry->use_modem_cfg    = Q_TRUE;
    new_entry->baud             = Q_BAUD_115200;
    new_entry->data_bits        = Q_DATA_BITS_8;
    new_entry->parity           = Q_PARITY_NONE;
    new_entry->stop_bits        = Q_STOP_BITS_1;
    new_entry->xonxoff          = Q_FALSE;
    new_entry->rtscts           = Q_TRUE;
    new_entry->lock_dte_baud    = Q_TRUE;
#endif
    new_entry->script_filename            = Xstrdup("", __FILE__, __LINE__);
    new_entry->capture_filename           = Xstrdup("", __FILE__, __LINE__);
    new_entry->translate_8bit_filename    = Xstrdup("", __FILE__, __LINE__);
    new_entry->translate_unicode_filename = Xstrdup("", __FILE__, __LINE__);
    new_entry->keybindings_filename       = Xstrdup("", __FILE__, __LINE__);
    new_entry->use_default_toggles        = Q_TRUE;
    new_entry->toggles          = 0;
    new_entry->last_call        = 0;
    new_entry->times_on         = 0;
    new_entry->quicklearn       = Q_FALSE;
    new_entry->next             = NULL;
    q_phonebook.entry_count++;

    /* mono.org - Monochrome BBS ------------------------------------------- */

    old_entry = new_entry;
    new_entry =
        (struct q_phone_struct *) Xmalloc(sizeof(struct q_phone_struct),
                                          __FILE__, __LINE__);
    if (q_phonebook.entry_count == 0) {
        q_phonebook.entries = new_entry;
        new_entry->prev = NULL;
    } else {
        old_entry->next = new_entry;
        new_entry->prev = old_entry;
    }
    new_entry->name             = Xwcsdup(L"mono.org - Monochrome BBS",
        __FILE__, __LINE__);
    new_entry->method           = Q_DIAL_METHOD_TELNET;
    new_entry->address          = Xstrdup("mono.org", __FILE__, __LINE__);
    new_entry->port             = Xstrdup("23", __FILE__, __LINE__);
    new_entry->username         = Xwcsdup(L"", __FILE__, __LINE__);
    new_entry->password         = Xwcsdup(L"", __FILE__, __LINE__);
    new_entry->notes            = NULL;
    new_entry->tagged           = Q_FALSE;
    new_entry->doorway          = Q_DOORWAY_CONFIG;
    new_entry->emulation        = Q_EMUL_XTERM_UTF8;
    new_entry->codepage         = default_codepage(new_entry->emulation);
#ifndef Q_NO_SERIAL
    new_entry->use_modem_cfg    = Q_TRUE;
    new_entry->baud             = Q_BAUD_115200;
    new_entry->data_bits        = Q_DATA_BITS_8;
    new_entry->parity           = Q_PARITY_NONE;
    new_entry->stop_bits        = Q_STOP_BITS_1;
    new_entry->xonxoff          = Q_FALSE;
    new_entry->rtscts           = Q_TRUE;
    new_entry->lock_dte_baud    = Q_TRUE;
#endif
    new_entry->script_filename            = Xstrdup("", __FILE__, __LINE__);
    new_entry->capture_filename           = Xstrdup("", __FILE__, __LINE__);
    new_entry->translate_8bit_filename    = Xstrdup("", __FILE__, __LINE__);
    new_entry->translate_unicode_filename = Xstrdup("", __FILE__, __LINE__);
    new_entry->keybindings_filename       = Xstrdup("", __FILE__, __LINE__);
    new_entry->use_default_toggles        = Q_TRUE;
    new_entry->toggles          = 0;
    new_entry->last_call        = 0;
    new_entry->times_on         = 0;
    new_entry->quicklearn       = Q_FALSE;
    new_entry->next             = NULL;
    q_phonebook.entry_count++;

    /* Electronic Chicken BBS ---------------------------------------------- */

    old_entry = new_entry;
    new_entry =
        (struct q_phone_struct *) Xmalloc(sizeof(struct q_phone_struct),
                                          __FILE__, __LINE__);
    if (q_phonebook.entry_count == 0) {
        q_phonebook.entries = new_entry;
        new_entry->prev = NULL;
    } else {
        old_entry->next = new_entry;
        new_entry->prev = old_entry;
    }
    new_entry->name             = Xwcsdup(L"Electronic Chicken BBS",
        __FILE__, __LINE__);
    new_entry->method           = Q_DIAL_METHOD_TELNET;
    new_entry->address          = Xstrdup("bbs.electronicchicken.com",
        __FILE__, __LINE__);
    new_entry->port             = Xstrdup("23", __FILE__, __LINE__);
    new_entry->username         = Xwcsdup(L"", __FILE__, __LINE__);
    new_entry->password         = Xwcsdup(L"", __FILE__, __LINE__);
    new_entry->notes            = NULL;
    new_entry->tagged           = Q_FALSE;
    new_entry->doorway          = Q_DOORWAY_CONFIG;
    new_entry->emulation        = Q_EMUL_ANSI;
    new_entry->codepage         = default_codepage(new_entry->emulation);
#ifndef Q_NO_SERIAL
    new_entry->use_modem_cfg    = Q_TRUE;
    new_entry->baud             = Q_BAUD_115200;
    new_entry->data_bits        = Q_DATA_BITS_8;
    new_entry->parity           = Q_PARITY_NONE;
    new_entry->stop_bits        = Q_STOP_BITS_1;
    new_entry->xonxoff          = Q_FALSE;
    new_entry->rtscts           = Q_TRUE;
    new_entry->lock_dte_baud    = Q_TRUE;
#endif
    new_entry->script_filename            = Xstrdup("", __FILE__, __LINE__);
    new_entry->capture_filename           = Xstrdup("", __FILE__, __LINE__);
    new_entry->translate_8bit_filename    = Xstrdup("", __FILE__, __LINE__);
    new_entry->translate_unicode_filename = Xstrdup("", __FILE__, __LINE__);
    new_entry->keybindings_filename       = Xstrdup("", __FILE__, __LINE__);
    new_entry->use_default_toggles        = Q_TRUE;
    new_entry->toggles          = 0;
    new_entry->last_call        = 0;
    new_entry->times_on         = 0;
    new_entry->quicklearn       = Q_FALSE;
    new_entry->next             = NULL;
    q_phonebook.entry_count++;

    /*
     * Now save it.  Note that we don't care if anyone else might have
     * modified it.
     */
    save_phonebook(Q_FALSE);
}

#ifdef Q_SSH_CRYPTLIB
/* Forward reference for function called in do_dialer() */
static Q_BOOL prompt_ssh_password(const wchar_t * initial_username,
                                  wchar_t ** returned_username,
                                  const wchar_t * initial_password,
                                  wchar_t ** returned_password);
#endif

/**
 * This is the top-level call to "dial" the selected phonebook entry.  It
 * prompts for password if needed, sets up capture, quicklearn, etc, and
 * ultimately calls dial_out() in dialer to obtain the modem/network
 * connection.
 */
void do_dialer() {

#ifdef Q_SSH_CRYPTLIB
    wchar_t * ssh_username = NULL;
    wchar_t * ssh_password = NULL;
#endif

    if (q_status.current_username != NULL) {
        Xfree(q_status.current_username, __FILE__, __LINE__);
        q_status.current_username = NULL;
    }

    if (q_status.current_password != NULL) {
        Xfree(q_status.current_password, __FILE__, __LINE__);
        q_status.current_password = NULL;
    }

#ifdef Q_SSH_CRYPTLIB

    /*
     * Make sure for SSH connections we have both the username and password
     * available.
     */
    if ((q_current_dial_entry->method == Q_DIAL_METHOD_SSH) &&
        (q_status.external_ssh == Q_FALSE) &&
        ((wcslen(q_current_dial_entry->username) == 0) ||
         (wcslen(q_current_dial_entry->password) == 0))
    ) {
        /*
         * This is our internal SSH, and either username or password is
         * blank.
         */
        if (prompt_ssh_password(q_current_dial_entry->username,
                                &ssh_username,
                                q_current_dial_entry->password,
                                &ssh_password) == Q_FALSE) {

            /*
             * The user cancelled
             */
            return;
        }

        /*
         * We've got a username/password, use it
         */
        q_status.current_username = Xwcsdup(ssh_username, __FILE__, __LINE__);
        q_status.current_password = Xwcsdup(ssh_password, __FILE__, __LINE__);
        /*
         * No leak
         */
        Xfree(ssh_username, __FILE__, __LINE__);
        Xfree(ssh_password, __FILE__, __LINE__);
        ssh_username = NULL;
        ssh_password = NULL;

    } else {
        /*
         * Either external SSH or some other dial method.
         */
        q_status.current_username =
            Xwcsdup(q_current_dial_entry->username, __FILE__, __LINE__);
        q_status.current_password =
            Xwcsdup(q_current_dial_entry->password, __FILE__, __LINE__);
    }

#else

    /*
     * Either external SSH or some other dial method.
     */
    q_status.current_username =
        Xwcsdup(q_current_dial_entry->username, __FILE__, __LINE__);
    q_status.current_password =
        Xwcsdup(q_current_dial_entry->password, __FILE__, __LINE__);

#endif /* Q_SSH_CRYPTLIB */

#ifndef Q_NO_SERIAL
    /*
     * Kill the modem.  Either we are switching to a non-modem connection, or
     * we want the terminal settings of the phonebook entry.
     */
    if (Q_SERIAL_OPEN) {
        close_serial_port();
    }
    if (q_current_dial_entry->method == Q_DIAL_METHOD_MODEM) {
        /*
         * Set inital modem_state
         */
        modem_state = DIAL_MODEM_INIT;
    }
#endif

    /*
     * Clear modem message
     */
    memset(q_dialer_modem_message, 0, sizeof(q_dialer_modem_message));

    if (q_status.remote_address != NULL) {
        Xfree(q_status.remote_address, __FILE__, __LINE__);
    }
    q_status.remote_address =
        Xstrdup(q_current_dial_entry->address, __FILE__, __LINE__);

    if (q_status.remote_port != NULL) {
        Xfree(q_status.remote_port, __FILE__, __LINE__);
    }
    if ((q_current_dial_entry->port != NULL) &&
        (strlen(q_current_dial_entry->port) > 0)) {
        q_status.remote_port =
            Xstrdup(q_current_dial_entry->port, __FILE__, __LINE__);
    } else {
        q_status.remote_port = default_port(q_current_dial_entry->method);
    }

    if (q_status.remote_phonebook_name != NULL) {
        Xfree(q_status.remote_phonebook_name, __FILE__, __LINE__);
    }
    q_status.remote_phonebook_name =
        Xwcsdup(q_current_dial_entry->name, __FILE__, __LINE__);

    /*
     * Save the method - qodem_read() needs it
     */
    q_status.dial_method = q_current_dial_entry->method;

    /*
     * Save phonebook - in case someone JUST added an entry and will be
     * dialing.
     */
    if (phonebook_is_mine(Q_FALSE) == Q_TRUE) {
        save_phonebook(Q_FALSE);
    }

    /*
     * Now do the connection
     */
    dial_out(q_current_dial_entry);

    /*
     * Switch keyboard
     */
    if (q_keyfile != NULL) {
        /*
         * The very first time we connect, we will honor the --keyfile
         * command line option.
         */
        switch_current_keyboard(q_keyfile);
        Xfree(q_keyfile, __FILE__, __LINE__);
        q_keyfile = NULL;
    } else {
        switch_current_keyboard(q_current_dial_entry->keybindings_filename);
    }

    /*
     * Capture file
     */
    if (strlen(q_current_dial_entry->capture_filename) > 0) {
        if (q_status.capture == Q_TRUE) {
            stop_capture();
        }
        start_capture(q_current_dial_entry->capture_filename);
    }

    /*
     * 8-bit translate table
     */
    if (q_xl8file != NULL) {
        use_translate_table_8bit(q_xl8file);
        Xfree(q_xl8file, __FILE__, __LINE__);
        q_xl8file = NULL;
    } else if (strlen(q_current_dial_entry->translate_8bit_filename) > 0) {
        use_translate_table_8bit(q_current_dial_entry->translate_8bit_filename);
    }

    /*
     * Unicode translate table
     */
    if (q_xlufile != NULL) {
        use_translate_table_unicode(q_xlufile);
        Xfree(q_xlufile, __FILE__, __LINE__);
        q_xlufile = NULL;
    } else if (strlen(q_current_dial_entry->translate_unicode_filename) > 0) {
        use_translate_table_unicode(q_current_dial_entry->
                                        translate_unicode_filename);
    }

    /*
     * QuickLearn
     */
    if (q_current_dial_entry->quicklearn == TRUE) {
        assert(q_current_dial_entry->script_filename != NULL);
        assert(strlen(q_current_dial_entry->script_filename) > 0);
        start_quicklearn(get_scriptdir_filename
                         (q_current_dial_entry->script_filename));
        /*
         * We just dialed out, don't quicklearn again
         */
        q_current_dial_entry->quicklearn = Q_FALSE;
    }

    /*
     * Save phonebook
     */
    if (phonebook_is_mine(Q_FALSE) == Q_TRUE) {
        save_phonebook(Q_FALSE);
    }
}

/**
 * Sort the phonebook.
 *
 * @param method one of the available sorting methods
 */
static void sort_phonebook(const SORT_METHOD method) {
    /*
     * This is a hideously inefficient method.  Basically I just keep looping
     * through the phonebook swapping adjacent entries until the whole thing
     * is sorted.
     */

    struct q_phone_struct * current_entry;
    struct q_phone_struct * swap;
    struct q_phone_struct * tail;
    Q_BOOL sorted = Q_FALSE;

    if (q_phonebook.entries == NULL) {
        return;
    }

    if (method == SORT_METHOD_REVERSE) {
        /*
         * Special case: reverse everything
         */
        for (current_entry = q_phonebook.entries; current_entry != NULL;
             current_entry = current_entry->prev) {
            /*
             * Tail will be the last entry and become the new head
             */
            tail = current_entry;

            /*
             * Switch out prev/next
             */
            swap = current_entry->next;
            current_entry->next = current_entry->prev;
            current_entry->prev = swap;
        }

        /*
         * Point back to the top
         */
        q_phonebook.entries = tail;
        q_phonebook.selected_entry = q_phonebook.entries;
        phonebook_page = 0;
        phonebook_entry_i = 0;
        return;
    }

    while (sorted == Q_FALSE) {

        sorted = Q_TRUE;

        for (current_entry = q_phonebook.entries; current_entry->next != NULL;
             current_entry = current_entry->next) {

            if (method == SORT_METHOD_NAME_ASC) {
                if (wcscmp(current_entry->name,
                        current_entry->next->name) <= 0) {
                    continue;
                }

            } else if (method == SORT_METHOD_ADDRESS_ASC) {
                if (strcasecmp(current_entry->address,
                        current_entry->next->address) <= 0) {
                    continue;
                }

            } else if (method == SORT_METHOD_TOTAL_CALLS_DESC) {
                if (current_entry->times_on >= current_entry->next->times_on) {
                    continue;
                }

            } else if (method == SORT_METHOD_METHOD_ASC) {
                if (current_entry->method <= current_entry->next->method) {
                    continue;
                }

            } else if (method == SORT_METHOD_LAST_CALL_DESC) {
                if (current_entry->last_call >= current_entry->next->last_call) {
                    continue;
                }

            }

            /*
             * Swap current_entry and current_entry->next
             */
            if (current_entry == q_phonebook.entries) {
                /*
                 * Swapping the head
                 */
                q_phonebook.entries = current_entry->next;
                swap = current_entry->next->next;

                q_phonebook.entries->next = current_entry;
                q_phonebook.entries->prev = NULL;
                current_entry->prev = current_entry->next;
                current_entry->next = swap;
                if (swap != NULL) {
                    swap->prev = current_entry;
                }
            } else {
                /*
                 * Swapping in the middle
                 */
                swap = current_entry->next->next;

                current_entry->prev->next = current_entry->next;
                current_entry->next->prev = current_entry->prev;
                current_entry->prev = current_entry->next;
                current_entry->next->next = current_entry;
                current_entry->next = swap;
                if (swap != NULL) {
                    swap->prev = current_entry;
                }
            }

            sorted = Q_FALSE;

            /*
             * Search again from the top
             */
            break;

        } /* for (...) */

    } /* while (sorted == Q_FALSE) */

    /*
     * Point back to the top
     */
    q_phonebook.selected_entry = q_phonebook.entries;
    phonebook_page = 0;
    phonebook_entry_i = 0;
}

/**
 * See if a phonebook entry matches the search string.  Note that the search
 * string is destructively switched to lowercase.
 *
 * @param search_string text to search for
 * @param entry the entry to search in
 * @return true if a match is found
 */
static Q_BOOL match_phonebook_entry(wchar_t * search_string,
                                    const struct q_phone_struct * entry) {

    wchar_t * field_string;
    unsigned int i, j;

    /*
     * Force the search string to lowercase
     */
    for (i = 0; i < wcslen(search_string); i++) {
        search_string[i] = towlower(search_string[i]);
    }

    /*
     * Check name
     */
    if (wcslen(entry->name) > 0) {
        field_string = Xwcsdup(entry->name, __FILE__, __LINE__);
        for (i = 0; i < wcslen(field_string); i++) {
            field_string[i] = towlower(field_string[i]);
        }

        if (wcsstr(field_string, search_string) != NULL) {
            Xfree(field_string, __FILE__, __LINE__);
            return Q_TRUE;
        }
        Xfree(field_string, __FILE__, __LINE__);
    }

    /*
     * Check address
     */
    if (strlen(entry->address) > 0) {
        field_string = Xstring_to_wcsdup(entry->address, __FILE__, __LINE__);
        for (i = 0; i < wcslen(field_string); i++) {
            field_string[i] = towlower(field_string[i]);
        }

        if (wcsstr(field_string, search_string) != NULL) {
            Xfree(field_string, __FILE__, __LINE__);
            return Q_TRUE;
        }
        Xfree(field_string, __FILE__, __LINE__);
    }

    /*
     * Last of all, check the notes
     */
    if (entry->notes == NULL) {
        /*
         * Can't match anymore without notes
         */
        return Q_FALSE;
    }

    /*
     * Notes is a list of strings
     */
    for (j = 0; entry->notes[j] != NULL; j++) {
        field_string = Xwcsdup(entry->notes[j], __FILE__, __LINE__);
        for (i = 0; i < wcslen(field_string); i++) {
            field_string[i] = towlower(field_string[i]);
        }

        if (wcsstr(field_string, search_string) != NULL) {
            Xfree(field_string, __FILE__, __LINE__);
            found_note_flag = Q_TRUE;
            return Q_TRUE;
        }
        Xfree(field_string, __FILE__, __LINE__);
    }

    /*
     * Nothing matched
     */
    return Q_FALSE;
}

/**
 * Tag all entries that match a search string.
 *
 * @param tag_string the search
 */
static void tag_multiple(const char * tag_string) {
    int i;
    int current_entry_i;
    char ** search_tokens;
    char * search_string;
    wchar_t * wcs_search_string;

    struct q_phone_struct * current_entry;
    if (q_phonebook.entries == NULL) {
        return;
    }
    search_tokens = tokenize_command(tag_string);

    current_entry_i = 0;
    for (current_entry = q_phonebook.entries; current_entry != NULL;
         current_entry = current_entry->next) {
        current_entry_i++;

        for (i = 0; search_tokens[i] != NULL; i++) {

            if (tolower(search_tokens[i][0]) == 't') {

                /*
                 * Text search
                 */
                search_string = &search_tokens[i][1];
                wcs_search_string =
                    Xstring_to_wcsdup(search_string, __FILE__, __LINE__);

                if (match_phonebook_entry(wcs_search_string, current_entry) ==
                    Q_TRUE) {
                    current_entry->tagged = Q_TRUE;
                    q_phonebook.tagged++;
                }
                /*
                 * No leak
                 */
                Xfree(wcs_search_string, __FILE__, __LINE__);

            }

            if (q_isdigit(search_tokens[i][0])) {
                /*
                 * Entry number selection
                 */
                if (atoi(search_tokens[i]) == current_entry_i) {
                    if (current_entry->tagged == Q_FALSE) {
                        current_entry->tagged = Q_TRUE;
                        q_phonebook.tagged++;
                    }
                }
            }
        } /* for (i = 0; search_tokens[i] != NULL; i++) */

    } /* for (...) */

    /*
     * Free up the array of token pointers
     */
    Xfree(search_tokens, __FILE__, __LINE__);
}

/**
 * Edit the note on an entry.
 *
 * @param entry the phonebook entry to edit
 */
static void edit_attached_note(struct q_phone_struct * entry) {
    FILE * file;
    wchar_t * notes_line;
    int notes_length;
    char command_line[COMMAND_LINE_SIZE];
    char line[PHONEBOOK_LINE_SIZE];
    char filename[COMMAND_LINE_SIZE];
    char notify_message[DIALOG_MESSAGE_SIZE];
    int current_notes_idx;
    wchar_t line_wchar[PHONEBOOK_LINE_SIZE];

#ifdef Q_PDCURSES_WIN32
    sprintf(filename, "/tmp/~qodem%u_%lu.tmp", GetCurrentProcessId(),
            time(NULL));
#else
    sprintf(filename, "/tmp/~qodem%u_%lu.tmp", getpid(), time(NULL));
#endif
    file = fopen(filename, "w");
    if (file == NULL) {
        snprintf(notify_message, sizeof(notify_message),
                 _("Error opening file \"%s\" for writing: %s"), filename,
                 strerror(errno));
        notify_form(notify_message, 0);
        return;
    }

    if (entry->notes != NULL) {
        current_notes_idx = 0;
        for (notes_line = entry->notes[current_notes_idx]; notes_line != NULL;
             current_notes_idx++, notes_line =
             entry->notes[current_notes_idx]) {
            fprintf(file, "%ls\n", notes_line);
        }
    }

    fclose(file);

    sprintf(command_line, "%s %s", get_option(Q_OPTION_EDITOR), filename);
    q_cursor_on();
    screen_clear();
    screen_flush();
    spawn_terminal(command_line);
    q_cursor_off();

    file = fopen(filename, "r");
    if (file == NULL) {
        snprintf(notify_message, sizeof(notify_message),
                 _("Error opening file \"%s\" for reading: %s"), filename,
                 strerror(errno));
        notify_form(notify_message, 0);
        return;
    }

    if (entry->notes != NULL) {
        current_notes_idx = 0;
        for (notes_line = entry->notes[current_notes_idx]; notes_line != NULL;
             current_notes_idx++, notes_line =
             entry->notes[current_notes_idx]) {
            Xfree(notes_line, __FILE__, __LINE__);
        }
        Xfree(entry->notes, __FILE__, __LINE__);
    }

    entry->notes = (wchar_t **) Xmalloc(sizeof(wchar_t *), __FILE__, __LINE__);
    notes_length = 0;
    entry->notes[notes_length] = NULL;

    while (!feof(file)) {
        if (fgets(line, sizeof(line), file) == NULL) {
            /*
             * This will cause the outer while's feof() check to fail and
             * smoothly exit the while loop.
             */
            continue;
        }

        while ((strlen(line) > 0) && q_isspace(line[strlen(line) - 1])) {
            /*
             * Trim trailing whitespace
             */
            line[strlen(line) - 1] = '\0';
        }
        notes_length++;
        entry->notes = (wchar_t **) Xrealloc(entry->notes,
                                             (notes_length +
                                              1) * sizeof(wchar_t *), __FILE__,
                                             __LINE__);
        memset(line_wchar, 0, sizeof(line_wchar));
        mbstowcs(line_wchar, line, strlen(line));
        entry->notes[notes_length - 1] =
            Xwcsdup(line_wchar, __FILE__, __LINE__);
        entry->notes[notes_length] = NULL;
    }

    fclose(file);
    unlink(filename);
}

/**
 * Delete one entry from the phonebook.
 *
 * @param entry the entry to remove
 */
static void delete_phonebook_entry(struct q_phone_struct * entry) {
    int i;

    if (entry->prev == NULL) {
        /*
         * Deleting the head
         */
        q_phonebook.entries = entry->next;
        if (entry->next != NULL) {
            entry->next->prev = NULL;
        }
    } else {
        entry->prev->next = entry->next;
        if (entry->next != NULL) {
            entry->next->prev = entry->prev;
        }
    }

    /*
     * No leak
     */
    Xfree(entry->name, __FILE__, __LINE__);
    Xfree(entry->address, __FILE__, __LINE__);
    Xfree(entry->port, __FILE__, __LINE__);
    Xfree(entry->username, __FILE__, __LINE__);
    Xfree(entry->password, __FILE__, __LINE__);
    if (entry->notes != NULL) {
        for (i = 0; entry->notes[i] != NULL; i++) {
            Xfree(entry->notes[i], __FILE__, __LINE__);
        }
        Xfree(entry->notes, __FILE__, __LINE__);
    }
    Xfree(entry->script_filename, __FILE__, __LINE__);
    Xfree(entry->capture_filename, __FILE__, __LINE__);
    Xfree(entry->translate_8bit_filename, __FILE__, __LINE__);
    Xfree(entry->translate_unicode_filename, __FILE__, __LINE__);
    Xfree(entry->keybindings_filename, __FILE__, __LINE__);
    Xfree(entry, __FILE__, __LINE__);
    q_phonebook.entry_count--;
}

/**
 * Popup the tag multiple entry box.
 *
 * @return the tag search string, or NULL if the user cancelled
 */
static char * pick_tag_string() {
    void * pick_window;
    struct field * field;
    struct fieldset * pick_form;
    char * return_string;
    int window_left;
    int window_top;
    int window_height;
    int window_length;
    char * status_string;
    int status_left_stop;
    int field_length;
    char * title;
    int title_left;
    int keystroke;

    window_height = 3;
    window_length = 73;

    /*
     * Window will be centered on the screen
     */
    window_left = WIDTH - 1 - window_length;
    if (window_left < 0) {
        window_left = 0;
    } else {
        window_left /= 2;
    }
    /*
     * ...but six rows above the status line
     */
    window_top = HEIGHT - STATUS_HEIGHT - 1 - 6;
    if (window_top < 0) {
        window_top = 0;
    }

    screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH,
                              Q_COLOR_STATUS);

    status_string = _(" Enter Line #'s,  T-Text  ESC/`-Exit ");
    status_left_stop = WIDTH - strlen(status_string);
    if (status_left_stop <= 0) {
        status_left_stop = 0;
    } else {
        status_left_stop /= 2;
    }
    screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string,
                            Q_COLOR_STATUS);

    pick_window =
        screen_subwin(window_height, window_length, window_top, window_left);
    if (check_subwin_result(pick_window) == Q_FALSE) {
        q_screen_dirty = Q_TRUE;
        return NULL;
    }

    field_length = window_length - strlen(_("Numbers to Tag > ")) - 4;
    field =
        field_malloc(field_length, 1, window_length - field_length - 2, Q_FALSE,
                     Q_COLOR_PHONEBOOK_FIELD_TEXT,
                     Q_COLOR_WINDOW_FIELD_HIGHLIGHTED);
    pick_form = fieldset_malloc(&field, 1, pick_window);

    screen_win_draw_box(pick_window, 0, 0, window_length, window_height);

    title = _("Select Entries");
    title_left = window_length - (strlen(title) + 2);
    if (title_left < 0) {
        title_left = 0;
    } else {
        title_left /= 2;
    }
    screen_win_put_color_printf_yx(pick_window, 0, title_left,
                                   Q_COLOR_WINDOW_BORDER, " %s ", title);

    screen_win_put_color_str_yx(pick_window, 1, 2, _("Numbers to Tag > "),
                                Q_COLOR_MENU_COMMAND);

    screen_flush();
    fieldset_render(pick_form);

    for (;;) {
        qodem_win_getch(pick_window, &keystroke, NULL, Q_KEYBOARD_DELAY);
        switch (keystroke) {

        case '`':
        case Q_KEY_ESCAPE:
            /*
             * The abort exit point
             */
            fieldset_free(pick_form);
            screen_delwin(pick_window);
            q_screen_dirty = Q_TRUE;
            return NULL;
        case Q_KEY_BACKSPACE:
            fieldset_backspace(pick_form);
            break;
        case Q_KEY_LEFT:
            fieldset_left(pick_form);
            break;
        case Q_KEY_RIGHT:
            fieldset_right(pick_form);
            break;
        case Q_KEY_HOME:
            fieldset_home_char(pick_form);
            break;
        case Q_KEY_END:
            fieldset_end_char(pick_form);
            break;
        case Q_KEY_IC:
            fieldset_insert_char(pick_form);
            break;
        case Q_KEY_DC:
            fieldset_delete_char(pick_form);
            break;
        case Q_KEY_ENTER:
            /*
             * The OK exit point
             */
            return_string = field_get_char_value(field);
            fieldset_free(pick_form);
            screen_delwin(pick_window);
            q_screen_dirty = Q_TRUE;
            return return_string;
        default:
            if ((!q_key_code_yes(keystroke)) && (keystroke != ERR)) {
                /*
                 * Pass normal keys to form driver
                 */
                fieldset_keystroke(pick_form, keystroke);
            }
            break;

        }
    } /* for (;;) */

    /*
     * Should never get here.
     */
    return NULL;
}

#ifdef Q_SSH_CRYPTLIB
/**
 * Prompt the user for ssh username and password.
 *
 * @param initial_username the username to show in the username field
 * @param returned_username the final username the user entered
 * @param initial_password the password to show in the username field
 * @param returned_password the final password the user entered
 * @return true if the user pressed enter, false if the user cancelled
 */
static Q_BOOL prompt_ssh_password(const wchar_t * initial_username,
                                  wchar_t ** returned_username,
                                  const wchar_t * initial_password,
                                  wchar_t ** returned_password) {

    void * pick_window;
    struct field ** fields;
    struct fieldset * pick_form;
    int window_left;
    int window_top;
    int window_height;
    int window_length;
    char * status_string;
    int status_left_stop;
    int field_length;
    char * title;
    int title_left;
    int keystroke;
    Q_BOOL old_keyboard_blocks = q_keyboard_blocks;

    window_height = 5;
    window_length = 30;

    /*
     * Window will be centered on the screen
     */
    window_left = WIDTH - 1 - window_length;
    if (window_left < 0) {
        window_left = 0;
    } else {
        window_left /= 2;
    }
    window_top = (HEIGHT - STATUS_HEIGHT) / 2;
    if (window_top < 0) {
        window_top = 0;
    }

    screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH,
                              Q_COLOR_STATUS);

    status_string =
        _(" Enter The SSH Logon Username And Password   ESC/`-Exit ");
    status_left_stop = WIDTH - strlen(status_string);
    if (status_left_stop <= 0) {
        status_left_stop = 0;
    } else {
        status_left_stop /= 2;
    }
    screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string,
                            Q_COLOR_STATUS);

    pick_window =
        screen_subwin(window_height, window_length, window_top, window_left);
    if (check_subwin_result(pick_window) == Q_FALSE) {
        q_cursor_off();
        q_screen_dirty = Q_TRUE;
        return Q_FALSE;
    }

    field_length = window_length - strlen(_("Username ")) - 4;

    fields =
        (struct field **) Xmalloc(2 * sizeof(struct field *), __FILE__,
                                  __LINE__);
    fields[0] =
        field_malloc(field_length, 1, window_length - field_length - 2, Q_FALSE,
                     Q_COLOR_PHONEBOOK_FIELD_TEXT,
                     Q_COLOR_WINDOW_FIELD_HIGHLIGHTED);
    fields[1] =
        field_malloc(field_length, 2, window_length - field_length - 2, Q_FALSE,
                     Q_COLOR_PHONEBOOK_FIELD_TEXT,
                     Q_COLOR_WINDOW_FIELD_HIGHLIGHTED);
    pick_form = fieldset_malloc(fields, 2, pick_window);

    /*
     * Populate with initial data
     */
    field_set_value(fields[0], initial_username);
    field_set_value(fields[1], initial_password);

    screen_win_draw_box(pick_window, 0, 0, window_length, window_height);

    title = _("SSH Logon Credentials");
    title_left = window_length - (strlen(title) + 2);
    if (title_left < 0) {
        title_left = 0;
    } else {
        title_left /= 2;
    }
    screen_win_put_color_printf_yx(pick_window, 0, title_left,
                                   Q_COLOR_WINDOW_BORDER, " %s ", title);

    screen_win_put_color_str_yx(pick_window, 1, 2, _("Username "),
                                Q_COLOR_MENU_COMMAND);
    screen_win_put_color_str_yx(pick_window, 2, 2, _("Password "),
                                Q_COLOR_MENU_COMMAND);

    /*
     * We will use the cursor
     */
    q_cursor_on();
    q_keyboard_blocks = Q_TRUE;

    screen_flush();
    fieldset_render(pick_form);

    for (;;) {
        qodem_win_getch(pick_window, &keystroke, NULL, Q_KEYBOARD_DELAY);
        switch (keystroke) {

        case '`':
        case Q_KEY_ESCAPE:
            /*
             * The abort exit point
             */
            fieldset_free(pick_form);
            screen_delwin(pick_window);
            q_cursor_off();
            q_screen_dirty = Q_TRUE;
            q_keyboard_blocks = old_keyboard_blocks;
            return Q_FALSE;
        case Q_KEY_BACKSPACE:
            fieldset_backspace(pick_form);
            break;
        case Q_KEY_LEFT:
            fieldset_left(pick_form);
            break;
        case Q_KEY_RIGHT:
            fieldset_right(pick_form);
            break;
        case Q_KEY_HOME:
            fieldset_home_char(pick_form);
            break;
        case Q_KEY_END:
            fieldset_end_char(pick_form);
            break;
        case Q_KEY_IC:
            fieldset_insert_char(pick_form);
            break;
        case Q_KEY_DC:
            fieldset_delete_char(pick_form);
            break;
        case Q_KEY_DOWN:
            fieldset_next_field(pick_form);
            fieldset_render(pick_form);
            break;
        case Q_KEY_UP:
            fieldset_prev_field(pick_form);
            fieldset_render(pick_form);
            break;
        case Q_KEY_ENTER:
            /*
             * The OK exit point
             */
            *returned_username = field_get_value(fields[0]);
            *returned_password = field_get_value(fields[1]);
            if ((wcslen(*returned_username) == 0) ||
                (wcslen(*returned_password) == 0)) {
                /*
                 * User pressed enter but field was blank
                 */
                break;
            }
            /*
             * Blank out the password field
             */
            field_set_value(fields[1], L"******");
            fieldset_render(pick_form);
            screen_flush();

            fieldset_free(pick_form);
            screen_delwin(pick_window);
            q_cursor_off();
            q_screen_dirty = Q_TRUE;
            q_keyboard_blocks = old_keyboard_blocks;
            return Q_TRUE;
        default:
            if ((!q_key_code_yes(keystroke)) && (keystroke != ERR)) {
                /*
                 * Pass normal keys to form driver
                 */
                fieldset_keystroke(pick_form, keystroke);
            }
            break;

        }
    } /* for (;;) */

    /*
     * Should never get here.
     */
    abort();
    return Q_FALSE;
}

#endif /* Q_SSH_CRYPTLIB */

#ifndef Q_NO_SERIAL

/**
 * Popup the manual dial phone number prompt.
 *
 * @return the phone number the user entered, or NULL if the user cancelled
 */
static char * pick_manual_phone_number() {
    void * pick_window;
    struct field * field;
    struct fieldset * pick_form;
    char * return_string;
    int window_left;
    int window_top;
    int window_height;
    int window_length;
    char * status_string;
    int status_left_stop;
    int field_length;
    char * title;
    int title_left;
    int keystroke;

    window_height = 3;
    window_length = 73;

    /*
     * Window will be centered on the screen
     */
    window_left = WIDTH - 1 - window_length;
    if (window_left < 0) {
        window_left = 0;
    } else {
        window_left /= 2;
    }
    /*
     * ...but six rows above the status line
     */
    window_top = HEIGHT - STATUS_HEIGHT - 1 - 6;
    if (window_top < 0) {
        window_top = 0;
    }

    screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH,
                              Q_COLOR_STATUS);

    status_string = _(" Enter The Phone Number To Call   ESC/`-Exit ");
    status_left_stop = WIDTH - strlen(status_string);
    if (status_left_stop <= 0) {
        status_left_stop = 0;
    } else {
        status_left_stop /= 2;
    }
    screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string,
                            Q_COLOR_STATUS);

    pick_window =
        screen_subwin(window_height, window_length, window_top, window_left);
    if (check_subwin_result(pick_window) == Q_FALSE) {
        q_screen_dirty = Q_TRUE;
        return NULL;
    }

    field_length = window_length - strlen(_("Phone number > ")) - 4;

    field =
        field_malloc(field_length, 1, window_length - field_length - 2, Q_FALSE,
                     Q_COLOR_PHONEBOOK_FIELD_TEXT,
                     Q_COLOR_WINDOW_FIELD_HIGHLIGHTED);
    pick_form = fieldset_malloc(&field, 1, pick_window);

    screen_win_draw_box(pick_window, 0, 0, window_length, window_height);

    title = _("Manual Dial");
    title_left = window_length - (strlen(title) + 2);
    if (title_left < 0) {
        title_left = 0;
    } else {
        title_left /= 2;
    }
    screen_win_put_color_printf_yx(pick_window, 0, title_left,
                                   Q_COLOR_WINDOW_BORDER, " %s ", title);

    /*
     * Place the prompt text
     */
    screen_win_put_color_str_yx(pick_window, 1, 2, _("Phone number > "),
                                Q_COLOR_MENU_COMMAND);

    screen_flush();
    fieldset_render(pick_form);

    for (;;) {
        qodem_win_getch(pick_window, &keystroke, NULL, Q_KEYBOARD_DELAY);
        switch (keystroke) {

        case '`':
        case Q_KEY_ESCAPE:
            /*
             * The abort exit point
             */
            fieldset_free(pick_form);
            screen_delwin(pick_window);
            q_screen_dirty = Q_TRUE;
            return NULL;
        case Q_KEY_BACKSPACE:
            fieldset_backspace(pick_form);
            break;
        case Q_KEY_LEFT:
            fieldset_left(pick_form);
            break;
        case Q_KEY_RIGHT:
            fieldset_right(pick_form);
            break;
        case Q_KEY_HOME:
            fieldset_home_char(pick_form);
            break;
        case Q_KEY_END:
            fieldset_end_char(pick_form);
            break;
        case Q_KEY_IC:
            fieldset_insert_char(pick_form);
            break;
        case Q_KEY_DC:
            fieldset_delete_char(pick_form);
            break;
        case Q_KEY_ENTER:
            /*
             * The OK exit point
             */
            return_string = field_get_char_value(field);
            fieldset_free(pick_form);
            screen_delwin(pick_window);
            q_screen_dirty = Q_TRUE;
            return return_string;
        default:
            if ((!q_key_code_yes(keystroke)) && (keystroke != ERR)) {
                /*
                 * Pass normal keys to form driver
                 */
                fieldset_keystroke(pick_form, keystroke);
            }
            break;

        }
    }

    /*
     * Should never get here.
     */
    return NULL;
}
#endif /* Q_NO_SERIAL */

/**
 * Ask the user for a print phonebook destination.
 *
 * @return the destination, or NULL if the user cancelled
 */
static char * pick_print_destination() {
    void * pick_window;
    struct field * field;
    struct fieldset * pick_form;
    char * return_string;
    int window_left;
    int window_top;
    int window_height;
    int window_length;
    char * status_string;
    int status_left_stop;
    int field_length;
    char * title;
    int title_left;
    int keystroke;

    window_height = 3;
    window_length = 73;

    /*
     * Window will be centered on the screen
     */
    window_left = WIDTH - 1 - window_length;
    if (window_left < 0) {
        window_left = 0;
    } else {
        window_left /= 2;
    }
    /*
     * ...but six rows above the status line
     */
    window_top = HEIGHT - STATUS_HEIGHT - 1 - 6;
    if (window_top < 0) {
        window_top = 0;
    }

    screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH,
                              Q_COLOR_STATUS);

    status_string =
        _(" Enter The Destination Device Or File Name.   ESC/`-Exit ");
    status_left_stop = WIDTH - strlen(status_string);
    if (status_left_stop <= 0) {
        status_left_stop = 0;
    } else {
        status_left_stop /= 2;
    }
    screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string,
                            Q_COLOR_STATUS);

    pick_window =
        screen_subwin(window_height, window_length, window_top, window_left);
    if (check_subwin_result(pick_window) == Q_FALSE) {
        q_screen_dirty = Q_TRUE;
        return NULL;
    }

    field_length = window_length - strlen(_("Device or File > ")) - 4;

    field =
        field_malloc(field_length, 1, window_length - field_length - 2, Q_FALSE,
                     Q_COLOR_PHONEBOOK_FIELD_TEXT,
                     Q_COLOR_WINDOW_FIELD_HIGHLIGHTED);
    pick_form = fieldset_malloc(&field, 1, pick_window);
    field_set_char_value(field, LPR_FILE_NAME);

    screen_win_draw_box(pick_window, 0, 0, window_length, window_height);

    title = _("Print Phone Book");
    title_left = window_length - (strlen(title) + 2);
    if (title_left < 0) {
        title_left = 0;
    } else {
        title_left /= 2;
    }
    screen_win_put_color_printf_yx(pick_window, 0, title_left,
                                   Q_COLOR_WINDOW_BORDER, " %s ", title);

    screen_win_put_color_str_yx(pick_window, 1, 2, _("Device or File > "),
                                Q_COLOR_MENU_COMMAND);

    screen_flush();
    fieldset_render(pick_form);

    for (;;) {
        qodem_win_getch(pick_window, &keystroke, NULL, Q_KEYBOARD_DELAY);
        switch (keystroke) {

        case '`':
        case Q_KEY_ESCAPE:
            /*
             * The abort exit point
             */
            fieldset_free(pick_form);
            screen_delwin(pick_window);
            q_screen_dirty = Q_TRUE;
            return NULL;
        case Q_KEY_BACKSPACE:
            fieldset_backspace(pick_form);
            break;
        case Q_KEY_LEFT:
            fieldset_left(pick_form);
            break;
        case Q_KEY_RIGHT:
            fieldset_right(pick_form);
            break;
        case Q_KEY_HOME:
            fieldset_home_char(pick_form);
            break;
        case Q_KEY_END:
            fieldset_end_char(pick_form);
            break;
        case Q_KEY_IC:
            fieldset_insert_char(pick_form);
            break;
        case Q_KEY_DC:
            fieldset_delete_char(pick_form);
            break;
        case Q_KEY_ENTER:
            /*
             * The OK exit point
             */
            return_string = field_get_char_value(field);
            fieldset_free(pick_form);
            screen_delwin(pick_window);
            q_screen_dirty = Q_TRUE;
            return return_string;
        default:
            if ((!q_key_code_yes(keystroke)) && (keystroke != ERR)) {
                /*
                 * Pass normal keys to form driver
                 */
                fieldset_keystroke(pick_form, keystroke);
            }
            break;

        }
    }

    /*
     * Should never get here.
     */
    return NULL;
}

/**
 * Switch to next appropriate number to dial.
 */
static void cycle_redialer_number() {
    struct q_phone_struct * entry;
    Q_BOOL wrapped_around;

    /*
     * Note this is identical code to phonebook_keyboard_handler() on
     * KEY_ENTER.  Keep these two functions in sync.
     */

    entry = q_phonebook.selected_entry;
    if (q_phonebook.tagged == 0) {
        /*
         * We're just dialing one number.
         */
        return;
    }

    wrapped_around = Q_FALSE;
    for (;;) {
        entry = entry->next;
        if (entry == NULL) {
            /*
             * Wrap around
             */
            entry = q_phonebook.entries;
            wrapped_around = Q_TRUE;
        }

        if (entry->tagged == Q_TRUE) {
            q_phonebook.selected_entry = entry;
            phonebook_normalize();
            return;
        }

        if ((entry == q_phonebook.selected_entry) &&
            (wrapped_around == Q_TRUE)) {
            /*
             * We circled around and found no tagged entries, so break
             * without setting a new selected_entry.  This is the normal
             * behavior when nothing is tagged.
             */
            return;
        }
    }
}

/**
 * Untag this number, then switch to next appropriate number to dial.
 *
 * @return false if no numbers remain to dial
 */
static Q_BOOL kill_redialer_number() {

    /*
     * Untag it
     */
    if (q_phonebook.selected_entry->tagged == Q_TRUE) {
        q_phonebook.selected_entry->tagged = Q_FALSE;
        q_phonebook.tagged--;
    }

    /*
     * See if anything is tagged
     */
    if (q_phonebook.tagged == 0) {
        return Q_FALSE;
    }

    /*
     * We can still call
     */
    cycle_redialer_number();
    return Q_TRUE;
}

/*
 * The implementations of swprintf() have different parameters and
 * limitations between Windows and POSIX systems.  For Windows systems,
 * implement these workarounds.
 */
#ifdef Q_PDCURSES_WIN32

/**
 * _snwprintf() has trouble with "%s" arguments.  Replace those calls
 * with a simple appender.
 */
static void my_swprintf(wchar_t * str, unsigned int n, wchar_t * format,
                        const char * arg1) {

    unsigned int i;
    for (i = 0; (i < strlen(arg1)) && (i < n); i++) {
        str[wcslen(str) + 1] = 0;
        str[wcslen(str)] = arg1[i];
    }
}

/**
 * Borland and Visual C++'s swprintf are non-conforming.
 */
static void my_swprintf2(wchar_t * str, int n, wchar_t * format, int arg1,
                         const wchar_t * arg2) {

#if defined(__BORLANDC__) || defined(_MSC_VER)
    /*
     * swprintf() doesn't take a length argument
     */
    swprintf(str, format, arg1, arg2);
#else
    swprintf(str, n, format, arg1, arg2);
#endif
}

/**
 * Borland and Visual C++'s swprintf are non-conforming.
 */
static void my_swprintf3(wchar_t * str, int n, wchar_t * format,
                         const wchar_t * arg1) {

#if defined(__BORLANDC__) || defined(_MSC_VER)
    /*
     * swprintf() doesn't take a length argument
     */
    swprintf(str, format, arg1);
#else
    swprintf(str, n, format, arg1);
#endif
}

/**
 * Borland and Visual C++'s swprintf are non-conforming.
 */
static void my_swprintf4(wchar_t * str, int n, wchar_t * format, int arg1) {
#if defined(__BORLANDC__) || defined(_MSC_VER)
    /*
     * swprintf() doesn't take a length argument
     */
    swprintf(str, format, arg1);
#else
    swprintf(str, n, format, arg1);
#endif
}

#else

/* POSIX systems can just use the regular swprintf() */
#define my_swprintf swprintf
#define my_swprintf2 swprintf
#define my_swprintf3 swprintf
#define my_swprintf4 swprintf

#endif /* Q_PDCURSES_WIN32 */

/**
 * Draw screen for the phonebook, including both the phonebook and dialer
 * states.
 */
void phonebook_refresh() {
    char * status_string = NULL;
    int status_left_stop;
    char * title;
    int title_left;
    int window_left;
    int window_top;
    int window_height = HEIGHT - 1;
    int window_length = WIDTH;
    int menu_top, menu_left, menu_title_left;
    char * menu_title = NULL;
    wchar_t entry_buffer[Q_MAX_LINE_LENGTH];
    char * password_stars_buffer;
    int i, j;
    struct q_phone_struct * entry;
    char time_string[TIME_STRING_LENGTH];
    int visible_entries_n = HEIGHT - 1 - 14;
    time_t now;
    int indent = (WIDTH - 80) / 2;

    if (q_screen_dirty == Q_FALSE) {
        return;
    }

    window_left = WIDTH - 1 - window_length;
    if (window_left < 0) {
        window_left = 0;
    } else {
        window_left /= 2;
    }
    window_top = HEIGHT - 1 - window_height;
    if (window_top < 0) {
        window_top = 0;
    } else {
        window_top /= 2;
    }

    screen_draw_box(window_left, window_top, window_left + window_length,
                    window_top + window_height);

    screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH,
                              Q_COLOR_STATUS);

    if (q_program_state == Q_STATE_PHONEBOOK) {
        status_string =
            _(",PgUp/Dn-Move Scroll Bar   ENTER-Dial   ESC/`-Exit ");
    } else if (q_program_state == Q_STATE_DIALER) {
        status_string = _(" C-Cycle   K-Kill   X-eXtend Timer   ESC/`-Exit ");
    }

    status_left_stop = WIDTH - (strlen(status_string) + 3);
    if (status_left_stop <= 0) {
        status_left_stop = 0;
    } else {
        status_left_stop /= 2;
    }
    if (q_program_state == Q_STATE_PHONEBOOK) {
        screen_put_color_char_yx(HEIGHT - 1, status_left_stop, ' ',
                                 Q_COLOR_STATUS);
        screen_put_color_char_yx(HEIGHT - 1, status_left_stop + 1,
                                 cp437_chars[UPARROW], Q_COLOR_STATUS);
        screen_put_color_char_yx(HEIGHT - 1, status_left_stop + 2,
                                 cp437_chars[DOWNARROW], Q_COLOR_STATUS);
        screen_put_color_str_yx(HEIGHT - 1, status_left_stop + 3, status_string,
                                Q_COLOR_STATUS);
    } else if (q_program_state == Q_STATE_DIALER) {
        screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string,
                                Q_COLOR_STATUS);
    }

    screen_put_color_str_yx(window_top + window_height - 1,
                            window_left + window_length - 11, _("F1 Help"),
                            Q_COLOR_WINDOW_BORDER);

    title = _("Phone Book");
    title_left = window_length - (strlen(title) + 2);
    if (title_left < 0) {
        title_left = 0;
    } else {
        title_left /= 2;
    }
    screen_put_color_printf_yx(window_top + 0, window_left + title_left,
                               Q_COLOR_WINDOW_BORDER, " %s ", title);

    /*
     * Draw the heading lines.
     */
    screen_put_color_printf_yx(window_top + 1, indent + window_left + 2,
                               Q_COLOR_MENU_TEXT, _("FON FILE : %s"),
                               q_phonebook.filename);
    screen_put_color_str_yx(window_top + 2, indent + window_left + 2,
                            _("Total Tags > "), Q_COLOR_MENU_TEXT);
    screen_put_color_printf(Q_COLOR_MENU_COMMAND, "%-u", q_phonebook.tagged);
    screen_put_color_str_yx(window_top + 3, indent + window_left + 1,
                            _("[D]   NAME"), Q_COLOR_MENU_COMMAND);

    if (q_phonebook.view_mode == 0) {
#ifndef Q_NO_SERIAL
        screen_put_color_str_yx(window_top + 3, window_left + 38 + indent,
                                _("ADDRESS/COMMAND/NUMBER"),
                                Q_COLOR_MENU_COMMAND);
#else
        screen_put_color_str_yx(window_top + 3, window_left + 38 + indent,
                                _("ADDRESS/COMMAND"), Q_COLOR_MENU_COMMAND);
#endif
        screen_put_color_str_yx(window_top + 3, window_left + 62 + indent,
                                _("METHOD"), Q_COLOR_MENU_COMMAND);
        screen_put_color_str_yx(window_top + 3, window_left + 70 + indent,
                                _("EMULATION"), Q_COLOR_MENU_COMMAND);
    } else if (q_phonebook.view_mode == 1) {
        screen_put_color_str_yx(window_top + 3, window_left + 38 + indent,
                                _("USERNAME"), Q_COLOR_MENU_COMMAND);
        screen_put_color_str_yx(window_top + 3, window_left + 62 + indent,
                                _("PASSWORD"), Q_COLOR_MENU_COMMAND);
    } else if (q_phonebook.view_mode == 2) {
        screen_put_color_str_yx(window_top + 3, window_left + 38 + indent,
                                _("CODEPAGE"), Q_COLOR_MENU_COMMAND);
        screen_put_color_str_yx(window_top + 3, window_left + 47 + indent,
                                _("DOORWAY"), Q_COLOR_MENU_COMMAND);
    } else if (q_phonebook.view_mode == 3) {
        screen_put_color_str_yx(window_top + 3, window_left + 38 + indent,
                                _("TOGGLES"), Q_COLOR_MENU_COMMAND);
        screen_put_color_str_yx(window_top + 3, window_left + 58 + indent,
                                _("SCRIPT"), Q_COLOR_MENU_COMMAND);
    } else if (q_phonebook.view_mode == 4) {
#ifndef Q_NO_SERIAL
        screen_put_color_str_yx(window_top + 3, window_left + 42 + indent,
                                _("SERIAL"), Q_COLOR_MENU_COMMAND);
#endif
        screen_put_color_str_yx(window_top + 3, window_left + 51 + indent,
                                _("TIMES ON"), Q_COLOR_MENU_COMMAND);
        screen_put_color_str_yx(window_top + 3, window_left + 62 + indent,
                                _("LAST CALL"), Q_COLOR_MENU_COMMAND);
    }

    /*
     * Determine the first entry that should be visible on this page.
     */
    entry = q_phonebook.entries;
    for (i = 0; i < (phonebook_entry_i / visible_entries_n) * visible_entries_n;
         i++) {
        entry = entry->next;
    }

    /*
     * Now draw the phonebook entries.
     */
    for (i = 0; (i < visible_entries_n) && (entry != NULL);
         i++, entry = entry->next) {
        wchar_t flag_tagged;
        wchar_t flag_notes;
        Q_COLOR color;

        if ((entry == q_phonebook.selected_entry) &&
            (entry->tagged == Q_TRUE)
        ) {
            color = Q_COLOR_PHONEBOOK_SELECTED_TAGGED;
        } else if (entry == q_phonebook.selected_entry) {
            color = Q_COLOR_PHONEBOOK_SELECTED;
        } else if (entry->tagged == Q_TRUE) {
            color = Q_COLOR_PHONEBOOK_TAGGED;
        } else {
            color = Q_COLOR_PHONEBOOK_ENTRY;
        }
        if (entry->tagged == Q_TRUE) {
            if (entry->quicklearn == Q_TRUE) {
                flag_tagged = 'Q';
            } else {
                flag_tagged = cp437_chars[CHECK];
            }
        } else {
            flag_tagged = ' ';
        }
        if (entry->notes != NULL) {
            flag_notes = cp437_chars[TRIPLET];
        } else {
            flag_notes = ' ';
        }
        memset(entry_buffer, 0, sizeof(entry_buffer));

        /*
         * Center on screen
         */
        for (j = 0; j < indent; j++) {
            entry_buffer[j] = ' ';
        }

        /*
         * NAME
         */
        my_swprintf2(entry_buffer + wcslen(entry_buffer),
                     Q_MAX_LINE_LENGTH - wcslen(entry_buffer),
                     L"%-3u   %ls",
                     (i + 1 + (visible_entries_n * phonebook_page)),
                     entry->name);
        if (wcslen(entry_buffer) >= 36 + indent) {
            entry_buffer[36 + indent] = ' ';
            entry_buffer[37 + indent] = '\0';
        } else if (wcslen(entry_buffer) < 36 + indent) {
            for (j = wcslen(entry_buffer); j < 37 + indent; j++) {
                entry_buffer[j] = ' ';
            }
            entry_buffer[37 + indent] = '\0';
        }

        if (q_phonebook.view_mode == 0) {
            /*
             * ADDRESS
             */
            my_swprintf(entry_buffer + wcslen(entry_buffer),
                        Q_MAX_LINE_LENGTH - wcslen(entry_buffer),
                        L"%s", entry->address);
            if (wcslen(entry_buffer) >= 60 + indent) {
                entry_buffer[60 + indent] = ' ';
                entry_buffer[61 + indent] = '\0';
            } else if (wcslen(entry_buffer) < 60 + indent) {
                for (j = wcslen(entry_buffer); j < 61 + indent; j++) {
                    entry_buffer[j] = ' ';
                }
                entry_buffer[61 + indent] = '\0';
            }

            /*
             * METHOD
             */
            my_swprintf(entry_buffer + wcslen(entry_buffer),
                        Q_MAX_LINE_LENGTH - wcslen(entry_buffer),
                        L"%s", method_string(entry->method));
            if (wcslen(entry_buffer) >= 68 + indent) {
                entry_buffer[68 + indent] = ' ';
                entry_buffer[69 + indent] = '\0';
            } else if (wcslen(entry_buffer) < 68 + indent) {
                for (j = wcslen(entry_buffer); j < 69 + indent; j++) {
                    entry_buffer[j] = ' ';
                }
                entry_buffer[69 + indent] = '\0';
            }

            /*
             * EMULATION
             */
            my_swprintf(entry_buffer + wcslen(entry_buffer),
                        Q_MAX_LINE_LENGTH - wcslen(entry_buffer),
                        L"%s", emulation_string(entry->emulation));

        } else if (q_phonebook.view_mode == 1) {
            /*
             * USERNAME
             */
            my_swprintf3(entry_buffer + wcslen(entry_buffer),
                         Q_MAX_LINE_LENGTH - wcslen(entry_buffer),
                         L"%ls", entry->username);
            if (wcslen(entry_buffer) >= 60 + indent) {
                entry_buffer[60 + indent] = ' ';
                entry_buffer[61 + indent] = '\0';
            } else if (wcslen(entry_buffer) < 60 + indent) {
                for (j = wcslen(entry_buffer); j < 61 + indent; j++) {
                    entry_buffer[j] = ' ';
                }
                entry_buffer[61 + indent] = '\0';
            }

            /*
             * PASSWORD
             */
            /*
             * Show it only as stars
             */
            password_stars_buffer =
                (char *) Xmalloc(wcslen(entry->password) + 1, __FILE__,
                                 __LINE__);
            memset(password_stars_buffer, 0, wcslen(entry->password) + 1);
            for (j = 0; j < wcslen(entry->password); j++) {
                password_stars_buffer[j] = '*';
            }
            my_swprintf(entry_buffer + wcslen(entry_buffer),
                        Q_MAX_LINE_LENGTH - wcslen(entry_buffer),
                        L"%s", password_stars_buffer);
            Xfree(password_stars_buffer, __FILE__, __LINE__);

        } else if (q_phonebook.view_mode == 2) {
            /*
             * CODEPAGE
             */
            my_swprintf(entry_buffer + wcslen(entry_buffer),
                        Q_MAX_LINE_LENGTH - wcslen(entry_buffer),
                        L"%s", codepage_string(entry->codepage));
            if (wcslen(entry_buffer) >= 45 + indent) {
                entry_buffer[45 + indent] = ' ';
                entry_buffer[46 + indent] = '\0';
            } else if (wcslen(entry_buffer) < 45 + indent) {
                for (j = wcslen(entry_buffer); j < 46 + indent; j++) {
                    entry_buffer[j] = ' ';
                }
                entry_buffer[46 + indent] = '\0';
            }

            /*
             * DOORWAY
             */
            my_swprintf(entry_buffer + wcslen(entry_buffer),
                        WIDTH - 2 - wcslen(entry_buffer),
                        L"%s", doorway_string(entry->doorway));

        } else if (q_phonebook.view_mode == 3) {

            /*
             * TOGGLES
             */
            if (entry->use_default_toggles == Q_FALSE) {
                my_swprintf(entry_buffer + wcslen(entry_buffer),
                            Q_MAX_LINE_LENGTH - wcslen(entry_buffer),
                            L"%s", toggles_to_string(entry->toggles));
            } else {
                my_swprintf(entry_buffer + wcslen(entry_buffer),
                            Q_MAX_LINE_LENGTH - wcslen(entry_buffer),
                            L"%s", _("Defaults"));
            }

            if (wcslen(entry_buffer) >= 56 + indent) {
                entry_buffer[56 + indent] = ' ';
                entry_buffer[57 + indent] = '\0';
            } else if (wcslen(entry_buffer) < 56 + indent) {
                for (j = wcslen(entry_buffer); j < 57 + indent; j++) {
                    entry_buffer[j] = ' ';
                }
                entry_buffer[57 + indent] = '\0';
            }

            /*
             * SCRIPT
             */
            my_swprintf(entry_buffer + wcslen(entry_buffer),
                        WIDTH - 2 - wcslen(entry_buffer),
                        L"%s", entry->script_filename);

        } else if (q_phonebook.view_mode == 4) {
#ifndef Q_NO_SERIAL
            /*
             * PORT SETTINGS
             */
            if (entry->use_modem_cfg == Q_TRUE) {
                my_swprintf(entry_buffer + wcslen(entry_buffer),
                            Q_MAX_LINE_LENGTH - wcslen(entry_buffer),
                            L"%s", _(" Modem Cfg"));
            } else {
                my_swprintf(entry_buffer + wcslen(entry_buffer),
                            Q_MAX_LINE_LENGTH - wcslen(entry_buffer),
                            L"%6s", baud_string(entry->baud));
                my_swprintf(entry_buffer + wcslen(entry_buffer),
                         Q_MAX_LINE_LENGTH - wcslen(entry_buffer),
                            L"%s", " ");
                my_swprintf(entry_buffer + wcslen(entry_buffer),
                            Q_MAX_LINE_LENGTH - wcslen(entry_buffer),
                            L"%s", data_bits_string(entry->data_bits));
                my_swprintf(entry_buffer + wcslen(entry_buffer),
                            Q_MAX_LINE_LENGTH - wcslen(entry_buffer),
                            L"%s", parity_string(entry->parity, Q_TRUE));
                my_swprintf(entry_buffer + wcslen(entry_buffer),
                            Q_MAX_LINE_LENGTH - wcslen(entry_buffer),
                            L"%s", stop_bits_string(entry->stop_bits));
            }
#endif

            if (wcslen(entry_buffer) >= 53 + indent) {
                entry_buffer[52 + indent] = ' ';
                entry_buffer[53 + indent] = '\0';
            } else if (wcslen(entry_buffer) < 52 + indent) {
                for (j = wcslen(entry_buffer); j < 53 + indent; j++) {
                    entry_buffer[j] = ' ';
                }
                entry_buffer[53 + indent] = '\0';
            }

            /*
             * TIMES ON
             */
            if (entry->times_on > 0) {
                my_swprintf4(entry_buffer + wcslen(entry_buffer),
                             Q_MAX_LINE_LENGTH - wcslen(entry_buffer),
                             L"%5u", entry->times_on);
                if (wcslen(entry_buffer) >= 60 + indent) {
                    entry_buffer[60 + indent] = ' ';
                    entry_buffer[61 + indent] = '\0';
                } else if (wcslen(entry_buffer) < 60 + indent) {
                    for (j = wcslen(entry_buffer); j < 61 + indent; j++) {
                        entry_buffer[j] = ' ';
                    }
                    entry_buffer[61 + indent] = '\0';
                }

                /*
                 * LAST CALL
                 */
                strftime(time_string, sizeof(time_string),
                         "%a, %d %b %Y %H:%M:%S %z",
                         localtime(&entry->last_call));
                my_swprintf(entry_buffer + wcslen(entry_buffer),
                            WIDTH - 2 - wcslen(entry_buffer),
                            L"%s", time_string);
            }
        }

        screen_put_color_wcs_yx(window_top + 4 + i, window_left + 1,
                                entry_buffer, color);

        screen_put_color_char_yx(window_top + 4 + i, window_left + 4 + indent,
                                 flag_notes, color);
        screen_put_color_char_yx(window_top + 4 + i, window_left + 5 + indent,
                                 flag_tagged, color);

        /*
         * Pad out to the end
         */
        screen_put_color_hline_yx(window_top + 4 + i,
                                  window_left + 1 + wcslen(entry_buffer), ' ',
                                  window_length - wcslen(entry_buffer) - 2,
                                  color);
    }

    /*
     * Display the bottom pane.  This will either be menu command hints
     * (Q_STATE_PHONEBOOK) or messages from the modem (Q_STATE_DIALER).
     */
    menu_left = window_left + 1;
    menu_top = window_top + window_height - 9;
    screen_put_color_char_yx(menu_top - 1, menu_left - 1,
                             cp437_chars[Q_WINDOW_LEFT_TEE],
                             Q_COLOR_WINDOW_BORDER);
    screen_put_color_char_yx(menu_top - 1, menu_left - 1 + window_length - 1,
                             cp437_chars[Q_WINDOW_RIGHT_TEE],
                             Q_COLOR_WINDOW_BORDER);
    screen_put_color_hline_yx(menu_top - 1, menu_left,
                              cp437_chars[Q_WINDOW_TOP], window_length - 2,
                              Q_COLOR_WINDOW_BORDER);

    if (q_program_state == Q_STATE_PHONEBOOK) {
        menu_title = _("Commands");
    } else if (q_program_state == Q_STATE_DIALER) {
        menu_title = _("Redialer");
    }

    menu_title_left = window_length - (strlen(menu_title) + 2);
    if (menu_title_left < 0) {
        menu_title_left = 0;
    } else {
        menu_title_left /= 2;
    }
    screen_put_color_printf_yx(menu_top - 1, menu_left - 1 + menu_title_left,
                               Q_COLOR_WINDOW_BORDER, " %s ", menu_title);

    menu_left = 1 + indent;

    if (q_program_state == Q_STATE_PHONEBOOK) {

        /*
         * Phone menu hints.
         */
        screen_put_color_str_yx(menu_top + 0, menu_left + 13, _("Entries"),
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str_yx(menu_top + 1, menu_left + 1, _("      SP"),
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_(" - Tag/Untag"), Q_COLOR_MENU_TEXT);

        screen_put_color_str_yx(menu_top + 2, menu_left + 1, _("   I-Ins"),
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_(" - Insert New Entry"), Q_COLOR_MENU_TEXT);

        screen_put_color_str_yx(menu_top + 3, menu_left + 1, _("^D/D-Del"),
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_(" - Delete Tagged/Bar"), Q_COLOR_MENU_TEXT);

        screen_put_color_str_yx(menu_top + 4, menu_left + 1, "    ^R/R",
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_(" - Revise Tagged/Bar"), Q_COLOR_MENU_TEXT);

        screen_put_color_str_yx(menu_top + 5, menu_left + 1, "       T",
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_(" - Tag Multiple"), Q_COLOR_MENU_TEXT);

        screen_put_color_str_yx(menu_top + 6, menu_left + 1, "       U",
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_(" - Untag all"), Q_COLOR_MENU_TEXT);

        screen_put_color_str_yx(menu_top + 7, menu_left + 1, "       Q",
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_(" - QuickLearn"), Q_COLOR_MENU_TEXT);

#ifndef Q_NO_SERIAL
        screen_put_color_str_yx(menu_top + 0, menu_left + 39, _("Dial"),
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str_yx(menu_top + 1, menu_left + 35, "M",
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_(" - Manual Dial"), Q_COLOR_MENU_TEXT);
#endif

        screen_put_color_str_yx(menu_top + 3, menu_left + 39, _("Edit"),
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str_yx(menu_top + 4, menu_left + 35, "N",
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_(" - Attached Note"), Q_COLOR_MENU_TEXT);

        screen_put_color_str_yx(menu_top + 5, menu_left + 35, "V",
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_(" - Linked Script"), Q_COLOR_MENU_TEXT);

        screen_put_color_str_yx(menu_top + 0, menu_left + 62, _("FON"),
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str_yx(menu_top + 1, menu_left + 58, "F",
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_(" - Find Text"), Q_COLOR_MENU_TEXT);

        screen_put_color_str_yx(menu_top + 2, menu_left + 58, "A",
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_(" - Find Again"), Q_COLOR_MENU_TEXT);

        screen_put_color_str_yx(menu_top + 3, menu_left + 58, "L",
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_(" - Load"), Q_COLOR_MENU_TEXT);

        screen_put_color_str_yx(menu_top + 4, menu_left + 58, "O",
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_(" - Other Info"), Q_COLOR_MENU_TEXT);

        screen_put_color_str_yx(menu_top + 5, menu_left + 55, "^P/P",
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_(" - Print 132/80"), Q_COLOR_MENU_TEXT);

        screen_put_color_str_yx(menu_top + 6, menu_left + 58, "S",
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_(" - Sort"), Q_COLOR_MENU_TEXT);

        screen_put_color_str_yx(menu_top + 7, menu_left + 57, "^U",
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_(" - Undo"), Q_COLOR_MENU_TEXT);

    } else if (q_program_state == Q_STATE_DIALER) {

        /*
         * The dialer screen.
         */

        /*
         * Figure out if we need to change state now
         */
        switch (q_dial_state) {

        case Q_DIAL_CYCLE:
            /*
             * Recompute the time remaining
             */
            time(&now);
            q_dialer_cycle_time = 1 - (now - q_dialer_cycle_start_time);
            if (q_dialer_cycle_time > 0) {
                break;
            }

            /*
             * We're out of time, fall through
             */
            q_dial_state = Q_DIAL_BETWEEN_PAUSE;
            /*
             * Hold this state for 5 seconds
             */
            time(&q_dialer_cycle_start_time);

            /*
             * Fall through ...
             */
        case Q_DIAL_BETWEEN_PAUSE:
            /*
             * Recompute the time remaining
             */
            time(&now);
            q_dialer_cycle_time = atoi(get_option(Q_OPTION_DIAL_BETWEEN_TIME)) -
                (now - q_dialer_cycle_start_time);

            if (q_dialer_cycle_time > 0) {
                break;
            }

            /*
             * Cycle
             */
            cycle_redialer_number();

            /*
             * Re-do the dial
             */
            q_current_dial_entry = q_phonebook.selected_entry;
            do_dialer();
            return;

        case Q_DIAL_DIALING:
            /*
             * Recompute the time remaining
             */
            time(&now);
            q_dialer_cycle_time = atoi(get_option(Q_OPTION_DIAL_CONNECT_TIME)) -
                (now - q_dialer_cycle_start_time);

            /*
             * If we're out of time, cycle
             */
            if (q_dialer_cycle_time == 0) {
                q_dial_state = Q_DIAL_CYCLE;
                /*
                 * Hold this state for one second
                 */
                time(&q_dialer_cycle_start_time);
                close_dial_entry();
            }

            break;

        case Q_DIAL_LINE_BUSY:
            /*
             * Recompute the time remaining
             */
            time(&now);
            q_dialer_cycle_time = 1 - (now - q_dialer_cycle_start_time);
            if (q_dialer_cycle_time > 0) {
                break;
            }

            /*
             * We're out of time, fall through
             */
            q_dial_state = Q_DIAL_BETWEEN_PAUSE;
            /*
             * Hold this state for 5 seconds
             */
            time(&q_dialer_cycle_start_time);
            break;

        case Q_DIAL_MANUAL_CYCLE:
            /*
             * Recompute the time remaining
             */
            time(&now);
            q_dialer_cycle_time = 1 - (now - q_dialer_cycle_start_time);
            if (q_dialer_cycle_time > 0) {
                break;
            }

            /*
             * We're out of time, fall through
             */
            q_dial_state = Q_DIAL_BETWEEN_PAUSE;
            /*
             * Hold this state for 5 seconds
             */
            time(&q_dialer_cycle_start_time);
            break;

        case Q_DIAL_KILLED:

            /*
             * Recompute the time remaining
             */
            time(&now);
            q_dialer_cycle_time = 1 - (now - q_dialer_cycle_start_time);
            if (q_dialer_cycle_time > 0) {
                break;
            }

            /*
             * We're out of time, fall through
             */
            q_dial_state = Q_DIAL_BETWEEN_PAUSE;
            /*
             * Hold this state for 5 seconds
             */
            time(&q_dialer_cycle_start_time);
            break;

        case Q_DIAL_CONNECTED:
            /*
             * Recompute the time remaining
             */
            time(&now);
            q_dialer_cycle_time = 3 - (now - q_dialer_cycle_start_time);
            if (q_dialer_cycle_time > 0) {
                break;
            }

            /*
             * Time is up, switch to console
             */
            switch_state(Q_STATE_CONSOLE);
            q_screen_dirty = Q_TRUE;
            check_for_dialup_script();
            break;

        case Q_DIAL_USER_ABORTED:
        case Q_DIAL_NO_NUMBERS_LEFT:

            /*
             * Recompute the time remaining
             */
            time(&now);
            q_dialer_cycle_time = 1 - (now - q_dialer_cycle_start_time);
            if (q_dialer_cycle_time > 0) {
                break;
            }
            /*
             * Now return to phonebook
             */
            close_dial_entry();
            q_current_dial_entry = NULL;
            switch_state(Q_STATE_PHONEBOOK);
            q_screen_dirty = Q_TRUE;
            /*
             * We need to explicitly call refresh_handler() because
             * phonebook_keyboard_handler() blocks.
             */
            refresh_handler();
            return;
        }

        /*
         * Put up the dialing message.
         */
        switch (q_dial_state) {

        case Q_DIAL_DIALING:
            sprintf(q_dialer_status_message,
                    _("%-3d Seconds remain until Cycle"),
                    (int) q_dialer_cycle_time);
            break;

        case Q_DIAL_CYCLE:
            sprintf(q_dialer_status_message, _("Dial timing period expired"));
            break;

        case Q_DIAL_BETWEEN_PAUSE:
            sprintf(q_dialer_status_message, _("Redial pausing %3d"),
                    (int) q_dialer_cycle_time);
            break;

        case Q_DIAL_LINE_BUSY:
            assert(q_current_dial_entry != NULL);
#ifndef Q_NO_SERIAL
            if (q_current_dial_entry->method == Q_DIAL_METHOD_MODEM) {
                sprintf(q_dialer_status_message,
                        _("Line busy or modem timed out"));
            } else {
#endif
                sprintf(q_dialer_status_message,
                        _("Network failed to connect"));
#ifndef Q_NO_SERIAL
            }
#endif
            break;

        case Q_DIAL_MANUAL_CYCLE:
            sprintf(q_dialer_status_message, _("Manual Cycle"));
            break;

        case Q_DIAL_KILLED:
            sprintf(q_dialer_status_message, _("Number has been un-tagged"));
            break;

        case Q_DIAL_CONNECTED:
            sprintf(q_dialer_status_message,
                    _("CONNECTED, press a key to continue"));
            break;

        case Q_DIAL_NO_NUMBERS_LEFT:
            sprintf(q_dialer_status_message,
                    _("No numbers remaining, Dialing aborted"));
            break;

        case Q_DIAL_USER_ABORTED:
            sprintf(q_dialer_status_message,
                    _("[ESC/`] pressed, Dialing aborted"));
            break;

        }

        assert(q_current_dial_entry != NULL);

        /*
         * Name
         */
        screen_put_color_str_yx(menu_top + 1, menu_left + 2, _("Name   : "),
                                Q_COLOR_MENU_TEXT);
        screen_put_color_wcs(q_current_dial_entry->name, Q_COLOR_MENU_COMMAND);

#ifndef Q_NO_SERIAL
        if (q_current_dial_entry->method == Q_DIAL_METHOD_MODEM) {
            /*
             * Number
             */
            screen_put_color_str_yx(menu_top + 2, menu_left + 2, _("Number : "),
                                    Q_COLOR_MENU_TEXT);
        } else {
#endif
            /*
             * Address
             */
            screen_put_color_str_yx(menu_top + 2, menu_left + 2, _("Address: "),
                                    Q_COLOR_MENU_TEXT);
#ifndef Q_NO_SERIAL
        }
#endif
        screen_put_color_str(q_current_dial_entry->address,
                             Q_COLOR_MENU_COMMAND);

        /*
         * Script
         */
        screen_put_color_str_yx(menu_top + 3, menu_left + 2, _("Script : "),
                                Q_COLOR_MENU_TEXT);
        screen_put_color_str(q_current_dial_entry->script_filename,
                             Q_COLOR_MENU_COMMAND);

#ifndef Q_NO_SERIAL
        assert(q_current_dial_entry != NULL);
        if (q_current_dial_entry->method == Q_DIAL_METHOD_MODEM) {
            /*
             * Modem
             */
            screen_put_color_str_yx(menu_top + 5, menu_left + 2, _("Modem  : "),
                                    Q_COLOR_MENU_TEXT);
        } else {
#endif
            /*
             * Network
             */
            screen_put_color_str_yx(menu_top + 5, menu_left + 2, _("Network: "),
                                    Q_COLOR_MENU_TEXT);
#ifndef Q_NO_SERIAL
        }
#endif
        screen_put_color_str(q_dialer_modem_message, Q_COLOR_MENU_COMMAND);

        /*
         * Status
         */
        screen_put_color_str_yx(menu_top + 6, menu_left + 2, _("Status : "),
                                Q_COLOR_MENU_TEXT);
        screen_put_color_str(q_dialer_status_message, Q_COLOR_MENU_COMMAND);

        /*
         * Last On
         */
        screen_put_color_str_yx(menu_top + 1, menu_left + 57, _("Last On : "),
                                Q_COLOR_MENU_TEXT);
        if (q_current_dial_entry->times_on > 0) {
            strftime(time_string, sizeof(time_string), "%m/%d/%Y",
                     localtime(&q_current_dial_entry->last_call));
            screen_put_color_str(time_string, Q_COLOR_MENU_COMMAND);
        }

        /*
         * Total #
         */
        screen_put_color_str_yx(menu_top + 2, menu_left + 57, _("Total # : "),
                                Q_COLOR_MENU_TEXT);
        screen_put_color_printf(Q_COLOR_MENU_COMMAND, "%d",
                                q_current_dial_entry->times_on);

        /*
         * Attempt
         */
        screen_put_color_str_yx(menu_top + 3, menu_left + 57, _("Attempt : "),
                                Q_COLOR_MENU_TEXT);
        screen_put_color_printf(Q_COLOR_MENU_COMMAND, "%d", q_dialer_attempts);

        /*
         * Start
         */
        screen_put_color_str_yx(menu_top + 5, menu_left + 57, _("Start   : "),
                                Q_COLOR_MENU_TEXT);
        strftime(time_string, sizeof(time_string), "%H:%M:%S",
                 localtime(&q_dialer_start_time));
        screen_put_color_str(time_string, Q_COLOR_MENU_COMMAND);

        /*
         * Current
         */
        screen_put_color_str_yx(menu_top + 6, menu_left + 57, _("Current : "),
                                Q_COLOR_MENU_TEXT);
        time(&now);
        strftime(time_string, sizeof(time_string), "%H:%M:%S", localtime(&now));
        screen_put_color_str(time_string, Q_COLOR_MENU_COMMAND);

    }

    /*
     * Push it to the screen
     */
    screen_flush();

    /*
     * Check found_note_flag.  If true, pop up a notification that find/find
     * again found the text in a note.
     */
    if (q_program_state == Q_STATE_PHONEBOOK) {
        if (found_note_flag == Q_TRUE) {
            notify_form(_("Text found in attached Note"), 1.5);
            found_note_flag = Q_FALSE;
            /*
             * Don't clear the dirty flag, we get to do this all again.
             */
            phonebook_refresh();
        } else {
            /*
             * Other states want dynamic updates
             */
            q_screen_dirty = Q_FALSE;
        }
    }

}

/**
 * Popup the emulation pick box.
 *
 * @return the emulation, or -1 if the user cancelled
 */
static Q_EMULATION pick_emulation() {
    void * pick_window;
    int window_left;
    int window_top;
    int window_height;
    int window_length;
    char * title;
    char selection_buffer[EMULATION_STRING_SIZE];

    int title_left;
    int keystroke;
    int i;
    int selected_field;

    title = _("Emulations");

    window_height = Q_EMULATION_MAX + 2;
    window_length = strlen(title) + 4;

    window_left = WIDTH - 1 - window_length;
    if (window_left < 0) {
        window_left = 0;
    } else {
        window_left /= 2;
    }
    window_top = HEIGHT - 1 - window_height;
    if (window_top < 0) {
        window_top = 0;
    } else {
        window_top /= 2;
    }

    pick_window =
        screen_subwin(window_height, window_length, window_top, window_left);
    if (check_subwin_result(pick_window) == Q_FALSE) {
        return -1;
    }

    screen_win_draw_box(pick_window, 0, 0, window_length, window_height);

    title_left = window_length - (strlen(title) + 2);
    if (title_left < 0) {
        title_left = 0;
    } else {
        title_left /= 2;
    }
    screen_win_put_color_printf_yx(pick_window, 0, title_left,
                                   Q_COLOR_WINDOW_BORDER, " %s ", title);

    selected_field = 0;

    for (;;) {

        for (i = 0; i < Q_EMULATION_MAX; i++) {

            snprintf(selection_buffer, sizeof(selection_buffer), " %s",
                     emulation_string(i));
            if (strlen(selection_buffer) < window_length - 3) {
                memset(selection_buffer + strlen(selection_buffer), ' ',
                       window_length - 2 - strlen(selection_buffer));
                selection_buffer[window_length - 2] = '\0';
            }

            if (selected_field == i) {
                screen_win_put_color_str_yx(pick_window, i + 1, 1,
                                            selection_buffer,
                                            Q_COLOR_PHONEBOOK_SELECTED);
            } else {
                screen_win_put_color_str_yx(pick_window, i + 1, 1,
                                            selection_buffer,
                                            Q_COLOR_PHONEBOOK_ENTRY);
            }
        }

        screen_win_flush(pick_window);
        screen_flush();

        qodem_win_getch(pick_window, &keystroke, NULL, Q_KEYBOARD_DELAY);
        switch (keystroke) {
        case '`':
        case Q_KEY_ESCAPE:
            /*
             * The abort exit point
             */
            screen_delwin(pick_window);
            return -1;
        case Q_KEY_DOWN:
            selected_field++;
            if (selected_field == Q_EMULATION_MAX) {
                selected_field = 0;
            }
            break;
        case Q_KEY_UP:
            selected_field--;
            if (selected_field < 0) {
                selected_field = Q_EMULATION_MAX - 1;
            }
            break;
        case Q_KEY_HOME:
            selected_field = 0;
            break;
        case Q_KEY_END:
            selected_field = Q_EMULATION_MAX - 1;
            break;
        case Q_KEY_ENTER:
        case Q_KEY_F(10):
            /*
             * The OK exit point
             */
            screen_delwin(pick_window);
            return selected_field;
        default:
            /*
             * Ignore
             */
            break;
        }
    } /* for (;;) */

    /*
     * Should never get here.
     */
    return -1;
}

/**
 * Popup the codepage pick box.  The available codepages differs based on
 * emulation.
 *
 * @param emulation the emulation enum
 * @return the codepage, or -1 if the user cancelled
 */
static Q_CODEPAGE pick_codepage(Q_EMULATION emulation) {
    void * pick_window;
    int window_left;
    int window_top;
    int window_height;
    int window_length;
    char * title;
    char selection_buffer[CODEPAGE_STRING_SIZE];

    int title_left;
    int keystroke;
    int i;
    int selected_field;

    /*
     * Do not permit selecting a codepage for those emulations that provide
     * their own codepage support.
     */
    switch (emulation) {
    case Q_EMUL_VT52:
    case Q_EMUL_VT100:
    case Q_EMUL_VT102:
    case Q_EMUL_VT220:
    case Q_EMUL_LINUX_UTF8:
    case Q_EMUL_XTERM_UTF8:
    case Q_EMUL_PETSCII:
    case Q_EMUL_ATASCII:
        return -1;
    case Q_EMUL_TTY:
    case Q_EMUL_DEBUG:
    case Q_EMUL_ANSI:
    case Q_EMUL_AVATAR:
    case Q_EMUL_LINUX:
    case Q_EMUL_XTERM:
        break;
    }

    title = _("Codepages");

    window_height = Q_CODEPAGE_PHONEBOOK_MAX + 2;
    window_length = strlen(codepage_string(Q_CODEPAGE_ISO8859_1)) + 4;

    window_left = WIDTH - 1 - window_length;
    if (window_left < 0) {
        window_left = 0;
    } else {
        window_left /= 2;
    }
    window_top = HEIGHT - 1 - window_height;
    if (window_top < 0) {
        window_top = 0;
    } else {
        window_top /= 2;
    }

    pick_window =
        screen_subwin(window_height, window_length, window_top, window_left);
    if (check_subwin_result(pick_window) == Q_FALSE) {
        return -1;
    }

    screen_win_draw_box(pick_window, 0, 0, window_length, window_height);

    title_left = window_length - (strlen(title) + 2);
    if (title_left < 0) {
        title_left = 0;
    } else {
        title_left /= 2;
    }
    screen_win_put_color_printf_yx(pick_window, 0, title_left,
                                   Q_COLOR_WINDOW_BORDER, " %s ", title);

    selected_field = 0;

    for (;;) {

        for (i = 0; i < Q_CODEPAGE_PHONEBOOK_MAX; i++) {

            snprintf(selection_buffer, sizeof(selection_buffer), " %s",
                     codepage_string(i));
            if (strlen(selection_buffer) < window_length - 3) {
                memset(selection_buffer + strlen(selection_buffer), ' ',
                       window_length - 2 - strlen(selection_buffer));
                selection_buffer[window_length - 2] = '\0';
            }

            if (selected_field == i) {
                screen_win_put_color_str_yx(pick_window, i + 1, 1,
                                            selection_buffer,
                                            Q_COLOR_PHONEBOOK_SELECTED);
            } else {
                screen_win_put_color_str_yx(pick_window, i + 1, 1,
                                            selection_buffer,
                                            Q_COLOR_PHONEBOOK_ENTRY);
            }
        }

        screen_win_flush(pick_window);
        screen_flush();

        qodem_win_getch(pick_window, &keystroke, NULL, Q_KEYBOARD_DELAY);
        switch (keystroke) {
        case '`':
        case Q_KEY_ESCAPE:
            /*
             * The abort exit point
             */
            screen_delwin(pick_window);
            return -1;
        case Q_KEY_DOWN:
            selected_field++;
            if (selected_field == Q_CODEPAGE_PHONEBOOK_MAX) {
                selected_field = 0;
            }
            break;
        case Q_KEY_UP:
            selected_field--;
            if (selected_field < 0) {
                selected_field = Q_CODEPAGE_PHONEBOOK_MAX - 1;
            }
            break;
        case Q_KEY_HOME:
            selected_field = 0;
            break;
        case Q_KEY_END:
            selected_field = Q_CODEPAGE_PHONEBOOK_MAX - 1;
            break;
        case Q_KEY_ENTER:
        case Q_KEY_F(10):
            /*
             * The OK exit point
             */
            screen_delwin(pick_window);
            return selected_field;
        default:
            /*
             * Ignore
             */
            break;
        }
    } /* for (;;) */

    /*
     * Should never get here.
     */
    return -1;
}

/**
 * Popup the method pick box.
 *
 * @return the method, or -1 if the user cancelled
 */
static Q_DIAL_METHOD pick_method() {
    void * pick_window;
    int window_left;
    int window_top;
    int window_height;
    int window_length;
    char * title;
    char selection_buffer[METHOD_STRING_SIZE];

    int title_left;
    int keystroke;
    int i;
    int selected_field;

    title = _("Connection Methods");

    window_height = Q_DIAL_METHOD_MAX + 2;
    window_length = strlen(title) + 4;

    window_left = WIDTH - 1 - window_length;
    if (window_left < 0) {
        window_left = 0;
    } else {
        window_left /= 2;
    }
    window_top = HEIGHT - 1 - window_height;
    if (window_top < 0) {
        window_top = 0;
    } else {
        window_top /= 2;
    }

    pick_window =
        screen_subwin(window_height, window_length, window_top, window_left);
    if (check_subwin_result(pick_window) == Q_FALSE) {
        return -1;
    }

    screen_win_draw_box(pick_window, 0, 0, window_length, window_height);

    title_left = window_length - (strlen(title) + 2);
    if (title_left < 0) {
        title_left = 0;
    } else {
        title_left /= 2;
    }
    screen_win_put_color_printf_yx(pick_window, 0, title_left,
                                   Q_COLOR_WINDOW_BORDER, " %s ", title);

    selected_field = 0;

    for (;;) {

        for (i = 0; i < Q_DIAL_METHOD_MAX; i++) {

            snprintf(selection_buffer, sizeof(selection_buffer), " %s",
                     method_string(i));
            if (strlen(selection_buffer) < window_length - 3) {
                memset(selection_buffer + strlen(selection_buffer), ' ',
                       window_length - 2 - strlen(selection_buffer));
                selection_buffer[window_length - 2] = '\0';
            }

            if (selected_field == i) {
                screen_win_put_color_str_yx(pick_window, i + 1, 1,
                                            selection_buffer,
                                            Q_COLOR_PHONEBOOK_SELECTED);
            } else {
                screen_win_put_color_str_yx(pick_window, i + 1, 1,
                                            selection_buffer,
                                            Q_COLOR_PHONEBOOK_ENTRY);
            }
        }

        screen_win_flush(pick_window);
        screen_flush();

        qodem_win_getch(pick_window, &keystroke, NULL, Q_KEYBOARD_DELAY);
        switch (keystroke) {
        case '`':
        case Q_KEY_ESCAPE:
            /*
             * The abort exit point
             */
            screen_delwin(pick_window);
            return -1;
        case Q_KEY_DOWN:
            selected_field++;
            if (selected_field == Q_DIAL_METHOD_MAX) {
                selected_field = 0;
            }
            break;
        case Q_KEY_UP:
            selected_field--;
            if (selected_field < 0) {
                selected_field = Q_DIAL_METHOD_MAX - 1;
            }
            break;
        case Q_KEY_HOME:
            selected_field = 0;
            break;
        case Q_KEY_END:
            selected_field = Q_DIAL_METHOD_MAX - 1;
            break;
        case Q_KEY_ENTER:
        case Q_KEY_F(10):
            /*
             * The OK exit point
             */
            screen_delwin(pick_window);
            return selected_field;
        default:
            /*
             * Ignore
             */
            break;
        }
    } /* for (;;) */

    /*
     * Should never get here.
     */
    return -1;
}

/**
 * Popup the sort pick box.
 *
 * @return the sort method, or -1 if the user cancelled
 */
static SORT_METHOD pick_sort() {
    void * pick_window;
    int window_left;
    int window_top;
    int window_height;
    int window_length;
    char * title;
    /*
     * This doubles as the window length, so it's hardcoded
     */
    char selection_buffer[38];

    int title_left;
    int keystroke;
    int i;
    int selected_field;

    char * sort_strings[6];

    sort_strings[0] = _("Name (ascending)");
    sort_strings[1] = _("Number / Address (ascending)");
    sort_strings[2] = _("Total Calls (descending)");
    sort_strings[3] = _("Connection Method (ascending)");
    sort_strings[4] = _("Last Call (descending)");
    sort_strings[5] = _("Reverse All");

    window_height = SORT_METHOD_MAX + 2;
    window_length = sizeof(selection_buffer);

    window_left = WIDTH - 1 - window_length;
    if (window_left < 0) {
        window_left = 0;
    } else {
        window_left /= 2;
    }
    window_top = (HEIGHT - 1 - window_height) * 2;
    if (window_top < 0) {
        window_top = 0;
    } else {
        window_top /= 3;
    }

    pick_window =
        screen_subwin(window_height, window_length, window_top, window_left);
    if (check_subwin_result(pick_window) == Q_FALSE) {
        return -1;
    }

    screen_win_draw_box(pick_window, 0, 0, window_length, window_height);

    title = _("Sort FON By:");
    title_left = window_length - (strlen(title) + 2);
    if (title_left < 0) {
        title_left = 0;
    } else {
        title_left /= 2;
    }
    screen_win_put_color_printf_yx(pick_window, 0, title_left,
                                   Q_COLOR_WINDOW_BORDER, " %s ", title);

    selected_field = 0;

    for (;;) {

        for (i = 0; i < SORT_METHOD_MAX; i++) {
            snprintf(selection_buffer, sizeof(selection_buffer), " %s",
                     sort_strings[i]);
            if (strlen(selection_buffer) < window_length - 3) {
                memset(selection_buffer + strlen(selection_buffer), ' ',
                       window_length - 2 - strlen(selection_buffer));
                selection_buffer[window_length - 2] = '\0';
            }

            if (selected_field == i) {
                screen_win_put_color_str_yx(pick_window, i + 1, 1,
                                            selection_buffer,
                                            Q_COLOR_PHONEBOOK_SELECTED);
            } else {
                screen_win_put_color_str_yx(pick_window, i + 1, 1,
                                            selection_buffer,
                                            Q_COLOR_PHONEBOOK_ENTRY);
            }
        }

        screen_win_flush(pick_window);
        screen_flush();

        qodem_win_getch(pick_window, &keystroke, NULL, Q_KEYBOARD_DELAY);
        switch (keystroke) {
        case '`':
        case Q_KEY_ESCAPE:
            /*
             * The abort exit point
             */
            screen_delwin(pick_window);
            return -1;
        case Q_KEY_DOWN:
            selected_field++;
            if (selected_field == SORT_METHOD_MAX) {
                selected_field = 0;
            }
            break;
        case Q_KEY_UP:
            selected_field--;
            if (selected_field < 0) {
                selected_field = SORT_METHOD_MAX - 1;
            }
            break;
        case Q_KEY_HOME:
            selected_field = 0;
            break;
        case Q_KEY_END:
            selected_field = SORT_METHOD_MAX - 1;
            break;
        case Q_KEY_ENTER:
        case Q_KEY_F(10):
            /*
             * The OK exit point
             */
            screen_delwin(pick_window);
            return selected_field;
        default:
            /*
             * Ignore
             */
            break;
        }
    } /* for (;;) */

    /*
     * Should never get here.
     */
    return -1;
}

/**
 * Popup the delete entries/notes pick box.
 *
 * @return 1 to delete notes only, 2 to delete entries and notes, or -1 if
 * the user cancelled
 */
static int delete_popup() {
    void * pick_window;
    int window_left;
    int window_top;
    int window_height;
    int window_length;
    char * title;

    int title_left;
    int keystroke;

    /*
     * Status string
     */
    char * status_string;
    int status_left_stop;

    /*
     * Put up the status line
     */
    screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH,
                              Q_COLOR_STATUS);

    status_string =
        _
        (" 1-Delete Attached Notes   2-Delete Entries and Notes   ESC/`-Exit ");
    status_left_stop = WIDTH - strlen(status_string);
    if (status_left_stop <= 0) {
        status_left_stop = 0;
    } else {
        status_left_stop /= 2;
    }
    screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string,
                            Q_COLOR_STATUS);

    screen_flush();

    window_height = 8;
    window_length = 34;

    /*
     * Window will be centered on the screen
     */
    window_left = WIDTH - 1 - window_length;
    if (window_left < 0) {
        window_left = 0;
    } else {
        window_left /= 2;
    }
    /*
     * On the bottom 1/3
     */
    window_top = (HEIGHT - 1 - window_height) * 2;
    if (window_top < 0) {
        window_top = 0;
    } else {
        window_top /= 3;
    }

    pick_window =
        screen_subwin(window_height, window_length, window_top, window_left);
    if (check_subwin_result(pick_window) == Q_FALSE) {
        return -1;
    }

    screen_win_draw_box(pick_window, 0, 0, window_length, window_height);

    title = _("Delete Entries and/or Notes");
    title_left = window_length - (strlen(title) + 2);
    if (title_left < 0) {
        title_left = 0;
    } else {
        title_left /= 2;
    }
    screen_win_put_color_printf_yx(pick_window, 0, title_left,
                                   Q_COLOR_WINDOW_BORDER, " %s ", title);

    screen_win_put_color_str_yx(pick_window, 2, 6, "  1.",
                                Q_COLOR_MENU_COMMAND);
    screen_win_put_color_str(pick_window, _(" Notes Only"), Q_COLOR_MENU_TEXT);
    screen_win_put_color_str_yx(pick_window, 3, 6, "  2.",
                                Q_COLOR_MENU_COMMAND);
    screen_win_put_color_str(pick_window, _(" Entries & Notes"),
                             Q_COLOR_MENU_TEXT);
    screen_win_put_color_str_yx(pick_window, 4, 6, _("ESC."),
                                Q_COLOR_MENU_COMMAND);
    screen_win_put_color_str(pick_window, _(" Return to Directory"),
                             Q_COLOR_MENU_TEXT);
    screen_win_put_color_str_yx(pick_window, 6, 2, _("Your Choice ? "),
                                Q_COLOR_MENU_TEXT);
    screen_flush();
    screen_win_flush(pick_window);

    for (;;) {

        qodem_win_getch(pick_window, &keystroke, NULL, Q_KEYBOARD_DELAY);
        switch (keystroke) {
        case '`':
        case Q_KEY_ESCAPE:
            /*
             * The abort exit point
             */
            screen_delwin(pick_window);
            return -1;
        case '1':
            /*
             * Notes Only
             */
            return 1;
        case '2':
            /*
             * Entries and Notes
             */
            return 2;
        default:
            /*
             * Ignore
             */
            break;
        }
    } /* for (;;) */

    /*
     * Should never get here.
     */
    return -1;
}

#define DOORWAY_STRING_SIZE 32

/**
 * Popup the doorway pick box.
 *
 * @return a doorway enum, or -1 if the user cancelled
 */
static Q_DOORWAY pick_doorway() {
    void * pick_window;
    int window_left;
    int window_top;
    int window_height;
    int window_length;
    char * title;
    char selection_buffer[DOORWAY_STRING_SIZE];

    int title_left;
    int keystroke;
    Q_DOORWAY i;
    Q_DOORWAY selected_field;

    title = _("Choose Doorway Option");

    window_height = 6;
    window_length = strlen(title) + 4;

    window_left = WIDTH - 1 - window_length;
    if (window_left < 0) {
        window_left = 0;
    } else {
        window_left /= 2;
    }
    window_top = HEIGHT - 1 - window_height;
    if (window_top < 0) {
        window_top = 0;
    } else {
        window_top /= 2;
    }

    pick_window =
        screen_subwin(window_height, window_length, window_top, window_left);
    if (check_subwin_result(pick_window) == Q_FALSE) {
        return -1;
    }

    screen_win_draw_box(pick_window, 0, 0, window_length, window_height);

    title_left = window_length - (strlen(title) + 2);
    if (title_left < 0) {
        title_left = 0;
    } else {
        title_left /= 2;
    }
    screen_win_put_color_printf_yx(pick_window, 0, title_left,
                                   Q_COLOR_WINDOW_BORDER, " %s ", title);

    selected_field = Q_DOORWAY_CONFIG;

    for (;;) {

        for (i = Q_DOORWAY_CONFIG; i <= Q_DOORWAY_NEVER; i++) {

            switch (i) {
            case Q_DOORWAY_CONFIG:
                snprintf(selection_buffer, sizeof(selection_buffer), " %s",
                         _("Use Global Option"));
                break;
            case Q_DOORWAY_ALWAYS_DOORWAY:
                snprintf(selection_buffer, sizeof(selection_buffer), " %s",
                         _("Always DOORWAY"));
                break;
            case Q_DOORWAY_ALWAYS_MIXED:
                snprintf(selection_buffer, sizeof(selection_buffer), " %s",
                         _("Always MIXED"));
                break;
            case Q_DOORWAY_NEVER:
                snprintf(selection_buffer, sizeof(selection_buffer), " %s",
                         _("Never"));
                break;
            }

            if (strlen(selection_buffer) < window_length - 3) {
                memset(selection_buffer + strlen(selection_buffer), ' ',
                       window_length - 2 - strlen(selection_buffer));
                selection_buffer[window_length - 2] = '\0';
            }

            if (selected_field == i) {
                screen_win_put_color_str_yx(pick_window, i + 1, 1,
                                            selection_buffer,
                                            Q_COLOR_PHONEBOOK_SELECTED);
            } else {
                screen_win_put_color_str_yx(pick_window, i + 1, 1,
                                            selection_buffer,
                                            Q_COLOR_PHONEBOOK_ENTRY);
            }
        }

        screen_win_flush(pick_window);
        screen_flush();

        qodem_win_getch(pick_window, &keystroke, NULL, Q_KEYBOARD_DELAY);
        switch (keystroke) {
        case '`':
        case Q_KEY_ESCAPE:
            /*
             * The abort exit point
             */
            screen_delwin(pick_window);
            return -1;
        case Q_KEY_DOWN:
            selected_field++;
            if (selected_field > Q_DOORWAY_NEVER) {
                selected_field = Q_DOORWAY_CONFIG;
            }
            break;
        case Q_KEY_UP:
            selected_field--;
            if ((selected_field < Q_DOORWAY_CONFIG)
                || (selected_field > Q_DOORWAY_NEVER)) {
                selected_field = Q_DOORWAY_NEVER;
            }
            break;
        case Q_KEY_HOME:
            selected_field = Q_DOORWAY_CONFIG;
            break;
        case Q_KEY_END:
            selected_field = Q_DOORWAY_NEVER;
            break;
        case Q_KEY_ENTER:
        case Q_KEY_F(10):
            /*
             * The OK exit point
             */
            screen_delwin(pick_window);
            return selected_field;
        default:
            /*
             * Ignore
             */
            break;
        }
    } /* for (;;) */

    /*
     * Should never get here.
     */
    return -1;
}

/**
 * Show the dial out toggles selection form.  Note that toggles is first
 * read, and then written to if the user made a change.
 *
 * @param toggles the toggles
 */
static void toggles_form(int * toggles) {
    void * form_window;
    int status_left_stop;
    char * status_string;
    int window_left;
    int window_top;
    int window_height = 19;
    int window_length = 37;
    int title_left;
    int keystroke;
    Q_BOOL local_dirty;
    char * title = _("Change Toggles");

    /*
     * The new version
     */
    int new_toggles = *toggles;

    /*
     * Window will be 1/3 down the screen
     */
    window_left = WIDTH - 1 - window_length;
    if (window_left < 0) {
        window_left = 0;
    } else {
        window_left /= 2;
    }
    window_top = (HEIGHT - 1 - window_height);
    if (window_top < 0) {
        window_top = 0;
    } else {
        window_top /= 3;
    }

    screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH,
                              Q_COLOR_STATUS);
    status_string = _(" LETTER-Select a Toggle   ENTER-Done   ESC/`-Exit ");
    status_left_stop = WIDTH - strlen(status_string);
    if (status_left_stop <= 0) {
        status_left_stop = 0;
    } else {
        status_left_stop /= 2;
    }
    screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string,
                            Q_COLOR_STATUS);

    form_window =
        screen_subwin(window_height, window_length, window_top, window_left);
    if (check_subwin_result(form_window) == Q_FALSE) {
        q_screen_dirty = Q_TRUE;
        return;
    }

    screen_win_draw_box(form_window, 0, 0, window_length, window_height);

    title_left = window_length - (strlen(title) + 2);
    if (title_left < 0) {
        title_left = 0;
    } else {
        title_left /= 2;
    }
    screen_win_put_color_printf_yx(form_window, 0, title_left,
                                   Q_COLOR_WINDOW_BORDER, " %s ", title);

    local_dirty = Q_TRUE;

    for (;;) {
        if (local_dirty == Q_TRUE) {
            /*
             * Re-draw the screen
             */

            screen_win_put_color_str_yx(form_window, 2, 4, "0",
                                        Q_COLOR_MENU_COMMAND);
            if (new_toggles & TOGGLE_SESSION_LOG) {
                screen_win_put_color_str_yx(form_window, 2, 7,
                                            _("Session Log ON "),
                                            Q_COLOR_MENU_TEXT);
            } else {
                screen_win_put_color_str_yx(form_window, 2, 7,
                                            _("Session Log OFF"),
                                            Q_COLOR_MENU_TEXT);
            }

            screen_win_put_color_str_yx(form_window, 3, 4, "1",
                                        Q_COLOR_MENU_COMMAND);
            if (new_toggles & TOGGLE_XONXOFF) {
                screen_win_put_color_str_yx(form_window, 3, 7,
                                            _("XON/XOFF ON "),
                                            Q_COLOR_MENU_TEXT);
            } else {
                screen_win_put_color_str_yx(form_window, 3, 7,
                                            _("XON/XOFF OFF"),
                                            Q_COLOR_MENU_TEXT);
            }

            screen_win_put_color_str_yx(form_window, 4, 4, "2",
                                        Q_COLOR_MENU_COMMAND);
            if (new_toggles & TOGGLE_HARD_BACKSPACE) {
                screen_win_put_color_str_yx(form_window, 4, 7,
                                            _("Backspace is ^H "),
                                            Q_COLOR_MENU_TEXT);
            } else {
                screen_win_put_color_str_yx(form_window, 4, 7,
                                            _("Backspace is DEL"),
                                            Q_COLOR_MENU_TEXT);
            }

            screen_win_put_color_str_yx(form_window, 5, 4, "3",
                                        Q_COLOR_MENU_COMMAND);
            if (new_toggles & TOGGLE_LINEWRAP) {
                screen_win_put_color_str_yx(form_window, 5, 7,
                                            _("Line Wrap OFF"),
                                            Q_COLOR_MENU_TEXT);
            } else {
                screen_win_put_color_str_yx(form_window, 5, 7,
                                            _("Line Wrap ON "),
                                            Q_COLOR_MENU_TEXT);
            }

            screen_win_put_color_str_yx(form_window, 6, 4, "4",
                                        Q_COLOR_MENU_COMMAND);
            if (new_toggles & TOGGLE_DISPLAY_NULL) {
                screen_win_put_color_str_yx(form_window, 6, 7,
                                            _("Display NULL ON "),
                                            Q_COLOR_MENU_TEXT);
            } else {
                screen_win_put_color_str_yx(form_window, 6, 7,
                                            _("Display NULL OFF"),
                                            Q_COLOR_MENU_TEXT);
            }

            screen_win_put_color_str_yx(form_window, 7, 4, "7",
                                        Q_COLOR_MENU_COMMAND);
            if (new_toggles & TOGGLE_STATUS_LINE_INFO) {
                screen_win_put_color_str_yx(form_window, 7, 7,
                                            _("Status Line - Info  "),
                                            Q_COLOR_MENU_TEXT);
            } else {
                screen_win_put_color_str_yx(form_window, 7, 7,
                                            _("Status Line - Normal"),
                                            Q_COLOR_MENU_TEXT);
            }

            screen_win_put_color_str_yx(form_window, 8, 4, "8",
                                        Q_COLOR_MENU_COMMAND);
            if (new_toggles & TOGGLE_STRIP_8TH) {
                screen_win_put_color_str_yx(form_window, 8, 7,
                                            _("Strip 8th ON "),
                                            Q_COLOR_MENU_TEXT);
            } else {
                screen_win_put_color_str_yx(form_window, 8, 7,
                                            _("Strip 8th OFF"),
                                            Q_COLOR_MENU_TEXT);
            }

            screen_win_put_color_str_yx(form_window, 9, 4, "B",
                                        Q_COLOR_MENU_COMMAND);
            if (new_toggles & TOGGLE_BEEPS) {
                screen_win_put_color_str_yx(form_window, 9, 7,
                                            _("Beeps & Bells OFF"),
                                            Q_COLOR_MENU_TEXT);
            } else {
                screen_win_put_color_str_yx(form_window, 9, 7,
                                            _("Beeps & Bells ON "),
                                            Q_COLOR_MENU_TEXT);
            }

            screen_win_put_color_str_yx(form_window, 10, 4, "E",
                                        Q_COLOR_MENU_COMMAND);
            if (new_toggles & TOGGLE_HALF_DUPLEX) {
                screen_win_put_color_str_yx(form_window, 10, 7,
                                            _("Half Duplex"),
                                            Q_COLOR_MENU_TEXT);
            } else {
                screen_win_put_color_str_yx(form_window, 10, 7,
                                            _("Full Duplex"),
                                            Q_COLOR_MENU_TEXT);
            }

            screen_win_put_color_str_yx(form_window, 11, 4, "U",
                                        Q_COLOR_MENU_COMMAND);
            if (new_toggles & TOGGLE_SCROLLBACK) {
                screen_win_put_color_str_yx(form_window, 11, 7,
                                            _("Scrollback OFF"),
                                            Q_COLOR_MENU_TEXT);
            } else {
                screen_win_put_color_str_yx(form_window, 11, 7,
                                            _("Scrollback ON "),
                                            Q_COLOR_MENU_TEXT);
            }

            screen_win_put_color_str_yx(form_window, 12, 4, "-",
                                        Q_COLOR_MENU_COMMAND);
            if (new_toggles & TOGGLE_STATUS_LINE) {
                screen_win_put_color_str_yx(form_window, 12, 7,
                                            _("Status Line OFF"),
                                            Q_COLOR_MENU_TEXT);
            } else {
                screen_win_put_color_str_yx(form_window, 12, 7,
                                            _("Status Line ON "),
                                            Q_COLOR_MENU_TEXT);
            }

            screen_win_put_color_str_yx(form_window, 13, 4, "+",
                                        Q_COLOR_MENU_COMMAND);
            if (new_toggles & TOGGLE_CRLF) {
                screen_win_put_color_str_yx(form_window, 13, 7, _("Add LF ON "),
                                            Q_COLOR_MENU_TEXT);
            } else {
                screen_win_put_color_str_yx(form_window, 13, 7, _("Add LF OFF"),
                                            Q_COLOR_MENU_TEXT);
            }

            screen_win_put_color_str_yx(form_window, 14, 4, ",",
                                        Q_COLOR_MENU_COMMAND);
            if (new_toggles & TOGGLE_ANSI_MUSIC) {
                screen_win_put_color_str_yx(form_window, 14, 7,
                                            _("Ansi Music OFF"),
                                            Q_COLOR_MENU_TEXT);
            } else {
                screen_win_put_color_str_yx(form_window, 14, 7,
                                            _("Ansi Music ON "),
                                            Q_COLOR_MENU_TEXT);
            }

            q_cursor_on();
            screen_win_put_color_str_yx(form_window, 16, 9, _("Your Choice ? "),
                                        Q_COLOR_MENU_COMMAND);

            screen_flush();
            screen_win_flush(form_window);

            local_dirty = Q_FALSE;
        }

        qodem_win_getch(form_window, &keystroke, NULL, Q_KEYBOARD_DELAY);

        if (!q_key_code_yes(keystroke)) {
            /*
             * Regular character, process and refresh
             */
            switch (keystroke) {

            case '0':
                new_toggles ^= TOGGLE_SESSION_LOG;
                break;
            case '1':
                new_toggles ^= TOGGLE_XONXOFF;
                break;
            case '2':
                new_toggles ^= TOGGLE_HARD_BACKSPACE;
                break;
            case '3':
                new_toggles ^= TOGGLE_LINEWRAP;
                break;
            case '4':
                new_toggles ^= TOGGLE_DISPLAY_NULL;
                break;
            case '7':
                new_toggles ^= TOGGLE_STATUS_LINE_INFO;
                break;
            case '8':
                new_toggles ^= TOGGLE_STRIP_8TH;
                break;
            case 'B':
            case 'b':
                new_toggles ^= TOGGLE_BEEPS;
                break;
            case 'E':
            case 'e':
                new_toggles ^= TOGGLE_HALF_DUPLEX;
                break;
            case 'U':
            case 'u':
                new_toggles ^= TOGGLE_SCROLLBACK;
                break;
            case '-':
                new_toggles ^= TOGGLE_STATUS_LINE;
                break;
            case '+':
                new_toggles ^= TOGGLE_CRLF;
                break;
            case ',':
                new_toggles ^= TOGGLE_ANSI_MUSIC;
                break;

            default:
                /*
                 * Disregard
                 */
                break;
            }

            /*
             * Refresh form window
             */
            local_dirty = Q_TRUE;

        } /* if (!q_key_code_yes(keystroke)) */

        switch (keystroke) {

        case '`':
        case Q_KEY_ESCAPE:
            q_screen_dirty = Q_TRUE;
            return;

        case Q_KEY_ENTER:
        case Q_KEY_F(10):

            /*
             * The OK exit point
             */

            /*
             * Save new values
             */
            *toggles = new_toggles;

            q_screen_dirty = Q_TRUE;
            return;

        default:
            /*
             * Disregard keystroke
             */
            break;
        }
    } /* for (;;) */

    /*
     * Should never get here.
     */
    return;
}

/**
 * Edit a phonebook entry logon script.
 *
 * @param script_filename the script to edit
 */
static void spawn_script_editor(const char * script_filename) {
    char command_line[COMMAND_LINE_SIZE];

    if (strlen(script_filename) > 0) {
        screen_clear();
        screen_put_str_yx(0, 0, _("Spawning editor...\n\n"), Q_A_NORMAL, 0);
        screen_flush();
        sprintf(command_line, "%s %s", get_option(Q_OPTION_EDITOR),
                get_scriptdir_filename(script_filename));
        spawn_terminal(command_line);
#ifndef Q_PDCURSES_WIN32
        if (access(get_scriptdir_filename(script_filename), X_OK) != 0) {
            /*
             * Script exists, make it executable
             */
            chmod(get_scriptdir_filename(script_filename),
                  S_IRUSR | S_IWUSR | S_IXUSR);
        }
#endif
    }
}

/**
 * Edit a phonebook entry.
 *
 * @param entry the entry to edit
 */
static void edit_phone_entry_form(struct q_phone_struct * entry) {

#ifdef Q_NO_SERIAL
    struct field * fields[15];
#else
    struct field * fields[16];
#endif

    struct fieldset * edit_form;
    struct field * address_field;
    struct field * port_field;
    void * form_window;
    Q_COLOR color_active = Q_COLOR_WINDOW_FIELD_TEXT_HIGHLIGHTED;
    Q_COLOR color_inactive = Q_COLOR_WINDOW_FIELD_HIGHLIGHTED;
    int status_left_stop;
    char * status_string = NULL;
    int window_left;
    int window_top;
    int window_height = 22;
    int window_length = 51;
    char * title;
    int title_left;
    int keystroke;
    int new_keystroke;
    int flags;
    int i;
    char * form_value_string;
    /*
     * I know for sure that this string will be short
     */
    char toggles_string[32];

    struct file_info * file_selection;

    /*
     * Local copies of the fields being edited
     */
    wchar_t * name;
    char * address;
    char * port;
    wchar_t * username;
    wchar_t * password;
    char * password_stars;
    char * script_filename;
    char * capture_filename;
    char * translate_8bit_filename;
    char * translate_unicode_filename;
    char * keybindings_filename;
    Q_DIAL_METHOD method;
    Q_EMULATION emulation;
    Q_CODEPAGE codepage;

#ifndef Q_NO_SERIAL
    Q_BAUD_RATE baud;
    Q_DATA_BITS data_bits;
    Q_STOP_BITS stop_bits;
    Q_PARITY parity;
    Q_BOOL use_modem_cfg;
    Q_BOOL xonxoff;
    Q_BOOL rtscts;
    Q_BOOL lock_dte_baud;

    /*
     * I know for sure that this string will be short
     */
    char comm_settings_string[32];
#endif

    Q_DOORWAY doorway;
    Q_BOOL use_default_toggles;
    int toggles;

    char time_string[TIME_STRING_LENGTH];
    Q_BOOL local_dirty;
    Q_BOOL real_dirty;
    Q_BOOL dont_reload = Q_FALSE;
    Q_BOOL must_use_picklist = Q_FALSE;
    enum fields {
        NAME,
        ADDRESS,
        PORT,
        METHOD,
        USERNAME,
        PASSWORD,
        SCRIPT_NAME,
        EMULATION,
        CODEPAGE,
        CAPTUREFILE_NAME,
        TRANSLATE_8BIT_NAME,
        TRANSLATE_UNICODE_NAME,
        KEYBINDINGS_NAME,
        DOORWAY,
#ifndef Q_NO_SERIAL
        COMM_SETTINGS,
#endif
        TOGGLES,

        /*
         * No more form fields
         */
        CLEAR_CALL_INFO
    };
    int field_number;

    /*
     * Window will be 1/3 down the screen
     */
    window_left = WIDTH - 1 - window_length;
    if (window_left < 0) {
        window_left = 0;
    } else {
        window_left /= 2;
    }
    window_top = HEIGHT - 1 - window_height;
    if (window_top < 0) {
        window_top = 0;
    } else {
        window_top /= 3;
    }

    form_window =
        screen_subwin(window_height, window_length, window_top, window_left);
    if (check_subwin_result(form_window) == Q_FALSE) {
        return;
    }

    /* NAME */
    fields[0] = field_malloc(32, 1, 16, Q_FALSE, color_active, color_inactive);
    /* ADDRESS */
    fields[1] = field_malloc(32, 2, 16, Q_FALSE, color_active, color_inactive);
    address_field = fields[1];
    /* PORT */
    fields[2] = field_malloc(5, 3, 16, Q_FALSE, color_active, color_inactive);
    port_field = fields[2];
    /* METHOD */
    fields[3] = field_malloc(7, 4, 16, Q_TRUE, color_active, color_inactive);
    /* USERNAME */
    fields[4] = field_malloc(32, 5, 16, Q_FALSE, color_active, color_inactive);
    /* PASSWORD */
    fields[5] = field_malloc(32, 6, 16, Q_FALSE, color_active, color_inactive);
    /* SCRIPT_NAME */
    fields[6] = field_malloc(32, 7, 16, Q_FALSE, color_active, color_inactive);
    /* EMULATION */
    fields[7] = field_malloc(7, 8, 16, Q_TRUE, color_active, color_inactive);
    /* CODEPAGE */
    fields[8] = field_malloc(15, 9, 16, Q_TRUE, color_active, color_inactive);
    /* CAPTUREFILE_NAME */
    fields[9] = field_malloc(32, 10, 16, Q_FALSE, color_active, color_inactive);
    /* TRANSLATE_8BIT_NAME */
    fields[10] = field_malloc(32, 11, 16, Q_FALSE, color_active,
        color_inactive);
    /* TRANSLATE_UNICODE_NAME */
    fields[11] = field_malloc(32, 12, 16, Q_FALSE, color_active,
        color_inactive);
    /* KEYBINDINGS_NAME */
    fields[12] = field_malloc(32, 13, 16, Q_FALSE, color_active,
        color_inactive);
    /* DOORWAY */
    fields[13] = field_malloc(32, 14, 16, Q_TRUE, color_active, color_inactive);
#ifdef Q_NO_SERIAL
    /* TOGGLES */
    fields[14] = field_malloc(32, 14, 16, Q_TRUE, color_active, color_inactive);

    /* All fields defined, now make the big form. */
    edit_form = fieldset_malloc(fields, 15, form_window);
#else
    /* COMM_SETTINGS */
    fields[14] = field_malloc(32, 15, 16, Q_TRUE, color_active, color_inactive);
    /* TOGGLES */
    fields[15] = field_malloc(32, 16, 16, Q_TRUE, color_active, color_inactive);

    /* All fields defined, now make the big form. */
    edit_form = fieldset_malloc(fields, 16, form_window);
#endif

    field_number = NAME;

    name                = Xwcsdup(entry->name, __FILE__, __LINE__);
    address             = Xstrdup(entry->address, __FILE__, __LINE__);
    port                = Xstrdup(entry->port, __FILE__, __LINE__);
    method              = entry->method;
    username            = Xwcsdup(entry->username, __FILE__, __LINE__);
    password            = Xwcsdup(entry->password, __FILE__, __LINE__);
    script_filename     = Xstrdup(entry->script_filename, __FILE__, __LINE__);
    emulation           = entry->emulation;
    codepage            = entry->codepage;
    capture_filename           = Xstrdup(entry->capture_filename,
        __FILE__, __LINE__);
    translate_8bit_filename    = Xstrdup(entry->translate_8bit_filename,
        __FILE__, __LINE__);
    translate_unicode_filename = Xstrdup(entry->translate_unicode_filename,
        __FILE__, __LINE__);
    keybindings_filename =
        Xstrdup(entry->keybindings_filename, __FILE__, __LINE__);

#ifndef Q_NO_SERIAL
    baud                = entry->baud;
    data_bits           = entry->data_bits;
    parity              = entry->parity;
    stop_bits           = entry->stop_bits;
    xonxoff             = entry->xonxoff;
    rtscts              = entry->rtscts;
    use_modem_cfg       = entry->use_modem_cfg;
    lock_dte_baud       = entry->lock_dte_baud;
#endif

    doorway             = entry->doorway;
    toggles             = entry->toggles;
    use_default_toggles = entry->use_default_toggles;

    /*
     * Convert password to stars
     */
    password_stars =
        (char *) Xmalloc(wcslen(entry->password) + 1, __FILE__, __LINE__);
    memset(password_stars, 0, wcslen(entry->password) + 1);
    for (i = 0; i < wcslen(entry->password); i++) {
        password_stars[i] = '*';
    }

    real_dirty = Q_TRUE;
    local_dirty = Q_TRUE;
    for (;;) {

        if (local_dirty == Q_TRUE) {

            if (real_dirty == Q_TRUE) {
                /*
                 * Refresh background
                 */
                q_screen_dirty = Q_TRUE;
                phonebook_refresh();

                screen_win_draw_box(form_window, 0, 0, window_length,
                                    window_height);

                screen_win_put_color_str_yx(form_window, window_height - 1,
                                            window_length - 10, _("F1 Help"),
                                            Q_COLOR_WINDOW_BORDER);

                title = _("Revise Entry");
                title_left = window_length - (strlen(title) + 2);
                if (title_left < 0) {
                    title_left = 0;
                } else {
                    title_left /= 2;
                }
                screen_win_put_color_printf_yx(form_window, 0, title_left,
                                               Q_COLOR_WINDOW_BORDER, " %s ",
                                               title);

                screen_win_put_color_str_yx(form_window, 1, 2, _("Name"),
                                            Q_COLOR_MENU_COMMAND);

                address_field->invisible = Q_FALSE;
                if (method == Q_DIAL_METHOD_COMMANDLINE) {
                    screen_win_put_color_str_yx(form_window, 2, 2,
                                                _("Command Line"),
                                                Q_COLOR_MENU_COMMAND);
#ifndef Q_NO_SERIAL
                } else if (method == Q_DIAL_METHOD_MODEM) {
                    screen_win_put_color_str_yx(form_window, 2, 2, _("Phone #"),
                                                Q_COLOR_MENU_COMMAND);
#endif
                } else if (method == Q_DIAL_METHOD_SHELL) {
                    address_field->invisible = Q_TRUE;
                } else {
                    screen_win_put_color_str_yx(form_window, 2, 2, _("Address"),
                                                Q_COLOR_MENU_COMMAND);
                }
                if ((method == Q_DIAL_METHOD_TELNET) ||
                    (method == Q_DIAL_METHOD_SSH) ||
                    (method == Q_DIAL_METHOD_SOCKET)
                ) {
                    screen_win_put_color_str_yx(form_window, 3, 2, _("Port"),
                                                Q_COLOR_MENU_COMMAND);
                    port_field->invisible = Q_FALSE;
                } else {
                    port_field->invisible = Q_TRUE;
                }
                screen_win_put_color_str_yx(form_window, 4, 2, _("Method"),
                                            Q_COLOR_MENU_COMMAND);
                screen_win_put_color_str_yx(form_window, 5, 2, _("Username"),
                                            Q_COLOR_MENU_COMMAND);
                screen_win_put_color_str_yx(form_window, 6, 2, _("Password"),
                                            Q_COLOR_MENU_COMMAND);
                screen_win_put_color_str_yx(form_window, 7, 2, _("Script"),
                                            Q_COLOR_MENU_COMMAND);
                screen_win_put_color_str_yx(form_window, 8, 2, _("Emulation"),
                                            Q_COLOR_MENU_COMMAND);
                screen_win_put_color_str_yx(form_window, 9, 2, _("Codepage"),
                                            Q_COLOR_MENU_COMMAND);
                screen_win_put_color_str_yx(form_window, 10, 2,
                                            _("Capture File"),
                                            Q_COLOR_MENU_COMMAND);
                screen_win_put_color_str_yx(form_window, 11, 2,
                                            _("Xlate 8-Bit"),
                                            Q_COLOR_MENU_COMMAND);
                screen_win_put_color_str_yx(form_window, 12, 2,
                                            _("Xlate Unicode"),
                                            Q_COLOR_MENU_COMMAND);
                screen_win_put_color_str_yx(form_window, 13, 2, _("Key File"),
                                            Q_COLOR_MENU_COMMAND);
                screen_win_put_color_str_yx(form_window, 14, 2, _("Doorway"),
                                            Q_COLOR_MENU_COMMAND);
#ifndef Q_NO_SERIAL
                screen_win_put_color_str_yx(form_window, 15, 2,
                                            _("Port Settings"),
                                            Q_COLOR_MENU_COMMAND);
#endif
                screen_win_put_color_str_yx(form_window, 16, 2, _("Toggles"),
                                            Q_COLOR_MENU_COMMAND);

                screen_win_put_color_str_yx(form_window, 19, 2, _("Last Call"),
                                            Q_COLOR_MENU_COMMAND);
                if (entry->times_on > 0) {
                    strftime(time_string, sizeof(time_string),
                             "%a, %d %b %Y %H:%M:%S",
                             localtime(&entry->last_call));
                    screen_win_put_color_printf(form_window, Q_COLOR_MENU_TEXT,
                                                "    %s", time_string);
                }
                screen_win_put_color_str_yx(form_window, 20, 2, _("Times On"),
                                            Q_COLOR_MENU_COMMAND);
                screen_win_put_color_printf(form_window, Q_COLOR_MENU_TEXT,
                                            "     %u", entry->times_on);

                if (dont_reload == Q_FALSE) {
                    field_set_value(fields[0], name);
                    field_set_char_value(fields[1], address);
                    field_set_char_value(fields[2], port);
                    field_set_char_value(fields[3], method_string(method));
                    field_set_value(fields[4], username);
                    if (field_number == PASSWORD) {
                        /*
                         * Replace with the real password
                         */
                        field_set_value(fields[5], password);
                    } else {
                        /*
                         * Show the password as stars
                         */
                        field_set_char_value(fields[5], password_stars);
                    }
                    field_set_char_value(fields[6], script_filename);
                    field_set_char_value(fields[7],
                                         emulation_string(emulation));
                    field_set_char_value(fields[8], codepage_string(codepage));
                    field_set_char_value(fields[9], capture_filename);
                    field_set_char_value(fields[10], translate_8bit_filename);
                    field_set_char_value(fields[11],
                                         translate_unicode_filename);
                    field_set_char_value(fields[12], keybindings_filename);
                    field_set_char_value(fields[13], doorway_string(doorway));

#ifndef Q_NO_SERIAL
                    /*
                     * Comm settings string
                     */
                    if (use_modem_cfg == Q_TRUE) {
                        sprintf(comm_settings_string, _("Use Modem Config"));
                    } else {
                        sprintf(comm_settings_string, "%s %s%s%s%s%s%s",
                                baud_string(baud), data_bits_string(data_bits),
                                parity_string(parity, Q_TRUE),
                                stop_bits_string(stop_bits),
                                (xonxoff == Q_TRUE ? " XON/XOFF" : ""),
                                (rtscts == Q_TRUE ? " RTS/CTS" : ""),
                                (lock_dte_baud ==
                                 Q_TRUE ? _(" DTE Locked") : "")
                            );
                    }
                    field_set_char_value(fields[14], comm_settings_string);
#endif

                    /*
                     * Toggles string
                     */
                    if (use_default_toggles == Q_TRUE) {
                        sprintf(toggles_string, _("Default Toggles"));
                    } else {
                        sprintf(toggles_string, "%s",
                                toggles_to_string(toggles));
                    }
#ifdef Q_NO_SERIAL
                    field_set_char_value(fields[14], toggles_string);
#else
                    field_set_char_value(fields[15], toggles_string);
#endif

                } /* if (dont_reload == Q_FALSE) */

                real_dirty = Q_FALSE;

            } /* if (real_dirty == Q_TRUE) */

            /*
             * Put up the status line
             */
            screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH,
                                      Q_COLOR_STATUS);

            switch (field_number) {
            case NAME:
                status_string = _(""
" Change NAME field                         [F10/Alt-Enter] Save   [ESC] "
                                  "Abort ");
                break;
            case ADDRESS:
                status_string = _(""
" Change ADDRESS/COMMAND/NUMBER Field       [F10/Alt-Enter] Save   [ESC] "
                                  "Abort ");
                break;
            case PORT:
                status_string = _(""
" Change PORT Field                         [F10/Alt-Enter] Save   [ESC] "
                                  "Abort ");
                break;
            case METHOD:
                status_string = _(""
" Change Con. Method      [F2/Space] Pick   [F10/Alt-Enter] Save   [ESC] "
                                  "Abort ");
                break;
            case USERNAME:
                status_string = _(""
" Change USERNAME Field                     [F10/Alt-Enter] Save   [ESC] "
                                  "Abort ");
                break;
            case PASSWORD:
                status_string = _(""
" Change PASSWORD Field                     [F10/Alt-Enter] Save   [ESC] "
                                  "Abort ");
                break;
            case SCRIPT_NAME:
                status_string = _(""
" Change Linked Script          [F2] Edit   [F10/Alt-Enter] Save   [ESC] "
                                  "Abort ");
                break;
            case EMULATION:
                status_string = _(""
" Change Emulation        [F2/Space] Pick   [F10/Alt-Enter] Save   [ESC] "
                                  "Abort ");
                break;
            case CODEPAGE:
                status_string = _(""
" Change Codepage         [F2/Space] Pick   [F10/Alt-Enter] Save   [ESC] "
                                  "Abort ");
                break;
            case CAPTUREFILE_NAME:
                status_string = _(""
" Change Capture File           [F2] Pick   [F10/Alt-Enter] Save   [ESC] "
                                  "Abort ");
                break;
            case TRANSLATE_8BIT_NAME:
                status_string = _(""
" Change 8-bit Xlate File       [F2] Pick   [F10/Alt-Enter] Save   [ESC] "
                                  "Abort ");
                break;
            case TRANSLATE_UNICODE_NAME:
                status_string = _(""
" Change UTF Xlate File         [F2] Pick   [F10/Alt-Enter] Save   [ESC] "
                                  "Abort ");
                break;
            case KEYBINDINGS_NAME:
                status_string = _(""
" Change Key File               [F2] Edit   [F10/Alt-Enter] Save   [ESC] "
                                  "Abort ");
                break;
            case DOORWAY:
                status_string = _(""
" Change Doorway Option   [F2/Space] Edit   [F10/Alt-Enter] Save   [ESC] "
                                  "Abort ");
                break;
#ifndef Q_NO_SERIAL
            case COMM_SETTINGS:
                status_string = _(""
" Change Port Settings    [F2/Space] Edit   [F10/Alt-Enter] Save   [ESC] "
                                  "Abort ");
                break;
#endif
            case TOGGLES:
                status_string = _(""
" Change Toggles          [F2/Space] Edit   [F10/Alt-Enter] Save   [ESC] "
                                  "Abort ");
                break;
            case CLEAR_CALL_INFO:
                status_string = _(""
" Clear Call Information    [ENTER] Clear   [F10/Alt-Enter] Save   [ESC] "
                                  "Abort ");
                break;
            }

            local_dirty = Q_FALSE;

            status_left_stop = WIDTH - strlen(status_string);
            if (status_left_stop <= 0) {
                status_left_stop = 0;
            } else {
                status_left_stop /= 2;
            }
            screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string,
                                    Q_COLOR_STATUS);

            if (field_number == CLEAR_CALL_INFO) {
                screen_win_put_color_str_yx(form_window, 17, 1,
                                            _(" Clear Call Info "),
                                            Q_COLOR_PHONEBOOK_SELECTED_TAGGED);
            } else {
                screen_win_put_color_str_yx(form_window, 17, 1,
                                            _(" Clear Call Info "),
                                            Q_COLOR_MENU_COMMAND);
                if (dont_reload == Q_FALSE) {
                    fieldset_home_char(edit_form);
                }
            }

            screen_win_flush(form_window);
            screen_flush();
            fieldset_render(edit_form);

            dont_reload = Q_FALSE;

        }  /* if (local_dirty == Q_TRUE) */

        /*
         * Some fields I don't want the cursor on or the ability to directly
         * edit.
         */
        must_use_picklist = Q_FALSE;
        if ((field_number == METHOD) ||
            (field_number == EMULATION) ||
            (field_number == CODEPAGE) ||
            (field_number == DOORWAY) ||
#ifndef Q_NO_SERIAL
            (field_number == COMM_SETTINGS) ||
#endif
            (field_number == TOGGLES)
        ) {
            must_use_picklist = Q_TRUE;
        }

        qodem_win_getch(form_window, &keystroke, &flags, Q_KEYBOARD_DELAY);

        /*
         * Support alternate keys
         */
        if (keystroke == Q_KEY_ENTER) {
            if (flags & KEY_FLAG_ALT) {
                keystroke = Q_KEY_F(10);
            }
        }

        switch (keystroke) {

        case ERR:
            /*
             * Do nothing
             */
            break;

        case Q_KEY_F(1):
            launch_help(Q_HELP_PHONEBOOK_REVISE_ENTRY);

            local_dirty = Q_TRUE;
            real_dirty = Q_TRUE;
            break;

        case Q_KEY_ESCAPE:
            /*
             * The abort exit point
             */
            fieldset_free(edit_form);
            screen_delwin(form_window);
            Xfree(password_stars, __FILE__, __LINE__);

            /*
             * No leak
             */
            if (name != NULL) {
                Xfree(name, __FILE__, __LINE__);
            }
            if (address != NULL) {
                Xfree(address, __FILE__, __LINE__);
            }
            if (port != NULL) {
                Xfree(port, __FILE__, __LINE__);
            }
            if (password != NULL) {
                Xfree(password, __FILE__, __LINE__);
            }
            if (username != NULL) {
                Xfree(username, __FILE__, __LINE__);
            }
            if (script_filename != NULL) {
                Xfree(script_filename, __FILE__, __LINE__);
            }
            if (capture_filename != NULL) {
                Xfree(capture_filename, __FILE__, __LINE__);
            }
            if (translate_8bit_filename != NULL) {
                Xfree(translate_8bit_filename, __FILE__, __LINE__);
            }
            if (translate_unicode_filename != NULL) {
                Xfree(translate_unicode_filename, __FILE__, __LINE__);
            }
            if (keybindings_filename != NULL) {
                Xfree(keybindings_filename, __FILE__, __LINE__);
            }
            return;

        case Q_KEY_ENTER:
            if (field_number == CLEAR_CALL_INFO) {
                entry->times_on = 0;
                entry->last_call = 0;

                /*
                 * Save settings ----------------------------
                 */

                if (name != NULL) {
                    Xfree(name, __FILE__, __LINE__);
                }
                name = field_get_value(fields[0]);

                if (address != NULL) {
                    Xfree(address, __FILE__, __LINE__);
                }
                address = field_get_char_value(fields[1]);

                if (port != NULL) {
                    Xfree(port, __FILE__, __LINE__);
                }
                port = field_get_char_value(fields[2]);

                form_value_string = field_get_char_value(fields[3]);
                method = method_from_string(form_value_string);
                Xfree(form_value_string, __FILE__, __LINE);

                if (username != NULL) {
                    Xfree(username, __FILE__, __LINE__);
                }
                username = field_get_value(fields[4]);

                if (script_filename != NULL) {
                    Xfree(script_filename, __FILE__, __LINE__);
                }
                script_filename = field_get_char_value(fields[6]);

                form_value_string = field_get_char_value(fields[7]);
                emulation = emulation_from_string(form_value_string);
                Xfree(form_value_string, __FILE__, __LINE);

                form_value_string = field_get_char_value(fields[8]);
                codepage = codepage_from_string(form_value_string);
                Xfree(form_value_string, __FILE__, __LINE);

                if (capture_filename != NULL) {
                    Xfree(capture_filename, __FILE__, __LINE__);
                }
                capture_filename = field_get_char_value(fields[9]);

                if (translate_8bit_filename != NULL) {
                    Xfree(translate_8bit_filename, __FILE__, __LINE__);
                }
                translate_8bit_filename = field_get_char_value(fields[10]);

                if (translate_unicode_filename != NULL) {
                    Xfree(translate_unicode_filename, __FILE__, __LINE__);
                }
                translate_unicode_filename = field_get_char_value(fields[11]);

                if (keybindings_filename != NULL) {
                    Xfree(keybindings_filename, __FILE__, __LINE__);
                }
                keybindings_filename = field_get_char_value(fields[12]);

                doorway = doorway_from_string(field_get_char_value(fields[13]));

                /*
                 * Save settings ----------------------------
                 */

                local_dirty = Q_TRUE;
                real_dirty = Q_TRUE;
                break;
            }

            /*
             * Fall through ...
             */
        case Q_KEY_DOWN:
            if (field_number < CLEAR_CALL_INFO) {
                if (field_number < TOGGLES) {
                    fieldset_next_field(edit_form);
                    edit_form->inactive = Q_FALSE;
                } else {
                    edit_form->inactive = Q_TRUE;
                }

                if (field_number == PASSWORD) {
                    /*
                     * Leaving the password field...
                     */
                    /*
                     * Save the edited password
                     */

                    /*
                     * No leak
                     */
                    if (password != NULL) {
                        Xfree(password, __FILE__, __LINE__);
                    }

                    password = field_get_value(fields[5]);
                    /*
                     * Rebuild password_stars
                     */
                    Xfree(password_stars, __FILE__, __LINE);
                    password_stars =
                        (char *) Xmalloc(wcslen(password) + 1, __FILE__,
                                         __LINE__);
                    memset(password_stars, 0, wcslen(password) + 1);
                    for (i = 0; i < wcslen(password); i++) {
                        password_stars[i] = '*';
                    }

                    /*
                     * Replace with the password stars
                     */
                    field_set_char_value(fields[5], password_stars);
                } else if (field_number == NAME) {
                    /*
                     * Leaving name
                     */
                    form_value_string = field_get_char_value(fields[3]);
                    if ((method_from_string(form_value_string) ==
                            Q_DIAL_METHOD_SHELL)
                        ) {
                        /*
                         * Skip over address and port
                         */
                        field_number++;
                        fieldset_next_field(edit_form);
                        field_number++;
                        fieldset_next_field(edit_form);
                    }
                    Xfree(form_value_string, __FILE__, __LINE);
                } else if (field_number == ADDRESS) {
                    /*
                     * Leaving address
                     */
                    form_value_string = field_get_char_value(fields[3]);
                    if ((method_from_string(form_value_string) !=
                            Q_DIAL_METHOD_TELNET) &&
                        (method_from_string(form_value_string) !=
                            Q_DIAL_METHOD_SSH) &&
                        (method_from_string(form_value_string) !=
                            Q_DIAL_METHOD_SOCKET)
                        ) {
                        /*
                         * Skip over port
                         */
                        field_number++;
                        fieldset_next_field(edit_form);
                    }
                    Xfree(form_value_string, __FILE__, __LINE);
                }

                if ((must_use_picklist == Q_TRUE) &&
                    (field_number != TOGGLES)) {
                    /*
                     * Leaving the picklist fields
                     */
                    q_cursor_on();
                }

                field_number++;
                local_dirty = Q_TRUE;

                if (field_number == PASSWORD) {
                    /*
                     * Entering the password field...
                     */
                    /*
                     * Replace with the real password
                     */
                    field_set_value(fields[5], password);
                }

                /*
                 * Not pretty, I just duplicated the must_use_picklist check
                 */
                if ((field_number == METHOD) ||
                    (field_number == EMULATION) ||
                    (field_number == CODEPAGE) ||
                    (field_number == DOORWAY) ||
#ifndef Q_NO_SERIAL
                    (field_number == COMM_SETTINGS) ||
#endif
                    (field_number == TOGGLES)
                ) {
                    q_cursor_off();
                }
            }
            break;

        case Q_KEY_UP:
            if (field_number > NAME) {
                if (field_number < CLEAR_CALL_INFO) {
                    fieldset_prev_field(edit_form);
                }
                edit_form->inactive = Q_FALSE;

                if (field_number == PASSWORD) {
                    /*
                     * Leaving the password field...
                     */
                    /*
                     * Save the edited password
                     */
                    /*
                     * No leak
                     */
                    if (password != NULL) {
                        Xfree(password, __FILE__, __LINE__);
                    }
                    password = field_get_value(fields[5]);
                    /*
                     * Rebuild password_stars
                     */
                    Xfree(password_stars, __FILE__, __LINE);
                    password_stars =
                        (char *) Xmalloc(wcslen(password) + 1, __FILE__,
                                         __LINE__);
                    memset(password_stars, 0, wcslen(password) + 1);
                    for (i = 0; i < wcslen(password); i++) {
                        password_stars[i] = '*';
                    }

                    /*
                     * Replace with the password stars
                     */
                    field_set_char_value(fields[5], password_stars);
                } else if (field_number == METHOD) {
                    /*
                     * Leaving method
                     */
                    form_value_string = field_get_char_value(fields[3]);
                    if ((method_from_string(form_value_string) !=
                            Q_DIAL_METHOD_TELNET) &&
                        (method_from_string(form_value_string) !=
                            Q_DIAL_METHOD_SSH) &&
                        (method_from_string(form_value_string) !=
                            Q_DIAL_METHOD_SOCKET)
                        ) {
                        /*
                         * Skip over port
                         */
                        field_number--;
                        fieldset_prev_field(edit_form);
                    }

                    if ((method_from_string(form_value_string) ==
                            Q_DIAL_METHOD_SHELL)
                        ) {
                        /*
                         * Skip over address
                         */
                        field_number--;
                        fieldset_prev_field(edit_form);
                    }
                    Xfree(form_value_string, __FILE__, __LINE);
                }

                if ((must_use_picklist == Q_TRUE) ||
                    (field_number == CLEAR_CALL_INFO)
                ) {
                    /*
                     * Leaving the picklist fields
                     */
                    q_cursor_on();
                }

                field_number--;
                local_dirty = Q_TRUE;

                if (field_number == PASSWORD) {
                    /*
                     * Entering the password field...
                     */
                    /*
                     * Replace with the real password
                     */
                    field_set_value(fields[5], password);
                }

                /*
                 * Not pretty, I just duplicated the
                 * must_use_picklist check
                 */
                if ((field_number == METHOD) ||
                    (field_number == EMULATION) ||
                    (field_number == CODEPAGE) ||
                    (field_number == DOORWAY) ||
#ifndef Q_NO_SERIAL
                    (field_number == COMM_SETTINGS) ||
#endif
                    (field_number == TOGGLES)
                ) {
                    q_cursor_off();
                }
            }

            break;

        case ' ':
            /*
             * This is deliberately copied verbatim from the switch default
             * case so that it can fall through into the F2 case.  This way,
             * Space and F2 do the same thing.
             *
             * If the default case changes, this might need to also.
             */
            if ((!q_key_code_yes(keystroke)) && (keystroke != ERR)) {
                if (must_use_picklist == Q_FALSE) {
                    /*
                     * Pass normal keys to form driver
                     */
                    fieldset_keystroke(edit_form, keystroke);
                    break;
                }
            }
            /*
             * Fall through...
             */

        case Q_KEY_F(2):
            if (must_use_picklist == Q_TRUE) {
                /*
                 * Put up a picklist
                 */
                switch (field_number) {
                case METHOD:
                    method = pick_method();
                    if (method != -1) {
                        field_set_char_value(fields[3], method_string(method));
                        if (port != NULL) {
                            Xfree(port, __FILE__, __LINE__);
                            port = NULL;
                        }
                        port = default_port(method);
                        field_set_char_value(fields[2], port);
                    } else {
                        method = entry->method;
                    }

                    if ((method == Q_DIAL_METHOD_TELNET) ||
                        (method == Q_DIAL_METHOD_SSH) ||
                        (method == Q_DIAL_METHOD_SOCKET)
                    ) {
                        port_field->invisible = Q_FALSE;
                    } else {
                        port_field->invisible = Q_TRUE;
                    }
                    break;

                case EMULATION:
                    emulation = pick_emulation();
                    if (emulation != -1) {
                        field_set_char_value(fields[7],
                                             emulation_string(emulation));
                        codepage = default_codepage(emulation);
                        field_set_char_value(fields[8],
                                             codepage_string(codepage));
                    } else {
                        emulation = entry->emulation;
                    }
                    break;

                case CODEPAGE:
                    codepage = pick_codepage(emulation);
                    if (codepage != -1) {
                        field_set_char_value(fields[8],
                                             codepage_string(codepage));
                    } else {
                        codepage = entry->codepage;
                    }
                    break;

                case DOORWAY:
                    doorway = pick_doorway();
                    if (doorway != -1) {
                        field_set_char_value(fields[13],
                                             doorway_string(doorway));
                    } else {
                        doorway = entry->doorway;
                    }
                    break;

#ifndef Q_NO_SERIAL
                case COMM_SETTINGS:
                    /*
                     * Ask the user if they want to deviate from the default
                     * modem configuration.
                     */
                    keystroke =
                        q_tolower(notify_prompt_form(
                            _("Change Port Settings"),
                            _("Use Modem Config? [Y/n] "),
                            _(" Y-Use the Modem Settings   N-Override the Modem Settings for This Entry "),
                            Q_TRUE, 0.0, "YyNn\r"));

                    if ((keystroke == 'y') || (keystroke == Q_KEY_ENTER)) {
                        use_modem_cfg = Q_TRUE;
                    } else {
                        use_modem_cfg = Q_FALSE;
                        comm_settings_form(_("Change Port Settings"), &baud,
                                           &data_bits, &parity, &stop_bits,
                                           &xonxoff, &rtscts);

                        keystroke =
                            q_tolower(notify_prompt_form(
                                _("DTE Baud"), _("Lock DTE Baud? [Y/n] "),
                                _(" Y-Lock Serial Port Speed   N-Change Serial Port Speed After CONNECT "),
                                Q_TRUE, 0.0, "YyNn\r"));
                        if ((keystroke == 'y') || (keystroke == Q_KEY_ENTER)) {
                            lock_dte_baud = Q_TRUE;
                        } else {
                            lock_dte_baud = Q_FALSE;
                        }
                        q_cursor_off();
                    }
                    break;
#endif /* Q_NO_SERIAL */

                case TOGGLES:
                    /*
                     * Ask the user if they want to deviate from the default
                     * toggles configuration.
                     */
                    keystroke =
                        q_tolower(notify_prompt_form(
                            _("Change Toggles"), _("Use Defaults? [Y/n] "),
                            _(" Y-Use the Default Settings   N-Override the Toggles for This Entry "),
                            Q_TRUE, 0.0, "YyNn\r"));

                    if ((keystroke == 'y') || (keystroke == Q_KEY_ENTER)) {
                        use_default_toggles = Q_TRUE;
                        toggles = 0;
                    } else {
                        use_default_toggles = Q_FALSE;
                        toggles_form(&toggles);
                        q_cursor_off();
                    }
                    break;

                default:
                    break;
                }

                local_dirty = Q_TRUE;
                real_dirty = Q_TRUE;
            } else {

                switch (field_number) {

                case SCRIPT_NAME:
                    /*
                     * Pull up external editor
                     */
                    if (script_filename != NULL) {
                        Xfree(script_filename, __FILE__, __LINE__);
                    }
                    script_filename = field_get_char_value(fields[6]);

                    spawn_script_editor(script_filename);
                    local_dirty = Q_TRUE;
                    real_dirty = Q_TRUE;
                    break;

                case CAPTUREFILE_NAME:
                    /*
                     * Pull up the pick list
                     */
                    file_selection =
                        view_directory(get_option(Q_OPTION_WORKING_DIR), "");

                    if (file_selection != NULL) {
                        field_set_char_value(fields[9],
                                             basename(file_selection->name));
                        Xfree(file_selection, __FILE__, __LINE__);
                    }

                    local_dirty = Q_TRUE;
                    real_dirty = Q_TRUE;
                    break;

                case TRANSLATE_8BIT_NAME:
                    /*
                     * Pull up the pick list
                     */
                    file_selection = view_directory(q_home_directory, "*.xl8");

                    if (file_selection != NULL) {
                        field_set_char_value(fields[10],
                                             basename(file_selection->name));
                        Xfree(file_selection, __FILE__, __LINE__);
                    }

                    local_dirty = Q_TRUE;
                    real_dirty = Q_TRUE;
                    break;

                case TRANSLATE_UNICODE_NAME:
                    /*
                     * Pull up the pick list
                     */
                    file_selection = view_directory(q_home_directory, "*.xlu");

                    if (file_selection != NULL) {
                        field_set_char_value(fields[11],
                                             basename(file_selection->name));
                        Xfree(file_selection, __FILE__, __LINE__);
                    }

                    local_dirty = Q_TRUE;
                    real_dirty = Q_TRUE;
                    break;

                case KEYBINDINGS_NAME:
                    /*
                     * Pull up keyboard editor window
                     */
                    if (keybindings_filename != NULL) {
                        Xfree(keybindings_filename, __FILE__, __LINE__);
                    }
                    keybindings_filename = field_get_char_value(fields[12]);
                    if (strlen(keybindings_filename) > 0) {
                        switch_current_keyboard(keybindings_filename);
                        switch_state(Q_STATE_FUNCTION_KEY_EDITOR);
                        /*
                         * This is ugly, but necessary.
                         */
                        while (q_program_state == Q_STATE_FUNCTION_KEY_EDITOR) {
                            refresh_handler();
                            keyboard_handler();
                        }
                        /*
                         * switch_state() made some changes that don't work
                         * for us
                         */
                        q_cursor_on();

                        local_dirty = Q_TRUE;
                        real_dirty = Q_TRUE;
                    }
                    break;

                default:
                    break;
                }

            } /* if (must_use_picklist == Q_FALSE) */

            /*
             * Save settings -----------------------------------
             */

            if (name != NULL) {
                Xfree(name, __FILE__, __LINE__);
            }
            name = field_get_value(fields[0]);

            if (address != NULL) {
                Xfree(address, __FILE__, __LINE__);
            }
            address = field_get_char_value(fields[1]);

            if (port != NULL) {
                Xfree(port, __FILE__, __LINE__);
            }
            port = field_get_char_value(fields[2]);

            form_value_string = field_get_char_value(fields[3]);
            method = method_from_string(form_value_string);
            Xfree(form_value_string, __FILE__, __LINE);

            if (username != NULL) {
                Xfree(username, __FILE__, __LINE__);
            }
            username = field_get_value(fields[4]);

            if (script_filename != NULL) {
                Xfree(script_filename, __FILE__, __LINE__);
            }
            script_filename = field_get_char_value(fields[6]);

            form_value_string = field_get_char_value(fields[7]);
            emulation = emulation_from_string(form_value_string);
            Xfree(form_value_string, __FILE__, __LINE);

            form_value_string = field_get_char_value(fields[8]);
            codepage = codepage_from_string(form_value_string);
            Xfree(form_value_string, __FILE__, __LINE);

            if (capture_filename != NULL) {
                Xfree(capture_filename, __FILE__, __LINE__);
            }
            capture_filename = field_get_char_value(fields[9]);

            if (translate_8bit_filename != NULL) {
                Xfree(translate_8bit_filename, __FILE__, __LINE__);
            }
            translate_8bit_filename = field_get_char_value(fields[10]);

            if (translate_unicode_filename != NULL) {
                Xfree(translate_unicode_filename, __FILE__, __LINE__);
            }
            translate_unicode_filename = field_get_char_value(fields[11]);

            if (keybindings_filename != NULL) {
                Xfree(keybindings_filename, __FILE__, __LINE__);
            }
            keybindings_filename = field_get_char_value(fields[12]);

            doorway = doorway_from_string(field_get_char_value(fields[13]));

            /*
             * Save settings -----------------------------------
             */

            break;

        case Q_KEY_BACKSPACE:
            if (must_use_picklist == Q_FALSE) {
                fieldset_backspace(edit_form);
            }
            break;
        case Q_KEY_LEFT:
            if (must_use_picklist == Q_FALSE) {
                fieldset_left(edit_form);
            }
            break;
        case Q_KEY_RIGHT:
            if (must_use_picklist == Q_FALSE) {
                fieldset_right(edit_form);
            }
            break;
        case Q_KEY_HOME:
            if (must_use_picklist == Q_FALSE) {
                fieldset_home_char(edit_form);
            }
            break;
        case Q_KEY_END:
            if (must_use_picklist == Q_FALSE) {
                fieldset_end_char(edit_form);
            }
            break;
        case Q_KEY_DC:
            if (must_use_picklist == Q_FALSE) {
                fieldset_delete_char(edit_form);
            }
            break;
        case Q_KEY_IC:
            if (must_use_picklist == Q_FALSE) {
                fieldset_insert_char(edit_form);
            }
            break;

        case Q_KEY_F(10):
            /*
             * The OK exit point
             */

            /*
             * Save settings -----------------------------------
             */

            if (entry->name != NULL) {
                Xfree(entry->name, __FILE__, __LINE__);
            }
            entry->name = field_get_value(fields[0]);

            if (entry->address != NULL) {
                Xfree(entry->address, __FILE__, __LINE__);
            }
            entry->address = field_get_char_value(fields[1]);

            if (entry->port != NULL) {
                Xfree(entry->port, __FILE__, __LINE__);
            }
            entry->port = field_get_char_value(fields[2]);

            form_value_string = field_get_char_value(fields[3]);
            entry->method = method_from_string(form_value_string);
            Xfree(form_value_string, __FILE__, __LINE);

            if (entry->username != NULL) {
                Xfree(entry->username, __FILE__, __LINE__);
            }
            entry->username = field_get_value(fields[4]);

            if (field_number == PASSWORD) {
                /*
                 * We're exiting on the password field
                 */
                /*
                 * Save the edited password
                 */
                if (password != NULL) {
                    Xfree(password, __FILE__, __LINE__);
                }
                password = field_get_value(fields[5]);
            }
            if (entry->password != NULL) {
                Xfree(entry->password, __FILE__, __LINE__);
            }
            entry->password = Xwcsdup(password, __FILE__, __LINE__);

            if (entry->script_filename != NULL) {
                Xfree(entry->script_filename, __FILE__, __LINE__);
            }
            entry->script_filename = field_get_char_value(fields[6]);

            form_value_string = field_get_char_value(fields[7]);
            entry->emulation = emulation_from_string(form_value_string);
            Xfree(form_value_string, __FILE__, __LINE);

            form_value_string = field_get_char_value(fields[8]);
            entry->codepage = codepage_from_string(form_value_string);
            Xfree(form_value_string, __FILE__, __LINE);

            if (entry->capture_filename != NULL) {
                Xfree(entry->capture_filename, __FILE__, __LINE__);
            }
            entry->capture_filename = field_get_char_value(fields[9]);

            if (entry->translate_8bit_filename != NULL) {
                Xfree(entry->translate_8bit_filename, __FILE__, __LINE__);
            }
            entry->translate_8bit_filename = field_get_char_value(fields[10]);

            if (entry->translate_unicode_filename != NULL) {
                Xfree(entry->translate_unicode_filename, __FILE__, __LINE__);
            }
            entry->translate_unicode_filename =
                field_get_char_value(fields[11]);

            if (entry->keybindings_filename != NULL) {
                Xfree(entry->keybindings_filename, __FILE__, __LINE__);
            }
            entry->keybindings_filename = field_get_char_value(fields[12]);

            entry->doorway =
                doorway_from_string(field_get_char_value(fields[13]));

#ifndef Q_NO_SERIAL
            entry->use_modem_cfg = use_modem_cfg;
            entry->baud = baud;
            entry->data_bits = data_bits;
            entry->parity = parity;
            entry->stop_bits = stop_bits;
            entry->xonxoff = xonxoff;
            entry->rtscts = rtscts;
            entry->lock_dte_baud = lock_dte_baud;
#endif
            entry->use_default_toggles = use_default_toggles;
            entry->toggles = toggles;

            /*
             * Save settings -----------------------------------
             */

            fieldset_free(edit_form);
            screen_delwin(form_window);
            Xfree(password_stars, __FILE__, __LINE__);

            /*
             * No leak
             */
            if (name != NULL) {
                Xfree(name, __FILE__, __LINE__);
            }
            if (address != NULL) {
                Xfree(address, __FILE__, __LINE__);
            }
            if (port != NULL) {
                Xfree(port, __FILE__, __LINE__);
            }
            if (password != NULL) {
                Xfree(password, __FILE__, __LINE__);
            }
            if (username != NULL) {
                Xfree(username, __FILE__, __LINE__);
            }
            if (script_filename != NULL) {
                Xfree(script_filename, __FILE__, __LINE__);
            }
            if (capture_filename != NULL) {
                Xfree(capture_filename, __FILE__, __LINE__);
            }
            if (translate_8bit_filename != NULL) {
                Xfree(translate_8bit_filename, __FILE__, __LINE__);
            }
            if (translate_unicode_filename != NULL) {
                Xfree(translate_unicode_filename, __FILE__, __LINE__);
            }
            if (keybindings_filename != NULL) {
                Xfree(keybindings_filename, __FILE__, __LINE__);
            }

            return;

        case '\\':
            /*
             * Alt-\ Alt Code key
             */
            if (flags & KEY_FLAG_ALT) {
                if (must_use_picklist == Q_FALSE) {
                    new_keystroke = alt_code_key(Q_TRUE);
                    if (new_keystroke > 0) {
                        /*
                         * Pass normal keys to form driver
                         */
                        if (q_key_code_yes(new_keystroke) == 0) {
                            fieldset_keystroke(edit_form, new_keystroke);
                        }
                    }
                    dont_reload = Q_TRUE;
                    local_dirty = Q_TRUE;
                    real_dirty = Q_TRUE;
                }
                break;
            }

            /*
             * Fall through so that '\' can be picked up by the form
             * handler.
             */
        default:
            /*
             * This code is also copied in the case ' ' above case F2.  Make
             * sure any changes here are reflected there too.
             */
            if ((!q_key_code_yes(keystroke)) && (keystroke != ERR)) {
                if (must_use_picklist == Q_FALSE) {
                    /*
                     * Pass normal keys to form driver
                     */
                    fieldset_keystroke(edit_form, keystroke);
                }
            }
            break;
        }
    } /* for (;;) */

    /*
     * Should never get here.
     */
    return;
}

/**
 * Keyboard handler for the phonebook screen.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
void phonebook_keyboard_handler(const int keystroke, const int flags) {
    struct q_phone_struct * entry;
    Q_BOOL wrapped_around;
    int delete_flag;
    SORT_METHOD sort_method;
    wchar_t * notes_line;
    int current_notes_idx;
    struct file_info * phonebook_file_info;
    int i;
    char * pick_string;
    int visible_entries_n = HEIGHT - 1 - 14;
    int new_phonebook_entry_i;
    int new_phonebook_page;
    static wchar_t * search_string = NULL;
    char * filename;
    char * print_destination = NULL;
    int new_keystroke;

    switch (keystroke) {

    case Q_KEY_F(1):
        launch_help(Q_HELP_PHONEBOOK);

        /*
         * Refresh the whole screen.  This is one of the few screens that is
         * "top level" where just a q_screen_dirty is enough.
         */
        q_screen_dirty = Q_TRUE;
        break;

    case '`':
        /*
         * Backtick works too
         */
    case Q_KEY_ESCAPE:
        /*
         * ESC return to TERMINAL mode
         */
        switch_state(Q_STATE_CONSOLE);

        /*
         * The ABORT exit point
         */

        /*
         * Save phonebook
         */
        if (phonebook_is_mine(Q_FALSE) == Q_TRUE) {
            save_phonebook(Q_FALSE);
        }
        return;

    case 'i':
    case 'I':
    case Q_KEY_IC:
        entry =
            (struct q_phone_struct *) Xmalloc(sizeof(struct q_phone_struct),
                                              __FILE__, __LINE__);
        entry->name             = Xwcsdup(L"", __FILE__, __LINE__);
        entry->address          = Xstrdup("", __FILE__, __LINE__);
        entry->port             = Xstrdup("", __FILE__, __LINE__);
        entry->username         = Xwcsdup(L"", __FILE__, __LINE__);
        entry->password         = Xwcsdup(L"", __FILE__, __LINE__);
        entry->method           = Q_DIAL_METHOD_SSH;
        entry->port             = default_port(entry->method);
        entry->emulation        = Q_EMUL_XTERM_UTF8;
        entry->codepage         = default_codepage(entry->emulation);
        entry->script_filename  = Xstrdup("", __FILE__, __LINE__);
        entry->capture_filename           = Xstrdup("", __FILE__, __LINE__);
        entry->translate_8bit_filename    = Xstrdup("", __FILE__, __LINE__);
        entry->translate_unicode_filename = Xstrdup("", __FILE__, __LINE__);
        entry->keybindings_filename       = Xstrdup("", __FILE__, __LINE__);
        entry->doorway          = Q_DOORWAY_CONFIG;
        entry->use_default_toggles = Q_TRUE;
        entry->toggles          = 0;
        entry->notes            = NULL;
        entry->last_call        = 0;
        entry->times_on         = 0;
        entry->tagged           = Q_FALSE;
#ifndef Q_NO_SERIAL
        entry->use_modem_cfg    = Q_TRUE;
        entry->baud             = q_modem_config.default_baud;
        entry->data_bits        = q_modem_config.default_data_bits;
        entry->parity           = q_modem_config.default_parity;
        entry->stop_bits        = q_modem_config.default_stop_bits;
        entry->rtscts           = q_modem_config.rtscts;
        entry->xonxoff          = q_modem_config.xonxoff;
        entry->lock_dte_baud    = q_modem_config.lock_dte_baud;
#endif
        entry->quicklearn       = Q_FALSE;

        if (q_phonebook.entry_count > 0) {
            entry->next = q_phonebook.selected_entry;
            entry->prev = q_phonebook.selected_entry->prev;
            if (q_phonebook.selected_entry->prev != NULL) {
                q_phonebook.selected_entry->prev->next = entry;
            } else {
                /*
                 * Inserting the head
                 */
                q_phonebook.entries = entry;
            }
            q_phonebook.selected_entry->prev = entry;
        } else {
            q_phonebook.entries = entry;
            entry->next = NULL;
            entry->prev = NULL;
        }
        q_phonebook.selected_entry = entry;
        q_phonebook.entry_count++;

        /*
         * Fall through ...
         */
    case 'r':
    case 'R':
        /*
         * Don't do anything if the phonebook is empty
         */
        if (q_phonebook.entry_count == 0) {
            break;
        }
        /*
         * Revise entry
         */
        q_cursor_on();
        edit_phone_entry_form(q_phonebook.selected_entry);
        q_cursor_off();
        break;

    case 'd':
    case 'D':
    case Q_KEY_DC:
        /*
         * Don't delete anything if the phonebook is empty
         */
        if (q_phonebook.entry_count == 0) {
            break;
        }

        /*
         * Popup the delete dialog
         */
        q_cursor_on();
        delete_flag = delete_popup();
        q_cursor_off();

        if (delete_flag == -1) {
            /*
             * User aborted
             */
            break;
        }

        /*
         * Save a backup copy
         */
        save_phonebook(Q_TRUE);

        if (delete_flag == 1) {
            /*
             * Notes only
             */
            entry = q_phonebook.selected_entry;
            if (entry->notes != NULL) {
                current_notes_idx = 0;
                for (notes_line = entry->notes[current_notes_idx];
                     notes_line != NULL;
                     current_notes_idx++, notes_line =
                     entry->notes[current_notes_idx]) {
                    Xfree(notes_line, __FILE__, __LINE__);
                }
                Xfree(entry->notes, __FILE__, __LINE__);
                entry->notes = NULL;
            }
            break;
        }

        if (delete_flag == 2) {
            entry = q_phonebook.selected_entry;

            if (entry == q_current_dial_entry) {
                /*
                 * Can't delete current connection
                 */
                notify_form(_("Can't delete current connection while Online"),
                            1.5);
            } else {
                if (entry->next != NULL) {
                    q_phonebook.selected_entry = entry->next;
                } else {
                    q_phonebook.selected_entry = entry->prev;
                    phonebook_entry_i--;
                }

                delete_phonebook_entry(entry);
            }
            break;
        }

        break;

    case 'f':
    case 'F':
        /*
         * Find Text
         */
        found_note_flag = Q_FALSE;
        if (search_string != NULL) {
            Xfree(search_string, __FILE__, __LINE__);
            search_string = NULL;
        }
        q_cursor_on();
        search_string = pick_find_string();
        q_cursor_off();
        if (search_string == NULL) {
            break;
        }
        /*
         * Search for the first matching entry
         */
        entry = q_phonebook.entries;
        new_phonebook_entry_i = 0;
        new_phonebook_page = 0;

        while (entry != NULL) {
            if (match_phonebook_entry(search_string, entry) == Q_TRUE) {
                q_phonebook.selected_entry = entry;
                phonebook_entry_i = new_phonebook_entry_i;
                phonebook_page = new_phonebook_page;
                break;
            }

            entry = entry->next;
            if (entry == NULL) {
                /*
                 * Text not found
                 */
                notify_form(_("Text not found"), 1.5);
                break;
            } else {
                new_phonebook_entry_i++;
                if (new_phonebook_entry_i % visible_entries_n == 0) {
                    new_phonebook_page++;
                }
            }
        }
        /*
         * No leak
         */
        Xfree(search_string, __FILE__, __LINE__);
        search_string = NULL;
        break;

    case 'a':
    case 'A':
        /*
         * Find Again
         */
        found_note_flag = Q_FALSE;
        if (q_phonebook.selected_entry == NULL) {
            /*
             * Phonebook size should be zero
             */
            break;
        }

        if (search_string == NULL) {
            /*
             * If this is the first search (even though it's "Find Again", go
             * ahead and pop up the find dialog.
             */
            q_cursor_on();
            search_string = pick_find_string();
            q_cursor_off();
            if (search_string == NULL) {
                break;
            }
        }

        new_phonebook_entry_i = phonebook_entry_i;
        new_phonebook_page = phonebook_page;
        entry = q_phonebook.selected_entry->next;
        if (entry == NULL) {
            /*
             * No more matches
             */
            notify_form(_("No more matches"), 1.5);
        }

        new_phonebook_entry_i++;
        if (new_phonebook_entry_i % visible_entries_n == 0) {
            new_phonebook_page++;
        }

        while (entry != NULL) {
            if (match_phonebook_entry(search_string, entry) == Q_TRUE) {
                q_phonebook.selected_entry = entry;
                phonebook_entry_i = new_phonebook_entry_i;
                phonebook_page = new_phonebook_page;
                break;
            }

            entry = entry->next;
            if (entry == NULL) {
                /*
                 * No more matches
                 */
                notify_form(_("No more matches"), 1.5);
                break;
            } else {
                new_phonebook_entry_i++;
                if (new_phonebook_entry_i % visible_entries_n == 0) {
                    new_phonebook_page++;
                }
            }
        }

        break;

#ifndef Q_NO_SERIAL
    case 'm':
    case 'M':
        /*
         * Manual dial
         */
        if (q_status.online == Q_TRUE) {
            notify_form(_("Cannot choose Manual Dial when already Online."),
                        1.5);
            break;
        }

        q_cursor_on();
        pick_string = pick_manual_phone_number();
        q_cursor_off();
        if (pick_string != NULL) {

            entry = (struct q_phone_struct *) Xmalloc(
                sizeof(struct q_phone_struct), __FILE__, __LINE__);

            /*
             * Set some defaults, the user may not like them
             */
            entry->name =
                Xstring_to_wcsdup(_("Manual Call"), __FILE__, __LINE__);
            entry->method = Q_DIAL_METHOD_MODEM;

            /*
             * This will leak, but I'm not sure how to cleanly free it.  It's
             * not really worth going after.
             */
            entry->address = pick_string;

            entry->port = "";
            entry->username = L"";
            entry->password = L"";
            entry->emulation = Q_EMUL_ANSI;
            entry->codepage = default_codepage(entry->emulation);
            entry->notes = NULL;
            entry->script_filename = "";
            entry->capture_filename = "";
            entry->translate_8bit_filename = "";
            entry->translate_unicode_filename = "";
            entry->keybindings_filename = "";
            entry->doorway = Q_DOORWAY_CONFIG;
            entry->use_default_toggles = Q_TRUE;
            entry->toggles = 0;
            entry->use_modem_cfg = Q_TRUE;
            entry->quicklearn = Q_FALSE;

            time(&q_dialer_start_time);
            q_dialer_attempts = 0;
            q_current_dial_entry = entry;
            do_dialer();
        }
        break;
#endif /* Q_NO_SERIAL */

    case 'n':
    case 'N':
        /*
         * Edit attached note
         */
        if (q_phonebook.selected_entry != NULL) {
            /*
             * Save a backup copy
             */
            save_phonebook(Q_TRUE);
            edit_attached_note(q_phonebook.selected_entry);
        }
        break;

    case 'l':
    case 'L':
        /*
         * Load new phonebook
         */
        /*
         * Save a copy of this one
         */
        if (phonebook_is_mine(Q_FALSE) == Q_TRUE) {
            save_phonebook(Q_FALSE);
        }
        phonebook_file_info = view_directory(q_home_directory, "*.txt");
        if (phonebook_file_info != NULL) {
            q_phonebook.filename = phonebook_file_info->name;
            load_phonebook(Q_FALSE);
            Xfree(phonebook_file_info, __FILE__, __LINE__);
        }
        break;

    case 'o':
    case 'O':
        /*
         * Switch view mode
         */
        q_phonebook.view_mode++;
        if (q_phonebook.view_mode == VIEW_MODE_MAX) {
            q_phonebook.view_mode = 0;
        }
        break;

    case 'p':
    case 'P':
    case 0x10:
        /*
         * Don't print anything if the phonebook is empty
         */
        if (q_phonebook.entry_count == 0) {
            break;
        }

        /*
         * Popup the print dialog
         */
        q_cursor_on();
        print_destination = pick_print_destination();
        q_cursor_off();

        if (print_destination == NULL) {
            /*
             * User aborted
             */
            break;
        }

        if (keystroke == 0x10) {
            print_phonebook_132(print_destination);
        } else {
            print_phonebook_80(print_destination);
        }

        /*
         * No leak
         */
        Xfree(print_destination, __FILE__, __LINE__);
        print_destination = NULL;
        break;

    case 'q':
    case 'Q':
        /*
         * Q - QuickLearn
         */
        if (q_phonebook.selected_entry == NULL) {
            /*
             * Phonebook size should be zero
             */
            break;
        }
        if (q_phonebook.selected_entry->tagged == Q_TRUE) {
            q_phonebook.selected_entry->tagged = Q_FALSE;
            q_phonebook.selected_entry->quicklearn = Q_FALSE;
            q_phonebook.tagged--;
        } else {
            assert(q_phonebook.selected_entry->script_filename != NULL);
            if (q_phonebook.selected_entry->quicklearn == Q_TRUE) {
                q_phonebook.selected_entry->quicklearn = Q_FALSE;
            } else {
                if (strlen(q_phonebook.selected_entry->script_filename) == 0) {
                    notify_form(
                        _("Script file must be specified to QuickLearn"), 1.5);
                    break;
                }
                if (file_exists
                    (get_scriptdir_filename
                     (q_phonebook.selected_entry->script_filename)) == Q_TRUE) {
                    /*
                     * Prompt to overwrite
                     */
                    new_keystroke =
                        q_tolower(notify_prompt_form(
                            _("Script File Already Exists"),
                            _(" Overwrite File? [Y/n] "),
                            _(" Y-Overwrite Script File   N-Do Not Quicklearn "),
                            Q_TRUE, 0.0, "YyNn\r"));
                    if ((new_keystroke == 'y') ||
                        (new_keystroke == Q_KEY_ENTER)
                    ) {
                        q_phonebook.selected_entry->quicklearn = Q_TRUE;
                        q_phonebook.selected_entry->tagged = Q_TRUE;
                        q_phonebook.tagged++;
                    }
                } else {
                    q_phonebook.selected_entry->quicklearn = Q_TRUE;
                    q_phonebook.selected_entry->tagged = Q_TRUE;
                    q_phonebook.tagged++;
                }
            }
        }
        /*
         * Advance to the next entry
         */
        if (q_phonebook.selected_entry->next != NULL) {
            q_phonebook.selected_entry = q_phonebook.selected_entry->next;
            phonebook_normalize();
        }
        break;

    case 's':
    case 'S':
        /*
         * Sort popup
         */
        sort_method = pick_sort();
        if (sort_method != -1) {
            /*
             * Save a backup copy
             */
            save_phonebook(Q_TRUE);
            sort_phonebook(sort_method);
        }
        break;

    case 't':
    case 'T':
        /*
         * Tag Multiple
         */
        q_cursor_on();
        pick_string = pick_tag_string();
        q_cursor_off();
        if (pick_string != NULL) {
            tag_multiple(pick_string);
            Xfree(pick_string, __FILE__, __LINE__);
        }
        break;

    case 'u':
    case 'U':
        /*
         * Untag all
         */
        for (entry = q_phonebook.entries; entry != NULL; entry = entry->next) {
            if (entry->tagged == Q_TRUE) {
                entry->tagged = Q_FALSE;
            }
        }
        q_phonebook.tagged = 0;
        break;

    case 'v':
    case 'V':
        /*
         * Pull up external editor
         */
        if (q_phonebook.selected_entry != NULL) {
            filename =
                Xstrdup(q_phonebook.selected_entry->script_filename, __FILE__,
                        __LINE__);
            for (i = 0; i < strlen(filename); i++) {
                if (filename[i] == ' ') {
                    filename[i] = 0x0;
                }
            }
            spawn_script_editor(filename);
            Xfree(filename, __FILE__, __LINE__);
        }
        break;

    case ' ':
        /*
         * Tag/Untag
         */
        if (q_phonebook.selected_entry != NULL) {
            if (q_phonebook.selected_entry->tagged == Q_TRUE) {
                q_phonebook.selected_entry->tagged = Q_FALSE;
                q_phonebook.selected_entry->quicklearn = Q_FALSE;
                q_phonebook.tagged--;
            } else {
                q_phonebook.selected_entry->tagged = Q_TRUE;
                q_phonebook.tagged++;
                if (strlen(q_phonebook.selected_entry->script_filename) > 0) {
                    if (file_exists
                        (get_scriptdir_filename
                         (q_phonebook.selected_entry->script_filename)) ==
                        Q_FALSE) {
                        /*
                         * Automatically QuickLearn on new scripts
                         */
                        q_phonebook.selected_entry->quicklearn = Q_TRUE;
                    }
                }
            }
            /*
             * Advance to the next entry
             */
            if (q_phonebook.selected_entry->next != NULL) {
                q_phonebook.selected_entry = q_phonebook.selected_entry->next;
                phonebook_normalize();
            }
        }
        break;

    case 0x04:
        /*
         * CTRL-D
         */

        /*
         * Popup the delete dialog
         */
        q_cursor_on();
        delete_flag = delete_popup();
        q_cursor_off();

        if (delete_flag == -1) {
            /*
             * User aborted
             */
            break;
        }

        /*
         * Save a backup copy
         */
        save_phonebook(Q_TRUE);

        entry = q_phonebook.entries;
        while (entry != NULL) {
            if (entry->tagged == Q_TRUE) {
                if (delete_flag == 1) {
                    /*
                     * Notes only
                     */
                    if (entry->notes != NULL) {
                        current_notes_idx = 0;
                        for (notes_line = entry->notes[current_notes_idx];
                             notes_line != NULL;
                             current_notes_idx++,
                                 notes_line = entry->notes[current_notes_idx]) {
                            Xfree(notes_line, __FILE__, __LINE__);
                        }
                        Xfree(entry->notes, __FILE__, __LINE__);
                        entry->notes = NULL;
                    }
                }

                if (delete_flag == 2) {
                    if (entry == q_current_dial_entry) {
                        /*
                         * Can't delete current connection
                         */
                        notify_form(
                            _("Can't delete current connection while Online"),
                            1.5);
                    } else {
                        /*
                         * Delete entire entry
                         */
                        delete_phonebook_entry(entry);
                    }
                }

                /*
                 * Restart from beginning
                 */
                entry = q_phonebook.entries;
                continue;
            }
            entry = entry->next;
        }

        q_phonebook.selected_entry = q_phonebook.entries;
        phonebook_entry_i = 0;
        phonebook_page = 0;
        break;

    case 0x12:
        /*
         * CTRL-R
         */
        /*
         * Revise tagged
         */
        for (entry = q_phonebook.entries; entry != NULL; entry = entry->next) {
            if (entry->tagged == Q_TRUE) {
                q_cursor_on();
                edit_phone_entry_form(entry);
                q_cursor_off();
            }
        }
        break;

    case 0x15:
        /*
         * CTRL-U
         */
        load_phonebook(Q_TRUE);
        break;

    case Q_KEY_UP:
        if ((q_phonebook.selected_entry != NULL)
            && (q_phonebook.selected_entry->prev != NULL)) {
            q_phonebook.selected_entry = q_phonebook.selected_entry->prev;
            phonebook_entry_i--;

            if (phonebook_entry_i % visible_entries_n ==
                visible_entries_n - 1) {

                phonebook_page--;
            }
        }

        break;

    case Q_KEY_DOWN:
        if ((q_phonebook.selected_entry != NULL) &&
            (q_phonebook.selected_entry->next != NULL)
        ) {
            q_phonebook.selected_entry = q_phonebook.selected_entry->next;
            phonebook_entry_i++;

            if (phonebook_entry_i % visible_entries_n == 0) {
                phonebook_page++;
            }
        }

        break;

    case Q_KEY_PPAGE:
        /*
         * PgUp
         */
        if (q_phonebook.selected_entry == NULL) {
            /*
             * Phonebook size should be zero
             */
            break;
        }
        entry = q_phonebook.selected_entry;
        for (i = 0; (i < visible_entries_n) && (entry->prev != NULL); i++) {
            entry = entry->prev;
        }
        q_phonebook.selected_entry = entry;
        phonebook_normalize();
        break;

    case Q_KEY_NPAGE:
        /*
         * PgDn
         */
        if (q_phonebook.selected_entry == NULL) {
            /*
             * Phonebook size should be zero
             */
            break;
        }
        entry = q_phonebook.selected_entry;
        for (i = 0; (i < visible_entries_n) && (entry->next != NULL); i++) {
            entry = entry->next;
        }
        q_phonebook.selected_entry = entry;
        phonebook_normalize();
        break;

    case Q_KEY_HOME:
        /*
         * Home
         */
        if (q_phonebook.selected_entry == NULL) {
            /*
             * Phonebook size should be zero
             */
            break;
        }
        q_phonebook.selected_entry = q_phonebook.entries;
        phonebook_entry_i = 0;
        phonebook_page = 0;
        break;

    case Q_KEY_END:
        /*
         * End
         */
        if (q_phonebook.selected_entry == NULL) {
            /*
             * Phonebook size should be zero
             */
            break;
        }
        entry = q_phonebook.selected_entry;
        while (entry->next != NULL) {
            entry = entry->next;
        }
        q_phonebook.selected_entry = entry;
        phonebook_normalize();
        break;

    case Q_KEY_ENTER:
        /*
         * Dial
         */
        if (q_phonebook.selected_entry == NULL) {
            /*
             * Phonebook size should be zero
             */
            break;
        }

        entry = q_phonebook.selected_entry;
        wrapped_around = Q_FALSE;
        for (;;) {
            if (entry->tagged == Q_TRUE) {
                q_phonebook.selected_entry = entry;
                break;
            }

            entry = entry->next;
            if (entry == NULL) {
                /*
                 * Wrap around
                 */
                entry = q_phonebook.entries;
                wrapped_around = Q_TRUE;
                phonebook_entry_i = 0;
                phonebook_page = 0;
            } else {
                phonebook_entry_i++;
                if (phonebook_entry_i % visible_entries_n == 0) {
                    phonebook_page++;
                }
            }

            if ((entry == q_phonebook.selected_entry) &&
                (wrapped_around == Q_TRUE)
            ) {
                /*
                 * We circled around and found no tagged entries, so break
                 * without setting a new selected_entry.  This is the normal
                 * behavior when nothing is tagged.
                 */
                break;
            }
        }

        if (strlen(q_phonebook.selected_entry->script_filename) > 0) {
            if (file_exists
                (get_scriptdir_filename
                 (q_phonebook.selected_entry->script_filename)) == Q_FALSE) {
                /*
                 * Automatically QuickLearn on new scripts
                 */
                q_phonebook.selected_entry->quicklearn = Q_TRUE;
            }
        }

        if (q_status.online == Q_FALSE) {
            time(&q_dialer_start_time);
            q_dialer_attempts = 0;
            q_current_dial_entry = q_phonebook.selected_entry;
            do_dialer();
        }
        break;

    default:
        /*
         * Ignore keystroke
         */
        return;
    }

    /*
     * The OK exit point
     */
    q_screen_dirty = Q_TRUE;
}

/**
 * Keyboard handler for the modem/connection dialer.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
void dialer_keyboard_handler(const int keystroke, const int flags) {

    /*
     * Press ANY key to continue
     */
    if (q_dial_state == Q_DIAL_CONNECTED) {
        /*
         * Time is up, switch to console
         */
        switch_state(Q_STATE_CONSOLE);
        q_screen_dirty = Q_TRUE;
        check_for_dialup_script();
        return;
    }

    switch (keystroke) {

    case 'K':
    case 'k':
        /*
         * Untag this entry and cycle to the next one
         */
        if (kill_redialer_number() == Q_TRUE) {
            q_dial_state = Q_DIAL_KILLED;
        } else {
            /*
             * We're out of numbers to call
             */
            q_dial_state = Q_DIAL_NO_NUMBERS_LEFT;
        }
        time(&q_dialer_cycle_start_time);
        close_dial_entry();
        break;

    case 'C':
    case 'c':
        /*
         * Cycle to the next entry
         */
        time(&q_dialer_cycle_start_time);
        if ((q_dial_state != Q_DIAL_MANUAL_CYCLE) &&
            (q_dial_state != Q_DIAL_BETWEEN_PAUSE)
        ) {
            q_dial_state = Q_DIAL_MANUAL_CYCLE;
        } else {
            q_dialer_cycle_start_time -=
                atoi(get_option(Q_OPTION_DIAL_BETWEEN_TIME));;
        }
        close_dial_entry();
        break;

    case 'X':
    case 'x':
        /*
         * Add 10 seconds to the timeout
         */
        q_dialer_cycle_start_time += 10;
        break;

    case '`':
        /*
         * Backtick works too
         */
    case Q_KEY_ESCAPE:
        if ((q_dial_state == Q_DIAL_NO_NUMBERS_LEFT)
            || (q_dial_state == Q_DIAL_USER_ABORTED)) {
            close_dial_entry();
            q_current_dial_entry = NULL;
            switch_state(Q_STATE_PHONEBOOK);
            q_screen_dirty = Q_TRUE;
            /*
             * We need to explicitly call refresh_handler() because
             * phonebook_keyboard_handler() blocks.
             */
            refresh_handler();
        } else {
            q_dial_state = Q_DIAL_USER_ABORTED;
            time(&q_dialer_cycle_start_time);
        }
        /*
         * The ABORT exit point
         */
        return;

    default:
        /*
         * Ignore keystroke
         */
        return;
    }

}

#ifndef Q_NO_SERIAL

/**
 * Process raw bytes to and from the modem during a MODEM connection attempt.
 * This is analogous to console_process_incoming_data().
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
static void modem_data(unsigned char * input, unsigned int input_n,
                       int * remaining, unsigned char * output,
                       unsigned int * output_n,
                       const unsigned int output_max) {
    int i;
    Q_BOOL complete_line = Q_FALSE;
    int new_dce_baud;
    char * begin = (char *) input;
    int menu_left = 1 + (WIDTH - 80) / 2;
    int menu_top = HEIGHT - 1 - 9;

    DLOG(("modem_data()\n"));

    if (modem_state == DIAL_MODEM_CONNECTED) {
        DLOG(("modem_data() DIAL_MODEM_CONNECTED\n"));

        /*
         * We got some data.  Let the console process it.
         */
        if (input_n > 0) {
            console_process_incoming_data(input, input_n, remaining);
        }
        return;
    }

    /*
     * Break up whatever is coming in into separate lines
     */
    for (i = 0; i < input_n; i++) {
        if (input[i] == '\n') {
            /*
             * Ignore line feeds
             */
            input[i] = 0;
        }
    }
    while ((*remaining > 0) && ((begin[0] == 0) || q_isspace(begin[0]))) {
        begin++;
        *remaining -= 1;
        if (*remaining == 0) {
            return;
        }
    }

    DLOG(("modem_data() %d input after stripping leading whitespace: ",
            input_n));
    for (i = 0; i < *remaining; i++) {
        DLOG2(("%02x ", begin[i]));
    }
    DLOG2((" | \""));
    for (i = 0; i < *remaining; i++) {
        DLOG2(("%c", begin[i]));
    }
    DLOG2(("\"\n"));

    for (i = 0; i < *remaining; i++) {
        if (begin[i] == '\r') {
            /*
             * Break on carriage return
             */
            complete_line = Q_TRUE;
            begin[i] = 0;

            /*
             * Clear UI line
             */
            memset(q_dialer_modem_message, 0, sizeof(q_dialer_modem_message));

            /*
             * Copy what is here to it
             */
            snprintf(q_dialer_modem_message, sizeof(q_dialer_modem_message),
                     "%s", begin);

            /*
             * Stop looking for a line terminator
             */
            break;
        }
    }

    DLOG(("modem_data() %d input after looking for CR: ", *remaining));
    for (i = 0; i < *remaining; i++) {
        DLOG2(("%02x ", begin[i]));
    }
    DLOG2((" | \""));
    for (i = 0; i < *remaining; i++) {
        DLOG2(("%c", begin[i]));
    }
    DLOG2(("\"\n"));

    DLOG(("modem_data() q_dialer_modem_message = \"%s\"\n",
            q_dialer_modem_message));

    switch (modem_state) {
    case DIAL_MODEM_INIT:
        DLOG(("modem_data() MODEM_INIT\n"));

        /*
         * Send AT, expect OK
         */
        snprintf((char *) output, output_max, "AT\r");
        *output_n = strlen((char *) output);
        modem_state = DIAL_MODEM_SENT_AT;
        break;

    case DIAL_MODEM_SENT_AT:
        DLOG(("modem_data() MODEM_SENT_AT\n"));

        if (complete_line == Q_TRUE) {
            if (strcasecmp(q_dialer_modem_message, "ok") == 0) {
                DLOG(("modem_data() MODEM_SENT_AT ** OK **\n"));
                /*
                 * Got OK.  Send the dial string
                 */
                snprintf((char *) output, output_max, "%s%s\r",
                         q_modem_config.dial_string,
                         q_current_dial_entry->address);
                *output_n = strlen((char *) output);

                /*
                 * Toss the input seen so far
                 */
                *remaining -= strlen(begin);
                if (*remaining < 0) {
                    *remaining = 0;
                }

                /*
                 * Show the dial command on the UI.
                 */
                memcpy(q_dialer_modem_message, output, *output_n - 1);

                /*
                 * New state
                 */
                modem_state = DIAL_MODEM_SENT_DIAL_STRING;

            } else {

                /*
                 * Modem sent something else.  Re-send AT, and expect OK.
                 */
                snprintf((char *) output, output_max, "AT\r");
                *output_n = strlen((char *) output);

                /*
                 * Toss the input seen so far
                 */
                *remaining -= strlen(begin);
                if (*remaining < 0) {
                    *remaining = 0;
                }
            }
        }

        break;

    case DIAL_MODEM_SENT_DIAL_STRING:
        DLOG(("modem_data() MODEM_SENT_DIAL_STRING\n"));

        if (complete_line == Q_TRUE) {
            DLOG(("modem_data() MODEM_SENT_DIAL_STRING line came in: %s\n",
                    q_dialer_modem_message));

            /*
             * Expect CONNECT, NO CARRIER, BUSY, or VOICE
             */
            if ((strstr(q_dialer_modem_message, "NO DIALTONE") != NULL) ||
                (strstr(q_dialer_modem_message, "BUSY") != NULL) ||
                (strstr(q_dialer_modem_message, "NO CARRIER") != NULL) ||
                (strstr(q_dialer_modem_message, "VOICE") != NULL)) {

                DLOG(("modem_data() MODEM_SENT_DIAL_STRING CYCLE\n"));

                /*
                 * Uh-oh, cycle
                 */
                q_dial_state = Q_DIAL_LINE_BUSY;
                time(&q_dialer_cycle_start_time);

                /*
                 * Toss the input seen so far
                 */
                *remaining -= strlen(begin);
                if (*remaining < 0) {
                    *remaining = 0;
                }

            } else {
                /*
                 * Modem sent something else, just discard this part.  But
                 * keep it on the display.
                 */

                /*
                 * Toss the input seen so far
                 */
                *remaining -= strlen(begin);
                if (*remaining < 0) {
                    *remaining = 0;
                }
            }

            if (strstr(q_dialer_modem_message, "CONNECT") != NULL) {
                /*
                 * Yippee, connect!
                 */
                DLOG(("modem_data() MODEM_SENT_DIAL_STRING *** CONNECT ***\n"));

                /*
                 * Find baud
                 */
                DLOG(("q_dialer_modem_message \'%s\'\n",
                        q_dialer_modem_message));

                if (sscanf(q_dialer_modem_message, "CONNECT %d",
                        &new_dce_baud) == 1) {

                    q_serial_port.dce_baud = new_dce_baud;

                    if (q_serial_port.lock_dte_baud == Q_FALSE) {
                        /*
                         * Change DTE baud rate
                         */
                        if (q_serial_port.dce_baud <= 300) {
                            q_serial_port.baud = Q_BAUD_300;
                        } else if (q_serial_port.dce_baud <= 1200) {
                            q_serial_port.baud = Q_BAUD_1200;
                        } else if (q_serial_port.dce_baud <= 2400) {
                            q_serial_port.baud = Q_BAUD_2400;
                        } else if (q_serial_port.dce_baud <= 4800) {
                            q_serial_port.baud = Q_BAUD_4800;
                        } else if (q_serial_port.dce_baud <= 9600) {
                            q_serial_port.baud = Q_BAUD_9600;
                        } else if (q_serial_port.dce_baud <= 19200) {
                            q_serial_port.baud = Q_BAUD_19200;
                        } else if (q_serial_port.dce_baud <= 38400) {
                            q_serial_port.baud = Q_BAUD_38400;
                        } else if (q_serial_port.dce_baud <= 57600) {
                            q_serial_port.baud = Q_BAUD_57600;
                        } else if (q_serial_port.dce_baud <= 115200) {
                            q_serial_port.baud = Q_BAUD_115200;
                        } else {
                            q_serial_port.baud = Q_BAUD_115200;
                        }
                        configure_serial_port();
                    }
                }
                dial_success();

                /*
                 * Set the status message here.  This is ripped directly out
                 * of phonebook_refresh().
                 */
                sprintf(q_dialer_status_message,
                        _("CONNECTED, press a key to continue"));
                screen_put_color_hline_yx(menu_top + 5, menu_left + 2, ' ', 55,
                                          Q_COLOR_MENU_TEXT);
                screen_put_color_str_yx(menu_top + 5, menu_left + 2,
                                        _("Modem  : "), Q_COLOR_MENU_TEXT);
                screen_put_color_str(q_dialer_modem_message,
                                     Q_COLOR_MENU_COMMAND);
                screen_put_color_str_yx(menu_top + 6, menu_left + 2,
                                        _("Status : "), Q_COLOR_MENU_TEXT);
                screen_put_color_str(q_dialer_status_message,
                                     Q_COLOR_MENU_COMMAND);
                screen_flush();

                modem_state = DIAL_MODEM_CONNECTED;
                time(&q_dialer_cycle_start_time);

                qlog(_("CONNECTION ESTABLISHED: %d baud\n"),
                    q_serial_port.dce_baud);

                /*
                 * Play connect sequence
                 */
                if (q_status.beeps == Q_TRUE) {
                    play_sequence(Q_MUSIC_CONNECT_MODEM);
                }
            }

        }
        break;

    case DIAL_MODEM_CONNECTED:
        /*
         * Should never get here.
         */
        abort();
        break;
    }

}

#endif /* Q_NO_SERIAL */

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
void dialer_process_data(unsigned char * input, const unsigned int input_n,
                         int * remaining, unsigned char * output,
                         unsigned int * output_n,
                         const unsigned int output_max) {


    unsigned int i;
    DLOG(("DIALER: %d input bytes:  ", input_n));
    for (i = 0; i < input_n; i++) {
        DLOG2(("%02x ", input[i]));
    }
    DLOG2((" | \""));
    for (i = 0; i < input_n; i++) {
        DLOG2(("%c", input[i]));
    }
    DLOG2(("\"\n"));

    switch (q_current_dial_entry->method) {

#ifndef Q_NO_SERIAL
    case Q_DIAL_METHOD_MODEM:
        /*
         * Go through the modem states
         */
        modem_data(input, input_n, remaining, output, output_n, output_max);
        break;
#endif

    case Q_DIAL_METHOD_SSH:
    case Q_DIAL_METHOD_RLOGIN:
    case Q_DIAL_METHOD_TELNET:
    case Q_DIAL_METHOD_SOCKET:
        /*
         * Do nothing.  We got here for the 1-2 seconds that we are
         * displaying the CONNECTED message on the phonebook redialer window.
         */
        break;
    case Q_DIAL_METHOD_SHELL:
    case Q_DIAL_METHOD_COMMANDLINE:
        /*
         * BUG - these go straight to CONSOLE
         */
        abort();
        break;
    }

    DLOG(("DIALER: EXITING %d input bytes:  ", *remaining));
    for (i = input_n - *remaining; i < input_n; i++) {
        DLOG2(("%02x ", input[i]));
    }
    DLOG2((" | \""));
    for (i = input_n - *remaining; i < input_n; i++) {
        DLOG2(("%c", input[i]));
    }
    DLOG2(("\"\n"));

    DLOG(("DIALER: %d output bytes: ", *output_n));
    for (i = 0; i < *output_n; i++) {
        DLOG2(("%02x ", output[i]));
    }
    DLOG2((" | \""));
    for (i = 0; i < *output_n; i++) {
        DLOG2(("%c", output[i]));
    }
    DLOG2(("\"\n"));
    DLOG2(("\n"));

}
