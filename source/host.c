/*
 * host.c
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

/*
 * This is a very simple host mode implementation that provides local and
 * remote users menus for messages, file transfer, and chat.  It uses only
 * 7-bit ASCII characters in its menus (by default - translations can change
 * that), and assumes an 8-bit clean channel for file transfers.  The only
 * file transfer protocols supported are plain Xmodem, Ymodem, Zmodem, and
 * Kermit; ASCII and -G protocols are not supported.
 */

#include "qcurses.h"
#include "common.h"
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#if defined(Q_PDCURSES_WIN32) && !defined(__BORLANDC__)
#  include <windows.h>
#  include <shlwapi.h>
#  define S_ISDIR(x) ((x & _S_IFDIR))
#endif
#include "screen.h"
#include "states.h"
#include "qodem.h"
#include "console.h"
#include "options.h"
#include "netclient.h"
#include "music.h"
#include "protocols.h"
#include "translate.h"
#include "host.h"

/* Set this to a not-NULL value to enable debug log. */
/* static const char * DLOGNAME = "host"; */
static const char * DLOGNAME = NULL;

#define EOL "\r\n"

/* The file in ~/qodem/hosts that stores user-generated messages. */
#define MESSAGE_FILENAME "messages.txt"

/**
 * The available host mode functions.
 */
typedef enum {
    LISTENING,
    MODEM_LISTENING_FOR_RING,
    MODEM_LISTENING_FOR_CONNECT,
    MODEM_CONNECTED,
    LOGIN,
    MAIN_MENU,
    ENTER_MESSAGE,
    ENTER_MESSAGE_FINISH,
    READ_MESSAGES,
    CHAT,
    PAGE_SYSOP,
    LOCAL_LOGIN,
    UPLOAD_FILE,
    UPLOAD_FILE_XMODEM,
    UPLOAD_FILE_YMODEM,
    UPLOAD_FILE_ZMODEM,
    UPLOAD_FILE_KERMIT,
    LIST_FILES,
    DOWNLOAD_FILE,
    DOWNLOAD_FILE_XMODEM,
    DOWNLOAD_FILE_YMODEM,
    DOWNLOAD_FILE_ZMODEM,
    DOWNLOAD_FILE_KERMIT,
    GOODBYE,
    NONE
} STATE;

/**
 * The state transition table for the host modes.
 *
 * (So many FSM's in this project, and this is the first time I use an actual
 * state transition table...)
 */
struct host_state {
    /**
     * Current state.
     */
    STATE state;

    /**
     * Input character.  NUL (0) matches any character.
     */
    char input;

    /**
     * Next state to switch to based on the input character.
     */
    STATE next_state;

    /**
     * If not NULL, call this function when transitioning to the new state.
     */
    void (*next_fn) ();
};

/* Forward references to various menu functions */
static void do_login();
static void view_current_message();
static void view_new_message();
static void enter_message();
static void save_message();
static void kill_message();
static void read_messages_menu();
static void kill_read_message();
static void previous_message();
static void next_message();
static void page_sysop();
static void chat();
static void list_files();
static void download_file_menu();
static void upload_file_menu();
static void main_menu();
static void goodbye();
static void download_file_xmodem();
static void download_file_ymodem();
static void download_file_zmodem();
static void download_file_kermit();
static void upload_file_xmodem();
static void upload_file_ymodem();
static void upload_file_zmodem();
static void upload_file_kermit();
static void enter_message_finish_menu();

/**
 * The state transition table
 */
static struct host_state states[] = {
    {MAIN_MENU, 'e', ENTER_MESSAGE, enter_message},
    {MAIN_MENU, 'r', READ_MESSAGES, read_messages_menu},
    {MAIN_MENU, 'p', PAGE_SYSOP, page_sysop},
    {MAIN_MENU, 'f', MAIN_MENU, list_files},
    {MAIN_MENU, 'd', DOWNLOAD_FILE, download_file_menu},
    {MAIN_MENU, 'u', UPLOAD_FILE, upload_file_menu},
    {MAIN_MENU, 'g', GOODBYE, goodbye},
    {MAIN_MENU, C_CR, MAIN_MENU, main_menu},
    {DOWNLOAD_FILE, 'x', DOWNLOAD_FILE_XMODEM, download_file_xmodem},
    {DOWNLOAD_FILE, 'y', DOWNLOAD_FILE_YMODEM, download_file_ymodem},
    {DOWNLOAD_FILE, 'z', DOWNLOAD_FILE_ZMODEM, download_file_zmodem},
    {DOWNLOAD_FILE, 'k', DOWNLOAD_FILE_KERMIT, download_file_kermit},
    {DOWNLOAD_FILE, 'q', MAIN_MENU, main_menu},
    {DOWNLOAD_FILE, C_CR, DOWNLOAD_FILE, download_file_menu},
    {DOWNLOAD_FILE_XMODEM, 0, DOWNLOAD_FILE_XMODEM, download_file_xmodem},
    {DOWNLOAD_FILE_YMODEM, 0, DOWNLOAD_FILE_YMODEM, download_file_ymodem},
    {DOWNLOAD_FILE_ZMODEM, 0, DOWNLOAD_FILE_ZMODEM, download_file_zmodem},
    {DOWNLOAD_FILE_KERMIT, 0, DOWNLOAD_FILE_KERMIT, download_file_kermit},
    {UPLOAD_FILE, 'x', UPLOAD_FILE_XMODEM, upload_file_xmodem},
    {UPLOAD_FILE, 'y', UPLOAD_FILE_YMODEM, upload_file_ymodem},
    {UPLOAD_FILE, 'z', UPLOAD_FILE_ZMODEM, upload_file_zmodem},
    {UPLOAD_FILE, 'k', UPLOAD_FILE_KERMIT, upload_file_kermit},
    {UPLOAD_FILE, 'q', MAIN_MENU, main_menu},
    {UPLOAD_FILE, C_CR, UPLOAD_FILE, upload_file_menu},
    {UPLOAD_FILE_XMODEM, 0, UPLOAD_FILE_XMODEM, upload_file_xmodem},
    {UPLOAD_FILE_YMODEM, 0, UPLOAD_FILE_YMODEM, upload_file_ymodem},
    {UPLOAD_FILE_ZMODEM, 0, UPLOAD_FILE_ZMODEM, upload_file_zmodem},
    {UPLOAD_FILE_KERMIT, 0, UPLOAD_FILE_KERMIT, upload_file_kermit},
    {READ_MESSAGES, 'q', MAIN_MENU, main_menu},
    {READ_MESSAGES, 'v', READ_MESSAGES, view_current_message},
    {READ_MESSAGES, 'e', ENTER_MESSAGE, enter_message},
    {READ_MESSAGES, 'p', READ_MESSAGES, previous_message},
    {READ_MESSAGES, 'n', READ_MESSAGES, next_message},
    {READ_MESSAGES, 'k', READ_MESSAGES, kill_read_message},
    {READ_MESSAGES, C_CR, READ_MESSAGES, read_messages_menu},
    {ENTER_MESSAGE, 0, ENTER_MESSAGE, enter_message},
    {ENTER_MESSAGE_FINISH, 'v', ENTER_MESSAGE_FINISH, view_new_message},
    {ENTER_MESSAGE_FINISH, 'k', MAIN_MENU, kill_message},
    {ENTER_MESSAGE_FINISH, 's', MAIN_MENU, save_message},
    {ENTER_MESSAGE_FINISH, C_CR, ENTER_MESSAGE_FINISH,
     enter_message_finish_menu},
    {CHAT, 0, CHAT, chat},
    {LOGIN, 0, LOGIN, do_login},

    /*
     * This must be the last entry.
     */
    {NONE, '-', NONE, NULL}
};

/**
 * The current host mode state.
 */
static STATE current_state;

/**
 * The state host mode was in when chat was entered.
 */
static STATE chat_previous_state;

/**
 * The value of the do_line_buffer flag when chat was entered.
 */
static STATE chat_previous_line_buffer;

/**
 * If true, we are in a call (either remote or local login).
 */
static Q_BOOL host_online;

/**
 * If true, we are in a local login session.
 */
static Q_BOOL local_login;

/**
 * If true, we are in chat.
 */
static Q_BOOL sysop_chat;

/**
 * The available states for entering a message.
 */
typedef enum {
    MSG_INIT,
    FROM,
    TO,
    BODY
} MSG_STATE;

/**
 * The leaving a message state.
 */
static MSG_STATE msg_state = MSG_INIT;

/* Message fields. */
static wchar_t * msg_from = NULL;
static wchar_t * msg_to = NULL;
static wchar_t ** msg_body = NULL;
static int msg_body_n = 0;

/* The messages list supporting the read messages function. */
static wchar_t *** all_messages = NULL;
static int all_messages_n = 0;
static int current_message = 0;

/**
 * The available states for the file transfer menus.
 */
typedef enum {
    FILENAME,
    FILENAME_WAIT,
    FILENAME_RESUME,
    TRANSFER
} FILE_STATE;

/**
 * The file transfer menus state.
 */
static FILE_STATE file_state = FILENAME;

/**
 * The name of the file to transfer.
 */
static char * transfer_filename;

/**
 * The available states for the login function.
 */
typedef enum {
    LOGIN_INIT,
    USERNAME,
    PASSWORD
} LOGIN_STATE;

/**
 * The login state.
 */
static LOGIN_STATE login_state = LOGIN_INIT;

/* The login fields. */
static char login_username[64];
static char login_password[64];

/* Line mode editing support buffer. */
static wchar_t line_buffer[80];
static int line_buffer_n = 0;
/*
 * save_line_buffer() and restore_line_buffer() copy between line_buffer and
 * saved_line_buffer.
 */
static wchar_t saved_line_buffer[80];
static int saved_line_buffer_n = 0;

/**
 * When true, we are collecting a full line into the line buffer.
 */
static Q_BOOL do_line_buffer;

/* UTF-8 decoder support */
static uint32_t utf8_state;
static uint32_t utf8_char;

/**
 * The network listener descriptor.
 */
static int listen_fd = -1;

/**
 * When in host mode, the type of host.  This is analagous to q_dial_method.
 */
Q_HOST_TYPE q_host_type;

/**
 * Whether or not host mode is active, even through file transfers.
 */
Q_BOOL q_host_active;

/**
 * When true, the sysop is being paged.
 */
static Q_BOOL page = Q_FALSE;

/**
 * Clear the line buffer.
 */
static void reset_line_buffer() {
    memset(line_buffer, 0, sizeof(line_buffer));
    do_line_buffer = Q_FALSE;
    line_buffer_n = 0;
    utf8_state = 0;
    utf8_char = 0;
}

/**
 * Save the line buffer into saved_line_buffer.
 */
static void save_line_buffer() {
    wmemcpy(saved_line_buffer, line_buffer, line_buffer_n);
    saved_line_buffer_n = line_buffer_n;
}

/**
 * Restore the line buffer from saved_line_buffer.
 */
static void restore_line_buffer() {
    wmemcpy(line_buffer, saved_line_buffer, saved_line_buffer_n);
    line_buffer_n = saved_line_buffer_n;
}

/**
 * Reset the internal host state in preparation for for next connection.
 */
static void reset_host() {
    current_state = LISTENING;
    host_online = Q_FALSE;
    local_login = Q_FALSE;
    sysop_chat = Q_FALSE;
    msg_state = MSG_INIT;
    file_state = FILENAME;
    login_state = LOGIN_INIT;
    reset_line_buffer();
}

/**
 * Send a string to the remote side, echoing to the local side also.
 *
 * @param buffer the bytes to send to the remote side
 * @param count the number of bytes in buffer
 */
static void host_write(char * buffer, int count) {
    int i;
    if (host_online == Q_TRUE) {
        qodem_write(q_child_tty_fd, buffer, count, Q_TRUE);
    }
    for (i = 0; i < count; i++) {

        /*
         * Capture
         */
        if (q_status.capture == Q_TRUE) {
            if (q_status.capture_type == Q_CAPTURE_TYPE_RAW) {
                /*
                 * Raw - use the translation map here because it will match
                 * what went out on the wire.
                 */
                fprintf(q_status.capture_file, "%c",
                    translate_8bit_out(buffer[i]));
                if (q_status.capture_flush_time < time(NULL)) {
                    fflush(q_status.capture_file);
                    q_status.capture_flush_time = time(NULL);
                }
            }
        }

        if (buffer[i] == 0x08) {
            /*
             * Backspace
             */
            cursor_left(1, Q_FALSE);
            print_character(' ');
            cursor_left(1, Q_FALSE);
            continue;
        }
        if (buffer[i] == 0x0D) {
            cursor_carriage_return();
            continue;
        }
        if (buffer[i] == 0x0A) {
            cursor_linefeed(Q_FALSE);
            continue;
        }
        print_character(buffer[i]);
    }
    /*
     * Refresh screen
     */
    q_screen_dirty = Q_TRUE;
}

/**
 * Emit a menu string to the remote side.  This also performs translation of
 * the string.
 *
 * @param menu_string an UNTRANSLATED menu to send
 */
static void do_menu(const char * menu_string) {
    char * menu = (char *) (_(menu_string));
    host_write(menu, strlen(menu));
}

/**
 * Begin host mode.
 *
 * @param type the method to listen for
 * @param port the port to listen on for network hosts.  This can also be
 * NEXT_AVAILABLE_PORT_STRING (see netclient.h).
 */
void host_start(Q_HOST_TYPE type, const char * port) {
    char notify_message[DIALOG_MESSAGE_SIZE];

    DLOG(("host_start() %u\n", type));

    /*
     * Set initial state
     */
    reset_host();
    q_host_active = Q_TRUE;

    /*
     * Setup the listener depending on host type
     */
    switch (type) {
    case Q_HOST_TYPE_SOCKET:
    case Q_HOST_TYPE_TELNETD:
#ifdef Q_SSH_CRYPTLIB
    case Q_HOST_TYPE_SSHD:
#endif

#ifdef Q_UPNP
        if (strcmp(port, UPNP_PORT_STRING) == 0) {
            /*
             * Get a port from UPnP
             */
            listen_fd = net_listen(port);
        } else if (strcmp(port, NEXT_AVAILABLE_PORT_STRING) == 0) {
#else
        if (strcmp(port, NEXT_AVAILABLE_PORT_STRING) == 0) {
#endif
            /*
             * Bind to the next available non-privileged port
             */
            listen_fd = net_listen(port);
        } else {
            /*
             * Bind directly to passed in port
             */
            listen_fd = net_listen(port);
        }

        if (listen_fd == -1) {
            /*
             * We failed to bind()/listen(), abort host mode
             */
            /*
             * Return to TERMINAL mode
             */
            switch_state(Q_STATE_CONSOLE);
            q_host_active = Q_FALSE;
            return;
        }

#ifdef Q_UPNP
        if (strcmp(port, UPNP_PORT_STRING) == 0) {
            sprintf(notify_message,
                _("%sHost Mode now listening at %s (remotely accessible on %s)...%s"),
                EOL, net_listen_string(), net_listen_external_string(), EOL);
        } else {
#endif
            sprintf(notify_message, _("%sHost Mode now listening at %s...%s"),
                EOL, net_listen_string(), EOL);

#ifdef Q_UPNP
        }
#endif

        host_write(notify_message, strlen(notify_message));
        break;

#ifndef Q_NO_SERIAL
    case Q_HOST_TYPE_MODEM:
        DLOG(("host_start() MODEM\n"));
        if (!Q_SERIAL_OPEN) {
            if (open_serial_port() == Q_FALSE) {
                /*
                 * notify_form() just turned off the cursor
                 */
                q_cursor_on();
                /*
                 * Return to TERMINAL mode
                 */
                switch_state(Q_STATE_CONSOLE);
                return;
            }
        }
        break;
    case Q_HOST_TYPE_SERIAL:

        DLOG(("host_start() SERIAL PORT\n"));
        if (!Q_SERIAL_OPEN) {
            if (open_serial_port() == Q_FALSE) {
                /*
                 * notify_form() just turned off the cursor
                 */
                q_cursor_on();
                /*
                 * Return to TERMINAL mode
                 */
                switch_state(Q_STATE_CONSOLE);
                return;
            }
        }
        /*
         * Serial port is now open,
         */
        break;
#endif /* Q_NO_SERIAL */

    } /* switch (type) */

    /*
     * Save our host type
     */
    q_host_type = type;

    DLOG(("host_start() q_host_type %u %u\n", q_host_type, type));

    qlog(_("Starting host mode.\n"));
}

/**
 * Kill host mode.
 */
static void host_stop() {

    qlog(_("Leaving host mode.\n"));

    /*
     * Kill the listening socket
     */
    switch (q_host_type) {
    case Q_HOST_TYPE_SOCKET:
        if (host_online == Q_TRUE) {
            assert(q_child_tty_fd != -1);
            assert(q_status.online == Q_TRUE);
            close_connection();
        }
        net_listen_close();
        listen_fd = -1;
        break;
    case Q_HOST_TYPE_TELNETD:
        if (host_online == Q_TRUE) {
            assert(q_child_tty_fd != -1);
            assert(q_status.online == Q_TRUE);
            close_connection();
        }
        net_listen_close();
        listen_fd = -1;
        break;
#ifdef Q_SSH_CRYPTLIB
    case Q_HOST_TYPE_SSHD:
        if (host_online == Q_TRUE) {
            assert(q_child_tty_fd != -1);
            assert(q_status.online == Q_TRUE);
            close_connection();
        }
        net_listen_close();
        listen_fd = -1;
        break;
#endif
#ifndef Q_NO_SERIAL
    case Q_HOST_TYPE_MODEM:
        if (Q_SERIAL_OPEN) {
            hangup_modem();
            close_serial_port();
        }
        break;
    case Q_HOST_TYPE_SERIAL:
        if (Q_SERIAL_OPEN) {
            close_serial_port();
        }
        break;
#endif
    }

    host_online = Q_FALSE;
    q_host_active = Q_FALSE;
}

/**
 * Reads one byte from the remote side, decodes UTF-8, and puts into
 * line_buffer.  Backspace and DEL both work to delete the current character.
 *
 * @param ch the byte from the remote side
 * @return true if the user pressed enter
 */
static Q_BOOL line_buffer_char(const unsigned char ch) {
    uint32_t last_utf8_state;
    char utf8_buffer[6];
    int rc;

    if ((ch == 0x08) || (ch == 0x7F)) {
        /*
         * Backspace
         */
        if (line_buffer_n > 0) {
            line_buffer_n--;
            line_buffer[line_buffer_n] = 0;
            /*
             * Emit the backspace
             */
            host_write("\x08", 1);
        }
        return Q_FALSE;
    }
    if (ch == C_CR) {
        /*
         * User pressed enter
         */
        return Q_TRUE;
    }

    last_utf8_state = utf8_state;
    utf8_decode(&utf8_state, &utf8_char, ch);
    if ((last_utf8_state == utf8_state) && (utf8_state != UTF8_ACCEPT)) {
        /*
         * Bad character, reset UTF8 decoder state
         */
        utf8_state = 0;

        /*
         * Discard character
         */
        return Q_FALSE;
    }
    if (utf8_state != UTF8_ACCEPT) {
        /*
         * Not enough characters to convert yet
         */
        return Q_FALSE;
    }
    /*
     * We've got a UTF-8 character, keep it
     */
    if (line_buffer_n < 80) {
        line_buffer[line_buffer_n] = (wchar_t) utf8_char;
        line_buffer_n++;
        print_character((wchar_t) utf8_char);
        q_screen_dirty = Q_TRUE;
        if ((current_state == LOGIN) && (login_state == PASSWORD)) {
            rc = utf8_encode(L'X', utf8_buffer);
        } else {
            rc = utf8_encode((wchar_t) utf8_char, utf8_buffer);
        }
        utf8_buffer[rc] = 0;
        if (host_online == Q_TRUE) {
            qodem_write(q_child_tty_fd, utf8_buffer, rc, Q_TRUE);
        }
    }
    return Q_FALSE;
}

/* Logging into the system */
static void do_login() {

    if (login_state == LOGIN_INIT) {
        DLOG(("do_login(): LOGIN_INIT\n"));
        do_menu(EOL "login: ");
        login_state = USERNAME;
        reset_line_buffer();
        do_line_buffer = Q_TRUE;
        return;
    }

    if (login_state == USERNAME) {
        /*
         * Line buffer has the username
         */
        memset(login_username, 0, sizeof(login_username));
        wcstombs(login_username, line_buffer, sizeof(login_username) - 1);
        DLOG(("do_login(): username = \'%s\'\n", login_username));

        do_menu(EOL "Password: ");

        login_state = PASSWORD;
        reset_line_buffer();
        do_line_buffer = Q_TRUE;
        return;
    }

    if (login_state == PASSWORD) {
        /*
         * Line buffer has the password
         */
        memset(login_password, 0, sizeof(login_password));
        wcstombs(login_password, line_buffer, sizeof(login_password) - 1);
        DLOG(("do_login(): password = \'%s\'\n", login_password));

        /*
         * Check username and password
         */
        if ((strcmp(login_username, get_option(Q_OPTION_HOST_USERNAME)) == 0) &&
            (strcmp(login_password, get_option(Q_OPTION_HOST_PASSWORD)) == 0)
        ) {
            /*
             * Login OK, move to main menu
             */
            login_state = LOGIN_INIT;
            current_state = MAIN_MENU;
            main_menu();
            qlog(_("Host mode LOGIN from user %s\n"), login_username);
        } else {
            /*
             * Login failed, back to LOGIN_INIT
             */
            do_menu(EOL "Login incorrect" EOL);
            login_state = LOGIN_INIT;
            do_login();
            qlog(_("INCORRECT LOGIN, username %s\n"), login_username);
        }
        return;
    }

    /*
     * Should never get here
     */
    abort();
}

/* Finished entering a message. */
static void enter_message_finish_menu() {
    do_menu(EOL
    "V)iew This Message Again   S)ave This Message   K)ill (Abort) This Message" EOL
    "Your choice?  ");
}

/* View the new message. */
static void view_new_message() {
    char buffer[Q_MAX_LINE_LENGTH];
    int line_i;
    wchar_t * line;

    do_menu(EOL);

    /*
     * Print message #
     */
    sprintf(buffer, _("New Message:%s"), EOL);
    host_write(buffer, strlen(buffer));
    sprintf(buffer, "--------------------------%s", EOL);
    host_write(buffer, strlen(buffer));

    sprintf(buffer, _("From: %ls%s"), msg_from, EOL);
    host_write(buffer, strlen(buffer));
    sprintf(buffer, _("To: %ls%s"), msg_to, EOL);
    host_write(buffer, strlen(buffer));

    sprintf(buffer, "-----%s", EOL);
    host_write(buffer, strlen(buffer));

    for (line_i = 0; line_i < msg_body_n; line_i++) {
        line = msg_body[line_i];
        sprintf(buffer, "%ls%s", line, EOL);
        host_write(buffer, strlen(buffer));
    }

    sprintf(buffer, "-----%s", EOL);
    host_write(buffer, strlen(buffer));

    do_menu(EOL);

    do_menu(EOL
    "V)iew This Message Again   S)ave This Message   K)ill (Abort) This Message" EOL
    "Your choice?  ");
}

/* Entering a new message handles the From, To, and Body. */
static void enter_message() {
    if (q_status.read_only == Q_TRUE) {
        do_menu(EOL "Cannot create new message with --read-only set." EOL);
        current_state = MAIN_MENU;
        main_menu();
        return;
    }

    if (msg_state == MSG_INIT) {
        DLOG(("enter_message(): MSG_INIT\n"));

        /*
         * Reset message state
         */
        kill_message();

        do_menu(EOL
            "Enter New Message" EOL
            EOL
            "-----------------" EOL
            EOL
            "From: ");
        msg_state = FROM;
        reset_line_buffer();
        do_line_buffer = Q_TRUE;
        return;
    }

    if (msg_state == FROM) {
        /*
         * Line buffer has the from field
         */
        assert(msg_from == NULL);
        msg_from = Xwcsdup(line_buffer, __FILE__, __LINE__);
        DLOG(("enter_message(): FROM = \'%ls\'\n", msg_from));

        do_menu(EOL "To: ");
        msg_state = TO;
        reset_line_buffer();
        do_line_buffer = Q_TRUE;
        return;
    }

    if (msg_state == TO) {
        /*
         * Line buffer has the to field
         */
        assert(msg_to == NULL);
        msg_to = Xwcsdup(line_buffer, __FILE__, __LINE__);
        DLOG(("enter_message(): TO = \'%ls\'\n", msg_to));

        do_menu(EOL
            "Enter a single period (.) and enter to finish this message." EOL);
        msg_state = BODY;
        reset_line_buffer();
        do_line_buffer = Q_TRUE;
        return;
    }

    if (msg_state == BODY) {
        /*
         * Line buffer has the next line in the body
         */
        if (msg_body_n == 0) {
            assert(msg_body == NULL);
        }

        if (wcscmp(line_buffer, L".") == 0) {
            enter_message_finish_menu();
            current_state = ENTER_MESSAGE_FINISH;
            /*
             * Reset for next message
             */
            msg_state = MSG_INIT;
        } else {
            /*
             * Append this line to body
             */
            msg_body = (wchar_t **) Xrealloc(msg_body,
                                             sizeof(wchar_t *) * (msg_body_n +
                                                                  1), __FILE__,
                                             __LINE__);
            msg_body[msg_body_n] = Xwcsdup(line_buffer, __FILE__, __LINE__);

            DLOG(("enter_message(): BODY %d = \'%ls\'\n",
                  msg_body_n, msg_body[msg_body_n]));

            msg_body_n++;
            do_menu(EOL);
            msg_state = BODY;
            reset_line_buffer();
            do_line_buffer = Q_TRUE;
        }
        return;
    }

    /*
     * Should never get here
     */
    abort();
}

/* Abandon a message without saving it */
static void kill_message() {
    int i;
    if (msg_from != NULL) {
        Xfree(msg_from, __FILE__, __LINE__);
        msg_from = NULL;
    }
    if (msg_to != NULL) {
        Xfree(msg_to, __FILE__, __LINE__);
        msg_to = NULL;
    }
    if (msg_body != NULL) {
        assert(msg_body_n > 0);
        for (i = 0; i < msg_body_n; i++) {
            Xfree(msg_body[i], __FILE__, __LINE__);
        }
        Xfree(msg_body, __FILE__, __LINE__);
        msg_body = NULL;
        msg_body_n = 0;
    }

    /*
     * Re-display the main menu
     */
    main_menu();
}

/* Save a message to the message file */
static void save_message() {
    FILE * file;
    char * filename;
    char notify_message[DIALOG_MESSAGE_SIZE];
    int i;

    filename = (char *) Xmalloc(strlen(MESSAGE_FILENAME) +
                                strlen(get_option(Q_OPTION_HOST_DIR)) + 2,
                                __FILE__, __LINE__);
    memset(filename, 0,
           strlen(MESSAGE_FILENAME) + strlen(get_option(Q_OPTION_HOST_DIR)) +
           2);
    strncpy(filename, get_option(Q_OPTION_HOST_DIR),
            strlen(get_option(Q_OPTION_HOST_DIR)));
    filename[strlen(filename)] = '/';
    strncpy(filename + strlen(filename), MESSAGE_FILENAME,
            strlen(MESSAGE_FILENAME));

    /*
     * Append to file
     */
    file = fopen(filename, "a");
    if (file == NULL) {
        snprintf(notify_message, sizeof(notify_message),
                 _("Error opening file \"%s\" for writing: %s"),
                 filename, strerror(errno));
        host_write(notify_message, strlen(notify_message));
        return;
    }

    /*
     * Emit message to file
     */
    /*
     * Single period is the message separator since it cannot be entered in
     * the line editor.
     */
    fprintf(file, ".\n");
    fprintf(file, "From: %ls\n", msg_from);
    fprintf(file, "To:   %ls\n", msg_to);
    fprintf(file, "----------------------------------------\n");
    for (i = 0; i < msg_body_n; i++) {
        fprintf(file, "%ls\n", msg_body[i]);
    }
    fprintf(file, "----------------------------------------\n");

    /*
     * All done
     */
    fclose(file);

    /*
     * No leak
     */
    Xfree(filename, __FILE__, __LINE__);

    /*
     * Reset message state
     */
    kill_message();

    /*
     * Re-display the main menu
     */
    main_menu();
}

/* Clear messages from memory */
static void clear_all_messages() {
    int message_i;
    int line_i;
    wchar_t ** message;
    wchar_t * line;
    if (all_messages != NULL) {
        assert(all_messages_n > 0);
        for (message_i = 0; message_i < all_messages_n; message_i++) {
            message = all_messages[message_i];
            line_i = 0;
            line = message[line_i];
            while (line != NULL) {
                Xfree(line, __FILE__, __LINE__);
                line_i++;
                line = message[line_i];
            }
            Xfree(message, __FILE__, __LINE__);
            message_i++;
        }

        Xfree(all_messages, __FILE__, __LINE__);
        all_messages = NULL;
        all_messages_n = 0;
    }
}

/* Load all messages into memory */
static void read_all_messages() {
    FILE * file;
    char * filename;
    char notify_message[DIALOG_MESSAGE_SIZE];
    char * begin;
    wchar_t ** message = NULL;
    int message_n = 0;
    wchar_t line_wchar[Q_MAX_LINE_LENGTH];
    char line[Q_MAX_LINE_LENGTH];

    filename = (char *) Xmalloc(strlen(MESSAGE_FILENAME) +
                                strlen(get_option(Q_OPTION_HOST_DIR)) + 2,
                                __FILE__, __LINE__);
    memset(filename, 0,
           strlen(MESSAGE_FILENAME) + strlen(get_option(Q_OPTION_HOST_DIR)) +
           2);
    strncpy(filename, get_option(Q_OPTION_HOST_DIR),
            strlen(get_option(Q_OPTION_HOST_DIR)));
    filename[strlen(filename)] = '/';
    strncpy(filename + strlen(filename), MESSAGE_FILENAME,
            strlen(MESSAGE_FILENAME));

    /*
     * Read from file
     */
    if (file_exists(filename) == Q_FALSE) {
        /*
         * File isn't present, bail out
         */
        return;
    }

    file = fopen(filename, "r");
    if (file == NULL) {
        snprintf(notify_message, sizeof(notify_message),
                 _("Error opening file \"%s\" for reading: %s"),
                 filename, strerror(errno));
        host_write(notify_message, strlen(notify_message));
        return;
    }

    while (!feof(file)) {
        if (fgets(line, sizeof(line), file) == NULL) {
            /*
             * This will cause the outer while's feof() check to fail and
             * smoothly exit the while loop.
             */
            continue;
        }
        begin = line;

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

        /*
         * Convert from UTF-8
         */
        memset(line_wchar, 0, sizeof(line_wchar));
        mbstowcs(line_wchar, begin, strlen(begin) + 1);

        /*
         * See if this is '.'
         */
        if (wcscmp(line_wchar, L".") == 0) {
            /*
             * New message
             */
            all_messages = (wchar_t ***) Xrealloc(all_messages,
                                                  sizeof(wchar_t **) *
                                                  (all_messages_n + 1),
                                                  __FILE__, __LINE__);
            all_messages_n++;

            if (message != NULL) {
                message = (wchar_t **) Xrealloc(message,
                                                sizeof(wchar_t *) * (message_n +
                                                                     1),
                                                __FILE__, __LINE__);
                message[message_n] = NULL;
                all_messages[all_messages_n - 2] = message;
                message = NULL;
                message_n = 0;
            }
            continue;
        }

        /*
         * Append this line to message
         */
        message =
            (wchar_t **) Xrealloc(message, sizeof(wchar_t *) * (message_n + 1),
                                  __FILE__, __LINE__);
        message[message_n] = Xwcsdup(line_wchar, __FILE__, __LINE__);
        message_n++;
    }

    /*
     * Append last message
     */
    if (message != NULL) {
        message =
            (wchar_t **) Xrealloc(message, sizeof(wchar_t *) * (message_n + 1),
                                  __FILE__, __LINE__);
        message[message_n] = NULL;
        all_messages[all_messages_n - 1] = message;
        message = NULL;
        message_n = 0;
    }

    /*
     * All done
     */
    fclose(file);

    /*
     * No leak
     */
    Xfree(filename, __FILE__, __LINE__);
}

/* Switch to previous message */
static void previous_message() {
    if (current_message > 0) {
        current_message--;
    }
    /*
     * Re-display the read message menu
     */
    read_messages_menu();
}

/* Switch to next message */
static void next_message() {
    if (current_message < all_messages_n - 1) {
        current_message++;
    }
    /*
     * Re-display the read message menu
     */
    read_messages_menu();
}

/* Update the entire messages file */
static void save_all_messages() {
    int message_i;
    int line_i;
    wchar_t ** message;
    wchar_t * line;
    FILE * file;
    char * filename;
    char notify_message[DIALOG_MESSAGE_SIZE];

    filename = (char *) Xmalloc(strlen(MESSAGE_FILENAME) +
                                strlen(get_option(Q_OPTION_HOST_DIR)) + 2,
                                __FILE__, __LINE__);
    memset(filename, 0,
           strlen(MESSAGE_FILENAME) + strlen(get_option(Q_OPTION_HOST_DIR)) +
           2);
    strncpy(filename, get_option(Q_OPTION_HOST_DIR),
            strlen(get_option(Q_OPTION_HOST_DIR)));
    filename[strlen(filename)] = '/';
    strncpy(filename + strlen(filename), MESSAGE_FILENAME,
            strlen(MESSAGE_FILENAME));

    /*
     * Overwrite file
     */
    file = fopen(filename, "w");
    if (file == NULL) {
        snprintf(notify_message, sizeof(notify_message),
                 _("Error opening file \"%s\" for writing: %s"),
                 filename, strerror(errno));
        host_write(notify_message, strlen(notify_message));
        return;
    }

    if (all_messages != NULL) {
        assert(all_messages_n > 0);
        for (message_i = 0; message_i < all_messages_n; message_i++) {
            /*
             * Single period is the message separator since it cannot be
             * entered in the line editor.
             */
            fprintf(file, ".\n");
            message = all_messages[message_i];
            line_i = 0;
            line = message[line_i];
            while (line != NULL) {
                fprintf(file, "%ls\n", line);
                line_i++;
                line = message[line_i];
            }
            message_i++;
        }
    }

    /*
     * All done
     */
    fclose(file);

    /*
     * No leak
     */
    Xfree(filename, __FILE__, __LINE__);
}

/* Remove the current message */
static void kill_read_message() {
    wchar_t * line;
    int line_i;
    wchar_t ** message;

    message = all_messages[current_message];
    memmove(all_messages + current_message,
            all_messages + current_message + 1,
            all_messages_n - current_message - 1);
    all_messages_n--;
    if (current_message == all_messages_n) {
        current_message--;
    }

    line_i = 0;
    line = message[line_i];
    while (line != NULL) {
        Xfree(line, __FILE__, __LINE__);
        line_i++;
        line = message[line_i];
    }
    Xfree(message, __FILE__, __LINE__);

    if (all_messages_n == 0) {
        Xfree(all_messages, __FILE__, __LINE__);
        all_messages = NULL;
    }

    /*
     * Update messages file
     */
    save_all_messages();

    /*
     * Re-display the read message menu
     */
    read_messages_menu();
}

/* Display one message to the console */
static void display_message(const int n) {
    char buffer[Q_MAX_LINE_LENGTH];
    int line_i;
    wchar_t ** message;
    wchar_t * line;
    if (all_messages_n == 0) {
        do_menu("No messages." EOL);
        return;
    }

    assert(all_messages != NULL);
    assert(all_messages_n > 0);
    assert(n < all_messages_n);

    message = all_messages[n];

    /*
     * Print message #
     */
    sprintf(buffer, _("Message #%d of %d%s"), current_message + 1,
            all_messages_n, EOL);
    host_write(buffer, strlen(buffer));

    line_i = 0;
    line = message[line_i];
    while (line != NULL) {
        sprintf(buffer, "%ls%s", line, EOL);
        host_write(buffer, strlen(buffer));
        line_i++;
        line = message[line_i];
    }
}

/* View a message again */
static void view_current_message() {
    do_menu(EOL);
    display_message(current_message);
    do_menu(EOL
        " V)iew  P)revious  N)ext  K)ill/Delete  E)nter New Message  Q)uit To Main Menu" EOL
        "Your choice?  ");
}

/* Read the saved messages */
static void read_messages_menu() {
    if (all_messages == NULL) {
        read_all_messages();
    }

    if ((current_message >= all_messages_n) && (all_messages_n > 0)
        ) {
        do_menu(EOL
            "A message was deleted, displaying last message." EOL);
        /*
         * Truncate to the last message
         */
        current_message = all_messages_n - 1;
    }

    do_menu(EOL);
    display_message(current_message);
    do_menu(EOL
        " V)iew  P)revious  N)ext  K)ill/Delete  E)nter New Message  Q)uit To Main Menu" EOL
        "Your choice?  ");
}

/* List files excluding '.', '..', and the messages file */
static void list_files() {
    DIR * directory = NULL;
    struct dirent * dir_entry;
    char buffer[COMMAND_LINE_SIZE];
    struct stat fstats;
    int total = 0;

    /*
     * Read directory
     */
    directory = opendir(get_option(Q_OPTION_HOST_DIR));
    if (directory == NULL) {
        sprintf(buffer, _("Unable to display files in %s%s"),
                get_option(Q_OPTION_HOST_DIR), EOL);
        host_write(buffer, strlen(buffer));
        return;
    }
    sprintf(buffer, _("%sFiles in host directory:%s"), EOL, EOL);
    host_write(buffer, strlen(buffer));

    dir_entry = readdir(directory);
    while (dir_entry != NULL) {
        char * full_filename;

        /*
         * Get the full filename
         */
        full_filename = (char *) Xmalloc(strlen(dir_entry->d_name) +
                                         strlen(get_option(Q_OPTION_HOST_DIR)) +
                                         2, __FILE__, __LINE__);
        memset(full_filename, 0,
               strlen(dir_entry->d_name) +
               strlen(get_option(Q_OPTION_HOST_DIR)) + 2);
        memcpy(full_filename, get_option(Q_OPTION_HOST_DIR),
               strlen(get_option(Q_OPTION_HOST_DIR)));
        full_filename[strlen(get_option(Q_OPTION_HOST_DIR))] = '/';
        memcpy(full_filename + strlen(get_option(Q_OPTION_HOST_DIR)) + 1,
               dir_entry->d_name, strlen(dir_entry->d_name));
        full_filename[strlen(dir_entry->d_name) +
                      strlen(get_option(Q_OPTION_HOST_DIR)) + 1] = '\0';

        /*
         * Skip '.', '..', and hidden files
         */
        if (dir_entry->d_name[0] == '.') {
            dir_entry = readdir(directory);
            Xfree(full_filename, __FILE__, __LINE__);
            full_filename = NULL;
            continue;
        }

        /*
         * Skip the messages file
         */
        if (strcmp(dir_entry->d_name, MESSAGE_FILENAME) == 0) {
            dir_entry = readdir(directory);
            Xfree(full_filename, __FILE__, __LINE__);
            full_filename = NULL;
            continue;
        }

        total++;

        /*
         * Get the file stats
         */
        if (stat(full_filename, &fstats) < 0) {
            sprintf(buffer, _("Can't stat %s: %s%s"),
                    full_filename, strerror(errno), EOL);
            host_write(buffer, strlen(buffer));
        }

        /*
         * Print the file information
         */

        /*
         * Size or <dir>
         */
        if (S_ISDIR(fstats.st_mode)) {
            /*
             * Name + Directory
             */
            snprintf(buffer, sizeof(buffer), _(" %-30s        <dir>"),
                     dir_entry->d_name);
        } else {
            /*
             * Name + File size
             */
            snprintf(buffer, sizeof(buffer), " %-30s %12lu",
                     dir_entry->d_name, (unsigned long) fstats.st_size);
        }

        /*
         * Time
         */
        strftime(buffer + strlen(buffer), sizeof(buffer),
                 "  %d/%b/%Y %H:%M:%S", localtime(&fstats.st_mtime));

        /*
         * Mask
         */
        snprintf(buffer + strlen(buffer), sizeof(buffer), " %s",
                 file_mode_string(fstats.st_mode));

        /*
         * EOL
         */
        snprintf(buffer + strlen(buffer), sizeof(buffer), "%s", EOL);

        /*
         * Emit
         */
        host_write(buffer, strlen(buffer));

        /*
         * Get next entry
         */
        dir_entry = readdir(directory);
        Xfree(full_filename, __FILE__, __LINE__);
        full_filename = NULL;
    }
    assert(directory != NULL);
    closedir(directory);
    directory = NULL;

    if (total == 0) {
        /*
         * No files
         */
        sprintf(buffer, _("%s     No files.%s"), EOL, EOL);
        host_write(buffer, strlen(buffer));
    }

    /*
     * Re-display the main menu
     */
    main_menu();
}

/* Download a file */
static void download_file_menu() {
    do_menu(EOL
        "Download File" EOL
        EOL
        "-----------" EOL
        EOL
        " X)modem" EOL
        " Y)modem" EOL
        " Z)modem" EOL
        " K)ermit" EOL
        EOL
        " Q)uit To Main Menu" EOL
        "-----------" EOL
        EOL
        "Your choice?  ");
}

/* Upload a file */
static void upload_file_menu() {
    do_menu(EOL
        "Upload File" EOL
        EOL
        "-----------" EOL
        EOL
        " X)modem" EOL
        " Y)modem" EOL
        " Z)modem" EOL
        " K)ermit" EOL
        EOL
        " Q)uit To Main Menu" EOL
        "-----------" EOL
        EOL
        "Your choice?  ");
}

/* Wipe out the transfer filename */
static void clear_filename() {
    if (transfer_filename != NULL) {
        Xfree(transfer_filename, __FILE__, __LINE__);
        transfer_filename = NULL;
    }
}

/* Do download */
static void download_file(Q_PROTOCOL protocol) {
    struct file_info * upload_file_info;
    char * filename;
    int rc;
    struct stat fstats;
    int length;

    if (local_login == Q_TRUE) {
        do_menu(EOL "Cannot download on local logon." EOL);
        current_state = MAIN_MENU;
        main_menu();
        return;
    }
    assert(host_online == Q_TRUE);

download_top:

    if (file_state == FILENAME) {

        DLOG(("download_file(): FILENAME\n"));

        /*
         * Reset download state
         */
        clear_filename();

        do_menu(EOL "Enter filename to download: ");
        file_state = FILENAME_WAIT;
        reset_line_buffer();
        do_line_buffer = Q_TRUE;
        return;
    }

    if (file_state == FILENAME_WAIT) {

        DLOG(("download_file(): FILENAME_WAIT\n"));

        /*
         * Line buffer has the download filename
         */
        assert(transfer_filename == NULL);
        length = wcstombs(NULL, line_buffer, wcslen(line_buffer)) + 1;
        transfer_filename =
            (char *) Xmalloc(sizeof(char) * length, __FILE__, __LINE__);
        memset(transfer_filename, 0, length);
        snprintf(transfer_filename, length, "%ls", line_buffer);
        DLOG(("download_file(): filename = \'%s\'\n", transfer_filename));

        filename = (char *) Xmalloc(strlen(transfer_filename) +
                                    strlen(get_option(Q_OPTION_HOST_DIR)) + 2,
                                    __FILE__, __LINE__);
        memset(filename, 0,
               strlen(transfer_filename) +
               strlen(get_option(Q_OPTION_HOST_DIR)) + 2);
        strncpy(filename, get_option(Q_OPTION_HOST_DIR),
                strlen(get_option(Q_OPTION_HOST_DIR)));
        filename[strlen(filename)] = '/';
        strncpy(filename + strlen(filename), transfer_filename,
                strlen(transfer_filename));

        rc = stat(filename, &fstats);
        if (rc < 0) {

            /*
             * No leak
             */
            Xfree(filename, __FILE__, __LINE__);

            if (errno == ENOENT) {
                /*
                 * File does not exist
                 */
                do_menu(EOL "File does not exist." EOL);
                clear_filename();
                file_state = FILENAME;
                current_state = DOWNLOAD_FILE;
                download_file_menu();
                return;
            }
            /*
             * Error stat()ing file
             */
            do_menu(EOL "Host mode error checking for file." EOL);
            clear_filename();
            current_state = DOWNLOAD_FILE;
            download_file_menu();
            return;
        }

        /*
         * Transfer can continue, switch to upload
         */
        file_state = TRANSFER;

        q_transfer_stats.protocol = protocol;
        /*
         * This logic needs to match the behavior of start_file_transfer():
         * single-file protocols get the filename from q_download_location,
         * batch protocols get it from the batch entry window.
         */
        switch (protocol) {

        case Q_PROTOCOL_XMODEM:
            if (q_download_location != NULL) {
                Xfree(q_download_location, __FILE__, __LINE__);
            }
            q_download_location = Xstrdup(filename, __FILE__, __LINE__);
            switch_state(Q_STATE_UPLOAD);
            break;
        case Q_PROTOCOL_KERMIT:
        case Q_PROTOCOL_YMODEM:
        case Q_PROTOCOL_ZMODEM:
            /*
             * Insert into the batch entry window
             */
            upload_file_info =
                (struct file_info *) Xmalloc(2 * sizeof(struct file_info),
                                             __FILE__, __LINE__);
            memset(upload_file_info, 0, 2 * sizeof(struct file_info));
            upload_file_info[0].name = Xstrdup(filename, __FILE__, __LINE__);
            memcpy(&upload_file_info[0].fstats, &fstats, sizeof(struct stat));
            set_batch_upload(upload_file_info);
            switch_state(Q_STATE_UPLOAD_BATCH);
            break;

        default:
            /*
             * Should never get here
             */
            abort();
        }

        DLOG(("START_FILE_TRANSFER\n"));

        start_file_transfer();
        clear_filename();
        current_state = DOWNLOAD_FILE;

        /*
         * No leak
         */
        Xfree(filename, __FILE__, __LINE__);
        return;
    }

    if (file_state == TRANSFER) {
        DLOG(("download_file(): TRANSFER\n"));
        file_state = FILENAME;
        goto download_top;
    }

    /*
     * Should never get here
     */
    abort();
}

/* Do download with xmodem */
static void download_file_xmodem() {
    download_file(Q_PROTOCOL_XMODEM);
}

/* Do download with ymodem */
static void download_file_ymodem() {
    download_file(Q_PROTOCOL_YMODEM);
}

/* Do download with zmodem */
static void download_file_zmodem() {
    download_file(Q_PROTOCOL_ZMODEM);
}

/* Do download with kermit */
static void download_file_kermit() {
    download_file(Q_PROTOCOL_KERMIT);
}

/* Do upload */
static void upload_file(Q_PROTOCOL protocol) {
    static char * filename = NULL;
    int length;
    int rc;
    struct stat fstats;

    if (local_login == Q_TRUE) {
        do_menu(EOL "Cannot upload on local logon." EOL);
        current_state = MAIN_MENU;
        main_menu();
        return;
    }
    if (q_status.read_only == Q_TRUE) {
        do_menu(EOL "Cannot upload with --read-only set." EOL);
        current_state = MAIN_MENU;
        main_menu();
        return;
    }
    assert(host_online == Q_TRUE);
upload_top:
    if (file_state == FILENAME) {
        DLOG(("upload_file(): FILENAME\n"));

        /*
         * Reset upload state
         */
        clear_filename();

        do_menu(EOL "Enter filename to upload: ");
        file_state = FILENAME_WAIT;
        reset_line_buffer();
        do_line_buffer = Q_TRUE;
        return;
    }

    if (file_state == FILENAME_WAIT) {
        DLOG(("upload_file(): FILENAME_WAIT\n"));

        /*
         * Line buffer has the download filename
         */
        assert(transfer_filename == NULL);
        if (wcslen(line_buffer) == 0) {
            /*
             * User did not enter a filename
             */
            clear_filename();
            file_state = FILENAME;
            current_state = UPLOAD_FILE;
            upload_file_menu();
            return;
        }

        length = wcstombs(NULL, line_buffer, wcslen(line_buffer)) + 1;
        transfer_filename =
            (char *) Xmalloc(sizeof(char) * length, __FILE__, __LINE__);
        memset(transfer_filename, 0, length);
        snprintf(transfer_filename, length, "%ls", line_buffer);
        DLOG(("upload_file(): filename = \'%s\'\n", transfer_filename));

        filename = (char *) Xmalloc(strlen(transfer_filename) +
                                    strlen(get_option(Q_OPTION_HOST_DIR)) + 2,
                                    __FILE__, __LINE__);
        memset(filename, 0,
               strlen(transfer_filename) +
               strlen(get_option(Q_OPTION_HOST_DIR)) + 2);
        strncpy(filename, get_option(Q_OPTION_HOST_DIR),
                strlen(get_option(Q_OPTION_HOST_DIR)));
        filename[strlen(filename)] = '/';
        strncpy(filename + strlen(filename), transfer_filename,
                strlen(transfer_filename));

        rc = stat(filename, &fstats);
        if (rc < 0) {

            if (errno == ENOENT) {
                /*
                 * File does not exist -- all is OK
                 */
                if (q_download_location != NULL) {
                    Xfree(q_download_location, __FILE__, __LINE__);
                }
                switch (protocol) {
                case Q_PROTOCOL_XMODEM:
                    /*
                     * Xmodem: full filename
                     */
                    q_download_location = Xstrdup(filename, __FILE__, __LINE__);
                    break;
                case Q_PROTOCOL_YMODEM:
                case Q_PROTOCOL_ZMODEM:
                case Q_PROTOCOL_KERMIT:
                    /*
                     * Others: q_download_location is HOST_DIR
                     */
                    q_download_location = Xstrdup(get_option(Q_OPTION_HOST_DIR),
                                                  __FILE__, __LINE__);
                    break;
                default:
                    /*
                     * Should never get here
                     */
                    abort();
                }

                q_transfer_stats.protocol = protocol;
                switch_state(Q_STATE_DOWNLOAD);
                file_state = TRANSFER;
                start_file_transfer();

                clear_filename();
                current_state = UPLOAD_FILE;

                /*
                 * No leak
                 */
                Xfree(filename, __FILE__, __LINE__);
                filename = NULL;
                return;
            }

            /*
             * No leak
             */
            Xfree(filename, __FILE__, __LINE__);
            filename = NULL;

            /*
             * Error stat()ing file
             */
            do_menu(EOL "Host mode error checking for file." EOL);
            clear_filename();
            current_state = UPLOAD_FILE;
            upload_file_menu();
            return;
        }

        /*
         * File already exists.  If it's resumable, ask about that, otherwise
         * tell them we can't accept it.
         */
        if ((protocol == Q_PROTOCOL_XMODEM) ||
            (protocol == Q_PROTOCOL_YMODEM)) {

            do_menu(EOL "File already exists, cannot resume with this protocol."
                    EOL);
            clear_filename();
            file_state = FILENAME;
            current_state = UPLOAD_FILE;
            upload_file_menu();
            /*
             * No leak
             */
            Xfree(filename, __FILE__, __LINE__);
            filename = NULL;
            return;
        }
        do_menu(EOL "File already exists, resume? ");
        file_state = FILENAME_RESUME;
        reset_line_buffer();
        do_line_buffer = Q_TRUE;
        return;
    }

    if (file_state == FILENAME_RESUME) {

        DLOG(("upload_file(): FILENAME_RESUME\n"));

        assert(filename != NULL);

        if (wcslen(line_buffer) > 0) {
            if ((line_buffer[0] == 'y') || (line_buffer[0] == 'Y')) {

                DLOG(("upload_file(): resume transfer\n"));

                /*
                 * Resume transfer
                 */
                q_download_location = Xstrdup(get_option(Q_OPTION_HOST_DIR),
                                              __FILE__, __LINE__);

                q_transfer_stats.protocol = protocol;
                switch_state(Q_STATE_DOWNLOAD);
                file_state = TRANSFER;
                start_file_transfer();

                clear_filename();
                current_state = UPLOAD_FILE;

                /*
                 * No leak
                 */
                Xfree(filename, __FILE__, __LINE__);
                filename = NULL;
                return;
            }
        }

        DLOG(("upload_file(): DON'T resume transfer\n"));

        /*
         * Chose not to resume
         */
        clear_filename();
        file_state = FILENAME;
        current_state = UPLOAD_FILE;
        upload_file_menu();

        /*
         * No leak
         */
        Xfree(filename, __FILE__, __LINE__);
        filename = NULL;
        return;
    }

    if (file_state == TRANSFER) {
        DLOG(("upload_file(): TRANSFER\n"));
        file_state = FILENAME;
        goto upload_top;
    }

    /*
     * Should never get here
     */
    abort();
}

/* Do upload with xmodem */
static void upload_file_xmodem() {
    upload_file(Q_PROTOCOL_XMODEM);
}

/* Do upload with ymodem */
static void upload_file_ymodem() {
    upload_file(Q_PROTOCOL_YMODEM);
}

/* Do upload with zmodem */
static void upload_file_zmodem() {
    upload_file(Q_PROTOCOL_ZMODEM);
}

/* Do upload with kermit */
static void upload_file_kermit() {
    upload_file(Q_PROTOCOL_KERMIT);
}

/* Hangup */
static void hangup(char *msg) {
    char * eol_msg = EOL;

    /*
     * Special case: we exit here
     */
    host_write(msg, strlen(msg));
    host_write(eol_msg, strlen(eol_msg));

    if (host_online == Q_TRUE) {
        /*
         * Only close if we're not locally logged in
         */
        assert(local_login == Q_FALSE);
        if (q_status.online == Q_TRUE) {
            assert(q_child_tty_fd != -1);
#ifndef Q_NO_SERIAL
            if (Q_SERIAL_OPEN) {
                if (q_host_type != Q_HOST_TYPE_SERIAL) {
                    /*
                     * Modem: close it to force DCD drop.
                     */
                    close_serial_port();
                }
            } else {
                close_connection();
            }
#else
            close_connection();
#endif /* Q_NO_SERIAL */
        }
    } else {
        assert(local_login == Q_TRUE);
        qlog(_("Host mode local login end.\n"));
    }

    reset_host();

#ifndef Q_NO_SERIAL
    if (q_host_type == Q_HOST_TYPE_MODEM) {
        /*
         * Modem: re-open serial port (to reset).
         */
        open_serial_port();
    }
#endif

    do_menu(_(EOL "Waiting for next call..." EOL));
    qlog(_("Host mode waiting for next call...\n"));
}

/* Hangup the nice way */
static void goodbye() {
    hangup(_("Goodbye!"));
}

/* Page sysop */
static void page_sysop() {
    static time_t page_start;
    static time_t music_start;
    time_t now;

    if (page == Q_FALSE) {
        /*
         * User requested sysop page
         */
        do_menu(EOL " ** Paging sysop... **" EOL);
        /*
         * Refresh the screen BEFORE playing the music.
         */
        refresh_handler();

        current_state = PAGE_SYSOP;
        page = Q_TRUE;
        time(&page_start);
        time(&music_start);
        play_sequence(Q_MUSIC_PAGE_SYSOP);

        return;
    }

    /*
     * Page continues, see if it is time to timeout
     */
    time(&now);
    if (now - page_start >= 15) {
        /*
         * 15 seconds, give up
         */
        current_state = MAIN_MENU;
        page = Q_FALSE;

        do_menu(EOL " ** Sysop did not respond to page. **" EOL);

        /*
         * Re-display the main menu
         */
        main_menu();
        return;
    }

    /*
     * Re-play page tone every 3 seconds
     */
    if (now - music_start >= 3) {
        do_menu(" ** Paging sysop... **" EOL);
        /*
         * Refresh the screen BEFORE playing the music.
         */
        refresh_handler();

        time(&music_start);
        play_sequence(Q_MUSIC_PAGE_SYSOP);
        return;
    }

    /*
     * Keep waiting for the sysop to show up
     */
}

/* Chat mode: enter each line in the line editor until the sysop kills it */
static void chat() {
    char * eol_msg = EOL;
    host_write(eol_msg, strlen(eol_msg));
    save_line_buffer();
    reset_line_buffer();
    do_line_buffer = Q_TRUE;
}

/* Main menu */
static void main_menu() {
    do_menu(EOL
        "Qodem Host Main Menu" EOL
        "--------------------" EOL
        EOL
        " R)ead Messages" EOL
        " E)nter A Message" EOL
        EOL
        " P)age The Sysop" EOL
        EOL
        " F)iles Listing" EOL
        " D)ownload A File" EOL
        " U)pload A File" EOL
        EOL
        " G)oodbye (HangUp)" EOL
        "--------------------" EOL
        EOL
        "Your choice?  ");

    /*
     * Reset read messages state
     */
    clear_all_messages();
    current_message = 0;
}

/* Handle menu keystrokes */
static void state_machine_keyboard_handler(const int keystroke) {
    struct host_state * state;
    int i;
    unsigned char ch;
    char * eol_msg = EOL;

    if (q_key_code_yes(keystroke)) {
        /*
         * Convert the ncurses keystroke to an ASCII char
         */
        switch (keystroke) {
        case Q_KEY_ENTER:
            ch = C_CR;
            break;
        case Q_KEY_BACKSPACE:
            ch = 0x08;
            break;
        case Q_KEY_DC:
            ch = 0x7F;
            break;
        default:
            /*
             * Throw this away
             */
            return;
        }
    } else {
        ch = keystroke & 0xFF;
    }

    /*
     * If we're in the line buffer, do that
     */
    if (do_line_buffer == Q_TRUE) {
        if (line_buffer_char(ch) == Q_FALSE) {
            /*
             * User hasn't finished editing, return here
             */
            return;
        }

        /*
         * The user finished editing, fall into the next state loop.
         */
        do_line_buffer = Q_FALSE;
    }

    /*
     * Loop through all states until we find the NONE state
     */
    i = 0;
    do {
        state = &states[i];

        if ((state->state == current_state) &&
            ((tolower(ch) == state->input) || (state->input == 0))
        ) {
            if (do_line_buffer == Q_FALSE) {
                /*
                 * User made a menu selection, echo it
                 */
                if (isalpha(ch)) {
                    host_write((char *) &ch, 1);
                    host_write(eol_msg, strlen(eol_msg));
                }
            }
            /*
             * We have a match, do it.  Switch state first, because next_fn()
             * might switch again.
             */
            current_state = state->next_state;
            state->next_fn();
            return;
        }

        /*
         * Loop to next state
         */
        i++;
    } while (state->state != NONE);

    /*
     * Did not match any states, NOP
     */
    return;
}

#ifndef Q_NO_SERIAL

/**
 * The modem response line to look at when answering a call.
 */
static char host_modem_message[DIALOG_MESSAGE_SIZE];

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
static void host_modem_data(unsigned char * input, unsigned int input_n,
                            int * remaining, unsigned char * output,
                            unsigned int * output_n,
                            const unsigned int output_max) {
    int i;
    Q_BOOL complete_line = Q_FALSE;
    int new_dce_baud;
    char * begin = (char *) input;
    char * eol_msg = EOL;

    DLOG(("host_modem_data() input_n %d remaining %d\n", input_n, *remaining));

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

    DLOG(("host_modem_data() %d input after stripping leading whitespace: ",
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
             * Clear answer line
             */
            memset(host_modem_message, 0, sizeof(host_modem_message));

            /*
             * Copy what is here to it
             */
            snprintf(host_modem_message, sizeof(host_modem_message),
                     "%s", begin);

            /*
             * Stop looking for a line terminator
             */
            break;
        }
    }

    DLOG(("host_modem_data() %d input after looking for CR: ", *remaining));
    for (i = 0; i < *remaining; i++) {
        DLOG2(("%02x ", begin[i]));
    }
    DLOG2((" | \""));
    for (i = 0; i < *remaining; i++) {
        DLOG2(("%c", begin[i]));
    }
    DLOG2(("\"\n"));

    DLOG(("host_modem_data() host_modem_message = \"%s\"\n",
            host_modem_message));

    if (complete_line == Q_TRUE) {
        DLOG(("   write to console '%s'\n", host_modem_message));
        host_write(host_modem_message, strlen(host_modem_message));
        host_write(eol_msg, strlen(eol_msg));
    }

    switch (current_state) {

    case LISTENING:
        DLOG(("host_modem_data() LISTENING\n"));
        if (strlen(q_modem_config.host_init_string) > output_max - *output_n) {
            /*
             * No room for the message, wait for another cycle.
             */
            break;
        }

        /*
         * Serial port is now open, send command to begin listening for RING.
         */
        for (i = 0; i < strlen(q_modem_config.host_init_string); i++) {
            if (q_modem_config.host_init_string[i] != '^') {
                output[(*output_n)] = q_modem_config.host_init_string[i];
                (*output_n)++;
            } else {
                i++;
                if (i == strlen(q_modem_config.host_init_string)) {
                    output[(*output_n)] = '^';
                    (*output_n)++;
                } else {
                    output[(*output_n)] = q_modem_config.host_init_string[i] - 64;
                    (*output_n)++;
                }
            }
        }

        /*
         * Clear modem message
         */
        memset(host_modem_message, 0, sizeof(host_modem_message));

        /*
         * Toss the input seen so far
         */
        *remaining -= strlen(begin);
        if (*remaining < 0) {
            *remaining = 0;
        }

        /*
         * New state
         */
        current_state = MODEM_LISTENING_FOR_RING;
        break;

    case MODEM_LISTENING_FOR_RING:
        DLOG(("host_modem_data() MODEM_LISTENING_FOR_RING\n"));

        if (complete_line == Q_TRUE) {
            /*
             * Check to see if the modem echo is inside the answer string,
             * e.g. match "ATE1Q0V1M1H0S0=0" inside of "ATE1Q0V1M1H0S0=0^M".
             */
            if ((strstr(q_modem_config.host_init_string, host_modem_message) !=
                    NULL) ||
                (strncasecmp(host_modem_message, "ok", 2) == 0)
            ) {
                /*
                 * Modem echo is on, just discard this part
                 */

                /*
                 * Clear modem message
                 */
                memset(host_modem_message, 0, sizeof(host_modem_message));

                /*
                 * Toss the input seen so far
                 */
                *remaining -= strlen(begin);
                if (*remaining < 0) {
                    *remaining = 0;
                }
            }

            /*
             * Expect RING
             */
            if (strcasecmp(host_modem_message, "ring") == 0) {
                DLOG(("host_modem_data() MODEM_LISTENING_FOR_RING * RING *\n"));
                /*
                 * Got RING.  Send the answer string.
                 */
                if (strlen(q_modem_config.answer_string) >
                                output_max - *output_n) {
                    /*
                     * No room for the message, wait for another cycle.
                     */
                    break;
                }
                for (i = 0; i < strlen(q_modem_config.answer_string);
                     i++) {
                    if (q_modem_config.answer_string[i] != '^') {
                        output[(*output_n)] = q_modem_config.answer_string[i];
                        (*output_n)++;
                    } else {
                        i++;
                        if (i == strlen(q_modem_config.answer_string)) {
                            output[(*output_n)] = '^';
                            (*output_n)++;
                        } else {
                            output[(*output_n)] =
                                q_modem_config.answer_string[i] - 64;
                            (*output_n)++;
                        }
                    }
                }

                /*
                 * Clear modem message
                 */
                memset(host_modem_message, 0, sizeof(host_modem_message));

                /*
                 * Toss the input seen so far
                 */
                *remaining -= strlen(begin);
                if (*remaining < 0) {
                    *remaining = 0;
                }

                /*
                 * New state
                 */
                current_state = MODEM_LISTENING_FOR_CONNECT;
            }
        }

        break;

    case MODEM_LISTENING_FOR_CONNECT:
        DLOG(("host_modem_data() MODEM_LISTENING_FOR_CONNECT\n"));

        if (complete_line == Q_TRUE) {
            DLOG(("host_modem_data() MODEM_LISTENING_FOR_CONNECT line: %s\n",
                    host_modem_message));

            /*
             * Expect CONNECT, NO CARRIER, ERROR, BUSY, or VOICE
             */
            if ((strstr(host_modem_message, "NO DIALTONE") != NULL) ||
                (strstr(host_modem_message, "BUSY") != NULL) ||
                (strstr(host_modem_message, "NO CARRIER") != NULL) ||
                (strstr(host_modem_message, "ERROR") != NULL) ||
                (strstr(host_modem_message, "VOICE") != 0)) {

                DLOG(("host_modem_data() MODEM_LISTENING_FOR_CONNECT fail\n"));

                /*
                 * Uh-oh, restart the answerer.
                 */
                current_state = LISTENING;

                /*
                 * Toss the input seen so far
                 */
                *remaining -= strlen(begin);
                if (*remaining < 0) {
                    *remaining = 0;
                }
            }

            /*
             * Check to see if the modem echo is inside the answer string,
             * e.g. match "ATA" inside of "ATA^M".
             */
            if ((strstr(q_modem_config.answer_string, host_modem_message) !=
                    NULL) ||
                (strstr(host_modem_message, "OK") != NULL)
            ) {
                /*
                 * Modem echo is on, just discard this part.  But keep it on
                 * the display.
                 */

                /*
                 * Toss the input seen so far
                 */
                *remaining -= strlen(begin);
                if (*remaining < 0) {
                    *remaining = 0;
                }
            }

            if (strstr(host_modem_message, "CONNECT") != NULL) {
                /*
                 * Yippee, connect!
                 */
                DLOG(("host_modem_data() *** CONNECT ***\n"));

                /*
                 * Find baud
                 */
                DLOG(("host_modem_message \'%s\'\n", host_modem_message));

                if (sscanf(host_modem_message, "CONNECT %d",
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

                qlog(_("Host mode connection established at %d baud.\n"),
                    q_serial_port.dce_baud);

                /*
                 * Toss the input seen so that it doesn't make it to
                 * do_login().
                 */
                *remaining = 0;

                /*
                 * Change online here so that my caller knows to move to
                 * login.
                 */
                host_online = Q_TRUE;
                current_state = MODEM_CONNECTED;
            }

        }
        break;

    default:
        /*
         * BUG
         */
        assert(1 == 0);
    }

}

#endif /* Q_NO_SERIAL */

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
void host_process_data(unsigned char * input, const unsigned int input_n,
                       int * remaining, unsigned char * output,
                       unsigned int * output_n,
                       const unsigned int output_max) {

    char notify_message[DIALOG_MESSAGE_SIZE];
    unsigned int i;

    DLOG(("host_process_data() : host_online %s\n",
            (host_online == Q_TRUE ? "true" : "false")));

    if ((host_online == Q_TRUE) || (local_login == Q_TRUE)) {
        /*
         * Special case: page the sysop
         */
        if (current_state == PAGE_SYSOP) {
            page_sysop();
        }
    }
#ifndef Q_NO_SERIAL
    if ((host_online == Q_FALSE) &&
        (q_host_type == Q_HOST_TYPE_SERIAL) && (input_n == 0)
    ) {
        /*
         * Serial port host: stay offline until a byte comes in from the
         * other side.
         */
        return;
    }
#endif /* Q_NO_SERIAL */

    if (host_online == Q_TRUE) {
        DLOG(("   -- %d input bytes online %s\n", input_n,
                (q_status.online == Q_TRUE ? "true" : "false")));

        /*
         * See if we really hungup
         */
        if (q_status.online == Q_FALSE) {
            /*
             * Disconnection
             */
            snprintf(notify_message, sizeof(notify_message) - 1,
                     _("%sConnection closed.%s"), EOL, EOL);
            host_write(notify_message, strlen(notify_message));
            q_screen_dirty = Q_TRUE;
            hangup("");
        } else {
            /*
             * Online: pass everything in as keystrokes
             */
            for (i = 0; i < input_n; i++) {
                /*
                 * Apply 8-bit translation
                 */
                input[i] = translate_8bit_in(input[i]);

                /*
                 * Capture
                 */
                if (q_status.capture == Q_TRUE) {
                    if (q_status.capture_type == Q_CAPTURE_TYPE_RAW) {
                        /*
                         * Raw
                         */
                        fprintf(q_status.capture_file, "%c", input[i]);
                        if (q_status.capture_flush_time < time(NULL)) {
                            fflush(q_status.capture_file);
                            q_status.capture_flush_time = time(NULL);
                        }
                    }
                }

                state_machine_keyboard_handler(input[i]);
            }
        }
        *remaining = 0;
        return;
    }

    /*
     * See if we have a TCP connection
     */
    switch (q_host_type) {
    case Q_HOST_TYPE_SOCKET:
    case Q_HOST_TYPE_TELNETD:
#ifdef Q_SSH_CRYPTLIB
    case Q_HOST_TYPE_SSHD:
#endif
        q_child_tty_fd = net_accept();
        if (q_child_tty_fd != -1) {
            /*
             * We've got a connection!
             */
            DLOG(("HOST ONLINE\n"));
            snprintf(notify_message, sizeof(notify_message) - 1,
                _("Incoming connection established from %s port %s...\r\n"),
                net_ip_address(), net_port());
            host_write(notify_message, strlen(notify_message));

            snprintf(notify_message, sizeof(notify_message) - 1,
                _("Incoming connection established from %s port %s\n"),
                net_ip_address(), net_port());
            qlog(notify_message);

            host_online = Q_TRUE;
            q_status.online = Q_TRUE;
            q_screen_dirty = Q_TRUE;
            assert(current_state == LISTENING);
            current_state = LOGIN;
            do_login();
            play_sequence(Q_MUSIC_CONNECT_MODEM);
        }
        return;
#ifndef Q_NO_SERIAL
    case Q_HOST_TYPE_MODEM:
        if ((host_online == Q_FALSE) && (local_login == Q_FALSE)) {
            host_modem_data(input, input_n, remaining,
                            output, output_n, output_max);
            if (host_online == Q_TRUE) {
                /*
                 * We've got a connection!
                 */
                DLOG(("HOST ONLINE\n"));
                snprintf(notify_message, sizeof(notify_message) - 1,
                    _("Incoming connection on modem...\r\n"));
                host_write(notify_message, strlen(notify_message));

                snprintf(notify_message, sizeof(notify_message) - 1,
                    _("Incoming connection on modem...\n"));
                qlog(notify_message);

                q_status.online = Q_TRUE;
                q_screen_dirty = Q_TRUE;
                assert(current_state == MODEM_CONNECTED);
                current_state = LOGIN;
                do_login();
            }
        }
        return;
    case Q_HOST_TYPE_SERIAL:
        if ((host_online == Q_FALSE) && (local_login == Q_FALSE)) {
            /*
             * We've got a connection!
             */
            DLOG(("HOST ONLINE\n"));
            snprintf(notify_message, sizeof(notify_message) - 1,
                     _("Incoming connection on serial port...\r\n"));
            host_write(notify_message, strlen(notify_message));

            snprintf(notify_message, sizeof(notify_message) - 1,
                     _("Incoming connection on serial port...\n"));
            qlog(notify_message);

            host_online = Q_TRUE;
            q_status.online = Q_TRUE;
            q_screen_dirty = Q_TRUE;
            assert(current_state == LISTENING);
            current_state = LOGIN;
            do_login();
            return;
        }
#endif /* Q_NO_SERIAL */
    }
}

/**
 * Keyboard handler for host mode.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
void host_keyboard_handler(const int keystroke, const int flags) {

    /*
     * See if online
     */
    if ((local_login == Q_TRUE) || (host_online == Q_TRUE)) {
        if ((tolower(keystroke) == 'c') && ((flags & KEY_FLAG_ALT) != 0)) {
            /*
             * Break in/out chat
             */
            /*
             * Reset sysop page flag
             */
            page = Q_FALSE;
            if (current_state != CHAT) {
                /*
                 * Breaking into chat
                 */
                chat_previous_state = current_state;
                chat_previous_line_buffer = do_line_buffer;
                do_menu(EOL
                    "------------------------" EOL
                    " ***  Entering Chat  ***" EOL
                    "------------------------" EOL);
                current_state = CHAT;
                chat();
                qlog(_("Entering sysop chat.\n"));
            } else {
                do_menu(EOL
                    "------------------------" EOL
                    " ***  Leaving Chat   ***" EOL
                    "------------------------" EOL);
                restore_line_buffer();
                current_state = chat_previous_state;
                do_line_buffer = chat_previous_line_buffer;
                qlog(_("Leaving sysop chat.\n"));
            }
            return;
        }

        if ((tolower(keystroke) == 'h') && ((flags & KEY_FLAG_ALT) != 0)) {
            /*
             * Hangup
             */
            hangup(_("Force Hangup"));
            return;
        }

        state_machine_keyboard_handler(keystroke);
        return;
    }

    /*
     * Had better be listening at this point
     */
    assert((current_state == LISTENING) ||
        (current_state == MODEM_LISTENING_FOR_RING) ||
        (current_state == MODEM_LISTENING_FOR_CONNECT));

    switch (keystroke) {
    case 'L':
    case 'l':
        /*
         * Local login
         */
        local_login = Q_TRUE;
        current_state = MAIN_MENU;
        main_menu();
        qlog(_("Host mode local login begins.\n"));
        break;

    case '`':
        /*
         * Backtick works too
         */
    case Q_KEY_ESCAPE:
        /*
         * Stop host mode
         */
        host_stop();

        /*
         * Return to TERMINAL mode
         */
        switch_state(Q_STATE_CONSOLE);
        return;

    default:
        /*
         * Ignore keystroke
         */
        break;
    }
}

/**
 * Draw screen for host mode.
 */
void host_refresh() {
    char * status_string;
    int status_left_stop;

    if (q_screen_dirty == Q_FALSE) {
        return;
    }

    /*
     * Render the scrollback.
     */
    render_scrollback(0);

    /*
     * Status line.
     */
    if (current_state == PAGE_SYSOP) {
        status_string =
            _(" *** PAGING SYSOP ***       Alt-C-Chat   Alt-H-Hangup Caller ");
    } else if (local_login == Q_TRUE) {
        status_string =
            _(" Host Mode - Local Logon    Alt-C-Chat   Alt-H-Hangup Caller ");
    } else if ((host_online == Q_TRUE) && (sysop_chat == Q_FALSE)) {
        status_string =
            _(" Host Mode - Remote Logon   Alt-C-Chat   Alt-H-Hangup Caller ");
    } else if ((host_online == Q_TRUE) && (sysop_chat == Q_TRUE)) {
        status_string = _(" Host Mode - Sysop Chat     Alt-C-End Chat ");
    } else {
        status_string = _(" Host Mode   L-Local Logon   ESC/`-Exit Host ");
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

    /*
     * Drop the cursor
     */
    if (q_scrollback_current->double_width == Q_TRUE) {
        screen_move_yx(q_status.cursor_y, (2 * q_status.cursor_x));
    } else {
        screen_move_yx(q_status.cursor_y, q_status.cursor_x);
    }

    screen_flush();
    q_screen_dirty = Q_FALSE;
}
