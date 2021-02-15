/*
 * protocols.c
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
 * The download sequence:
 *
 *   STATE = Q_STATE_DOWNLOAD_MENU
 *     protocol_menu_refresh()
 *     protocol_menu_keyboard_handler()
 *   STATE = Q_STATE_DOWNLOAD_PATHDIALOG
 *     protocol_pathdialog_refresh()
 *     protocol_pathdialog_keyboard_handler(): NOP
 *     setup_transfer_program()
 *   STATE = Q_STATE_DOWNLOAD
 *     protocol_transfer_refresh()
 *       state.state = Q_TRANSFER_STATE_INIT
 *     process_transfer_stats()
 *       state.state = Q_TRANSFER_STATE_FILE_INFO
 *       state.state = Q_TRANSFER_STATE_TRANSFER
 *       state.state = Q_TRANSFER_STATE_FILE_DONE
 *     protocol_transfer_keyboard_handler()
 *     close_transfer_program()
 *       state.state = Q_TRANSFER_STATE_END
 *
 * The upload sequence:
 *
 *   STATE = Q_STATE_UPLOAD_MENU
 *     protocol_menu_refresh()
 *     protocol_menu_keyboard_handler()
 *   STATE = Q_STATE_UPLOAD_PATHDIALOG
 *     protocol_pathdialog_refresh()
 *     protocol_pathdialog_keyboard_handler(): NOP
 *     setup_transfer_program()
 *   STATE = Q_STATE_UPLOAD
 *     protocol_transfer_refresh()
 *       state.state = Q_TRANSFER_STATE_INIT
 *     process_transfer_stats()
 *       state.state = Q_TRANSFER_STATE_FILE_INFO
 *       state.state = Q_TRANSFER_STATE_TRANSFER
 *       state.state = Q_TRANSFER_STATE_FILE_DONE
 *     protocol_transfer_keyboard_handler()
 *     close_transfer_program()
 *       state.state = Q_TRANSFER_STATE_END
 *
 * The BATCH upload sequence:
 *
 *   STATE = Q_STATE_UPLOAD_MENU
 *     protocol_menu_refresh()
 *     protocol_menu_keyboard_handler()
 *   STATE = Q_STATE_UPLOAD_PATHDIALOG
 *     protocol_pathdialog_refresh()
 *       STATE = Q_STATE_UPLOAD_BATCH_DIALOG
 *         batch_entry_window()
 *           protocol_pathdialog_refresh()
 *     protocol_pathdialog_keyboard_handler(): NOP
 *   STATE = Q_STATE_UPLOAD_BATCH
 *     setup_transfer_program()
 *   STATE = Q_STATE_UPLOAD_BATCH
 *     protocol_transfer_refresh()
 *       setup_transfer_program()
 *       state.state = Q_TRANSFER_STATE_INIT
 *     process_transfer_stats()
 *       state.state = Q_TRANSFER_STATE_FILE_INFO
 *       state.state = Q_TRANSFER_STATE_TRANSFER
 *       state.state = Q_TRANSFER_STATE_FILE_DONE
 *     protocol_transfer_keyboard_handler()
 *     close_transfer_program()
 *       state.state = Q_TRANSFER_STATE_END
 *   STATE = Q_STATE_CONSOLE
 *
 */

#include "qcurses.h"
#include "common.h"
#include <stdlib.h>
#include <errno.h>
#ifndef Q_PDCURSES_WIN32
#  include <sys/resource.h>
#  include <sys/wait.h>
#  include <sys/ioctl.h>
#  include <sys/statvfs.h>
#  include <unistd.h>
#endif

#include <libgen.h>
#include <sys/time.h>
#include <string.h>
#include <assert.h>
#ifdef __linux
#  include <sys/statfs.h>
#endif
#include "screen.h"
#include "qodem.h"
#include "console.h"
#include "xmodem.h"
#include "zmodem.h"
#include "kermit.h"
#include "translate.h"
#include "options.h"
#include "help.h"
#include "states.h"
#include "protocols.h"

/**
 * Transfer statistics.  Lots of places need to peek into this structure.
 */
struct q_transfer_stats_struct q_transfer_stats = {
    Q_TRANSFER_STATE_INIT,
    Q_PROTOCOL_ASCII,
    NULL,
    NULL,
    NULL,
    NULL,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/**
 * Download location file or directory.
 */
char * q_download_location = NULL;

/* List of files for batch upload */
static struct file_info * batch_upload_file_list = NULL;

/* Current file # for batch upload */
static int batch_upload_file_list_i;

/**
 * Save a list of files to the batch upload window data file.  This is used
 * by host mode to perform an upload ("download" to the remote side).
 *
 * @param upload the new list of files
 */
void set_batch_upload(struct file_info * upload) {
    batch_upload_file_list = upload;
    if (batch_upload_file_list != NULL) {
        batch_upload_file_list_i = 0;
    }
}

/* ------------------------------------------------------------------------
 * ASCII transfer support -------------------------------------------------
 * ------------------------------------------------------------------------
 *
 * Rather than spill to a seperate module, it's so simple I'll just leave it
 * here.
 */

typedef enum {
    ASCII_XFER_STATE_OK,
    ASCII_XFER_STATE_ABORT
} ASCII_XFER_STATE;

typedef enum {
    ASCII_XFER_CRLF_NONE,
    ASCII_XFER_CRLF_STRIP,
    ASCII_XFER_CRLF_ADD
} ASCII_XFER_CRLF_POLICY;

static ASCII_XFER_STATE ascii_xfer_state;
static FILE * ascii_xfer_file = NULL;
static char * ascii_xfer_filename = NULL;
static Q_BOOL ascii_xfer_sending;
static Q_BOOL ascii_xfer_upload_use_xlate_table = Q_FALSE;
static Q_BOOL ascii_xfer_download_use_xlate_table = Q_FALSE;
static ASCII_XFER_CRLF_POLICY ascii_xfer_upload_cr_handling =
    ASCII_XFER_CRLF_NONE;
static ASCII_XFER_CRLF_POLICY ascii_xfer_upload_lf_handling =
    ASCII_XFER_CRLF_NONE;
static ASCII_XFER_CRLF_POLICY ascii_xfer_download_cr_handling =
    ASCII_XFER_CRLF_NONE;
static ASCII_XFER_CRLF_POLICY ascii_xfer_download_lf_handling =
    ASCII_XFER_CRLF_NONE;

/**
 * Perform CRLF handling on an ASCII transfer buffer.
 *
 * @param input the bytes from the remote side
 * @param input_n the number of bytes in input_n
 * @param output a buffer to contain the bytes to send to the remote side
 * @param output_n the number of bytes that this function wrote to output
 * @param cr_policy selection for CR (strip, add, or leave as-is)
 * @param lf_policy selection for LF (strip, add, or leave as-is)
 */
static void ascii_transfer_crlf_handling(const unsigned char * input,
                                         const unsigned int input_n,
                                         unsigned char * output,
                                         unsigned int * output_n,
                                         const ASCII_XFER_CRLF_POLICY cr_policy,
                                         const ASCII_XFER_CRLF_POLICY
                                         lf_policy) {

    unsigned int i, j;

    /*
     * Check if we need to do anything
     */
    if ((cr_policy == ASCII_XFER_CRLF_NONE) &&
        (lf_policy == ASCII_XFER_CRLF_NONE)) {
        /*
         * Nothing to do
         */
        memcpy(output, input, input_n);
        *output_n = input_n;
        return;
    }

    /*
     * Iterate through the buffer
     */
    i = 0;
    j = 0;

    while (i < input_n) {

        if (input[i] == C_CR) {

            /*
             * CR
             */
            if (cr_policy == ASCII_XFER_CRLF_STRIP) {
                i++;
                continue;
            }
            if (cr_policy == ASCII_XFER_CRLF_ADD) {
                output[j] = input[i];
                j++;
                output[j] = C_LF;
                j++;
                i++;
                continue;
            }
        }

        if (input[i] == C_LF) {

            /*
             * LF
             */
            if (lf_policy == ASCII_XFER_CRLF_STRIP) {
                i++;
                continue;
            }
            if (lf_policy == ASCII_XFER_CRLF_ADD) {
                output[j] = C_CR;
                j++;
                output[j] = input[i];
                j++;
                i++;
                continue;
            }
        }

        /*
         * Copy the byte over
         */
        output[j] = input[i];
        j++;
        i++;
    }

    /*
     * Set the output buffer size
     */
    *output_n = j;
}

/**
 * Process raw bytes from the remote side through the transfer protocol.  See
 * also protocol_process_data().
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
static void ascii_transfer(unsigned char * input, const unsigned int input_n,
                           int * remaining, unsigned char * output,
                           unsigned int * output_n,
                           const unsigned int output_max) {

    unsigned char working_buffer[2 * Q_BUFFER_SIZE];
    unsigned int working_buffer_n;
    char notify_message[DIALOG_MESSAGE_SIZE];
    int rc;
    unsigned int i;

    /*
     * Check my input arguments
     */
    assert(input_n >= 0);
    assert(input != NULL);
    assert(output != NULL);
    assert(output_max >= 1);

    if (ascii_xfer_state == ASCII_XFER_STATE_ABORT) {
        return;
    }

    /*
     * Clear working buffer
     */
    memset(working_buffer, 0, sizeof(working_buffer));
    working_buffer_n = 0;

    if (ascii_xfer_sending == Q_TRUE) {
        /*
         * If the outgoing transfer buffer is full, abort
         */
        if (output_max - *output_n < Q_BUFFER_SIZE) {
            return;
        }

        /*
         * Send the next few bytes
         */
        rc = fread(working_buffer, 1, (output_max / 2) - 1, ascii_xfer_file);
        working_buffer_n = rc;

        /*
         * Perform CRLF handling.
         */
        ascii_transfer_crlf_handling(working_buffer, working_buffer_n, output,
                                     output_n, ascii_xfer_upload_cr_handling,
                                     ascii_xfer_upload_lf_handling);

        q_transfer_stats.bytes_transfer += *output_n;

        /*
         * Perform translation table processing
         */
        if (ascii_xfer_upload_use_xlate_table == Q_TRUE) {
            for (i = 0; i < (*output_n); i++) {
                output[i] = translate_8bit_out(output[i]);
            }
        }

        if (ferror(ascii_xfer_file)) {
            /*
             * Error
             */
            char notify_message[DIALOG_MESSAGE_SIZE];
            snprintf(notify_message, sizeof(notify_message),
                     _("Error reading from file \"%s\": %s"),
                     ascii_xfer_filename, strerror(errno));
            notify_form(notify_message, 0);

            stop_file_transfer(Q_TRANSFER_STATE_ABORT);
            ascii_xfer_state = ASCII_XFER_STATE_ABORT;
            return;
        }

        if (feof(ascii_xfer_file)) {
            /*
             * End of file
             */
            stop_file_transfer(Q_TRANSFER_STATE_END);
            time(&q_transfer_stats.end_time);
            q_screen_dirty = Q_TRUE;

            /*
             * Don't do everything again
             */
            ascii_xfer_state = ASCII_XFER_STATE_ABORT;
        }

    } else {

        /*
         * Perform translation table processing
         */
        if (ascii_xfer_download_use_xlate_table == Q_TRUE) {
            for (i = 0; i < input_n; i++) {
                input[i] = translate_8bit_in(input[i]);
            }
        }

        /*
         * Perform CRLF handling.
         */
        ascii_transfer_crlf_handling(input, input_n, working_buffer,
                                     &working_buffer_n,
                                     ascii_xfer_download_cr_handling,
                                     ascii_xfer_download_lf_handling);

        /*
         * Save the input bytes to file
         */
        rc = fwrite(working_buffer, 1, working_buffer_n, ascii_xfer_file);
        if (ferror(ascii_xfer_file)) {
            snprintf(notify_message, sizeof(notify_message),
                     _("Error writing to file \"%s\": %s"), ascii_xfer_filename,
                     strerror(errno));
            notify_form(notify_message, 0);

            stop_file_transfer(Q_TRANSFER_STATE_ABORT);
            ascii_xfer_state = ASCII_XFER_STATE_ABORT;
            return;
        } else {
            /*
             * Flush it
             */
            fflush(ascii_xfer_file);

            if (rc < working_buffer_n) {
                /*
                 * Short file, filesystem is probably full
                 */
                snprintf(notify_message, sizeof(notify_message),
                         _("Error writing to file \"%s\": %s"),
                         ascii_xfer_filename, strerror(errno));
                notify_form(notify_message, 0);

                stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                ascii_xfer_state = ASCII_XFER_STATE_ABORT;
                return;
            }
        }
    }

    /*
     * Run the input side through the console
     */
    console_process_incoming_data(input, input_n, remaining);

    /*
     * ...and refresh the display
     */
    q_screen_dirty = Q_TRUE;
    console_refresh(Q_FALSE);
}

/**
 * Setup for a new ASCII file transfer.
 *
 * @param in_filename the filename to save downloaded file data to, or the
 * name of the file to upload.
 * @param send if true, this is an upload
 */
static Q_BOOL ascii_transfer_start(const char * in_filename,
                                   const Q_BOOL send) {

    struct stat fstats;

    /*
     * Assume we don't start up successfully
     */
    ascii_xfer_state = ASCII_XFER_STATE_ABORT;

    /*
     * Pull the options
     */
    if (strcasecmp
        (get_option(Q_OPTION_ASCII_UPLOAD_USE_TRANSLATE_TABLE), "true") == 0) {
        ascii_xfer_upload_use_xlate_table = Q_TRUE;
    }
    if (strcasecmp(get_option(Q_OPTION_ASCII_UPLOAD_CR_POLICY), "strip") == 0) {
        ascii_xfer_upload_cr_handling = ASCII_XFER_CRLF_STRIP;
    }
    if (strcasecmp(get_option(Q_OPTION_ASCII_UPLOAD_CR_POLICY), "add") == 0) {
        ascii_xfer_upload_cr_handling = ASCII_XFER_CRLF_ADD;
    }
    if (strcasecmp(get_option(Q_OPTION_ASCII_UPLOAD_LF_POLICY), "strip") == 0) {
        ascii_xfer_upload_lf_handling = ASCII_XFER_CRLF_STRIP;
    }
    if (strcasecmp(get_option(Q_OPTION_ASCII_UPLOAD_LF_POLICY), "add") == 0) {
        ascii_xfer_upload_lf_handling = ASCII_XFER_CRLF_ADD;
    }
    if (strcasecmp
        (get_option(Q_OPTION_ASCII_DOWNLOAD_USE_TRANSLATE_TABLE),
         "true") == 0) {
        ascii_xfer_download_use_xlate_table = Q_TRUE;
    }
    if (strcasecmp
        (get_option(Q_OPTION_ASCII_DOWNLOAD_CR_POLICY), "strip") == 0) {
        ascii_xfer_download_cr_handling = ASCII_XFER_CRLF_STRIP;
    }
    if (strcasecmp(get_option(Q_OPTION_ASCII_DOWNLOAD_CR_POLICY), "add") == 0) {
        ascii_xfer_download_cr_handling = ASCII_XFER_CRLF_ADD;
    }
    if (strcasecmp
        (get_option(Q_OPTION_ASCII_DOWNLOAD_LF_POLICY), "strip") == 0) {
        ascii_xfer_download_lf_handling = ASCII_XFER_CRLF_STRIP;
    }
    if (strcasecmp(get_option(Q_OPTION_ASCII_DOWNLOAD_LF_POLICY), "add") == 0) {
        ascii_xfer_download_lf_handling = ASCII_XFER_CRLF_ADD;
    }

    if (send == Q_TRUE) {
        /*
         * Pull the file size
         */
        if (stat(in_filename, &fstats) < 0) {
            return Q_FALSE;
        }

        if ((ascii_xfer_file = fopen(in_filename, "rb")) == NULL) {
            return Q_FALSE;
        }

        q_transfer_stats.bytes_total = fstats.st_size;
        q_transfer_stats.block_size = 128;
        q_transfer_stats.blocks = fstats.st_size / 128;
        if ((q_transfer_stats.blocks % 128) > 0) {
            q_transfer_stats.blocks++;
        }

    } else {
        if ((ascii_xfer_file = fopen(in_filename, "w+b")) == NULL) {
            return Q_FALSE;
        }
    }

    ascii_xfer_filename = Xstrdup(in_filename, __FILE__, __LINE__);
    ascii_xfer_sending = send;

    /*
     * Let's go!
     */
    ascii_xfer_state = ASCII_XFER_STATE_OK;

    return Q_TRUE;
}

/**
 * Stop the ASCII file transfer.  Note that this function is only called in
 * stop_file_transfer() and save_partial is always true.  However it is left
 * in for API completeness.
 *
 * @param save_partial if true, save any partially-downloaded files.
 */
static void ascii_transfer_stop(const Q_BOOL save_partial) {

    if ((save_partial == Q_TRUE) || (ascii_xfer_sending == Q_TRUE)) {
        if (ascii_xfer_file != NULL) {
            fflush(ascii_xfer_file);
            fclose(ascii_xfer_file);
        }
    } else {
        if (ascii_xfer_file != NULL) {
            fclose(ascii_xfer_file);
            unlink(ascii_xfer_filename);
        }
    }
    ascii_xfer_file = NULL;
    if (ascii_xfer_filename != NULL) {
        Xfree(ascii_xfer_filename, __FILE__, __LINE__);
    }
    ascii_xfer_filename = NULL;
}

/* ------------------------------------------------------------------------
 * ASCII transfer support -------------------------------------------------
 * ------------------------------------------------------------------------ */

/**
 * Set the exposed protocol name.  Allocates a copy of the string which is
 * freed when the program state is switched to Q_STATE_CONSOLE.
 *
 * @param new_string the new protocol name
 */
void set_transfer_stats_protocol_name(const char * new_string) {
    /*
     * Do nothing if being set to what it already is.  Note this is a pointer
     * comparison, not a string comparison.
     */
    if (new_string == q_transfer_stats.protocol_name) {
        return;
    }

    if (q_transfer_stats.protocol_name != NULL) {
        Xfree(q_transfer_stats.protocol_name, __FILE__, __LINE__);
    }
    q_transfer_stats.protocol_name = Xstrdup(new_string, __FILE__, __LINE__);
}

/**
 * Set the exposed filename.  Allocates a copy of the string which is freed
 * when the program state is switched to Q_STATE_CONSOLE.
 *
 * @param new_string the new filname
 */
void set_transfer_stats_filename(const char * new_string) {
    /*
     * Do nothing if being set to what it already is.  Note this is a pointer
     * comparison, not a string comparison.
     */
    if (new_string == q_transfer_stats.filename) {
        return;
    }

    if (q_transfer_stats.filename != NULL) {
        Xfree(q_transfer_stats.filename, __FILE__, __LINE__);
    }
    q_transfer_stats.filename = Xstrdup(new_string, __FILE__, __LINE__);
}

/**
 * Set the exposed path name.  Allocates a copy of the string which is freed
 * when the program state is switched to Q_STATE_CONSOLE.
 *
 * @param new_string the new path name
 */
void set_transfer_stats_pathname(const char * new_string) {
    /*
     * Do nothing if being set to what it already is.  Note this is a pointer
     * comparison, not a string comparison.
     */
    if (new_string == q_transfer_stats.pathname) {
        return;
    }

    if (q_transfer_stats.pathname != NULL) {
        Xfree(q_transfer_stats.pathname, __FILE__, __LINE__);
    }
    q_transfer_stats.pathname = Xstrdup(new_string, __FILE__, __LINE__);
}

/**
 * Set the exposed message.  Allocates a copy of the string which is freed
 * when the program state is switched to Q_STATE_CONSOLE.
 *
 * @param new_string the new message
 */
void set_transfer_stats_last_message(const char * format, ...) {
    char outbuf[DIALOG_MESSAGE_SIZE];
    va_list arglist;
    memset(outbuf, 0, sizeof(outbuf));
    va_start(arglist, format);
    vsprintf((char *) (outbuf + strlen(outbuf)), format, arglist);
    va_end(arglist);

    if (q_transfer_stats.last_message != NULL) {
        Xfree(q_transfer_stats.last_message, __FILE__, __LINE__);
    }
    q_transfer_stats.last_message = Xstrdup(outbuf, __FILE__, __LINE__);

    /*
     * Report the message immediately
     */
    q_screen_dirty = Q_TRUE;
}

/**
 * Process raw bytes from the remote side through the transfer protocol.
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
void protocol_process_data(unsigned char * input, const unsigned int input_n,
                           int * remaining, unsigned char * output,
                           unsigned int * output_n,
                           const unsigned int output_max) {

    if ((q_transfer_stats.state == Q_TRANSFER_STATE_ABORT) ||
        (q_transfer_stats.state == Q_TRANSFER_STATE_END)) {
        return;
    }

    switch (q_transfer_stats.protocol) {

    case Q_PROTOCOL_ASCII:
        ascii_transfer(input, input_n, remaining, output, output_n, output_max);
        break;
    case Q_PROTOCOL_KERMIT:
        /*
         * Kermit - I don't need to pass remaining because kermit.c does its
         * own buffering in packet_buffer .
         */
        kermit(input, input_n, output, output_n, output_max);
        /*
         * Kermit always disposes of everything sent to it.
         */
        *remaining = 0;
        break;
    case Q_PROTOCOL_XMODEM:
    case Q_PROTOCOL_XMODEM_CRC:
    case Q_PROTOCOL_XMODEM_RELAXED:
    case Q_PROTOCOL_XMODEM_1K:
    case Q_PROTOCOL_YMODEM:
    case Q_PROTOCOL_XMODEM_1K_G:
    case Q_PROTOCOL_YMODEM_G:
        /*
         * Yes, ALL of these protocols use the same function to transmit
         * data.
         */
        xmodem(input, input_n, remaining, output, output_n, output_max);
        break;
    case Q_PROTOCOL_ZMODEM:
        /*
         * Zmodem - I don't need to pass remaining because zmodem.c does its
         * own buffering in packet_buffer .
         */
        zmodem(input, input_n, output, output_n, output_max);
        /*
         * Zmodem always disposes of everything sent to it.
         */
        *remaining = 0;
        break;
    }

}

/**
 * Reset the transfer statistics.
 */
static void clear_stats() {
    int i;

    if (q_transfer_stats.filename != NULL) {
        Xfree(q_transfer_stats.filename, __FILE__, __LINE__);
        q_transfer_stats.filename = NULL;
    }
    if (q_transfer_stats.pathname != NULL) {
        Xfree(q_transfer_stats.pathname, __FILE__, __LINE__);
        q_transfer_stats.pathname = NULL;
    }
    if (q_transfer_stats.protocol_name != NULL) {
        Xfree(q_transfer_stats.protocol_name, __FILE__, __LINE__);
        q_transfer_stats.protocol_name = NULL;
    }
    if (q_transfer_stats.last_message != NULL) {
        Xfree(q_transfer_stats.last_message, __FILE__, __LINE__);
        q_transfer_stats.last_message = NULL;
    }
    q_transfer_stats.state = Q_TRANSFER_STATE_INIT;
    q_transfer_stats.bytes_total = 0;
    q_transfer_stats.bytes_transfer = 0;
    q_transfer_stats.error_count = 0;
    q_transfer_stats.blocks = 0;
    q_transfer_stats.block_size = 0;
    q_transfer_stats.blocks_transfer = 0;
    q_transfer_stats.batch_bytes_total = 0;
    q_transfer_stats.batch_bytes_transfer = 0;

    /*
     * Batch uploads: calculate q_transfer_stats.batch_bytes_total
     */
    if (q_program_state == Q_STATE_UPLOAD_BATCH) {
        for (i = 0;; i++) {
            if (batch_upload_file_list[i].name == NULL) {
                break;
            }
            q_transfer_stats.batch_bytes_total +=
                batch_upload_file_list[i].fstats.st_size;
        }
    }
    if (q_program_state == Q_STATE_UPLOAD) {
        struct stat fstats;
        if (q_transfer_stats.filename != NULL) {
            if (stat(q_transfer_stats.filename, &fstats) < 0) {
                fprintf(stderr, "Can't stat %s: %s\n",
                        q_transfer_stats.filename, strerror(errno));
            }
            q_transfer_stats.bytes_total = fstats.st_size;
        }
    }

}

/**
 * Start a file transfer.  Usually this is done in one of the protocol
 * dialogs, but the console can do it for Zmodem/Kermit autostart, and host
 * mode can do it.
 *
 * For ASCII and Xmodem, q_download_location is a full filename.
 *
 * For Kermit, Ymodem, and Zmodem, q_download_location is a path name.
 */
void start_file_transfer() {
    char * basename_arg;
    char * dirname_arg;

    /*
     * Refresh the background
     */
    q_screen_dirty = Q_TRUE;
    console_refresh(Q_FALSE);

    /*
     * Clear stats
     */
    clear_stats();

    /*
     * Set protocol name
     */
    switch (q_transfer_stats.protocol) {

    case Q_PROTOCOL_ASCII:
        set_transfer_stats_protocol_name(_("ASCII"));
        break;
    case Q_PROTOCOL_KERMIT:
        set_transfer_stats_protocol_name(_("Kermit"));
        break;
    case Q_PROTOCOL_XMODEM:
        set_transfer_stats_protocol_name(_("Xmodem"));
        break;
    case Q_PROTOCOL_XMODEM_CRC:
        set_transfer_stats_protocol_name(_("Xmodem CRC"));
        break;
    case Q_PROTOCOL_XMODEM_RELAXED:
        set_transfer_stats_protocol_name(_("Xmodem Relaxed"));
        break;
    case Q_PROTOCOL_XMODEM_1K:
        set_transfer_stats_protocol_name(_("XMODEM_1K"));
        break;
    case Q_PROTOCOL_XMODEM_1K_G:
        set_transfer_stats_protocol_name(_("Xmodem-1K/G"));
        break;
    case Q_PROTOCOL_YMODEM:
        set_transfer_stats_protocol_name(_("Ymodem Batch"));
        break;
    case Q_PROTOCOL_YMODEM_G:
        set_transfer_stats_protocol_name(_("Ymodem/G Batch"));
        break;
    case Q_PROTOCOL_ZMODEM:
        set_transfer_stats_protocol_name(_("Zmodem Batch"));
        break;
    }

    /*
     * Log it
     */
    switch (q_transfer_stats.protocol) {

    case Q_PROTOCOL_ASCII:
    case Q_PROTOCOL_XMODEM:
    case Q_PROTOCOL_XMODEM_CRC:
    case Q_PROTOCOL_XMODEM_RELAXED:
    case Q_PROTOCOL_XMODEM_1K:
    case Q_PROTOCOL_XMODEM_1K_G:
        /*
         * Single-file protocols
         */
        if (q_program_state == Q_STATE_DOWNLOAD) {
            qlog(_("DOWNLOAD BEGIN: protocol %s, filename %s\n"),
                 q_transfer_stats.protocol_name, q_download_location);
        } else {
            qlog(_("UPLOAD BEGIN: protocol %s, filename %s\n"),
                 q_transfer_stats.protocol_name, q_download_location);
        }
        break;

    default:
        /*
         * Batch protocols
         */
        if (q_program_state == Q_STATE_DOWNLOAD) {
            qlog(_("DOWNLOAD BEGIN: protocol %s\n"),
                 q_transfer_stats.protocol_name);
        } else {
            qlog(_("UPLOAD BEGIN: protocol %s\n"),
                 q_transfer_stats.protocol_name);
        }
        break;
    }

    /*
     * Cursor off
     */
    q_cursor_off();

    if (q_program_state != Q_STATE_UPLOAD_BATCH) {
        /*
         * Strip trailing '/' from q_download_location
         */
        while (q_download_location[strlen(q_download_location) - 1] == '/') {
            q_download_location[strlen(q_download_location) - 1] = '\0';
        }
    }

    /*
     * Start the protocols
     */
    switch (q_transfer_stats.protocol) {

    case Q_PROTOCOL_ASCII:
        if (ascii_transfer_start
            (q_download_location,
             ((q_program_state == Q_STATE_UPLOAD) ? Q_TRUE : Q_FALSE)) ==
            Q_FALSE) {
            /*
             * Couldn't start the protocol, switch back to console.
             */
            switch_state(Q_STATE_CONSOLE);
            return;
        }
        break;
    case Q_PROTOCOL_KERMIT:
        if (kermit_start
            (batch_upload_file_list, q_download_location,
             ((q_program_state == Q_STATE_DOWNLOAD) ? Q_FALSE : Q_TRUE)) ==
            Q_FALSE) {
            /*
             * Couldn't start the protocol, switch back to console or host
             * mode.
             */
            if ((original_state == Q_STATE_HOST) ||
                (original_state == Q_STATE_CONSOLE)) {
                switch_state(original_state);
            } else {
                switch_state(Q_STATE_CONSOLE);
            }
            return;
        }
        break;
    case Q_PROTOCOL_XMODEM:
        if (xmodem_start
            (q_download_location,
             ((q_program_state == Q_STATE_UPLOAD) ? Q_TRUE : Q_FALSE),
             X_NORMAL) == Q_FALSE) {
            /*
             * Couldn't start the protocol, switch back to console or host
             * mode.
             */
            if ((original_state == Q_STATE_HOST) ||
                (original_state == Q_STATE_CONSOLE)) {
                switch_state(original_state);
            } else {
                switch_state(Q_STATE_CONSOLE);
            }
            return;
        }
        break;
    case Q_PROTOCOL_XMODEM_CRC:
        if (xmodem_start
            (q_download_location,
             ((q_program_state == Q_STATE_UPLOAD) ? Q_TRUE : Q_FALSE),
             X_CRC) == Q_FALSE) {
            /*
             * Couldn't start the protocol, switch back to console.
             */
            switch_state(Q_STATE_CONSOLE);
            return;
        }
        break;
    case Q_PROTOCOL_XMODEM_RELAXED:
        if (xmodem_start
            (q_download_location,
             ((q_program_state == Q_STATE_UPLOAD) ? Q_TRUE : Q_FALSE),
             X_RELAXED) == Q_FALSE) {
            /*
             * Couldn't start the protocol, switch back to console.
             */
            switch_state(Q_STATE_CONSOLE);
            return;
        }
        break;
    case Q_PROTOCOL_XMODEM_1K:
        if (xmodem_start
            (q_download_location,
             ((q_program_state == Q_STATE_UPLOAD) ? Q_TRUE : Q_FALSE),
             X_1K) == Q_FALSE) {
            /*
             * Couldn't start the protocol, switch back to console.
             */
            switch_state(Q_STATE_CONSOLE);
            return;
        }
        break;
    case Q_PROTOCOL_XMODEM_1K_G:
        if (xmodem_start
            (q_download_location,
             ((q_program_state == Q_STATE_UPLOAD) ? Q_TRUE : Q_FALSE),
             X_1K_G) == Q_FALSE) {
            /*
             * Couldn't start the protocol, switch back to console.
             */
            switch_state(Q_STATE_CONSOLE);
            return;
        }
        break;
    case Q_PROTOCOL_YMODEM:
        if (ymodem_start
            (batch_upload_file_list, q_download_location,
             ((q_program_state == Q_STATE_UPLOAD_BATCH) ? Q_TRUE : Q_FALSE),
             Y_NORMAL) == Q_FALSE) {
            /*
             * Couldn't start the protocol, switch back to console or host
             * mode.
             */
            if ((original_state == Q_STATE_HOST) ||
                (original_state == Q_STATE_CONSOLE)) {
                switch_state(original_state);
            } else {
                switch_state(Q_STATE_CONSOLE);
            }
            return;
        }
        break;
    case Q_PROTOCOL_YMODEM_G:
        if (ymodem_start
            (batch_upload_file_list, q_download_location,
             ((q_program_state == Q_STATE_DOWNLOAD) ? Q_FALSE : Q_TRUE),
             Y_G) == Q_FALSE) {
            /*
             * Couldn't start the protocol, switch back to console.
             */
            switch_state(Q_STATE_CONSOLE);
            return;
        }
        break;
    case Q_PROTOCOL_ZMODEM:
        /*
         * By default, always use CRC32
         */
        if (zmodem_start
            (batch_upload_file_list, q_download_location,
             ((q_program_state == Q_STATE_DOWNLOAD) ? Q_FALSE : Q_TRUE),
             Z_CRC32) == Q_FALSE) {
            /*
             * Couldn't start the protocol, switch back to console or host
             * mode.
             */
            if ((original_state == Q_STATE_HOST) ||
                (original_state == Q_STATE_CONSOLE)) {
                switch_state(original_state);
            } else {
                switch_state(Q_STATE_CONSOLE);
            }
            return;
        }
        break;
    }

    /*
     * Setup filename and basename
     */
    switch (q_transfer_stats.protocol) {

    case Q_PROTOCOL_KERMIT:
        /*
         * Filename is setup by kermit_start() or during transfer
         */
        break;
    case Q_PROTOCOL_ASCII:
    case Q_PROTOCOL_XMODEM:
    case Q_PROTOCOL_XMODEM_CRC:
    case Q_PROTOCOL_XMODEM_RELAXED:
    case Q_PROTOCOL_XMODEM_1K:
    case Q_PROTOCOL_XMODEM_1K_G:
        /*
         * Note that basename and dirname modify the arguments
         */
        basename_arg = Xstrdup(q_download_location, __FILE__, __LINE__);
        dirname_arg = Xstrdup(q_download_location, __FILE__, __LINE__);
        set_transfer_stats_filename(basename(basename_arg));
        set_transfer_stats_pathname(dirname(dirname_arg));
        /*
         * Free the copies passed to basename() and dirname()
         */
        Xfree(basename_arg, __FILE__, __LINE__);
        Xfree(dirname_arg, __FILE__, __LINE__);
        break;

    case Q_PROTOCOL_YMODEM:
    case Q_PROTOCOL_YMODEM_G:
        /*
         * Filename is setup by ymodem_start() or during transfer
         */
        break;
    case Q_PROTOCOL_ZMODEM:
        /*
         * Filename is setup by zmodem_start() or during transfer
         */
        break;

    }

    /*
     * Record the time
     */
    time(&q_transfer_stats.file_start_time);
    time(&q_transfer_stats.batch_start_time);

}

/**
 * End the file transfer.
 *
 * @param new_state the state to switch to after a brief display pause
 */
void stop_file_transfer(const Q_TRANSFER_STATE new_state) {
    int i;

    switch (q_transfer_stats.protocol) {

    case Q_PROTOCOL_ASCII:
        ascii_transfer_stop(Q_TRUE);
        break;
    case Q_PROTOCOL_KERMIT:
        kermit_stop(Q_TRUE);
        break;
    case Q_PROTOCOL_XMODEM:
    case Q_PROTOCOL_XMODEM_CRC:
    case Q_PROTOCOL_XMODEM_RELAXED:
    case Q_PROTOCOL_XMODEM_1K:
    case Q_PROTOCOL_XMODEM_1K_G:
        xmodem_stop(Q_TRUE);
        break;
    case Q_PROTOCOL_YMODEM:
    case Q_PROTOCOL_YMODEM_G:
        ymodem_stop(Q_TRUE);
        break;
    case Q_PROTOCOL_ZMODEM:
        zmodem_stop(Q_TRUE);
        break;

    }

    q_transfer_stats.state = new_state;
    q_screen_dirty = Q_TRUE;
    time(&q_transfer_stats.end_time);

    /*
     * Free the batch upload list
     */
    if (batch_upload_file_list != NULL) {
        for (i = 0; batch_upload_file_list[i].name != NULL; i++) {
            Xfree(batch_upload_file_list[i].name, __FILE__, __LINE__);
        }
        Xfree(batch_upload_file_list, __FILE__, __LINE__);
        batch_upload_file_list_i = 0;
        batch_upload_file_list = NULL;
    }

    if (q_download_location != NULL) {
        Xfree(q_download_location, __FILE__, __LINE__);
        q_download_location = NULL;
    }

    /*
     * Log it
     */
    switch (q_transfer_stats.protocol) {

    case Q_PROTOCOL_ASCII:
    case Q_PROTOCOL_XMODEM:
    case Q_PROTOCOL_XMODEM_CRC:
    case Q_PROTOCOL_XMODEM_RELAXED:
    case Q_PROTOCOL_XMODEM_1K:
    case Q_PROTOCOL_XMODEM_1K_G:
        /*
         * Single-file protocols
         */
        if (q_program_state == Q_STATE_DOWNLOAD) {
            if (new_state == Q_TRANSFER_STATE_ABORT) {
                qlog(_("DOWNLOAD ABORTED: protocol %s, filename %s\n"),
                     q_transfer_stats.protocol_name);
            } else {
                qlog(_("DOWNLOAD FILE COMPLETE: protocol %s, filename %s, filesize %d\n"),
                     q_transfer_stats.protocol_name, q_download_location,
                     q_transfer_stats.bytes_total);
            }
        } else {
            if (new_state == Q_TRANSFER_STATE_ABORT) {
                qlog(_("UPLOAD ABORTED: protocol %s, filename %s\n"),
                     q_transfer_stats.protocol_name);
            } else {
                qlog(_("UPLOAD FILE COMPLETE: protocol %s, filename %s, filesize %d\n"),
                     q_transfer_stats.protocol_name, q_download_location,
                     q_transfer_stats.bytes_total);
            }
        }
        break;

    default:
        /*
         * Batch protocols
         */
        if (q_program_state == Q_STATE_DOWNLOAD) {
            if (new_state == Q_TRANSFER_STATE_ABORT) {
                qlog(_("DOWNLOAD ABORTED: protocol %s\n"),
                     q_transfer_stats.protocol_name);
            } else {
                qlog(_("DOWNLOAD END: protocol %s\n"),
                     q_transfer_stats.protocol_name, q_download_location,
                     q_transfer_stats.bytes_total);
            }
        } else {
            if (new_state == Q_TRANSFER_STATE_ABORT) {
                qlog(_("UPLOAD ABORTED: protocol %s\n"),
                     q_transfer_stats.protocol_name);
            } else {
                qlog(_("UPLOAD END: protocol %s\n"),
                     q_transfer_stats.protocol_name, q_download_location,
                     q_transfer_stats.bytes_total);
            }
        }
        break;
    }

    /*
     * Force a repaint to see the message before the protocols call
     * play_sequence().
     */
    protocol_transfer_refresh();

}

/**
 * Draw screen for the protocol selection dialog.
 */
void protocol_menu_refresh() {
    char * status_string;
    /*
     * Number of digits in the hard drive size display.  10^32 is pretty darn
     * big.
     */
    char size_string[32];
    int status_left_stop;
    char * message;
    int message_left;
    int window_left;
    int window_top;
    int window_height = 13;
    int window_length;
    int i;
#ifdef Q_PDCURSES_WIN32
    unsigned long free_kbytes = 0;
    ULARGE_INTEGER free_bytes_available;
#else
    unsigned long long free_kbytes = 0;
    struct statvfs buf;
#endif

    if (q_screen_dirty == Q_FALSE) {
        return;
    }

    /*
     * Clear screen for when it resizes
     */
    console_refresh(Q_FALSE);

    if (q_program_state == Q_STATE_DOWNLOAD_MENU) {
        window_height++;
        message = _("Download Protocols");
    } else {
        message = _("Upload Protocols");
    }

    /*
     * Put up the status line
     */
    screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH,
                              Q_COLOR_STATUS);

    status_string =
        _(" LETTER-Select a Protocol for the File Transfer   ESC/`-Exit ");
    status_left_stop = WIDTH - strlen(status_string);
    if (status_left_stop <= 0) {
        status_left_stop = 0;
    } else {
        status_left_stop /= 2;
    }
    screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string,
                            Q_COLOR_STATUS);

    memset(size_string, 0, sizeof(size_string));
    if (q_program_state == Q_STATE_DOWNLOAD_MENU) {
#ifdef Q_PDCURSES_WIN32
        if (GetDiskFreeSpaceExA(get_option(Q_OPTION_DOWNLOAD_DIR),
                &free_bytes_available, NULL, NULL) == FALSE) {
            DWORD error = GetLastError();
            sprintf(size_string, _("Unknown (Error %d)"), error);
        } else {
            /*
             * Note that this numeric string is NOT localized.  The comma and
             * the 'k' for kilobytes are English units.
             */
            char comma_string[sizeof(size_string)];
            int comma_string_i;
            int comma_string_n;
            memset(size_string, 0, sizeof(size_string));
            free_kbytes = free_bytes_available.QuadPart / 1024L;
            sprintf(comma_string, "%lu", free_kbytes);
            comma_string_n = strlen(comma_string);
            for (comma_string_i = 0; comma_string_i < comma_string_n;
                 comma_string_i++) {
                size_string[strlen(size_string)] = comma_string[comma_string_i];
                if (((comma_string_n - comma_string_i) % 3 == 1) &&
                    (comma_string_i != comma_string_n - 1)
                ) {
                    size_string[strlen(size_string)] = ',';
                }
            }
            size_string[strlen(size_string)] = ' ';
            size_string[strlen(size_string)] = 'k';
        }
#else
        statvfs(get_option(Q_OPTION_DOWNLOAD_DIR), &buf);
        free_kbytes =
            (unsigned long long) buf.f_bavail *
            (unsigned long long) buf.f_bsize / 1024LL;
        sprintf(size_string, _("%'-llu k"), free_kbytes);
#endif
    }

    if (strlen(message) > strlen(size_string) + 12) {
        window_length = strlen(message);
    } else {
        window_length = strlen(size_string) + 12;
    }

    /*
     * Add room for border + 1 spaces on each side
     */
    window_length += 4;

    if (q_program_state == Q_STATE_UPLOAD_MENU) {
        /*
         * Upload window needs a tad more space
         */
        window_length += 2;
    }

    /*
     * Window will be centered on the screen
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
        window_top /= 10;
    }

    screen_draw_box(window_left, window_top, window_left + window_length,
                    window_top + window_height);
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

    i = 1;
    if (q_program_state == Q_STATE_DOWNLOAD_MENU) {
        screen_put_color_str_yx(window_top + 1, window_left + 2,
                                _("Free Space"), Q_COLOR_MENU_COMMAND);
        screen_put_color_printf(Q_COLOR_MENU_TEXT, "  %s", size_string);
        i++;
    }
    screen_put_color_str_yx(window_top + i, window_left + 2, "A",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_str(_(" - Ascii"), Q_COLOR_MENU_TEXT);
    i++;
    screen_put_color_str_yx(window_top + i, window_left + 2, "K",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_str(_(" - Kermit"), Q_COLOR_MENU_TEXT);
    i++;
    screen_put_color_str_yx(window_top + i, window_left + 2, "X",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_str(_(" - Xmodem"), Q_COLOR_MENU_TEXT);
    i++;
    screen_put_color_str_yx(window_top + i, window_left + 2, "C",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_str(_(" - Xmodem CRC"), Q_COLOR_MENU_TEXT);
    i++;
    screen_put_color_str_yx(window_top + i, window_left + 2, "R",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_str(_(" - Xmodem Relaxed"), Q_COLOR_MENU_TEXT);
    i++;
    screen_put_color_str_yx(window_top + i, window_left + 2, "O",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_str(_(" - Xmodem-1K"), Q_COLOR_MENU_TEXT);
    i++;
    screen_put_color_str_yx(window_top + i, window_left + 2, "Y",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_str(_(" - Ymodem Batch"), Q_COLOR_MENU_TEXT);
    i++;
    screen_put_color_str_yx(window_top + i, window_left + 2, "Z",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_str(_(" - Zmodem Batch"), Q_COLOR_MENU_TEXT);
    i++;
    screen_put_color_str_yx(window_top + i, window_left + 2, "F",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_str(_(" - Xmodem-1K/G"), Q_COLOR_MENU_TEXT);
    i++;
    screen_put_color_str_yx(window_top + i, window_left + 2, "G",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_str(_(" - Ymodem/G Batch"), Q_COLOR_MENU_TEXT);
    i++;

    /*
     * Prompt
     */
    screen_put_color_str_yx(window_top + i, window_left + 2,
                            _("Your Choice ? "), Q_COLOR_MENU_COMMAND);

    screen_flush();
    q_screen_dirty = Q_FALSE;
}

/**
 * Keyboard handler for the protocol selection dialog.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
void protocol_menu_keyboard_handler(const int keystroke, const int flags) {

    switch (keystroke) {

    case 'a':
    case 'A':
        q_transfer_stats.protocol = Q_PROTOCOL_ASCII;
        break;
    case 'k':
    case 'K':
        q_transfer_stats.protocol = Q_PROTOCOL_KERMIT;
        break;
    case 'x':
    case 'X':
        q_transfer_stats.protocol = Q_PROTOCOL_XMODEM;
        break;
    case 'c':
    case 'C':
        q_transfer_stats.protocol = Q_PROTOCOL_XMODEM_CRC;
        break;
    case 'r':
    case 'R':
        q_transfer_stats.protocol = Q_PROTOCOL_XMODEM_RELAXED;
        break;
    case 'o':
    case 'O':
        q_transfer_stats.protocol = Q_PROTOCOL_XMODEM_1K;
        break;
    case 'y':
    case 'Y':
        q_transfer_stats.protocol = Q_PROTOCOL_YMODEM;
        break;
    case 'z':
    case 'Z':
        q_transfer_stats.protocol = Q_PROTOCOL_ZMODEM;
        break;
    case 'f':
    case 'F':
        q_transfer_stats.protocol = Q_PROTOCOL_XMODEM_1K_G;
        break;
    case 'g':
    case 'G':
        q_transfer_stats.protocol = Q_PROTOCOL_YMODEM_G;
        break;

    case Q_KEY_F(1):
        launch_help(Q_HELP_PROTOCOLS);

        /*
         * Refresh the whole screen.
         */
        console_refresh(Q_FALSE);
        q_screen_dirty = Q_TRUE;
        return;

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
        return;

    default:
        /*
         * Ignore keystroke
         */
        return;

    }

    /*
     * Protocol selected, switch to prompt.
     */
    if (q_program_state == Q_STATE_DOWNLOAD_MENU) {
        switch_state(Q_STATE_DOWNLOAD_PATHDIALOG);
    } else {
        switch_state(Q_STATE_UPLOAD_PATHDIALOG);
    }

    /*
     * The OK exit point
     */

}

/**
 * Draw screen for the protocol path to save file dialog.
 */
void protocol_pathdialog_refresh() {
    /*
     * Refresh the background
     */
    q_screen_dirty = Q_TRUE;
    console_refresh(Q_FALSE);

    /*
     * Special case: I could be called from inside batch_entry_window()
     */
    if (q_program_state == Q_STATE_UPLOAD_BATCH_DIALOG) {
        return;
    }

    if (q_download_location != NULL) {
        Xfree(q_download_location, __FILE__, __LINE__);
        q_download_location = NULL;
    }

    switch (q_transfer_stats.protocol) {

    case Q_PROTOCOL_KERMIT:
    case Q_PROTOCOL_YMODEM:
    case Q_PROTOCOL_YMODEM_G:
    case Q_PROTOCOL_ZMODEM:
        /*
         * Kermit, Ymodem and Zmodem get the filename from the download
         * itself, so prompt for a pathname rather than a filename.
         */
        if (q_program_state == Q_STATE_DOWNLOAD_PATHDIALOG) {
            q_download_location = save_form(_("Download Directory"),
                                            get_option(Q_OPTION_DOWNLOAD_DIR),
                                            Q_TRUE, Q_FALSE);
        } else {
            /*
             * Special case: batch entry window
             */
            switch_state(Q_STATE_UPLOAD_BATCH_DIALOG);
            batch_upload_file_list =
                batch_entry_window(get_option(Q_OPTION_UPLOAD_DIR), Q_TRUE);
            if (batch_upload_file_list != NULL) {
                /*
                 * Begin uploading
                 */
                batch_upload_file_list_i = 0;
                switch_state(Q_STATE_UPLOAD_BATCH);
                start_file_transfer();
            } else {
                /*
                 * Abort
                 */
                switch_state(Q_STATE_CONSOLE);
            }
            return;
        }
        break;

    default:
        if (q_program_state == Q_STATE_DOWNLOAD_PATHDIALOG) {
            q_download_location = save_form(_("Download File"),
                                            get_option(Q_OPTION_DOWNLOAD_DIR),
                                            Q_FALSE, Q_TRUE);
        } else {
            q_download_location = save_form(_("Upload File"),
                                            get_option(Q_OPTION_UPLOAD_DIR),
                                            Q_FALSE, Q_FALSE);
        }
        break;

    }

    if (q_program_state == Q_STATE_DOWNLOAD_PATHDIALOG) {
        /*
         * Begin downloading
         */
        switch_state(Q_STATE_DOWNLOAD);
    } else {
        /*
         * Begin uploading
         */
        switch_state(Q_STATE_UPLOAD);
    }

    /*
     * Start the transfer
     */
    if (q_download_location != NULL) {
        start_file_transfer();
    } else {
        /*
         * Abort
         */
        switch_state(Q_STATE_CONSOLE);
    }

}

/**
 * Keyboard handler for the protocol path to save file dialog.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
void protocol_pathdialog_keyboard_handler(const int keystroke, const int flags) {
    /*
     * This function gets called after protocol_pathdialog_refresh(), so it
     * needs to exist, but it doesn't actually do anything.
     * protocol_pathdialog_refresh() calls functions in forms.c that do their
     * own internal keyboard handling.
     */

    /*
     * NOP
     */
    return;
}

/**
 * Keyboard handler for the transferring file screen.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
void protocol_transfer_keyboard_handler(const int keystroke, const int flags) {
    switch (keystroke) {
    case 's':
    case 'S':
        /*
         * Kermit transfers can skip files
         */
        if (q_transfer_stats.protocol == Q_PROTOCOL_KERMIT) {
            kermit_skip_file();
        }
        break;

    default:
        if (q_transfer_stats.state == Q_TRANSFER_STATE_END) {
            /*
             * This is the last few seconds of wait time.  Let any keystroke
             * switch out of the protocol screen by falling through into the
             * backtick / ESCAPE case.
             */
        } else {
            /*
             * Ignore keystroke
             */
            break;
        }

        /*
         * Fall through...
         */
    case '`':
        /*
         * Backtick works too
         */
    case Q_KEY_ESCAPE:
        /*
         * ABORT the transfer
         */
        if ((q_transfer_stats.state != Q_TRANSFER_STATE_END) &&
            (q_transfer_stats.state != Q_TRANSFER_STATE_ABORT)
        ) {
            stop_file_transfer(Q_TRANSFER_STATE_ABORT);
        }

        /*
         * Return to TERMINAL mode  or host mode
         */
        if ((original_state == Q_STATE_HOST) ||
            (original_state == Q_STATE_CONSOLE)
        ) {
            switch_state(original_state);
        } else {
            switch_state(Q_STATE_CONSOLE);
        }
        return;

    }

}

/**
 * Draw screen for the transferring file screen.
 */
void protocol_transfer_refresh() {
    char * status_string;
    int status_left_stop;
    char * message;
    int message_left;
    int window_left;
    int window_top;
    int window_height = 13;
    int window_length = 75;
    int i;
    int percent_complete;
    int batch_percent_complete;
    unsigned int cps;

    char time_elapsed_string[SHORT_TIME_SIZE];
    char remaining_time_string[SHORT_TIME_SIZE];
    char batch_time_elapsed_string[SHORT_TIME_SIZE];
    char batch_remaining_time_string[SHORT_TIME_SIZE];
    time_t current_time;
    int hours, minutes, seconds;
    time_t transfer_time;
    time_t remaining_time;
    time_t batch_transfer_time;
    time_t batch_remaining_time;
#ifndef Q_NO_SERIAL
    int bits_per_byte = 8;
#endif
    static struct timeval last_update;
    struct timeval now;

    /*
     * Get current time
     */
    gettimeofday(&now, NULL);
    if ((q_transfer_stats.state != Q_TRANSFER_STATE_END) &&
        (last_update.tv_sec == now.tv_sec) &&
        (abs(last_update.tv_usec - now.tv_usec) < 250000)) {
        /*
         * Only update the screen every 1/4 second during a file transfer
         */
        return;
    }
    /*
     * Save last screen update time
     */
    memcpy(&last_update, &now, sizeof(struct timeval));

    /*
     * Compute time
     */
    time(&current_time);
    /*
     * time_string needs to be hours/minutes/seconds TRANSFER
     */
    if ((q_transfer_stats.state == Q_TRANSFER_STATE_END) ||
        (q_transfer_stats.state == Q_TRANSFER_STATE_ABORT)) {
        transfer_time =
            (time_t) difftime(q_transfer_stats.end_time,
                              q_transfer_stats.file_start_time);
    } else {
        transfer_time =
            (time_t) difftime(current_time, q_transfer_stats.file_start_time);
    }
    hours   = (int)  (transfer_time / 3600);
    minutes = (int) ((transfer_time % 3600) / 60);
    seconds = (int)  (transfer_time % 60);
    snprintf(time_elapsed_string, sizeof(time_elapsed_string), "%02u:%02u:%02u",
             hours, minutes, seconds);

    /*
     * Compute the transfer time and time remaining
     */
    if (q_transfer_stats.bytes_transfer > 0) {
        remaining_time =
            (q_transfer_stats.bytes_total -
             q_transfer_stats.bytes_transfer) * transfer_time /
            q_transfer_stats.bytes_transfer;
    } else {
        remaining_time = 0;
    }
    if ((q_transfer_stats.state == Q_TRANSFER_STATE_END) ||
        (q_transfer_stats.state == Q_TRANSFER_STATE_FILE_DONE)) {
        remaining_time = 0;
    }

    hours = remaining_time / 3600;
    minutes = (remaining_time % 3600) / 60;
    seconds = remaining_time % 60;
    snprintf(remaining_time_string, sizeof(remaining_time_string),
             "%02u:%02u:%02u", hours, minutes, seconds);

    /*
     * Batch timings
     */
    if ((q_transfer_stats.state == Q_TRANSFER_STATE_END) ||
        (q_transfer_stats.state == Q_TRANSFER_STATE_ABORT)) {
        batch_transfer_time =
            (time_t) difftime(q_transfer_stats.end_time,
                              q_transfer_stats.batch_start_time);
    } else {
        batch_transfer_time =
            (time_t) difftime(current_time, q_transfer_stats.batch_start_time);
    }
    hours = batch_transfer_time / 3600;
    minutes = (batch_transfer_time % 3600) / 60;
    seconds = batch_transfer_time % 60;
    snprintf(batch_time_elapsed_string, sizeof(batch_time_elapsed_string),
             "%02u:%02u:%02u", hours, minutes, seconds);

    /*
     * Compute the transfer time and time remaining
     */
    if (q_transfer_stats.batch_bytes_transfer +
        q_transfer_stats.bytes_transfer > 0) {
        batch_remaining_time =
            (q_transfer_stats.batch_bytes_total -
             q_transfer_stats.batch_bytes_transfer -
             q_transfer_stats.bytes_transfer) * transfer_time /
            (q_transfer_stats.batch_bytes_transfer +
             q_transfer_stats.bytes_transfer);
    } else {
        batch_remaining_time = 0;
    }
    if ((q_transfer_stats.state == Q_TRANSFER_STATE_END) ||
        (q_transfer_stats.state == Q_TRANSFER_STATE_ABORT)) {
        batch_remaining_time = 0;
    }

    assert(batch_remaining_time >= 0);
    hours = batch_remaining_time / 3600;
    minutes = (batch_remaining_time % 3600) / 60;
    seconds = batch_remaining_time % 60;
    snprintf(batch_remaining_time_string, sizeof(batch_remaining_time_string),
             "%02u:%02u:%02u", hours, minutes, seconds);

    /*
     * Filename and pathname could get quite long, let's reduce them.
     */
    shorten_string(q_transfer_stats.filename, window_length - 10);
    shorten_string(q_transfer_stats.pathname, window_length - 10);

    /*
     * Special case: check the timeout
     */
    if (((q_transfer_stats.state == Q_TRANSFER_STATE_END) ||
            (q_transfer_stats.state == Q_TRANSFER_STATE_ABORT)) &&
        (q_screen_dirty == Q_FALSE)
    ) {
        transfer_time = (time_t) difftime(current_time, q_transfer_stats.end_time);

        /*
         * Wait up to 3 seconds
         */
        if (transfer_time > 3) {
            /*
             * Switch back to TERMINAL mode  or host mode
             */
            if ((original_state == Q_STATE_HOST) ||
                (original_state == Q_STATE_CONSOLE)) {
                switch_state(original_state);
            } else {
                switch_state(Q_STATE_CONSOLE);
            }
            return;
        }
    }

    /*
     * ASCII special case: we don't put up the dialog window, instead we
     * update the status line.
     */
    if (q_transfer_stats.protocol == Q_PROTOCOL_ASCII) {
        /*
         * Max 80 columns wide
         */
        char status_buffer[80];

        /*
         * Put up the status line
         */
        screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH,
                                  Q_COLOR_STATUS);

        if (q_program_state == Q_STATE_DOWNLOAD) {
            status_string =
                _(" ASCII DOWNLOAD IN PROGRESS    ESC/`-Save and Exit ");

        } else {
            if (q_transfer_stats.bytes_total == 0) {
                /*
                 * If the file is 0, percent complete is always 100.
                 */
                percent_complete = 100;
            } else if (q_transfer_stats.bytes_transfer ==
                       q_transfer_stats.bytes_total) {
                /*
                 * If file is complete percent complete is 100.
                 */
                percent_complete = 100;
            } else {
                /*
                 * Else do a true percent complete calculation
                 */
                if (q_transfer_stats.bytes_total > 0) {
                    percent_complete =
                        (q_transfer_stats.bytes_transfer * 100) /
                        q_transfer_stats.bytes_total;
                } else {
                    percent_complete = 0;
                }
            }
            sprintf(status_buffer,
                    _(" Uploading %s  Sent = %lu    Complete = %d%%   ESC/`-Terminate "),
                    q_transfer_stats.filename, q_transfer_stats.bytes_transfer,
                    percent_complete);
            status_string = status_buffer;
        }

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
        return;

    } /* if (q_transfer_stats.protocol == Q_PROTOCOL_ASCII) */

    /*
     * Batch upload has more stuff
     */
    if (q_program_state == Q_STATE_UPLOAD_BATCH) {
        window_height += 3;
    }

    /*
     * Window will be centered on the screen
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

    if (q_screen_dirty == Q_FALSE) {
        /*
         * Only update the time fields
         */
        screen_put_color_str_yx(window_top + 6, window_left + 51,
                                _("Time Elapsed "), Q_COLOR_MENU_TEXT);
        screen_put_color_str(time_elapsed_string, Q_COLOR_MENU_COMMAND);
        screen_put_color_str_yx(window_top + 7, window_left + 51,
                                _("++ Remaining "), Q_COLOR_MENU_TEXT);
        screen_put_color_str(remaining_time_string, Q_COLOR_MENU_COMMAND);

        /*
         * Batch times
         */
        if (q_program_state == Q_STATE_UPLOAD_BATCH) {
            screen_put_color_str_yx(window_top + window_height - 1 - 2,
                                    window_left + 2, _("Batch Time Elapsed "),
                                    Q_COLOR_MENU_TEXT);
            screen_put_color_str_yx(window_top + window_height - 1 - 2,
                                    window_left + 21, batch_time_elapsed_string,
                                    Q_COLOR_MENU_COMMAND);
            screen_put_color_str_yx(window_top + window_height - 1 - 2,
                                    window_left + 49, _("++ Remaining "),
                                    Q_COLOR_MENU_TEXT);
            screen_put_color_str_yx(window_top + window_height - 1 - 2,
                                    window_left + 62,
                                    batch_remaining_time_string,
                                    Q_COLOR_MENU_COMMAND);
        }
        return;
    }

    if (q_program_state == Q_STATE_DOWNLOAD) {
        message = _("Download Status");
        if (q_transfer_stats.protocol == Q_PROTOCOL_KERMIT) {
            status_string =
                _(" Download in Progress   S-Skip File   ESC/`-Cancel Transfer ");
        } else {
            status_string = _(" Download in Progress   ESC/`-Cancel Transfer ");
        }
    } else if (q_program_state == Q_STATE_UPLOAD) {
        message = _("Upload Status");
        status_string = _(" Upload in Progress   ESC/`-Cancel Transfer ");
    } else {
        message = _("Upload Status");
        if (q_transfer_stats.protocol == Q_PROTOCOL_KERMIT) {
            status_string =
                _(" Batch Upload in Progress   S-Skip File   ESC/`-Cancel Transfer ");
        } else {
            status_string =
                _(" Batch Upload in Progress   ESC/`-Cancel Transfer ");
        }
    }

    /*
     * Put up the status line
     */
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

    screen_draw_box(window_left, window_top, window_left + window_length,
                    window_top + window_height);
    message_left = window_length - (strlen(message) + 2);
    if (message_left < 0) {
        message_left = 0;
    } else {
        message_left /= 2;
    }
    screen_put_color_printf_yx(window_top + 0, window_left + message_left,
                               Q_COLOR_WINDOW_BORDER, " %s ", message);

    /*
     * Protocol name, filename, pathname
     */
    screen_put_color_str_yx(window_top + 3, window_left + 2, _("File "),
                            Q_COLOR_MENU_TEXT);
    screen_put_color_str(q_transfer_stats.filename, Q_COLOR_MENU_COMMAND);
    screen_put_color_str_yx(window_top + 1, window_left + 27, _("Protocol "),
                            Q_COLOR_MENU_TEXT);
    screen_put_color_str(q_transfer_stats.protocol_name, Q_COLOR_MENU_COMMAND);

    screen_put_color_str_yx(window_top + 4, window_left + 2, _("Path "),
                            Q_COLOR_MENU_TEXT);
    screen_put_color_str(q_transfer_stats.pathname, Q_COLOR_MENU_COMMAND);

    /*
     * Bytes and blocks total fields
     */
    screen_put_color_str_yx(window_top + 6, window_left + 2, _("Bytes Total "),
                            Q_COLOR_MENU_TEXT);
    screen_put_color_printf(Q_COLOR_MENU_COMMAND, "%-lu",
                            q_transfer_stats.bytes_total);
    screen_put_color_str_yx(window_top + 6, window_left + 27,
                            _("Blocks Total "), Q_COLOR_MENU_TEXT);
    screen_put_color_printf(Q_COLOR_MENU_COMMAND, "%-lu",
                            q_transfer_stats.blocks);

    /*
     * Time fields
     */
    screen_put_color_str_yx(window_top + 6, window_left + 51,
                            _("Time Elapsed "), Q_COLOR_MENU_TEXT);
    screen_put_color_str(time_elapsed_string, Q_COLOR_MENU_COMMAND);
    screen_put_color_str_yx(window_top + 7, window_left + 51,
                            _("++ Remaining "), Q_COLOR_MENU_TEXT);
    screen_put_color_str(remaining_time_string, Q_COLOR_MENU_COMMAND);

    /*
     * Bytes and blocks transferred fields
     */
    if (q_program_state == Q_STATE_DOWNLOAD) {
        screen_put_color_str_yx(window_top + 7, window_left + 2,
                                _("Bytes Rcvd  "), Q_COLOR_MENU_TEXT);
    } else {
        screen_put_color_str_yx(window_top + 7, window_left + 2,
                                _("Bytes Sent  "), Q_COLOR_MENU_TEXT);
    }
    screen_put_color_printf(Q_COLOR_MENU_COMMAND, "%-lu",
                            q_transfer_stats.bytes_transfer);
    if (q_program_state == Q_STATE_DOWNLOAD) {
        screen_put_color_str_yx(window_top + 7, window_left + 27,
                                _("Blocks Rcvd  "), Q_COLOR_MENU_TEXT);
    } else {
        screen_put_color_str_yx(window_top + 7, window_left + 27,
                                _("Blocks Sent  "), Q_COLOR_MENU_TEXT);
    }
    screen_put_color_printf(Q_COLOR_MENU_COMMAND, "%-lu",
                            q_transfer_stats.blocks_transfer);

    /*
     * Block size, error count, and efficiency
     */
    screen_put_color_str_yx(window_top + 8, window_left + 2, _("Error Count "),
                            Q_COLOR_MENU_TEXT);
    screen_put_color_printf(Q_COLOR_MENU_COMMAND, "%-lu",
                            q_transfer_stats.error_count);
    screen_put_color_str_yx(window_top + 8, window_left + 27,
                            _("Block Size   "), Q_COLOR_MENU_TEXT);
    screen_put_color_printf(Q_COLOR_MENU_COMMAND, "%-lu",
                            q_transfer_stats.block_size);

    /*
     * CPS
     */
    screen_put_color_str_yx(window_top + 9, window_left + 51,
                            _("Chars/second "), Q_COLOR_MENU_TEXT);
    if (transfer_time > 0) {
        cps = q_transfer_stats.bytes_transfer / transfer_time;
    } else {
        cps = q_transfer_stats.bytes_transfer;
    }
    if (cps > q_transfer_stats.bytes_transfer) {
        cps = q_transfer_stats.bytes_transfer;
    }
    screen_put_color_printf(Q_COLOR_MENU_COMMAND, "%-lu", cps);

    screen_put_color_str_yx(window_top + 8, window_left + 51,
                            _("Efficiency   "), Q_COLOR_MENU_TEXT);

#ifndef Q_NO_SERIAL
    if (Q_SERIAL_OPEN) {
        switch (q_serial_port.data_bits) {
        case Q_DATA_BITS_8:
            bits_per_byte = 8;
            break;
        case Q_DATA_BITS_7:
            bits_per_byte = 7;
            break;
        case Q_DATA_BITS_6:
            bits_per_byte = 6;
            break;
        case Q_DATA_BITS_5:
            bits_per_byte = 5;
            break;
        }
        switch (q_serial_port.stop_bits) {
        case Q_STOP_BITS_1:
            bits_per_byte += 1;
            break;
        case Q_STOP_BITS_2:
            bits_per_byte += 2;
            break;
        }

        /*
         * Add mark bit
         */
        bits_per_byte += 1;

        if (q_serial_port.dce_baud > 0) {
            screen_put_color_printf(Q_COLOR_MENU_COMMAND, " %6.2f%%",
                                    ((float) cps * bits_per_byte * 100.0) /
                                    (float) q_serial_port.dce_baud);
        } else {
            screen_put_color_str("N/A", Q_COLOR_MENU_COMMAND);
        }
    } else {
        screen_put_color_str("N/A", Q_COLOR_MENU_COMMAND);
    }
#else
    screen_put_color_str("N/A", Q_COLOR_MENU_COMMAND);
#endif /* Q_NO_SERIAL */

    /*
     * Last message
     */
    screen_put_color_str_yx(window_top + 10, window_left + 2, _("Status Msgs "),
                            Q_COLOR_MENU_TEXT);
    screen_put_color_str(q_transfer_stats.last_message, Q_COLOR_MENU_COMMAND);

    /*
     * Three cases:
     * 1) Transfer complete
     * 2) Transfer aborted
     * 3) Transfer in progress
     */
    if ((q_transfer_stats.state == Q_TRANSFER_STATE_END) ||
        (q_transfer_stats.state == Q_TRANSFER_STATE_FILE_DONE)) {
        /*
         * File complete, percent complete is 100.
         */
        percent_complete = 100;
    } else if (q_transfer_stats.bytes_transfer == 0) {
        /*
         * 0 bytes transferred, percent complete is 0.
         */
        percent_complete = 0;
    } else if ((q_program_state == Q_STATE_DOWNLOAD) &&
        ((q_transfer_stats.protocol == Q_PROTOCOL_XMODEM) ||
            (q_transfer_stats.protocol == Q_PROTOCOL_XMODEM_RELAXED) ||
            (q_transfer_stats.protocol == Q_PROTOCOL_XMODEM_CRC) ||
            (q_transfer_stats.protocol == Q_PROTOCOL_XMODEM_1K) ||
            (q_transfer_stats.protocol == Q_PROTOCOL_XMODEM_1K_G))
    ) {
        /*
         * Xmodem does not report file size on downloads, percent complete is
         * always 0
         */
        percent_complete = 0;
    } else if (q_transfer_stats.bytes_total == 0) {
        /*
         * 0-byte files have percent complete = 0
         */
        percent_complete = 0;
    } else if (q_transfer_stats.bytes_transfer ==
                   q_transfer_stats.bytes_total) {
        /*
         * Another check: if file is complete percent complete is 100.
         */
        percent_complete = 100;
    } else {
        /*
         * Finally, fall through to a true percent complete calculation
         */
        percent_complete =
            (q_transfer_stats.bytes_transfer * 100) /
            q_transfer_stats.bytes_total;
    }
    if (percent_complete > 100) {
        percent_complete = 100;
    }

    screen_put_color_str_yx(window_top + 11, window_left + 2, _("Completion  "),
                            Q_COLOR_MENU_TEXT);
    screen_move_yx(window_top + 11, window_left + 14);
    screen_put_color_printf(Q_COLOR_MENU_COMMAND, "%-3u%%   ",
                            percent_complete);
    for (i = 0; i < (percent_complete * 50) / 100; i++) {
        screen_put_color_char(cp437_chars[HATCH], Q_COLOR_MENU_COMMAND);
    }
    for (; i < 50; i++) {
        screen_put_color_char(cp437_chars[BOX], Q_COLOR_MENU_COMMAND);
    }

    /*
     * Batch upload
     */
    if (q_program_state == Q_STATE_UPLOAD_BATCH) {
        if (q_transfer_stats.batch_bytes_transfer +
            q_transfer_stats.bytes_transfer == 0) {
            batch_percent_complete = 0;
        } else if (q_transfer_stats.batch_bytes_transfer +
                   q_transfer_stats.bytes_transfer >=
                   q_transfer_stats.batch_bytes_total) {
            batch_percent_complete = 100;
        } else if (q_transfer_stats.batch_bytes_total == 0) {
            /*
             * If we ever send a batch of 0-byte files...
             */
            batch_percent_complete = 0;
        } else {
            batch_percent_complete =
                ((q_transfer_stats.batch_bytes_transfer +
                  q_transfer_stats.bytes_transfer) * 100) /
                q_transfer_stats.batch_bytes_total;
        }
        if (batch_percent_complete > 100) {
            batch_percent_complete = 100;
        }

        message = _("Batch Upload Status");

        screen_put_color_hline_yx(window_top + window_height - 1 - 3,
                                  window_left + 1, cp437_chars[Q_WINDOW_TOP],
                                  window_length - 2, Q_COLOR_WINDOW_BORDER);
        screen_put_color_char_yx(window_top + window_height - 1 - 3,
                                 window_left + 0,
                                 cp437_chars[Q_WINDOW_LEFT_TEE],
                                 Q_COLOR_WINDOW_BORDER);
        screen_put_color_char_yx(window_top + window_height - 1 - 3,
                                 window_left + window_length - 1,
                                 cp437_chars[Q_WINDOW_RIGHT_TEE],
                                 Q_COLOR_WINDOW_BORDER);

        message_left = window_length - (strlen(message) + 2);
        if (message_left < 0) {
            message_left = 0;
        } else {
            message_left /= 2;
        }
        screen_put_color_printf_yx(window_top + window_height - 1 - 3,
                                   window_left + message_left,
                                   Q_COLOR_WINDOW_BORDER, " %s ", message);

        /*
         * Batch times
         */
        screen_put_color_str_yx(window_top + window_height - 1 - 2,
                                window_left + 2, _("Batch Time Elapsed "),
                                Q_COLOR_MENU_TEXT);
        screen_put_color_str_yx(window_top + window_height - 1 - 2,
                                window_left + 21, batch_time_elapsed_string,
                                Q_COLOR_MENU_COMMAND);
        screen_put_color_str_yx(window_top + window_height - 1 - 2,
                                window_left + 49, _("++ Remaining "),
                                Q_COLOR_MENU_TEXT);
        screen_put_color_str_yx(window_top + window_height - 1 - 2,
                                window_left + 62, batch_remaining_time_string,
                                Q_COLOR_MENU_COMMAND);

        /*
         * Progress bar
         */
        screen_put_color_str_yx(window_top + window_height - 1 - 1,
                                window_left + 2, _("Completion  "),
                                Q_COLOR_MENU_TEXT);
        screen_move_yx(window_top + window_height - 1 - 1, window_left + 14);
        screen_put_color_printf(Q_COLOR_MENU_COMMAND, "%-3u%%   ",
                                batch_percent_complete);
        for (i = 0; i < (batch_percent_complete * 50) / 100; i++) {
            screen_put_color_char(cp437_chars[HATCH], Q_COLOR_MENU_COMMAND);
        }
        for (; i < 50; i++) {
            screen_put_color_char(cp437_chars[BOX], Q_COLOR_MENU_COMMAND);
        }
    }

    screen_flush();
    q_screen_dirty = Q_FALSE;
}
