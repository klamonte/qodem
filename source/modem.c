/*
 * modem.c
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

#ifndef Q_NO_SERIAL

/*
 * NOTE:  Some of the code in the non-Windows versions of open_serial_port()
 *        and configure_serial_port() was modeled after minicom.  Thank you
 *        to the many people involved in minicom's development.
 */

#include "qcurses.h"
#include "common.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifndef Q_PDCURSES_WIN32
#include <fcntl.h>
#include <pwd.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <libgen.h>
#endif
#include "qodem.h"
#include "screen.h"
#include "forms.h"
#include "console.h"
#include "states.h"
#include "field.h"
#include "help.h"
#include "modem.h"

#define MODEM_CONFIG_FILENAME   "modem.cfg"
#define MODEM_CONFIG_LINE_SIZE  128

/* Set this to a not-NULL value to enable debug log. */
/* static const char * DLOGNAME = "modem"; */
static const char * DLOGNAME = NULL;

#ifdef Q_PDCURSES_WIN32

/**
 * The serial port handle.
 */
HANDLE q_serial_handle = NULL;

#endif

/**
 * Whether we have changed the modem strings in the config screen.
 */
static Q_BOOL saved_changes = Q_TRUE;

/**
 * Which field is highlighted in the editor.
 */
typedef enum {
    M_NONE,
    M_NAME,
    M_DEV_NAME,
    M_LOCK_DIR,
    M_INIT_STRING,
    M_HANGUP_STRING,
    M_DIAL_STRING,
    M_HOST_INIT_STRING,
    M_ANSWER_STRING,
    M_COMM_SETTINGS,
    M_DTE_BAUD,
    M_IGNORE_DCD
} EDITOR_ROW;

/**
 * The global modem configuration settings.
 */
struct q_modem_config_struct q_modem_config = {
    Q_TRUE, Q_FALSE, Q_TRUE, Q_FALSE, NULL, NULL, NULL, NULL, NULL, NULL
};

/**
 * The global serial port settings.
 */
struct q_serial_port_struct q_serial_port;

/**
 * Which row is being edited.  M_NONE means no rows are being edited.
 */
static EDITOR_ROW highlighted_row = M_NONE;

/**
 * Return a string for a Q_BAUD_RATE enum.
 *
 * @param baud Q_BAUD_2400 etc.
 * @return "2400" etc.
 */
char * baud_string(const Q_BAUD_RATE baud) {

    switch (baud) {
    case Q_BAUD_300:
        return "300";

    case Q_BAUD_1200:
        return "1200";

    case Q_BAUD_2400:
        return "2400";

    case Q_BAUD_4800:
        return "4800";

    case Q_BAUD_9600:
        return "9600";

    case Q_BAUD_19200:
        return "19200";

    case Q_BAUD_38400:
        return "38400";

    case Q_BAUD_57600:
        return "57600";

    case Q_BAUD_115200:
        return "115200";

    case Q_BAUD_230400:
        return "230400";

    default:
        /*
         * BUG
         */
        abort();
    }
    return NULL;
}

/**
 * Return a string for a Q_DATA_BITS enum.
 *
 * @param bits Q_DATA_BITS_8 etc.
 * @return "8" etc.
 */
char * data_bits_string(const Q_DATA_BITS bits) {

    switch (bits) {
    case Q_DATA_BITS_8:
        return "8";

    case Q_DATA_BITS_7:
        return "7";

    case Q_DATA_BITS_6:
        return "6";

    case Q_DATA_BITS_5:
        return "5";

    default:
        /*
         * BUG
         */
        abort();
    }
    return NULL;
}

/**
 * Return a string for a Q_PARITY enum.
 *
 * @param parity Q_PARITY_NONE etc.
 * @param short_form if true, return a single capital letter, otherwise
 * return a lowercase string.
 * @return "N" or "none", etc.
 */
char * parity_string(const Q_PARITY parity, const Q_BOOL short_form) {

    switch (parity) {

    case Q_PARITY_NONE:
        return (short_form == Q_TRUE ? "N" : "none");

    case Q_PARITY_EVEN:
        return (short_form == Q_TRUE ? "E" : "even");

    case Q_PARITY_ODD:
        return (short_form == Q_TRUE ? "O" : "odd");

    case Q_PARITY_MARK:
        return (short_form == Q_TRUE ? "M" : "mark");

    case Q_PARITY_SPACE:
        return (short_form == Q_TRUE ? "S" : "space");

    default:
        /*
         * BUG
         */
        abort();
    }
    return NULL;
}

/**
 * Return a string for a Q_STOP_BITS enum.
 *
 * @param bits Q_STOP_BITS_1 etc.
 * @return "1" etc.
 */
char * stop_bits_string(const Q_STOP_BITS bits) {

    switch (bits) {
    case Q_STOP_BITS_1:
        return "1";

    case Q_STOP_BITS_2:
        return "2";

    default:
        /*
         * BUG
         */
        abort();
    }
    return NULL;
}

/**
 * Save the current modem settings to the config file.
 */
static void save_modem_config() {
    char notify_message[DIALOG_MESSAGE_SIZE];
    char * full_filename;
    FILE * file;

    if (q_status.read_only == Q_TRUE) {
        return;
    }

    file = open_datadir_file(MODEM_CONFIG_FILENAME, &full_filename, "w");
    if (file == NULL) {
        snprintf(notify_message, sizeof(notify_message),
                 _("Error opening file \"%s\" for writing: %s"),
                 MODEM_CONFIG_FILENAME, strerror(errno));
        notify_form(notify_message, 0);
        return;
    }

    /*
     * Header
     */
    fprintf(file, "# Qodem modem configuration file\n");
    fprintf(file, "#\n");

    fprintf(file, "name = %ls\n", q_modem_config.name);
    fprintf(file, "dev_name = %s\n", q_modem_config.dev_name);
    fprintf(file, "lock_dir = %s\n", q_modem_config.lock_dir);
    fprintf(file, "init_string = %s\n", q_modem_config.init_string);
    fprintf(file, "hangup_string = %s\n", q_modem_config.hangup_string);
    fprintf(file, "dial_string = %s\n", q_modem_config.dial_string);
    fprintf(file, "host_init_string = %s\n", q_modem_config.host_init_string);
    fprintf(file, "answer_string = %s\n", q_modem_config.answer_string);

    fprintf(file, "baud = %s\n", baud_string(q_modem_config.default_baud));
    fprintf(file, "data_bits = %s\n",
            data_bits_string(q_modem_config.default_data_bits));
    fprintf(file, "parity = %s\n",
            parity_string(q_modem_config.default_parity, Q_FALSE));
    fprintf(file, "stop_bits = %s\n",
            stop_bits_string(q_modem_config.default_stop_bits));
    fprintf(file, "xonxoff = %s\n",
            (q_modem_config.xonxoff == Q_TRUE ? "true" : "false"));
    fprintf(file, "rtscts = %s\n",
            (q_modem_config.rtscts == Q_TRUE ? "true" : "false"));
    fprintf(file, "lock_dte_baud = %s\n",
            (q_modem_config.lock_dte_baud == Q_TRUE ? "true" : "false"));
    fprintf(file, "ignore_dcd = %s\n",
            (q_modem_config.ignore_dcd == Q_TRUE ? "true" : "false"));

    /*
     * Close file.
     */
    Xfree(full_filename, __FILE__, __LINE__);
    fclose(file);

    /*
     * Note that we have no outstanding changes to save
     */
    saved_changes = Q_TRUE;
}

/**
 * Load the modem setting from the config file (modem.cfg).
 */
void load_modem_config() {
    FILE * file;
    char * full_filename;
    char line[MODEM_CONFIG_LINE_SIZE];
    char * key;
    char * value;
    wchar_t value_wchar[MODEM_CONFIG_LINE_SIZE];

    /*
     * No leak
     */
    if (q_modem_config.name != NULL) {
        Xfree(q_modem_config.name, __FILE__, __LINE__);
        q_modem_config.name = NULL;
    }
    if (q_modem_config.dev_name != NULL) {
        Xfree(q_modem_config.dev_name, __FILE__, __LINE__);
        q_modem_config.dev_name = NULL;
    }
    if (q_modem_config.lock_dir != NULL) {
        Xfree(q_modem_config.lock_dir, __FILE__, __LINE__);
        q_modem_config.lock_dir = NULL;
    }
    if (q_modem_config.init_string != NULL) {
        Xfree(q_modem_config.init_string, __FILE__, __LINE__);
        q_modem_config.init_string = NULL;
    }
    if (q_modem_config.hangup_string != NULL) {
        Xfree(q_modem_config.hangup_string, __FILE__, __LINE__);
        q_modem_config.hangup_string = NULL;
    }
    if (q_modem_config.dial_string != NULL) {
        Xfree(q_modem_config.dial_string, __FILE__, __LINE__);
        q_modem_config.dial_string = NULL;
    }

    file = open_datadir_file(MODEM_CONFIG_FILENAME, &full_filename, "r");

    if (file == NULL) {
        /*
         * If the file can't be opened, use the defaults.  Keep this in sync
         * with create_modem_config_file().
         */
        q_modem_config.name = Xwcsdup(MODEM_DEFAULT_NAME, __FILE__, __LINE__);
        q_modem_config.dev_name =
            Xstrdup(MODEM_DEFAULT_DEVICE_NAME, __FILE__, __LINE__);
        q_modem_config.lock_dir =
            Xstrdup(MODEM_DEFAULT_LOCK_DIR, __FILE__, __LINE__);
        q_modem_config.init_string =
            Xstrdup(MODEM_DEFAULT_INIT_STRING, __FILE__, __LINE__);
        q_modem_config.hangup_string =
            Xstrdup(MODEM_DEFAULT_HANGUP_STRING, __FILE__, __LINE__);
        q_modem_config.dial_string =
            Xstrdup(MODEM_DEFAULT_DIAL_STRING, __FILE__, __LINE__);
        q_modem_config.host_init_string =
            Xstrdup(MODEM_DEFAULT_HOST_INIT_STRING, __FILE__, __LINE__);
        q_modem_config.answer_string =
            Xstrdup(MODEM_DEFAULT_ANSWER_STRING, __FILE__, __LINE__);

        q_modem_config.xonxoff = Q_FALSE;
        q_modem_config.rtscts = Q_TRUE;
        q_modem_config.lock_dte_baud = Q_TRUE;
        q_modem_config.ignore_dcd = Q_FALSE;
        q_modem_config.default_baud = Q_BAUD_115200;
        q_modem_config.default_data_bits = Q_DATA_BITS_8;
        q_modem_config.default_parity = Q_PARITY_NONE;
        q_modem_config.default_stop_bits = Q_STOP_BITS_1;

        q_serial_port.xonxoff = q_modem_config.xonxoff;
        q_serial_port.rtscts = q_modem_config.rtscts;
        q_serial_port.baud = q_modem_config.default_baud;
        q_serial_port.data_bits = q_modem_config.default_data_bits;
        q_serial_port.parity = q_modem_config.default_parity;
        q_serial_port.stop_bits = q_modem_config.default_stop_bits;
        q_serial_port.dce_baud = 0;
        q_serial_port.lock_dte_baud = q_modem_config.lock_dte_baud;
        q_serial_port.ignore_dcd = q_modem_config.ignore_dcd;

        /*
         * No leak
         */
        Xfree(full_filename, __FILE__, __LINE__);

        /*
         * Try to save these values
         */
        save_modem_config();

        /*
         * Quietly exit.
         */
        return;
    }

    memset(line, 0, sizeof(line));
    while (!feof(file)) {

        if (fgets(line, sizeof(line), file) == NULL) {
            /*
             * This will cause the outer while's feof() check to fail and
             * smoothly exit the while loop.
             */
            continue;
        }
        line[sizeof(line) - 1] = 0;

        if ((strlen(line) == 0) || (line[0] == '#')) {
            /*
             * Empty or comment line.
             */
            continue;
        }

        /*
         * Nix trailing whitespace.
         */
        while (q_isspace(line[strlen(line) - 1])) {
            line[strlen(line) - 1] = 0;
        }
        key = line;
        while ((strlen(key) > 0) && (q_isspace(*key))) {
            key++;
        }

        value = strchr(key, '=');
        if (value == NULL) {
            /*
             * Invalid line.
             */
            continue;
        }

        *value = 0;
        value++;
        while ((strlen(value) > 0) && (q_isspace(*value))) {
            value++;
        }
        if (*value == 0) {
            /*
             * No data.
             */
            continue;
        }

        mbstowcs(value_wchar, value, strlen(value) + 1);

        if (strncmp(key, "name", strlen("name")) == 0) {
            q_modem_config.name = Xwcsdup(value_wchar, __FILE__, __LINE__);
        } else if (strncmp(key, "dev_name", strlen("dev_name")) == 0) {
            q_modem_config.dev_name = Xstrdup(value, __FILE__, __LINE__);
        } else if (strncmp(key, "lock_dir", strlen("lock_dir")) == 0) {
            q_modem_config.lock_dir = Xstrdup(value, __FILE__, __LINE__);
        } else if (strncmp(key, "init_string", strlen("init_string")) == 0) {
            q_modem_config.init_string = Xstrdup(value, __FILE__, __LINE__);
        } else if (strncmp(key, "hangup_string",
                           strlen("hangup_string")) == 0) {
            q_modem_config.hangup_string = Xstrdup(value, __FILE__, __LINE__);
        } else if (strncmp(key, "dial_string", strlen("dial_string")) == 0) {
            q_modem_config.dial_string = Xstrdup(value, __FILE__, __LINE__);
        } else if (strncmp(key, "host_init_string", strlen("host_init_string"))
                   == 0) {
            q_modem_config.host_init_string =
                Xstrdup(value, __FILE__, __LINE__);
        } else if (strncmp(key, "answer_string",
                           strlen("answer_string")) == 0) {
            q_modem_config.answer_string = Xstrdup(value, __FILE__, __LINE__);
        } else if (strncmp(key, "baud", strlen("baud")) == 0) {
            if (strcmp(value, "300") == 0) {
                q_modem_config.default_baud = Q_BAUD_300;
            } else if (strcmp(value, "1200") == 0) {
                q_modem_config.default_baud = Q_BAUD_1200;
            } else if (strcmp(value, "2400") == 0) {
                q_modem_config.default_baud = Q_BAUD_2400;
            } else if (strcmp(value, "4800") == 0) {
                q_modem_config.default_baud = Q_BAUD_4800;
            } else if (strcmp(value, "9600") == 0) {
                q_modem_config.default_baud = Q_BAUD_9600;
            } else if (strcmp(value, "19200") == 0) {
                q_modem_config.default_baud = Q_BAUD_19200;
            } else if (strcmp(value, "38400") == 0) {
                q_modem_config.default_baud = Q_BAUD_38400;
            } else if (strcmp(value, "57600") == 0) {
                q_modem_config.default_baud = Q_BAUD_57600;
            } else if (strcmp(value, "115200") == 0) {
                q_modem_config.default_baud = Q_BAUD_115200;
            } else if (strcmp(value, "230400") == 0) {
                q_modem_config.default_baud = Q_BAUD_230400;
            }

        } else if (strncmp(key, "data_bits", strlen("data_bits")) == 0) {
            if (strcmp(value, "8") == 0) {
                q_modem_config.default_data_bits = Q_DATA_BITS_8;
            } else if (strcmp(value, "7") == 0) {
                q_modem_config.default_data_bits = Q_DATA_BITS_7;
            } else if (strcmp(value, "6") == 0) {
                q_modem_config.default_data_bits = Q_DATA_BITS_6;
            } else if (strcmp(value, "5") == 0) {
                q_modem_config.default_data_bits = Q_DATA_BITS_5;
            }
        } else if (strncmp(key, "parity", strlen("parity")) == 0) {
            if (strcmp(value, "none") == 0) {
                q_modem_config.default_parity = Q_PARITY_NONE;
            } else if (strcmp(value, "even") == 0) {
                q_modem_config.default_parity = Q_PARITY_EVEN;
            } else if (strcmp(value, "odd") == 0) {
                q_modem_config.default_parity = Q_PARITY_ODD;
            } else if (strcmp(value, "mark") == 0) {
                /*
                 * Mark and space parity are only supported for 7-bit bytes.
                 */
                q_modem_config.default_parity = Q_PARITY_MARK;
                q_modem_config.default_data_bits = Q_DATA_BITS_7;
            } else if (strcmp(value, "space") == 0) {
                /*
                 * Mark and space parity are only supported for 7-bit bytes.
                 */
                q_modem_config.default_parity = Q_PARITY_SPACE;
                q_modem_config.default_data_bits = Q_DATA_BITS_7;
            }
        } else if (strncmp(key, "stop_bits", strlen("stop_bits")) == 0) {
            if (strcmp(value, "1") == 0) {
                q_modem_config.default_stop_bits = Q_STOP_BITS_1;
            } else if (strcmp(value, "2") == 0) {
                q_modem_config.default_stop_bits = Q_STOP_BITS_2;
            }
        } else if (strncmp(key, "xonxoff", strlen("xonxoff")) == 0) {
            if (strcmp(value, "true") == 0) {
                q_modem_config.xonxoff = Q_TRUE;
            } else {
                q_modem_config.xonxoff = Q_FALSE;
            }
        } else if (strncmp(key, "rtscts", strlen("rtscts")) == 0) {
            if (strcmp(value, "true") == 0) {
                q_modem_config.rtscts = Q_TRUE;
            } else {
                q_modem_config.rtscts = Q_FALSE;
            }
        } else if (strncmp(key, "lock_dte_baud",
                           strlen("lock_dte_baud")) == 0) {
            if (strcmp(value, "true") == 0) {
                q_modem_config.lock_dte_baud = Q_TRUE;
            } else {
                q_modem_config.lock_dte_baud = Q_FALSE;
            }
        } else if (strncmp(key, "ignore_dcd",
                           strlen("ignore_dcd")) == 0) {
            if (strcmp(value, "true") == 0) {
                q_modem_config.ignore_dcd = Q_TRUE;
            } else {
                q_modem_config.ignore_dcd = Q_FALSE;
            }
        }
    }

    /*
     * Done reading, close file.
     */
    Xfree(full_filename, __FILE__, __LINE__);
    fclose(file);

    /*
     * Change any NULLs to empty strings.
     */
    if (q_modem_config.name == NULL) {
        q_modem_config.name = Xwcsdup(L"", __FILE__, __LINE__);
    }
    if (q_modem_config.dev_name == NULL) {
        q_modem_config.dev_name = Xstrdup("", __FILE__, __LINE__);
    }
    if (q_modem_config.lock_dir == NULL) {
        q_modem_config.lock_dir = Xstrdup("", __FILE__, __LINE__);
    }
    if (q_modem_config.init_string == NULL) {
        q_modem_config.init_string = Xstrdup("", __FILE__, __LINE__);
    }
    if (q_modem_config.hangup_string == NULL) {
        q_modem_config.hangup_string = Xstrdup("", __FILE__, __LINE__);
    }
    if (q_modem_config.dial_string == NULL) {
        q_modem_config.dial_string = Xstrdup("", __FILE__, __LINE__);
    }
    if (q_modem_config.host_init_string == NULL) {
        q_modem_config.host_init_string = Xstrdup("", __FILE__, __LINE__);
    }
    if (q_modem_config.answer_string == NULL) {
        q_modem_config.answer_string = Xstrdup("", __FILE__, __LINE__);
    }

    q_serial_port.xonxoff       = q_modem_config.xonxoff;
    q_serial_port.rtscts        = q_modem_config.rtscts;
    q_serial_port.baud          = q_modem_config.default_baud;
    q_serial_port.data_bits     = q_modem_config.default_data_bits;
    q_serial_port.parity        = q_modem_config.default_parity;
    q_serial_port.stop_bits     = q_modem_config.default_stop_bits;
    q_serial_port.dce_baud      = 0;
    q_serial_port.lock_dte_baud = q_modem_config.lock_dte_baud;
    q_serial_port.ignore_dcd    = q_modem_config.ignore_dcd;

    /*
     * Note that we have no outstanding changes to save.
     */
    saved_changes = Q_TRUE;
}

/**
 * Create the config file for the modem (modem.cfg).
 */
void create_modem_config_file() {
    FILE * file;
    char buffer[FILENAME_SIZE];
    char * full_filename;

    sprintf(buffer, MODEM_CONFIG_FILENAME);
    file = open_datadir_file(buffer, &full_filename, "a");
    if (file != NULL) {
        fclose(file);
    } else {
        fprintf(stderr, _("Error creating file \"%s\": %s"), full_filename,
                strerror(errno));
    }

    /*
     * Reset to defaults -- keep in sync with load_modem_config()
     */

    q_modem_config.name = Xwcsdup(MODEM_DEFAULT_NAME, __FILE__, __LINE__);
    q_modem_config.dev_name =
        Xstrdup(MODEM_DEFAULT_DEVICE_NAME, __FILE__, __LINE__);
    q_modem_config.lock_dir =
        Xstrdup(MODEM_DEFAULT_LOCK_DIR, __FILE__, __LINE__);
    q_modem_config.init_string =
        Xstrdup(MODEM_DEFAULT_INIT_STRING, __FILE__, __LINE__);
    q_modem_config.hangup_string =
        Xstrdup(MODEM_DEFAULT_HANGUP_STRING, __FILE__, __LINE__);
    q_modem_config.dial_string =
        Xstrdup(MODEM_DEFAULT_DIAL_STRING, __FILE__, __LINE__);
    q_modem_config.host_init_string =
        Xstrdup(MODEM_DEFAULT_HOST_INIT_STRING, __FILE__, __LINE__);
    q_modem_config.answer_string =
        Xstrdup(MODEM_DEFAULT_ANSWER_STRING, __FILE__, __LINE__);
    q_modem_config.xonxoff = Q_FALSE;
    q_modem_config.rtscts = Q_TRUE;
    q_modem_config.lock_dte_baud = Q_TRUE;
    q_modem_config.ignore_dcd = Q_FALSE;
    q_modem_config.default_baud = Q_BAUD_115200;
    q_modem_config.default_data_bits = Q_DATA_BITS_8;
    q_modem_config.default_parity = Q_PARITY_NONE;
    q_modem_config.default_stop_bits = Q_STOP_BITS_1;

    q_serial_port.xonxoff       = q_modem_config.xonxoff;
    q_serial_port.rtscts        = q_modem_config.rtscts;
    q_serial_port.baud          = q_modem_config.default_baud;
    q_serial_port.data_bits     = q_modem_config.default_data_bits;
    q_serial_port.parity        = q_modem_config.default_parity;
    q_serial_port.stop_bits     = q_modem_config.default_stop_bits;
    q_serial_port.dce_baud      = 0;
    q_serial_port.lock_dte_baud = q_modem_config.lock_dte_baud;
    q_serial_port.ignore_dcd    = q_modem_config.ignore_dcd;

    /*
     * No leak
     */
    Xfree(full_filename, __FILE__, __LINE__);

    /*
     * Now save the default values
     */
    save_modem_config();
}

/*
 * Historical note: qodem was playing catch-up to Qmodem(tm) for its first
 * two years of development (2003-2005), implementing the various toggles and
 * flags in the Alt-Z menu.  This modem config screen is the first place
 * where qodem created a really new feature, re-defining Alt-O from "Change
 * Directory" to "Modem Config".
 */
static int window_left;
static int window_top;
static int window_length = 70;
static int window_height = 16;

/**
 * Draw screen for the Alt-O modem settings dialog.
 */
void modem_config_refresh() {
    char * status_string;
    int status_left_stop;
    char * message;
    int message_left;
    int values_column = 24;
    int i;
    char comm_settings_string[MODEM_CONFIG_LINE_SIZE];
    Q_COLOR color;

    if (q_screen_dirty == Q_FALSE) {
        return;
    }

    console_refresh(Q_FALSE);

    screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH,
                              Q_COLOR_STATUS);
    status_string = _(" DIGIT-Select a Configuration Option   F10/Enter-Save   ESC/`-Exit ");

    status_left_stop = WIDTH - strlen(status_string);
    if (status_left_stop <= 0) {
        status_left_stop = 0;
    } else {
        status_left_stop /= 2;
    }
    screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string,
                            Q_COLOR_STATUS);

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
        window_top /= 10;
    }

    screen_draw_box(window_left, window_top, window_left + window_length,
                    window_top + window_height);

    message = _("Modem Configuration");
    message_left = window_length - (strlen(message) + 2);
    if (message_left < 0) {
        message_left = 0;
    } else {
        message_left /= 2;
    }
    screen_put_color_printf_yx(window_top + 0, window_left + message_left,
                               Q_COLOR_WINDOW_BORDER, " %s ", message);
    screen_put_color_str_yx(window_top + window_height - 1,
                            window_left + window_length - 10, _("F1 Help"),
                            Q_COLOR_WINDOW_BORDER);

    /*
     * NAME
     */
    screen_put_color_str_yx(window_top + 2, window_left + 2, _("1. Name"),
                            Q_COLOR_MENU_COMMAND);
    if (highlighted_row == M_NAME) {
        color = Q_COLOR_MENU_COMMAND;
    } else {
        color = Q_COLOR_MENU_TEXT;
    }
    for (i = 0; (i < wcslen(q_modem_config.name)) &&
                (2 + values_column + i < window_length); i++) {
        screen_put_color_char_yx(window_top + 2,
                                 window_left + values_column + i,
                                 q_modem_config.name[i], color);
    }
    if (1 + values_column + i < window_length) {
        screen_put_color_hline_yx(window_top + 2,
                                  window_left + values_column +
                                  wcslen(q_modem_config.name),
                                  cp437_chars[HATCH],
                                  window_length - values_column -
                                  wcslen(q_modem_config.name) - 2, color);
    }

    /*
     * DEV_NAME
     */
    screen_put_color_str_yx(window_top + 3, window_left + 2,
                            _("2. Serial Device"), Q_COLOR_MENU_COMMAND);
    if (highlighted_row == M_DEV_NAME) {
        color = Q_COLOR_MENU_COMMAND;
    } else {
        color = Q_COLOR_MENU_TEXT;
    }
    for (i = 0; (i < strlen(q_modem_config.dev_name)) &&
                (2 + values_column + i < window_length); i++) {
        screen_put_color_char_yx(window_top + 3,
                                 window_left + values_column + i,
                                 q_modem_config.dev_name[i], color);
    }
    if (1 + values_column + i < window_length) {
        screen_put_color_hline_yx(window_top + 3,
                                  window_left + values_column +
                                  strlen(q_modem_config.dev_name),
                                  cp437_chars[HATCH],
                                  window_length - values_column -
                                  strlen(q_modem_config.dev_name) - 2, color);
    }

    /*
     * LOCK_DIR
     */
    screen_put_color_str_yx(window_top + 4, window_left + 2,
                            _("3. Lock Directory"), Q_COLOR_MENU_COMMAND);
    if (highlighted_row == M_LOCK_DIR) {
        color = Q_COLOR_MENU_COMMAND;
    } else {
        color = Q_COLOR_MENU_TEXT;
    }
    for (i = 0; (i < strlen(q_modem_config.lock_dir)) &&
                (2 + values_column + i < window_length); i++) {
        screen_put_color_char_yx(window_top + 4,
                                 window_left + values_column + i,
                                 q_modem_config.lock_dir[i], color);
    }
    if (1 + values_column + i < window_length) {
        screen_put_color_hline_yx(window_top + 4,
                                  window_left + values_column +
                                  strlen(q_modem_config.lock_dir),
                                  cp437_chars[HATCH],
                                  window_length - values_column -
                                  strlen(q_modem_config.lock_dir) - 2, color);
    }

    /*
     * INIT_STRING
     */
    screen_put_color_str_yx(window_top + 5, window_left + 2,
                            _("4. Init String"), Q_COLOR_MENU_COMMAND);
    if (highlighted_row == M_INIT_STRING) {
        color = Q_COLOR_MENU_COMMAND;
    } else {
        color = Q_COLOR_MENU_TEXT;
    }
    for (i = 0; (i < strlen(q_modem_config.init_string)) &&
                (2 + values_column + i < window_length); i++) {
        screen_put_color_char_yx(window_top + 5,
                                 window_left + values_column + i,
                                 q_modem_config.init_string[i], color);
    }
    if (1 + values_column + i < window_length) {
        screen_put_color_hline_yx(window_top + 5,
                                  window_left + values_column +
                                  strlen(q_modem_config.init_string),
                                  cp437_chars[HATCH],
                                  window_length - values_column -
                                  strlen(q_modem_config.init_string) - 2,
                                  color);
    }

    /*
     * HANGUP_STRING
     */
    screen_put_color_str_yx(window_top + 6, window_left + 2,
                            _("5. Hangup String"), Q_COLOR_MENU_COMMAND);
    if (highlighted_row == M_HANGUP_STRING) {
        color = Q_COLOR_MENU_COMMAND;
    } else {
        color = Q_COLOR_MENU_TEXT;
    }
    for (i = 0; (i < strlen(q_modem_config.hangup_string)) &&
                (2 + values_column + i < window_length); i++) {
        screen_put_color_char_yx(window_top + 6,
                                 window_left + values_column + i,
                                 q_modem_config.hangup_string[i], color);
    }
    if (1 + values_column + i < window_length) {
        screen_put_color_hline_yx(window_top + 6,
                                  window_left + values_column +
                                  strlen(q_modem_config.hangup_string),
                                  cp437_chars[HATCH],
                                  window_length - values_column -
                                  strlen(q_modem_config.hangup_string) - 2,
                                  color);
    }

    /*
     * DIAL_STRING
     */
    screen_put_color_str_yx(window_top + 7, window_left + 2,
                            _("6. Dial String"), Q_COLOR_MENU_COMMAND);
    if (highlighted_row == M_DIAL_STRING) {
        color = Q_COLOR_MENU_COMMAND;
    } else {
        color = Q_COLOR_MENU_TEXT;
    }
    for (i = 0; (i < strlen(q_modem_config.dial_string)) &&
                (2 + values_column + i < window_length); i++) {
        screen_put_color_char_yx(window_top + 7,
                                 window_left + values_column + i,
                                 q_modem_config.dial_string[i], color);
    }
    if (1 + values_column + i < window_length) {
        screen_put_color_hline_yx(window_top + 7,
                                  window_left + values_column +
                                  strlen(q_modem_config.dial_string),
                                  cp437_chars[HATCH],
                                  window_length - values_column -
                                  strlen(q_modem_config.dial_string) - 2,
                                  color);
    }

    /*
     * HOST_INIT_STRING
     */
    screen_put_color_str_yx(window_top + 8, window_left + 2,
                            _("7. Host Init String"), Q_COLOR_MENU_COMMAND);
    if (highlighted_row == M_DIAL_STRING) {
        color = Q_COLOR_MENU_COMMAND;
    } else {
        color = Q_COLOR_MENU_TEXT;
    }
    for (i = 0; (i < strlen(q_modem_config.host_init_string)) &&
                (2 + values_column + i < window_length); i++) {
        screen_put_color_char_yx(window_top + 8,
                                 window_left + values_column + i,
                                 q_modem_config.host_init_string[i], color);
    }
    if (1 + values_column + i < window_length) {
        screen_put_color_hline_yx(window_top + 8,
                                  window_left + values_column +
                                  strlen(q_modem_config.host_init_string),
                                  cp437_chars[HATCH],
                                  window_length - values_column -
                                  strlen(q_modem_config.host_init_string) - 2,
                                  color);
    }

    /*
     * ANSWER_STRING
     */
    screen_put_color_str_yx(window_top + 9, window_left + 2,
                            _("8. Answer String"), Q_COLOR_MENU_COMMAND);
    if (highlighted_row == M_DIAL_STRING) {
        color = Q_COLOR_MENU_COMMAND;
    } else {
        color = Q_COLOR_MENU_TEXT;
    }
    for (i = 0; (i < strlen(q_modem_config.answer_string)) &&
                (2 + values_column + i < window_length); i++) {
        screen_put_color_char_yx(window_top + 9,
                                 window_left + values_column + i,
                                 q_modem_config.answer_string[i], color);
    }
    if (1 + values_column + i < window_length) {
        screen_put_color_hline_yx(window_top + 9,
                                  window_left + values_column +
                                  strlen(q_modem_config.answer_string),
                                  cp437_chars[HATCH],
                                  window_length - values_column -
                                  strlen(q_modem_config.answer_string) - 2,
                                  color);
    }

    /*
     * COMM_SETTINGS
     */
    screen_put_color_str_yx(window_top + 10, window_left + 2,
                            _("9. Speed/Parity/Bits"), Q_COLOR_MENU_COMMAND);
    if (highlighted_row == M_COMM_SETTINGS) {
        color = Q_COLOR_MENU_COMMAND;
    } else {
        color = Q_COLOR_MENU_TEXT;
    }
    sprintf(comm_settings_string, "%s %s%s%s%s%s",
            baud_string(q_modem_config.default_baud),
            data_bits_string(q_modem_config.default_data_bits),
            parity_string(q_modem_config.default_parity, Q_TRUE),
            stop_bits_string(q_modem_config.default_stop_bits),
            (q_modem_config.xonxoff == Q_TRUE ? " XON/XOFF" : ""),
            (q_modem_config.rtscts == Q_TRUE ? " RTS/CTS" : ""));

    screen_put_color_str_yx(window_top + 10, window_left + values_column,
                            comm_settings_string, color);
    screen_put_color_hline_yx(window_top + 10,
                              window_left + values_column +
                              strlen(comm_settings_string), cp437_chars[HATCH],
                              window_length - values_column -
                              strlen(comm_settings_string) - 2, color);

    /*
     * DTE_BAUD
     */
    screen_put_color_str_yx(window_top + 11, window_left + 2, _("A. DTE Baud"),
                            Q_COLOR_MENU_COMMAND);
    if (highlighted_row == M_DTE_BAUD) {
        color = Q_COLOR_MENU_COMMAND;
    } else {
        color = Q_COLOR_MENU_TEXT;
    }
    if (q_modem_config.lock_dte_baud) {
        screen_put_color_printf_yx(window_top + 11, window_left + values_column,
                                   color, _("Locked (currently %s)"),
                                   baud_string(q_modem_config.default_baud));
    } else {
        screen_put_color_str_yx(window_top + 11, window_left + values_column,
                                _("Varies with connection speed"), color);
    }

    /*
     * IGNORE_DCD
     */
    screen_put_color_str_yx(window_top + 12, window_left + 2,
        _("B. Ignore DCD"), Q_COLOR_MENU_COMMAND);
    if (highlighted_row == M_IGNORE_DCD) {
        color = Q_COLOR_MENU_COMMAND;
    } else {
        color = Q_COLOR_MENU_TEXT;
    }
    if (q_modem_config.ignore_dcd) {
        screen_put_color_str_yx(window_top + 12, window_left + values_column,
            _("Carrier detect ignored, hangup with string"), color);
    } else {
        screen_put_color_str_yx(window_top + 12, window_left + values_column,
            _("Carrier detect honored, hangup with DTR"), color);
    }

    screen_flush();
    q_screen_dirty = Q_FALSE;
}

/*
 * A form and its fields to handle the editing of a given modem configuration
 * option.
 */
static void * modem_config_entry_window;
static struct fieldset * modem_config_entry_form;
static struct field * modem_config_entry_field;

/**
 * Keyboard handler for the Alt-O modem settings dialog.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
void modem_config_keyboard_handler(const int keystroke, const int flags) {
    int new_keystroke;

    switch (keystroke) {

    case '1':
        if (highlighted_row == M_NONE) {
            highlighted_row = M_NAME;
            break;
        } else {
            fieldset_keystroke(modem_config_entry_form, keystroke);
            return;
        }

    case '2':
        if (highlighted_row == M_NONE) {
            highlighted_row = M_DEV_NAME;
            break;
        } else {
            fieldset_keystroke(modem_config_entry_form, keystroke);
            return;
        }

    case '3':
        if (highlighted_row == M_NONE) {
            highlighted_row = M_LOCK_DIR;
            break;
        } else {
            fieldset_keystroke(modem_config_entry_form, keystroke);
            return;
        }

    case '4':
        if (highlighted_row == M_NONE) {
            highlighted_row = M_INIT_STRING;
            break;
        } else {
            fieldset_keystroke(modem_config_entry_form, keystroke);
            return;
        }

    case '5':
        if (highlighted_row == M_NONE) {
            highlighted_row = M_HANGUP_STRING;
            break;
        } else {
            fieldset_keystroke(modem_config_entry_form, keystroke);
            return;
        }

    case '6':
        if (highlighted_row == M_NONE) {
            highlighted_row = M_DIAL_STRING;
            break;
        } else {
            fieldset_keystroke(modem_config_entry_form, keystroke);
            return;
        }

    case '7':
        if (highlighted_row == M_NONE) {
            highlighted_row = M_HOST_INIT_STRING;
            break;
        } else {
            fieldset_keystroke(modem_config_entry_form, keystroke);
            return;
        }

    case '8':
        if (highlighted_row == M_NONE) {
            highlighted_row = M_ANSWER_STRING;
            break;
        } else {
            fieldset_keystroke(modem_config_entry_form, keystroke);
            return;
        }

    case '9':
        if (highlighted_row == M_NONE) {
            highlighted_row = M_COMM_SETTINGS;

            /*
             * Force repaint
             */
            q_screen_dirty = Q_TRUE;
            modem_config_refresh();

            /*
             * Use the comm_settings_form to get the values
             */
            if (comm_settings_form(_("Default Modem Port Settings"),
                    &q_modem_config.default_baud,
                    &q_modem_config.default_data_bits,
                    &q_modem_config.default_parity,
                    &q_modem_config.default_stop_bits, &q_modem_config.xonxoff,
                    &q_modem_config.rtscts) == Q_TRUE) {
                /*
                 * We edited something
                 */
                saved_changes = Q_FALSE;
            }

            /*
             * comm_settings_form() turns on the cursor.  Turn it off.
             */
            q_cursor_off();

            highlighted_row = M_NONE;

            /*
             * Force repaint
             */
            q_screen_dirty = Q_TRUE;
            console_refresh(Q_FALSE);
            q_screen_dirty = Q_TRUE;
            modem_config_refresh();
        } else {
            fieldset_keystroke(modem_config_entry_form, keystroke);
        }
        return;

    case 'A':
    case 'a':
        if (highlighted_row == M_NONE) {
            /*
             * Swap DTE locked flag
             */
            if (q_modem_config.lock_dte_baud == Q_TRUE) {
                q_modem_config.lock_dte_baud = Q_FALSE;
            } else {
                q_modem_config.lock_dte_baud = Q_TRUE;
            }
            /*
             * Refresh
             */
            q_screen_dirty = Q_TRUE;
        } else {
            fieldset_keystroke(modem_config_entry_form, keystroke);
        }
        return;

    case 'B':
    case 'b':
        if (highlighted_row == M_NONE) {
            /*
             * Swap ignore DCD flag
             */
            if (q_modem_config.ignore_dcd == Q_TRUE) {
                q_modem_config.ignore_dcd = Q_FALSE;
            } else {
                q_modem_config.ignore_dcd = Q_TRUE;
            }
            /*
             * Refresh
             */
            q_screen_dirty = Q_TRUE;
        } else {
            fieldset_keystroke(modem_config_entry_form, keystroke);
        }
        return;

    case Q_KEY_F(1):
        launch_help(Q_HELP_MODEM_CONFIG);

        /*
         * Refresh the whole screen.
         */
        console_refresh(Q_FALSE);
        q_screen_dirty = Q_TRUE;
        return;

    case Q_KEY_RIGHT:
        if (highlighted_row != M_NONE) {
            fieldset_right(modem_config_entry_form);
        }
        return;

    case Q_KEY_LEFT:
        if (highlighted_row != M_NONE) {
            fieldset_left(modem_config_entry_form);
        }
        return;

    case Q_KEY_BACKSPACE:
        if (highlighted_row != M_NONE) {
            fieldset_backspace(modem_config_entry_form);
        }
        return;

    case Q_KEY_IC:
        if (highlighted_row != M_NONE) {
            fieldset_insert_char(modem_config_entry_form);
        }
        return;

    case Q_KEY_HOME:
        if (highlighted_row != M_NONE) {
            fieldset_home_char(modem_config_entry_form);
        }
        return;

    case Q_KEY_END:
        if (highlighted_row != M_NONE) {
            fieldset_end_char(modem_config_entry_form);
        }
        return;

    case Q_KEY_DC:
        if (highlighted_row != M_NONE) {
            fieldset_delete_char(modem_config_entry_form);
        }
        return;

    case Q_KEY_F(10):
        /*
         * Save values.
         */
        if (highlighted_row == M_NONE) {
            save_modem_config();
            load_modem_config();
        } else {
            return;
        }
exit_dialog:
        /*
         * Fall through ...
         */
    case '`':
        /*
         * Backtick works too
         */
    case Q_KEY_ESCAPE:
        if (highlighted_row != M_NONE) {
            /*
             * Done editing
             */
            highlighted_row = M_NONE;

            /*
             * Delete the editing form
             */
            fieldset_free(modem_config_entry_form);
            screen_delwin(modem_config_entry_window);

            q_screen_dirty = Q_TRUE;
            q_cursor_off();
            return;
        }

        /*
         * ESC return to TERMINAL mode
         */
        if (saved_changes == Q_FALSE) {
            /*
             * Ask if the user wants to save changes.
             */
            new_keystroke = notify_prompt_form(_("Attention!"),
                _("Changes have been made!  Save them? [Y/n] "),
                _(" Y-Save Changes   N-Exit "),
                Q_TRUE, 0.0, "YyNn\r");
            new_keystroke = q_tolower(new_keystroke);

            /*
             * Save if the user said so.
             */
            if ((new_keystroke == 'y') || (new_keystroke == Q_KEY_ENTER)) {
                save_modem_config();
                load_modem_config();
            } else {
                /*
                 * Abandon changes.
                 */
                load_modem_config();
            }

        }
        switch_state(Q_STATE_CONSOLE);

        /*
         * The ABORT exit point.
         */
        return;

    case Q_KEY_ENTER:
        if (highlighted_row != M_NONE) {
            /*
             * The OK exit point.
             */

            wchar_t * new_value_wchar =
                field_get_value(modem_config_entry_field);
            char * new_value = field_get_char_value(modem_config_entry_field);

            switch (highlighted_row) {
            case M_NONE:
            case M_COMM_SETTINGS:
                /*
                 * BUG
                 */
                abort();

            case M_NAME:
                Xfree(q_modem_config.name, __FILE__, __LINE__);
                q_modem_config.name =
                    Xwcsdup(new_value_wchar, __FILE__, __LINE__);
                break;

            case M_DEV_NAME:
                Xfree(q_modem_config.dev_name, __FILE__, __LINE__);
                q_modem_config.dev_name =
                    Xstrdup(new_value, __FILE__, __LINE__);
                break;

            case M_LOCK_DIR:
                Xfree(q_modem_config.lock_dir, __FILE__, __LINE__);
                q_modem_config.lock_dir =
                    Xstrdup(new_value, __FILE__, __LINE__);
                break;

            case M_INIT_STRING:
                Xfree(q_modem_config.init_string, __FILE__, __LINE__);
                q_modem_config.init_string =
                    Xstrdup(new_value, __FILE__, __LINE__);
                break;

            case M_HANGUP_STRING:
                Xfree(q_modem_config.hangup_string, __FILE__, __LINE__);
                q_modem_config.hangup_string =
                    Xstrdup(new_value, __FILE__, __LINE__);
                break;

            case M_DIAL_STRING:
                Xfree(q_modem_config.dial_string, __FILE__, __LINE__);
                q_modem_config.dial_string =
                    Xstrdup(new_value, __FILE__, __LINE__);
                break;

            case M_HOST_INIT_STRING:
                Xfree(q_modem_config.host_init_string, __FILE__, __LINE__);
                q_modem_config.host_init_string =
                    Xstrdup(new_value, __FILE__, __LINE__);
                break;

            case M_ANSWER_STRING:
                Xfree(q_modem_config.answer_string, __FILE__, __LINE__);
                q_modem_config.answer_string =
                    Xstrdup(new_value, __FILE__, __LINE__);
                break;

            case M_DTE_BAUD:
            case M_IGNORE_DCD:
                /*
                 * Never get here
                 */
                abort();
                break;
            }

            /*
             * No leak.
             */
            Xfree(new_value, __FILE__, __LINE__);

            /*
             * Edits have been made, now see if the user wants to save them.
             */
            saved_changes = Q_FALSE;

            /*
             * Done editing.
             */
            highlighted_row = M_NONE;

            fieldset_free(modem_config_entry_form);
            screen_delwin(modem_config_entry_window);
            q_cursor_off();
        } else if (highlighted_row == M_NONE) {
            /*
             * Treat like F10 - save values.
             */
            save_modem_config();
            load_modem_config();
            goto exit_dialog;
        }

        /*
         * Refresh
         */
        q_screen_dirty = Q_TRUE;
        return;

    default:
        if (highlighted_row != M_NONE) {
            /*
             * Pass to form handler
             */
            if (!q_key_code_yes(keystroke)) {
                /*
                 * Pass normal keys to form driver
                 */
                fieldset_keystroke(modem_config_entry_form, keystroke);
            }
        }
        /*
         * Ignore keystroke
         */
        return;
    }

    if (highlighted_row == M_NONE) {
        /*
         * All done for most cases
         */
        return;
    }

    /*
     * We get here if we selected an entry to begin editing.
     */

    /*
     * Post the editing form.
     */
    modem_config_entry_window =
        screen_subwin(1, window_length - 4, window_top + window_height - 2,
                      window_left + 2);
    if (check_subwin_result(modem_config_entry_window) == Q_FALSE) {
        /*
         * Couldn't create the edit box window, bail out.
         */
        highlighted_row = M_NONE;
        q_screen_dirty = Q_TRUE;
        q_cursor_off();
        return;
    }

    /*
     * Force repaint
     */
    q_screen_dirty = Q_TRUE;
    modem_config_refresh();

    modem_config_entry_field = field_malloc(window_length - 6, 0, 2, Q_TRUE,
        Q_COLOR_WINDOW_FIELD_TEXT_HIGHLIGHTED,
        Q_COLOR_WINDOW_FIELD_HIGHLIGHTED);

    modem_config_entry_form = fieldset_malloc(&modem_config_entry_field, 1,
                                              modem_config_entry_window);

    screen_put_color_str_yx(window_top + window_height - 2, window_left + 2,
                            "> ", Q_COLOR_MENU_COMMAND);

    switch (highlighted_row) {
    case M_NONE:
    case M_COMM_SETTINGS:
        /*
         * BUG
         */
        abort();

    case M_NAME:
        field_set_value(modem_config_entry_field, q_modem_config.name);
        break;

    case M_DEV_NAME:
        field_set_char_value(modem_config_entry_field, q_modem_config.dev_name);
        break;

    case M_LOCK_DIR:
        field_set_char_value(modem_config_entry_field, q_modem_config.lock_dir);
        break;

    case M_INIT_STRING:
        field_set_char_value(modem_config_entry_field,
                             q_modem_config.init_string);
        break;

    case M_HANGUP_STRING:
        field_set_char_value(modem_config_entry_field,
                             q_modem_config.hangup_string);
        break;

    case M_DIAL_STRING:
        field_set_char_value(modem_config_entry_field,
                             q_modem_config.dial_string);
        break;

    case M_HOST_INIT_STRING:
        field_set_char_value(modem_config_entry_field,
                             q_modem_config.host_init_string);
        break;

    case M_ANSWER_STRING:
        field_set_char_value(modem_config_entry_field,
                             q_modem_config.answer_string);
        break;

    case M_DTE_BAUD:
    case M_IGNORE_DCD:
        /*
         * Never get here
         */
        abort();
        break;
    }

    screen_flush();
    fieldset_render(modem_config_entry_form);
    q_cursor_on();
    return;
}

/**
 * Send a string to the modem.  '~' in the string will result in a 1/2 second
 * pause.  '^' is used to note control characters.
 *
 * @param string the modem string to send
 */
static void send_modem_string(const char * string) {
    char ch;
    int i;
    int n;

    DLOG(("send_modem_string(): '%s'\n", string));

#ifdef Q_PDCURSES_WIN32
    assert(q_serial_handle != NULL);
#else
    assert(q_child_tty_fd != -1);
#endif

    n = strlen(string);
    for (i = 0; i < n; i++) {
        ch = string[i];

        DLOG(("send_modem_string(): --> '%c'\n", ch));

        if (ch == '~') {
            /*
             * Pause 1/2 second
             */
#ifdef Q_PDCURSES_WIN32
            Sleep(500);
#else
            usleep(500000);
#endif
        } else if (ch == '^') {
            /*
             * Control char
             */
            i++;
            if (i < n) {
                ch = string[i];
                /*
                 * Bring into control char range
                 */
                ch = ch - 0x40;
                qodem_write(q_child_tty_fd, &ch, 1, Q_TRUE);
            }
        } else {
            /*
             * Regular character
             */
            qodem_write(q_child_tty_fd, &ch, 1, Q_TRUE);
        }
    }
}

#ifdef Q_PDCURSES_WIN32

/**
 * Try to hang up the modem, first by dropping DTR and then if that doesn't
 * work by sending the hangup string.
 */
void hangup_modem() {
    DWORD pins = 0;
    BOOL rc;
    Q_BOOL do_hangup_string = Q_TRUE;

    assert(q_serial_handle != NULL);
    assert(Q_SERIAL_OPEN);

    if (q_serial_port.ignore_dcd == Q_FALSE) {

        /*
         * First, drop DTR.  Most modems will hangup with this.
         */
        rc = EscapeCommFunction(q_serial_handle, CLRDTR);
        if (rc == FALSE) {
            /*
             * Uh-oh
             */
            goto hangup_modem_last_chance;
        }
        Sleep(1000);

        /*
         * See if CD is still there.
         */
        rc = GetCommModemStatus(q_serial_handle, &pins);
        if (rc == FALSE) {
            /*
             * Uh-oh
             */
            goto hangup_modem_restore_dtr;
        }
        if ((pins & MS_RLSD_ON) == 0) {
            /*
             * DCD went down, we're done.
             */
            do_hangup_string = Q_FALSE;
        }

hangup_modem_restore_dtr:
        rc = EscapeCommFunction(q_serial_handle, SETDTR);
        if (rc == FALSE) {
            /*
             * Uh-oh
             */
            goto hangup_modem_last_chance;
        }

    } /* if (q_serial_port.ignore_dcd == Q_FALSE) */

hangup_modem_last_chance:
    /*
     * Finally, if we're still online send the remote string.
     */
    if (do_hangup_string == Q_TRUE) {
        send_modem_string(q_modem_config.hangup_string);
    }

    /*
     * Update global status: we are offline.
     */
    q_status.online = Q_FALSE;
}

/**
 * Open the serial port.
 *
 * @return true if the port was successfully opened
 */
Q_BOOL open_serial_port() {
    char notify_message[DIALOG_MESSAGE_SIZE];

    assert(q_serial_handle == NULL);

    /*
     * We will be opening "COMx", not a generic i18n filename, so use the
     * ANSI function rather than the wide-char version.
     */
    q_serial_handle = CreateFileA(q_modem_config.dev_name,
        GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, NULL);

    if (q_serial_handle == INVALID_HANDLE_VALUE) {
        /*
         * An error occurred opening the port.  Tell the user and bail out.
         */
        snprintf(notify_message, sizeof(notify_message),
                 _("Error opening \"%s\": %s"), q_modem_config.dev_name,
                 strerror(GetLastError()));
        notify_form(notify_message, 0);
        q_serial_handle = NULL;
        return Q_FALSE;
    }

    /*
     * Note that we've got the port open now.
     */
    q_status.serial_open = Q_TRUE;

    /*
     * Wait 150 milliseconds for the modem to see DTR.
     */
    Sleep(150);

    /*
     * Now set it up the way we want to.
     */
    return configure_serial_port();
}

/**
 * Configure the serial port with the values in q_serial_port.
 *
 * @return true if the port was successfully re-configured
 */
Q_BOOL configure_serial_port() {
    char notify_message[DIALOG_MESSAGE_SIZE];
    static Q_BOOL first = Q_TRUE;
    DWORD new_speed = CBR_9600;
    int new_dce_speed = 9600;
    COMMTIMEOUTS comm_timeouts;

    if (first == Q_TRUE) {
        /*
         * First time to open, get the original DCB
         */
        if (GetCommState(q_serial_handle,
                         &q_serial_port.original_comm_state) == 0) {
            snprintf(notify_message, sizeof(notify_message),
                     _("Error reading terminal parameters from \"%s\": %s"),
                     q_modem_config.dev_name, strerror(GetLastError()));
            notify_form(notify_message, 0);
            CloseHandle(q_serial_handle);
            q_serial_handle = NULL;
            q_status.serial_open = Q_FALSE;
            return Q_FALSE;
        }
        memcpy(&q_serial_port.qodem_comm_state,
               &q_serial_port.original_comm_state, sizeof(DCB));
    }

    /*
     * Setup with our own parameters
     */

    /* Default to no hardware handshaking */
    q_serial_port.qodem_comm_state.fOutxCtsFlow = FALSE;
    q_serial_port.qodem_comm_state.fRtsControl = RTS_CONTROL_DISABLE;
    if (q_serial_port.rtscts == Q_TRUE) {
        /*
         * Verify first that we have DSR up
         */
        if ((query_serial_port() == Q_TRUE) &&
            (q_serial_port.rs232.DSR == Q_TRUE)) {
            /*
             * Looks good so far, enable hardware handshaking
             */
            q_serial_port.qodem_comm_state.fOutxCtsFlow = TRUE;
            q_serial_port.qodem_comm_state.fRtsControl = RTS_CONTROL_HANDSHAKE;
        }
    }
    if (q_serial_port.xonxoff == Q_TRUE) {
        q_serial_port.qodem_comm_state.fInX = TRUE;
        q_serial_port.qodem_comm_state.fOutX = TRUE;
    } else {
        q_serial_port.qodem_comm_state.fInX  = FALSE;
        q_serial_port.qodem_comm_state.fOutX = FALSE;
    }

    /*
     * Set speed
     */
    switch (q_serial_port.baud) {
    case Q_BAUD_300:
        new_speed = CBR_300;
        new_dce_speed = 300;
        break;
    case Q_BAUD_1200:
        new_speed = CBR_1200;
        new_dce_speed = 1200;
        break;
    case Q_BAUD_2400:
        new_speed = CBR_2400;
        new_dce_speed = 2400;
        break;
    case Q_BAUD_4800:
        new_speed = CBR_4800;
        new_dce_speed = 4800;
        break;
    case Q_BAUD_9600:
        new_speed = CBR_9600;
        new_dce_speed = 9600;
        break;
    case Q_BAUD_19200:
        new_speed = CBR_19200;
        new_dce_speed = 19200;
        break;
    case Q_BAUD_38400:
        new_speed = CBR_38400;
        new_dce_speed = 38400;
        break;
    case Q_BAUD_57600:
        new_speed = CBR_57600;
        new_dce_speed = 57600;
        break;
    case Q_BAUD_115200:
        new_speed = CBR_115200;
        new_dce_speed = 115200;
        break;

    case Q_BAUD_230400:
        /*
         * 230400 doesn't have a constant.  However allegedly the device
         * manufacturers in general are taking a raw int value now.  Maybe it
         * will work, who knows?
         */
        new_speed = 230400;
        new_dce_speed = 230400;
        break;

    }
    q_serial_port.qodem_comm_state.BaudRate = new_speed;

    /*
     * Check bits
     */
    switch (q_serial_port.data_bits) {
    case Q_DATA_BITS_8:
        q_serial_port.qodem_comm_state.ByteSize = 8;
        break;

    case Q_DATA_BITS_7:
        q_serial_port.qodem_comm_state.ByteSize = 7;
        break;

    case Q_DATA_BITS_6:
        q_serial_port.qodem_comm_state.ByteSize = 6;
        break;

    case Q_DATA_BITS_5:
        q_serial_port.qodem_comm_state.ByteSize = 5;
        break;
    }

    switch (q_serial_port.stop_bits) {
    case Q_STOP_BITS_1:
        q_serial_port.qodem_comm_state.StopBits = ONESTOPBIT;
        break;

    case Q_STOP_BITS_2:
        q_serial_port.qodem_comm_state.StopBits = TWOSTOPBITS;
        break;
    }

    switch (q_serial_port.parity) {
    case Q_PARITY_NONE:
        q_serial_port.qodem_comm_state.Parity = NOPARITY;
        break;
    case Q_PARITY_EVEN:
        q_serial_port.qodem_comm_state.Parity = EVENPARITY;
        break;
    case Q_PARITY_ODD:
        q_serial_port.qodem_comm_state.Parity = ODDPARITY;
        break;
    case Q_PARITY_MARK:
        q_serial_port.qodem_comm_state.Parity = MARKPARITY;
        break;
    case Q_PARITY_SPACE:
        q_serial_port.qodem_comm_state.Parity = SPACEPARITY;
        break;
    }

    if (SetCommState(q_serial_handle, &q_serial_port.qodem_comm_state) == 0) {
        /*
         * Uh-oh
         */
        snprintf(notify_message, sizeof(notify_message),
                 _("Error setting terminal parameters for \"%s\": %s"),
                 q_modem_config.dev_name, strerror(GetLastError()));
        notify_form(notify_message, 0);
        close_serial_port();
        return Q_FALSE;
    }

    /*
     * Set timeouts.
     *
     * TODO: verify that these are reasonable to achieve decent performance
     * at 56k.
     */
    comm_timeouts.ReadIntervalTimeout = 3;
    comm_timeouts.ReadTotalTimeoutMultiplier = 3;
    comm_timeouts.ReadTotalTimeoutConstant = 2;
    comm_timeouts.WriteTotalTimeoutMultiplier = 3;
    comm_timeouts.WriteTotalTimeoutConstant = 2;
    SetCommTimeouts(q_serial_handle, &comm_timeouts);

    /*
     * Set new DCE speed
     */
    q_serial_port.dce_baud = new_dce_speed;

    if (first == Q_TRUE) {
        /*
         * Initialize modem
         */
        send_modem_string(q_modem_config.init_string);
        first = Q_FALSE;
    }

    /*
     * All OK
     */
    return Q_TRUE;
}

/**
 * Close the serial port.
 *
 * @return true if the serial port was able to be re-configured with its
 * original terminal settings before it was closed
 */
Q_BOOL close_serial_port() {
    char notify_message[DIALOG_MESSAGE_SIZE];
    Q_BOOL rc = Q_TRUE;

    assert(q_serial_handle != NULL);

    /*
     * Put the original DCB back
     */
    if (SetCommState(q_serial_handle,
                     &q_serial_port.original_comm_state) == 0) {
        /*
         * Uh-oh
         */
        snprintf(notify_message, sizeof(notify_message),
            _("Error restoring original terminal parameters for \"%s\": %s"),
            q_modem_config.dev_name, strerror(GetLastError()));
        notify_form(notify_message, 0);
        rc = Q_FALSE;
    }

    CloseHandle(q_serial_handle);
    q_serial_handle = NULL;
    q_status.serial_open = Q_FALSE;
    q_status.online = Q_FALSE;
    return rc;
}

/**
 * Query the serial port and set the values of q_serial_port.rs232.
 *
 * @return true if the RS-232 state was able to be read
 */
Q_BOOL query_serial_port() {
    BOOL rc;
    DWORD pins;

    assert(q_serial_handle != NULL);
    assert(Q_SERIAL_OPEN);

    /*
     * Clear the existing pins
     */
    q_serial_port.rs232.LE = Q_FALSE;
    q_serial_port.rs232.DTR = Q_FALSE;
    q_serial_port.rs232.RTS = Q_FALSE;
    q_serial_port.rs232.ST = Q_FALSE;
    q_serial_port.rs232.SR = Q_FALSE;
    q_serial_port.rs232.CTS = Q_FALSE;
    q_serial_port.rs232.DCD = Q_FALSE;
    q_serial_port.rs232.RI = Q_FALSE;
    q_serial_port.rs232.DSR = Q_FALSE;

    rc = GetCommModemStatus(q_serial_handle, &pins);
    if (rc == FALSE) {
        /*
         * Uh-oh
         */
        return Q_FALSE;
    }

    /*
     * Unsupported in Windows Comm API:
     *     LE
     *     DTR
     *     RTS
     *     ST
     *     SR
     */

    if ((pins & MS_CTS_ON) == 0) {
        q_serial_port.rs232.CTS = Q_TRUE;
    }
    if ((pins & MS_RLSD_ON) == 0) {
        q_serial_port.rs232.DCD = Q_TRUE;
    }
    if ((pins & MS_RING_ON) == 0) {
        q_serial_port.rs232.RI = Q_TRUE;
    }
    if ((pins & MS_DSR_ON) == 0) {
        q_serial_port.rs232.DSR = Q_TRUE;
    }



    return Q_TRUE;
}

/**
 * Send a BREAK to the serial port.
 */
void send_break() {
    assert(q_serial_handle != NULL);
    assert(Q_SERIAL_OPEN);

    SetCommBreak(q_serial_handle);
    Sleep(300);
    ClearCommBreak(q_serial_handle);
}

#else

/**
 * Try to hang up the modem, first by dropping DTR and then if that doesn't
 * work by sending the hangup string.
 */
void hangup_modem() {
    int pins;
    int rc = 0;
    Q_BOOL do_hangup_string = Q_TRUE;

    assert(q_child_tty_fd != -1);
    assert(Q_SERIAL_OPEN);

    if (q_serial_port.ignore_dcd == Q_FALSE) {

        /*
         * First, drop DTR.  Most modems will hangup with this.
         */
        rc = ioctl(q_child_tty_fd, TIOCMGET, &pins);
        if (rc < 0) {
            /*
             * Uh-oh
             */
            goto hangup_modem_last_chance;
        }
        if ((pins & TIOCM_DTR) != 0) {
            /*
             * If DTR is set, drop it, sleep 1 second, and bring it back up.
             */
            pins = pins & ~TIOCM_DTR;
            rc = ioctl(q_child_tty_fd, TIOCMSET, &pins);
            if (rc < 0) {
                /*
                 * Uh-oh
                 */
                goto hangup_modem_last_chance;
            }
            sleep(1);

            /*
             * See if CD is still there.
             */
            rc = ioctl(q_child_tty_fd, TIOCMGET, &pins);
            if (rc < 0) {
                /*
                 * Uh-oh
                 */
                goto hangup_modem_restore_dtr;
            }
            if ((pins & TIOCM_CAR) == 0) {
                /*
                 * DCD went down, we're done.
                 */
                do_hangup_string = Q_FALSE;
            }

hangup_modem_restore_dtr:
            pins = pins | TIOCM_DTR;
            rc = ioctl(q_child_tty_fd, TIOCMSET, &pins);
            if (rc < 0) {
                /*
                 * Uh-oh
                 */
                goto hangup_modem_last_chance;
            }
        }

    } /* if (q_serial_port.ignore_dcd == Q_FALSE) */

hangup_modem_last_chance:
    /*
     * Finally, if we're still online send the remote string.
     */
    if (do_hangup_string == Q_TRUE) {
        send_modem_string(q_modem_config.hangup_string);
    }

    /*
     * Update global status: we are offline.
     */
    q_status.online = Q_FALSE;
}

/*
 * We follow a locking strategy similar to minicom's.
 */
static char lock_filename[FILENAME_SIZE];

/**
 * Open the serial port.
 *
 * @return true if the port was successfully opened
 */
Q_BOOL open_serial_port() {
    char notify_message[DIALOG_MESSAGE_SIZE];
    char * basename_arg = NULL;
    char * base_dev_name = NULL;
    struct stat fstats;
    int keystroke;
    pid_t other_pid = -1;
    char lockfile_data[64];
    pid_t * lockfile_data_pid;
    int lockfile_fd;
    int rc;
    struct passwd *pw = NULL;

    /*
     * Make sure I was asked to open at the right time.
     */
    assert(q_child_tty_fd == -1);

    /*
     * Lock the port.  This approach came from minicom's main.c.
     */
    basename_arg = Xstrdup(q_modem_config.dev_name, __FILE__, __LINE__);
    base_dev_name = Xstrdup(basename(basename_arg), __FILE__, __LINE__);
    Xfree(basename_arg, __FILE__, __LINE__);
    snprintf(lock_filename, sizeof(lock_filename), "%s/LCK..%s",
             q_modem_config.lock_dir, base_dev_name);
    Xfree(base_dev_name, __FILE__, __LINE__);
    /*
     * See if the directory exists.
     */
    if (stat(q_modem_config.lock_dir, &fstats) < 0) {
        /*
         * Error
         */
        snprintf(notify_message, sizeof(notify_message),
            _("Error stat()'ing lock directory \"%s\": %s.  Proceed anyway? [Y/n] "),
            q_modem_config.lock_dir, strerror(errno));
        keystroke = q_tolower(notify_prompt_form(
            _("Attention!"), notify_message,
            _(" Y-Proceed Without a Lock File   N-Do Not Open Serial Port "),
            Q_TRUE, 0.0, "YyNn\r"));
        if ((keystroke == 'y') || (keystroke == Q_KEY_ENTER)) {
            memset(lock_filename, 0, sizeof(lock_filename));
        } else {
            return Q_FALSE;
        }

    } else {

        /*
         * See if the lockfile is already there.
         */
        if (stat(lock_filename, &fstats) == 0) {
            /*
             * It is.  Who owns it?
             */
            lockfile_fd = open(lock_filename, O_RDONLY);
            if (lockfile_fd >= 0) {
                /*
                 * See if the other process is still running.
                 */
                rc = read(lockfile_fd, lockfile_data, sizeof(lockfile_data));
                if (rc == 4) {
                    /*
                     * Kermit-style lockfile
                     */
                    lockfile_data_pid = (pid_t *) lockfile_data;
                    other_pid = *lockfile_data_pid;
                } else if (rc > 0) {
                    /*
                     * ASCII lockfile
                     */
                    lockfile_data[rc] = 0;
                    other_pid = atoi(lockfile_data);
                } else {
                    /*
                     * Locked
                     */
                    snprintf(notify_message, sizeof(notify_message),
                             _("\"%s\" is locked."), q_modem_config.dev_name);
                    notify_form(notify_message, 0);
                    return Q_FALSE;
                }

                close(lockfile_fd);
                if (other_pid > 0) {
                    rc = kill(other_pid, 0);
                    if ((rc < 0) && (errno == ESRCH)) {
                        /*
                         * Lockfile is stale
                         */
                        unlink(lock_filename);
                    } else {
                        /*
                         * We have to assume it's still locked
                         */
                        snprintf(notify_message, sizeof(notify_message),
                                 _("\"%s\" is locked by process %d."),
                                 q_modem_config.dev_name, other_pid);
                        notify_form(notify_message, 0);
                        return Q_FALSE;
                    }
                }
            }
        }
        /*
         * Get real username
         */
        pw = getpwuid(getuid());
        if (pw == NULL) {
            snprintf(notify_message, sizeof(notify_message),
                _("The system does not know who you are.  Are you the One? (%s)"),
                strerror(errno));
            notify_form(notify_message, 0);
            return Q_FALSE;
        }
        /*
         * Create the lock
         */
        lockfile_fd = open(lock_filename, O_WRONLY | O_CREAT | O_EXCL,
                           S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (lockfile_fd < 0) {
            snprintf(notify_message, sizeof(notify_message),
                _("Error creating lockfile \"%s\": %s.  Proceed anyway? [Y/n] "),
                q_modem_config.lock_dir, strerror(errno));
            keystroke = q_tolower(notify_prompt_form(
                _("Attention!"), notify_message,
                _(" Y-Proceed Without a Lock File   N-Do Not Open Serial Port "),
                Q_TRUE, 0.0, "YyNn\r"));
            if ((keystroke == 'y') || (keystroke == Q_KEY_ENTER)) {
                memset(lock_filename, 0, sizeof(lock_filename));
            } else {
                return Q_FALSE;
            }
        }
        sprintf(lockfile_data, "%10ld qodem %.20s\n", (long) (getpid()),
                pw->pw_name);
        write(lockfile_fd, lockfile_data, strlen(lockfile_data));
        close(lockfile_fd);
    }

    /*
     * Open port
     */
    q_child_tty_fd = open(q_modem_config.dev_name,
                          O_RDWR | O_NOCTTY | O_SYNC | O_NONBLOCK);
    if (q_child_tty_fd < 0) {
        /*
         * Error, bail out.
         */
        snprintf(notify_message, sizeof(notify_message),
                 _("Error opening \"%s\": %s"), q_modem_config.dev_name,
                 strerror(errno));
        notify_form(notify_message, 0);
        if (strlen(lock_filename) > 0) {
            unlink(lock_filename);
        }
        return Q_FALSE;
    }

    /*
     * Note that we've got the port open now.
     */
    q_status.serial_open = Q_TRUE;

    /*
     * Wait 150 milliseconds for the modem to see DTR.
     */
    usleep(150000);

    /*
     * Now set it up the way we want to.
     */
    return configure_serial_port();
}

/**
 * Configure the serial port with the values in q_serial_port.
 *
 * @return true if the port was successfully re-configured
 */
Q_BOOL configure_serial_port() {
    char notify_message[DIALOG_MESSAGE_SIZE];
    static Q_BOOL first = Q_TRUE;
    speed_t new_speed = B9600;
    int new_dce_speed = 9600;

    if (first == Q_TRUE) {
        /*
         * First time to open, get the original termios
         */
        if (tcgetattr(q_child_tty_fd, &q_serial_port.original_termios) < 0) {
            snprintf(notify_message, sizeof(notify_message),
                     _("Error reading terminal parameters from \"%s\": %s"),
                     q_modem_config.dev_name, strerror(errno));
            notify_form(notify_message, 0);
            close(q_child_tty_fd);
            q_child_tty_fd = -1;
            q_status.serial_open = Q_FALSE;
            if (strlen(lock_filename) > 0) {
                unlink(lock_filename);
            }
            return Q_FALSE;
        }
        memcpy(&q_serial_port.qodem_termios, &q_serial_port.original_termios,
               sizeof(struct termios));
    }

    /*
     * Setup with our own parameters
     */
    cfmakeraw(&q_serial_port.qodem_termios);

    /*
     * These parameters taken from minicom.  Seems to work for them.
     */
    q_serial_port.qodem_termios.c_iflag = IGNBRK;
    q_serial_port.qodem_termios.c_lflag = 0;
    q_serial_port.qodem_termios.c_oflag = 0;
    /*
     * c_cflag is special -- it CANNOT be reset to 0 (at least on Linux).
     */
    q_serial_port.qodem_termios.c_cflag |= CLOCAL | CREAD;
    q_serial_port.qodem_termios.c_cflag &= ~CRTSCTS;
    if (q_serial_port.rtscts == Q_TRUE) {
        /*
         * Verify first that we have DSR up
         */
        if ((query_serial_port() == Q_TRUE) &&
            (q_serial_port.rs232.DSR == Q_TRUE)) {
            /*
             * Looks good so far
             */
            q_serial_port.qodem_termios.c_cflag |= CRTSCTS;
        }
    }
    if (q_serial_port.xonxoff == Q_TRUE) {
        q_serial_port.qodem_termios.c_iflag |= IXON | IXOFF;
        q_serial_port.qodem_termios.c_oflag |= IXON | IXOFF;
    } else {
        q_serial_port.qodem_termios.c_iflag &= ~(IXON | IXOFF);
        q_serial_port.qodem_termios.c_oflag &= ~(IXON | IXOFF);
    }
    q_serial_port.qodem_termios.c_cc[VMIN] = 1;
    q_serial_port.qodem_termios.c_cc[VTIME] = 5;

    /*
     * Set speed
     */
#ifdef __linux
    q_serial_port.qodem_termios.c_cflag &= ~(CBAUDEX | CBAUD);
#endif

    switch (q_serial_port.baud) {
    case Q_BAUD_300:
        new_speed = B300;
        new_dce_speed = 300;
        break;
    case Q_BAUD_1200:
        new_speed = B1200;
        new_dce_speed = 1200;
        break;
    case Q_BAUD_2400:
        new_speed = B2400;
        new_dce_speed = 2400;
        break;
    case Q_BAUD_4800:
        new_speed = B4800;
        new_dce_speed = 4800;
        break;
    case Q_BAUD_9600:
        new_speed = B9600;
        new_dce_speed = 9600;
        break;
    case Q_BAUD_19200:
        new_speed = B19200;
        new_dce_speed = 19200;
        break;
    case Q_BAUD_38400:
        new_speed = B38400;
        new_dce_speed = 38400;
        break;
    case Q_BAUD_57600:
        new_speed = B57600;
        new_dce_speed = 57600;
        break;
    case Q_BAUD_115200:
#ifdef __linux
        q_serial_port.qodem_termios.c_cflag |= CBAUD;
#endif
        new_speed = B115200;
        new_dce_speed = 115200;
        break;

    case Q_BAUD_230400:
#ifdef __linux
        q_serial_port.qodem_termios.c_cflag |= CBAUDEX;
#endif
        new_speed = B230400;
        new_dce_speed = 230400;
        break;

    }

    if (cfsetispeed(&q_serial_port.qodem_termios, new_speed) < 0) {
        /*
         * Uh-oh
         */
        snprintf(notify_message, sizeof(notify_message),
                 _("Error setting terminal parameters for \"%s\": %s"),
                 q_modem_config.dev_name, strerror(errno));
        notify_form(notify_message, 0);
        close_serial_port();
        return Q_FALSE;
    }

    if (cfsetospeed(&q_serial_port.qodem_termios, new_speed) < 0) {
        /*
         * Uh-oh
         */
        snprintf(notify_message, sizeof(notify_message),
                 _("Error setting terminal parameters for \"%s\": %s"),
                 q_modem_config.dev_name, strerror(errno));
        notify_form(notify_message, 0);
        close_serial_port();
        return Q_FALSE;
    }

    /*
     * Check bits
     */
    switch (q_serial_port.data_bits) {
    case Q_DATA_BITS_8:
        /*
         * NOP
         */
        break;

    case Q_DATA_BITS_7:
        if ((q_serial_port.parity != Q_PARITY_MARK) &&
            (q_serial_port.parity != Q_PARITY_SPACE)) {
            /*
             * MARK and SPACE parity actually use 8 bits, but we expose it as
             * seven bits to the user.
             */
            q_serial_port.qodem_termios.c_cflag &= ~CSIZE;
            q_serial_port.qodem_termios.c_cflag |= CS7;
        }
        break;

    case Q_DATA_BITS_6:
        q_serial_port.qodem_termios.c_cflag &= ~CSIZE;
        q_serial_port.qodem_termios.c_cflag |= CS6;
        break;

    case Q_DATA_BITS_5:
        q_serial_port.qodem_termios.c_cflag &= ~CSIZE;
        q_serial_port.qodem_termios.c_cflag |= CS5;
        break;
    }

    switch (q_serial_port.stop_bits) {
    case Q_STOP_BITS_1:
        q_serial_port.qodem_termios.c_cflag &= ~CSTOPB;
        break;

    case Q_STOP_BITS_2:
        q_serial_port.qodem_termios.c_cflag |= CSTOPB;
        break;
    }

    switch (q_serial_port.parity) {
    case Q_PARITY_NONE:
        /*
         * NOP
         */
        break;

    case Q_PARITY_EVEN:
        q_serial_port.qodem_termios.c_cflag |= PARENB;
        break;

    case Q_PARITY_ODD:
        q_serial_port.qodem_termios.c_cflag |= PARENB | PARODD;
        break;

    case Q_PARITY_MARK:
    case Q_PARITY_SPACE:
        /*
         * We perform MARK and SPACE parity in process_incoming_data() and
         * qodem_write().
         */
        break;
    }

    if (tcsetattr(q_child_tty_fd, TCSANOW, &q_serial_port.qodem_termios) < 0) {
        /*
         * Uh-oh
         */
        snprintf(notify_message, sizeof(notify_message),
                 _("Error setting terminal parameters for \"%s\": %s"),
                 q_modem_config.dev_name, strerror(errno));
        notify_form(notify_message, 0);
        close_serial_port();
        return Q_FALSE;
    }

    /*
     * Set new DCE speed
     */
    q_serial_port.dce_baud = new_dce_speed;

    if (first == Q_TRUE) {
        /*
         * Initialize modem
         */
        send_modem_string(q_modem_config.init_string);
        first = Q_FALSE;
    }

    /*
     * All OK
     */
    return Q_TRUE;
}

/**
 * Close the serial port.
 *
 * @return true if the serial port was able to be re-configured with its
 * original terminal settings before it was closed
 */
Q_BOOL close_serial_port() {
    char notify_message[DIALOG_MESSAGE_SIZE];
    Q_BOOL rc = Q_TRUE;

    assert(q_child_tty_fd != -1);

    /*
     * Put the original termios back
     */
    if (tcsetattr(q_child_tty_fd, TCSANOW,
                  &q_serial_port.original_termios) < 0) {
        /*
         * Uh-oh
         */
        snprintf(notify_message, sizeof(notify_message),
            _("Error restoring original terminal parameters for \"%s\": %s"),
            q_modem_config.dev_name, strerror(errno));
        notify_form(notify_message, 0);
        rc = Q_FALSE;
    }

    /*
     * Close port
     */
    close(q_child_tty_fd);
    q_child_tty_fd = -1;

    /*
     * Release lockfile
     */
    if (strlen(lock_filename) > 0) {
        unlink(lock_filename);
    }

    /*
     * Set status
     */
    q_status.serial_open = Q_FALSE;
    q_status.online = Q_FALSE;

    return rc;
}

/**
 * Query the serial port and set the values of q_serial_port.rs232.
 *
 * @return true if the RS-232 state was able to be read
 */
Q_BOOL query_serial_port() {
    char notify_message[DIALOG_MESSAGE_SIZE];
    int pins;
    int rc = 0;

    assert(q_child_tty_fd != -1);

    rc = ioctl(q_child_tty_fd, TIOCMGET, &pins);
    if (rc < 0) {
        /*
         * Uh-oh
         */
        snprintf(notify_message, sizeof(notify_message),
                 _("Error retrieving RS232 line state from \"%s\": %s"),
                 q_modem_config.dev_name, strerror(errno));
        notify_form(notify_message, 0);
        return Q_FALSE;
    }

    /*
     * Clear the existing pins
     */
    q_serial_port.rs232.LE = Q_FALSE;
    q_serial_port.rs232.DTR = Q_FALSE;
    q_serial_port.rs232.RTS = Q_FALSE;
    q_serial_port.rs232.ST = Q_FALSE;
    q_serial_port.rs232.SR = Q_FALSE;
    q_serial_port.rs232.CTS = Q_FALSE;
    q_serial_port.rs232.DCD = Q_FALSE;
    q_serial_port.rs232.RI = Q_FALSE;
    q_serial_port.rs232.DSR = Q_FALSE;

#ifdef TIOCM_LE
    if (pins & TIOCM_LE) {
        q_serial_port.rs232.LE = Q_TRUE;
    }
#endif
    if (pins & TIOCM_DTR) {
        q_serial_port.rs232.DTR = Q_TRUE;
    }
    if (pins & TIOCM_RTS) {
        q_serial_port.rs232.RTS = Q_TRUE;
    }
#ifdef TIOCM_ST
    if (pins & TIOCM_ST) {
        q_serial_port.rs232.ST = Q_TRUE;
    }
#endif
#ifdef TIOCM_SR
    if (pins & TIOCM_SR) {
        q_serial_port.rs232.SR = Q_TRUE;
    }
#endif
    if (pins & TIOCM_CTS) {
        q_serial_port.rs232.CTS = Q_TRUE;
    }
#ifdef TIOCM_CAR
    if (pins & TIOCM_CAR) {
        /*
         * a.k.a. DCD
         */
        q_serial_port.rs232.DCD = Q_TRUE;
    }
#endif
#ifdef TIOCM_RNG
    if (pins & TIOCM_RNG) {
        /*
         * a.k.a. RI
         */
        q_serial_port.rs232.RI = Q_TRUE;
    }
#endif
    if (pins & TIOCM_DSR) {
        q_serial_port.rs232.DSR = Q_TRUE;
    }

    /*
     * OK
     */
    return Q_TRUE;
}

/**
 * Send a BREAK to the serial port.
 */
void send_break() {
    char notify_message[DIALOG_MESSAGE_SIZE];

    assert(q_child_tty_fd != -1);

    /*
     * For linux, break value is in 'jiffies' -- apparently 1
     * jiffie = 1/100 seconds
     *
     * On all architectures:
     *
     * 0 means 0.25 secs <= duration <= 0.50 secs
     */
    if (tcsendbreak(q_child_tty_fd, 0) < 0) {
        /*
         * Error
         */
        snprintf(notify_message, sizeof(notify_message),
                 _("Error sending BREAK to \"%s\": %s"),
                 q_modem_config.dev_name, strerror(errno));
                 notify_form(notify_message, 0);
                 q_cursor_on();
    } else {
        qlog(_("Sent BREAK\n"));
    }
}

#endif /* Q_PDCURSES_WIN32 */

#endif /* Q_NO_SERIAL */
