/*
 * xmodem.c
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
 * How Xmodem send works:
 *
 * xmodem_start()  --> state = INIT
 *
 * xmodem()
 *
 *  STATE == INIT:
 *   Check for ACK/'C'/'G'
 *      Got it     --> state = BLOCK
 *   Check for timeout
 *      Timeout    --> state = PURGE_INPUT
 *
 *  STATE == BLOCK:
 *   Check for timeout
 *      Timeout    --> state = PURGE_INPUT
 *   Send a block
 *      Last block?
 *         Yes     --> state = LAST_BLOCK
 *         No      --> state = BLOCK
 *
 *  STATE == LAST_BLOCK:
 *   Check for timeout
 *      Timeout    --> state = PURGE_INPUT
 *   Check for ACK
 *      Got it     --> state = EOT_ACK
 *
 *  STATE == EOT_ACK:
 *   Check for timeout
 *      Timeout    --> state = PURGE_INPUT
 *   Check for ACK
 *      Got it     --> state = COMPLETE
 *
 * xmodem_stop()
 *
 *
 *
 * How Xmodem receive works:
 *
 * xmodem_start()  --> state = INIT
 *
 * xmodem()
 *
 *  STATE == INIT:
 *   Normal: Send ACK        --> state = BLOCK
 *   Enhanced: Send 'C'/'G'  --> state = FIRST_BLOCK
 *
 *  STATE == FIRST_BLOCK:
 *   Got data?
 *      Yes            --> state = BLOCK
 *   Timeout?
 *      Yes            --> downgrade to X_NORMAL, state = BLOCK
 *
 *  STATE == BLOCK:
 *   Check for timeout
 *      Timeout        --> state = PURGE_INPUT
 *   Got a block?
 *      Yes
 *        verify_block()
 *          true       --> save, send ACK
 *          false      --> send NAK
 *      No
 *   EOT?
 *      Yes            --> state = COMPLETE
 *
 * xmodem_stop()
 *
 */

#include "qcurses.h"
#include "common.h"

#include <errno.h>
#include <assert.h>
#if defined(__BORLANDC__) || defined(_MSC_VER)
#  include <io.h>
#  define ftruncate chsize
#else
#  include <unistd.h>
#endif
#include <libgen.h>
#include <string.h>
#include <stdlib.h>
#ifdef _MSC_VER
#  include <time.h>
#  include <sys/utime.h>
#else
#  include <utime.h>
#endif
#include "console.h"
#include "music.h"
#include "protocols.h"
#include "xmodem.h"

/* Set this to a not-NULL value to enable debug log. */
/* static const char * DLOGNAME = "xymodem"; */
static const char * DLOGNAME = NULL;

/* Filename to send or receive */
static char * filename = NULL;

/* File to send or receive */
static FILE * file = NULL;

/*
 * An Xmodem block can have up to 1024 data bytes plus:
 *     1 byte HEADER
 *     1 byte block number
 *     1 byte inverted block number
 *     2 bytes CRC
 */
#define XMODEM_MAX_BLOCK_SIZE 1024 + 5

/* Current block to send or receive */
static unsigned char current_block[XMODEM_MAX_BLOCK_SIZE];

/* Size of current_block */
static unsigned int current_block_n = 0;

/* Sequence # of current_block.  Start with 1. */
static unsigned char current_block_sequence_i = 1;

/*
 * Actual block # of current_block.  Start with 1. (Sequence # is what is
 * transmitted in the Xmodem block, block # is what we surface to the user on
 * the progress dialog.)
 */
static unsigned int current_block_number = 1;

/* The first byte to start Xmodem for this flavor. Default for X_NORMAL. */
static unsigned char first_byte = C_NAK;

/* Whether sending or receiving */
static Q_BOOL sending = Q_FALSE;

/* The state of the protocol */
typedef enum {
    /* Before the first byte is sent */
    INIT,

    /* Before a regular NAK is sent */
    PURGE_INPUT,

    /* Receiver: waiting for first block after 'C' or 'G' first NAK */
    FIRST_BLOCK,

    /* Collecting data for block */
    BLOCK,

    /* Sender: waiting for ACK on final block before sending EOT */
    LAST_BLOCK,

    /* Sender: waiting for final ACK to EOT */
    EOT_ACK,

    /* Transfer complete */
    COMPLETE,

    /* Transfer was aborted due to excessive timeouts/errors */
    ABORT,

    /*
     * Receiver: looking for block 0 (file information)
     *
     * Sender: got start, need to send block 0
     */
    YMODEM_BLOCK0,

    /* Sender: sent block 0, waiting for ACK */
    YMODEM_BLOCK0_ACK1,

    /* Sender: got block 0 ACK, waiting for 'C'/'G' */
    YMODEM_BLOCK0_ACK2

} STATE;

/* Transfer state */
static STATE state;

/* For PURGE_INPUT, state we came from */
static STATE prior_state;

/* Timeout normally lasts 10 seconds */
static int timeout_length = 10;

/* The beginning time for the most recent timeout cycle */
static time_t timeout_begin;

/* Total number of timeouts before aborting is 10 */
static unsigned int timeout_max = 10;

/* Total number of timeouts so far */
static unsigned int timeout_count;

/* Total number of errors before aborting is 15 */
static unsigned int errors_max = 15;

/* The flavor of Xmodem to use */
static XMODEM_FLAVOR flavor;

/* YMODEM ONLY ----------------------------------------------------- */

/* The list of files to upload */
static struct file_info * upload_file_list;

/* The current entry in upload_file_list being sent */
static int upload_file_list_i;

/*
 * The path to download to.  Note download_path is Xstrdup'd TWICE: once HERE
 * and once more on the progress dialog.  The q_program_state transition to
 * Q_STATE_CONSOLE is what Xfree's the copy in the progress dialog.  This
 * copy is Xfree'd in ymodem_stop().
 */
static char * download_path = NULL;

/* The modification time of the current downloading file */
static time_t download_file_modtime;

/* Whether or not Ymodem block 0 has been seen */
static Q_BOOL block0_has_been_seen = Q_FALSE;

/* YMODEM ONLY ----------------------------------------------------- */

/**
 * Clear current_block.
 */
static void clear_block() {
    DLOG(("clear_block()\n"));
    memset(current_block, 0, sizeof(current_block));
    current_block_n = 0;
}

/**
 * Reset the timeout timer.
 */
static void reset_timer() {
    DLOG(("reset_timer()\n"));
    time(&timeout_begin);
}

/**
 * Check for a timeout.  Pass the output buffer because we
 * might send a CAN if timeout_max is exceeded.
 *
 * @param output the output buffer
 * @param output_n length of the output buffer
 * @return true if a timeout has occurred
 */
static Q_BOOL check_timeout(unsigned char * output, unsigned int * output_n) {
    time_t now;
    time(&now);

    DLOG(("check_timeout()\n"));

    /*
     * Let the receive have one freebie
     */
    if ((sending == Q_TRUE) && (now - timeout_begin < 2 * timeout_length)) {
        return Q_FALSE;
    }

    if (now - timeout_begin >= timeout_length) {
        /*
         * Timeout
         */
        timeout_count++;

        DLOG(("Timeout #%d\n", timeout_count));
        q_transfer_stats.error_count++;
        if (timeout_count >= timeout_max) {
            /*
             * ABORT
             */
            set_transfer_stats_last_message(
                _("TOO MANY TIMEOUTS, TRANSFER CANCELLED"));
            if (sending == Q_FALSE) {
                output[0] = C_CAN;
                *output_n = 1;
            }
            stop_file_transfer(Q_TRANSFER_STATE_ABORT);
            state = ABORT;
        } else {
            /*
             * Timeout
             */
            set_transfer_stats_last_message(_("TIMEOUT"));
            prior_state = state;
            state = PURGE_INPUT;
        }

        /*
         * Reset timeout
         */
        reset_timer();
        return Q_TRUE;
    }

    return Q_FALSE;
}

/**
 * Statistics: a block was sent out or received successfuly.
 *
 * @param input the block bytes, used to check for 128 or 1024 size blocks
 */
static void stats_increment_blocks(const unsigned char * input) {
    int new_block_size;
    int old_block_size;
    int bytes_left;

    old_block_size = q_transfer_stats.block_size;

    /*
     * The block increment is in its own check because Xmodem-1k
     * and 1K/G still don't get the full file size.
     */
    if ((sending == Q_FALSE) && (flavor != Y_NORMAL) && (flavor != Y_G)) {
        q_transfer_stats.blocks++;
    }

    q_transfer_stats.blocks_transfer++;
    if (((flavor == X_1K) ||
            (flavor == X_1K_G) ||
            (flavor == Y_NORMAL) ||
            (flavor == Y_G)) &&
        (input[0] == C_STX) &&
        (sending == Q_FALSE)
    ) {
        /*
         * Receiver case: we got a 1024-byte block
         */
        q_transfer_stats.bytes_transfer += 1024;
        if ((sending == Q_FALSE) && (flavor != Y_NORMAL) && (flavor != Y_G)) {
            q_transfer_stats.bytes_total += 1024;
        }
        new_block_size = 1024;

    } else if ((current_block_n >= 1024) && (sending == Q_TRUE)) {
        /*
         * Sender case: we sent a 1024-byte block
         */
        q_transfer_stats.bytes_transfer += 1024;
        new_block_size = 1024;
    } else {
        /*
         * Sender and receiver case: 128-byte block
         */
        q_transfer_stats.bytes_transfer += 128;

        if ((sending == Q_FALSE) && (flavor != Y_NORMAL) && (flavor != Y_G)) {
            /*
             * Xmodem receive only: increment the number of bytes to report
             * for the file because Xmodem doesn't send the file size.
             */
            q_transfer_stats.bytes_total += 128;
        }
        new_block_size = 128;
    }

    /*
     * Special check: If we're receiving via Ymodem, and we just incremented
     * q_transfer_stats.bytes_transfer by a full block size and went past the
     * known file size, then trim it back to the actual file size.
     */
    if ((sending == Q_FALSE) &&
        ((flavor == Y_NORMAL) || (flavor == Y_G)) &&
        (q_transfer_stats.bytes_transfer > q_transfer_stats.bytes_total)
    ) {
        q_transfer_stats.bytes_transfer = q_transfer_stats.bytes_total;
    } else {
        /*
         * Special check: if we just changed block size, re-compute the
         * number of blocks remaining based on the bytes left.
         */
        if (new_block_size != old_block_size) {
            if ((sending == Q_TRUE) ||
                (flavor == Y_NORMAL) ||
                (flavor == Y_G)
            ) {
                bytes_left = q_transfer_stats.bytes_total -
                    q_transfer_stats.bytes_transfer;
                if (bytes_left > 0) {
                    q_transfer_stats.blocks = bytes_left / new_block_size;
                    if ((bytes_left % new_block_size) > 0) {
                        q_transfer_stats.blocks++;
                    }
                    q_transfer_stats.blocks += q_transfer_stats.blocks_transfer;
                    q_transfer_stats.block_size = new_block_size;
                }
            }
        }
    }

    /*
     * Update the progress dialog
     */
    q_screen_dirty = Q_TRUE;
}

/**
 * Downgrade to vanilla Xmodem.
 */
static void downgrade_to_vanilla_xmodem() {
    set_transfer_stats_protocol_name(_("Xmodem"));
    q_transfer_stats.block_size = 128;
    if ((flavor == X_1K) || (flavor == X_1K_G)) {
        q_transfer_stats.blocks = q_transfer_stats.bytes_total / 128;
        if ((q_transfer_stats.bytes_total % 128) > 0) {
            q_transfer_stats.blocks++;
        }
    }
    flavor = X_NORMAL;
}

/**
 * Statistics: an error was encountered.
 *
 * @param format a format string + arguments for emitting to the progress
 * dialog
 */
static void stats_increment_errors(const char * format, ...) {
    char outbuf[DIALOG_MESSAGE_SIZE];
    va_list arglist;
    memset(outbuf, 0, sizeof(outbuf));
    va_start(arglist, format);
    vsprintf((char *) (outbuf + strlen(outbuf)), format, arglist);
    va_end(arglist);
    set_transfer_stats_last_message(outbuf);

    q_transfer_stats.error_count++;

    if (q_transfer_stats.error_count >= errors_max) {
        /*
         * Too many errors, abort the transfer.
         */
        set_transfer_stats_last_message(_("TRANSFER_MAX_ERRORS"));
        stop_file_transfer(Q_TRANSFER_STATE_ABORT);
        state = ABORT;
    }
}

/**
 * Statistics: a file is complete.
 */
static void stats_file_complete_ok() {
    set_transfer_stats_last_message(_("SUCCESS"));
    q_transfer_stats.bytes_transfer = q_transfer_stats.bytes_total;
    state = COMPLETE;
    stop_file_transfer(Q_TRANSFER_STATE_END);
    time(&q_transfer_stats.end_time);

    /*
     * Play music at the end of a file transfer.
     */
    if (sending == Q_TRUE) {
        play_sequence(Q_MUSIC_UPLOAD);
    } else {
        play_sequence(Q_MUSIC_DOWNLOAD);
    }
}

/**
 * Statistics: the transfer was cancelled.
 *
 * @param format a format string + arguments for emitting to the progress
 * dialog
 */
static void stats_file_cancelled(const char * format, ...) {
    char outbuf[DIALOG_MESSAGE_SIZE];
    va_list arglist;
    memset(outbuf, 0, sizeof(outbuf));
    va_start(arglist, format);
    vsprintf((char *) (outbuf + strlen(outbuf)), format, arglist);
    va_end(arglist);
    set_transfer_stats_last_message(outbuf);

    stop_file_transfer(Q_TRANSFER_STATE_ABORT);
    state = ABORT;
}

/**
 * Statistics: reset for a new file.  This is only used by Ymodem.
 *
 * @param filename the file being transferred
 * @param pathname the path filename is being transferred to or from
 * @param filesize the size of the file (known from either block 0 or because
 * this is an upload)
 * @param blocks the total number of blocks
 */
static void stats_new_file(const char * filename, const char * pathname,
                           const int filesize, const int blocks) {
    /*
     * Clear per-file statistics
     */
    q_transfer_stats.batch_bytes_transfer += q_transfer_stats.bytes_transfer;
    q_transfer_stats.blocks_transfer = 0;
    q_transfer_stats.bytes_transfer = 0;
    q_transfer_stats.error_count = 0;
    set_transfer_stats_last_message("");
    set_transfer_stats_filename(filename);
    set_transfer_stats_pathname(pathname);
    q_transfer_stats.bytes_total = filesize;
    q_transfer_stats.blocks = blocks;

    /*
     * Reset block size.  In practice this will only be used for Ymodem, but
     * for completeness here let's make it correct for all of the flavors.
     */
    if ((flavor == X_1K) ||
        (flavor == X_1K_G) ||
        (flavor == Y_NORMAL) ||
        (flavor == Y_G)
    ) {
        q_transfer_stats.block_size = 1024;
    } else {
        q_transfer_stats.block_size = 128;
    }

    q_transfer_stats.state = Q_TRANSFER_STATE_TRANSFER;
    time(&q_transfer_stats.file_start_time);

    /*
     * Log it
     */
    if (sending == Q_TRUE) {
        qlog(_("UPLOAD: sending file %s/%s, %d bytes\n"), pathname, filename,
             filesize);
    } else {
        qlog(_("DOWNLOAD: receiving file %s/%s, %d bytes\n"), pathname,
             filename, filesize);
    }

}

/**
 * Initialize a new file to upload.
 *
 * @return true if OK, false if the file could not be opened
 */
static Q_BOOL setup_for_next_file() {
    char * basename_arg;
    char * dirname_arg;
    int blocks;

    /*
     * Reset our dynamic variables
     */
    if (file != NULL) {
        fclose(file);
    }
    file = NULL;
    if (filename != NULL) {
        Xfree(filename, __FILE__, __LINE__);
    }
    filename = NULL;

    if (upload_file_list[upload_file_list_i].name == NULL) {
        /*
         * Special case: the terminator block
         */

        DLOG(("YMODEM: Terminator block (name='%s')\n",
                upload_file_list[upload_file_list_i].name));

        /*
         * Let's keep all the information the same, just increase the total
         * bytes.
         */
        q_transfer_stats.batch_bytes_transfer +=
            q_transfer_stats.bytes_transfer;
        return Q_TRUE;
    }

    /*
     * Open the file
     */
    if ((file = fopen(upload_file_list[upload_file_list_i].name,
                "rb")) == NULL) {

        DLOG(("Unable to open file %s, returning false\n",
                upload_file_list[upload_file_list_i].name));

        return Q_FALSE;
    }

    /*
     * Initialize timer for the first timeout
     */
    reset_timer();

    /*
     * Note that basename and dirname modify the arguments
     */
    basename_arg =
        Xstrdup(upload_file_list[upload_file_list_i].name, __FILE__, __LINE__);
    dirname_arg =
        Xstrdup(upload_file_list[upload_file_list_i].name, __FILE__, __LINE__);

    if (filename != NULL) {
        Xfree(filename, __FILE__, __LINE__);
    }
    filename = Xstrdup(basename(basename_arg), __FILE__, __LINE__);

    /*
     * Determine total blocks
     */
    blocks = upload_file_list[upload_file_list_i].fstats.st_size / 1024;
    if ((upload_file_list[upload_file_list_i].fstats.st_size % 1024) > 0) {
        blocks++;
    }

    /*
     * Update the stats
     */
    stats_new_file(Xstrdup(filename, __FILE__, __LINE__),
                   Xstrdup(dirname(dirname_arg), __FILE__, __LINE__),
                   upload_file_list[upload_file_list_i].fstats.st_size, blocks);

    /*
     * Free the copies passed to basename() and dirname()
     */
    Xfree(basename_arg, __FILE__, __LINE__);
    Xfree(dirname_arg, __FILE__, __LINE__);


    DLOG(("Set up for %s, returning true...\n",
            upload_file_list[upload_file_list_i].name));


    return Q_TRUE;
}

/*
 * KAL - This CRC routine was taken verbatim from XYMODEM.DOC.
 *
 * This function calculates the CRC used by the XMODEM/CRC Protocol
 * The first argument is a pointer to the message block.
 * The second argument is the number of bytes in the message block.
 * The function returns an integer which contains the CRC.
 * The low order 16 bits are the coefficients of the CRC.
 */
static int calcrc(unsigned char *ptr, int count) {
    int crc, i;

    crc = 0;
    while (--count >= 0) {
        crc = crc ^ ((int) *ptr++ << 8);
        for (i = 0; i < 8; ++i)
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc = crc << 1;
    }
    return (crc & 0xFFFF);
}

/**
 * Read from file and construct a block in current_block.  This function
 * makes the first Ymodem block that contains the filename, file size, and
 * time.
 */
static void ymodem_construct_block_0() {
    unsigned int i;
    int crc;
    char local_buffer[32];

    DLOG(("YMODEM: ymodem_construct_block0()\n"));

    assert((flavor == Y_NORMAL) || (flavor == Y_G));

    /*
     * Clear out current block
     */
    memset(current_block, 0, sizeof(current_block));
    current_block_n = 3;

    if (upload_file_list[upload_file_list_i].name != NULL) {
        /*
         * Filename
         */
        for (i = 0; i < strlen(filename); i++) {
            current_block[current_block_n] = filename[i];
            current_block_n++;
        }
        /*
         * Push past null terminator
         */
        current_block_n++;

        /*
         * Length
         */
        if (sizeof(upload_file_list[upload_file_list_i].fstats.st_size) ==
            sizeof(int)) {
            snprintf(local_buffer, sizeof(local_buffer), "%u",
                (unsigned int)
                    upload_file_list[upload_file_list_i].fstats.st_size);
        } else if (sizeof(upload_file_list[upload_file_list_i].fstats.st_size)
            == sizeof(long)) {

            snprintf(local_buffer, sizeof(local_buffer), "%lu",
                (unsigned long)
                upload_file_list[upload_file_list_i].fstats.st_size);
#ifndef WIN32
        } else if (sizeof(upload_file_list[upload_file_list_i].fstats.st_size)
            == sizeof(long long)) {

            snprintf(local_buffer, sizeof(local_buffer), "%llu",
                (unsigned long long)
                upload_file_list[upload_file_list_i].fstats.st_size);
#endif
        } else {
            snprintf(local_buffer, sizeof(local_buffer), "%u",
                (unsigned int)
                    upload_file_list[upload_file_list_i].fstats.st_size);
        }
        for (i = 0; i < strlen(local_buffer); i++) {
            current_block[current_block_n] = local_buffer[i];
            current_block_n++;
        }
        /*
         * Push past ' ' terminator
         */
        current_block[current_block_n] = ' ';
        current_block_n++;

        /*
         * Modification date
         */
        snprintf(local_buffer, sizeof(local_buffer), "%lo",
                 upload_file_list[upload_file_list_i].fstats.st_mtime);
        for (i = 0; i < strlen(local_buffer); i++) {
            current_block[current_block_n] = local_buffer[i];
            current_block_n++;
        }

        /*
         * Push past null terminator
         */
        current_block_n++;
    }

    /*
     * CRC
     */
    if (current_block_n > 128 + 5 - 2) {
        /*
         * Long block
         */
        crc = calcrc(current_block + 3, 1024);
        current_block[1023 + 4] = (crc >> 8) & 0xFF;
        current_block[1023 + 5] = crc & 0xFF;
        current_block_n = 1024 + 5;
        /*
         * STX
         */
        current_block[0] = C_STX;
    } else {
        crc = calcrc(current_block + 3, 128);
        current_block[127 + 4] = (crc >> 8) & 0xFF;
        current_block[127 + 5] = crc & 0xFF;
        current_block_n = 128 + 5;
        /*
         * SOH
         */
        current_block[0] = C_SOH;
    }

    /*
     * Sequence number
     */
    current_block[1] = current_block_sequence_i;
    current_block[2] = (0xFF - current_block_sequence_i) & 0xFF;
    current_block_sequence_i++;

    DLOG(("YMODEM: ymodem_construct_block0() current_block_n = %d\nBytes: ",
            current_block_n));
    for (i = 0; i < current_block_n; i++) {
        DLOG2(("%02x ", (current_block[i] & 0xFF)));
    }
    DLOG2(("\n"));
}

/**
 * Decode the Ymodem block 0.
 *
 * @return true if this was a valid block, false if the block was invalid OR
 * the file could not be opened.
 */
static Q_BOOL ymodem_decode_block_0() {
    unsigned int i;
    int crc;
    char full_filename[FILENAME_SIZE];
    char local_buffer[FILENAME_SIZE];
    int length;
    int blocks;

    assert((flavor == Y_NORMAL) || (flavor == Y_G));

    DLOG(("YMODEM: ymodem_decode_block0() checking block size, current_block_n = %d\nInput bytes: ",
            current_block_n));
    for (i = 0; i < current_block_n; i++) {
        DLOG2(("%02x ", (current_block[i] & 0xFF)));
    }
    DLOG2(("\n"));

    /*
     * Verify the sequence # and CRC first
     */
    if ((current_block_n != 1024 + 5) && (current_block_n != 128 + 5)) {
        stats_increment_errors(_("SHORT/LONG BLOCK #%d"), current_block_number);
        return Q_FALSE;
    }

    DLOG(("ymodem_decode_block_0() checking byte 0\n"));

    /*
     * Byte 0: SOH or STX
     */
    if ((current_block[0] != C_SOH) && (current_block[0] != C_STX)) {
        stats_increment_errors(_("HEADER ERROR IN BLOCK #%d"),
                               current_block_number);
        return Q_FALSE;
    }

    DLOG(("ymodem_decode_block_0() checking sequence #\n"));

    /*
     * Byte 1 and 2: current block counter
     */
    if (current_block_sequence_i != current_block[1]) {
        stats_increment_errors(_("BAD BLOCK NUMBER IN BLOCK #%d"),
                               current_block_number);
        return Q_FALSE;
    }
    if ((current_block[1] & 0xFF) + (current_block[2] & 0xFF) != 0xFF) {
        stats_increment_errors(_("COMPLIMENT BYTE BAD IN BLOCK #%d"),
                               current_block_number);
        return Q_FALSE;
    }

    DLOG(("ymodem_decode_block_0() checking CRC\n"));

    if (current_block[0] == C_SOH) {
        crc = calcrc(current_block + 3, 128);
        if ((((crc >> 8) & 0xFF) != current_block[127 + 4]) &&
            ((crc & 0xFF) != (current_block[127 + 5] & 0xFF))
        ) {
            /*
             * CRC didn't match
             */
            stats_increment_errors(_("CRC ERROR IN BLOCK #%d"),
                                   current_block_number);

            DLOG(("ymodem_decode_block_0() crc error: %02x %02x\n",
                    ((crc >> 8) & 0xFF), (crc & 0xFF)));

            return Q_FALSE;
        }
    } else {
        crc = calcrc(current_block + 3, 1024);
        if ((((crc >> 8) & 0xFF) != (current_block[1023 + 4] & 0xFF)) &&
            ((crc & 0xFF) != (current_block[1023 + 5] & 0xFF))
        ) {
            /*
             * CRC didn't match
             */
            stats_increment_errors(_("CRC ERROR IN BLOCK #%d"),
                                   current_block_number);

            DLOG(("ymodem_decode_block_0() crc error: %02x %02x\n",
                    ((crc >> 8) & 0xFF), (crc & 0xFF)));

            return Q_FALSE;
        }
    }

    /*
     * Block is OK, read filename
     */
    current_block_n = 3;

    /*
     * Filename
     */
    memset(local_buffer, 0, sizeof(local_buffer));
    for (i = 0; i < sizeof(local_buffer) - 1; i++) {
        local_buffer[i] = current_block[current_block_n];
        current_block_n++;
        if (current_block[current_block_n] == '\0') {
            break;
        }
    }
    /*
     * Push past null terminator
     */
    current_block_n++;

    /*
     * Save filename
     */
    if (filename != NULL) {
        Xfree(filename, __FILE__, __LINE__);
    }
    filename = Xstrdup(local_buffer, __FILE__, __LINE__);

    DLOG(("ymodem_decode_block_0() filename: %s\n", filename));

    /*
     * Return immediately on the terminator block
     */
    if (strlen(filename) == 0) {
        return Q_TRUE;
    }

    /*
     * Open file
     */
    sprintf(full_filename, "%s/%s", download_path, filename);
    if ((file = fopen(full_filename, "w+b")) == NULL) {

        DLOG(("Unable to open file for writing\n"));

        stats_increment_errors(_("FILE OPEN ERROR"));
        return Q_FALSE;
    }

    /*
     * Length
     */
    memset(local_buffer, 0, sizeof(local_buffer));
    for (i = 0; i < sizeof(local_buffer) - 1; i++) {
        local_buffer[i] = current_block[current_block_n];
        current_block_n++;
        if ((current_block[current_block_n] == '\0')
            || (current_block[current_block_n] == ' ')) {
            break;
        }
    }
    /*
     * Push past ' ' terminator
     */
    current_block[current_block_n] = ' ';
    current_block_n++;

    /*
     * Convert length to integer
     */
    length = atol(local_buffer);
    blocks = length / 1024;
    if ((length % 1024) > 0) {
        blocks++;
    }

    DLOG(("ymodem_decode_block_0() length: %d\n", length));

    /*
     * File modification time
     */
    memset(local_buffer, 0, sizeof(local_buffer));
    for (i = 0; i < sizeof(local_buffer) - 1; i++) {
        local_buffer[i] = current_block[current_block_n];
        current_block_n++;
        if ((current_block[current_block_n] == '\0') ||
            (current_block[current_block_n] == ' ')) {
            break;
        }
    }

    /*
     * Convert mod time to binary
     */
    sscanf(local_buffer, "%lo", &download_file_modtime);

    DLOG(("ymodem_decode_block_0() mod time: %s\n",
            ctime(&download_file_modtime)));


    /*
     * Finally, call stats_new_file() to initialize the progress dialog
     */
    stats_new_file(filename, q_transfer_stats.pathname, length, blocks);

    /*
     * Update for the next expected sequence number
     */
    current_block_sequence_i++;
    return Q_TRUE;
}

/**
 * Read from file and construct a block in current_block.
 *
 * @return true if OK, false if the file could not be read
 */
static Q_BOOL construct_block() {
    int i;
    unsigned char checksum;
    int crc;
    int rc;
    char notify_message[DIALOG_MESSAGE_SIZE];

    /*
     * First, verify block size
     */

    DLOG(("construct_block() crc16 = %s length = ",
            ((flavor == X_CRC) ||
                ((flavor != X_RELAXED) && (flavor != X_NORMAL))) ?
            "true" : "false"));

    if ((flavor == X_RELAXED) || (flavor == X_NORMAL) || (flavor == X_CRC)) {
        /*
         * 128-byte
         */
        DLOG2(("128\n"));

        rc = fread(current_block + 3, 1, 128, file);
        if (ferror(file)) {
            snprintf(notify_message, sizeof(notify_message),
                     _("Error reading from file \"%s\": %s"), filename,
                     strerror(errno));
            notify_form(notify_message, 0);
            stats_file_cancelled(_("DISK READ ERROR"));
            return Q_FALSE;
        }
        if (feof(file)) {
            DLOG(("LAST BLOCK\n"));
            state = LAST_BLOCK;
        }
        for (i = rc + 1; i <= 128; i++) {
            /*
             * Pad the remaining space with SUB
             */
            current_block[i + 2] = C_SUB;
        }
        if (flavor == X_CRC) {
            /*
             * CRC
             */
            crc = calcrc(current_block + 3, 128);
            current_block[127 + 4] = (crc >> 8) & 0xFF;
            current_block[127 + 5] = crc & 0xFF;
            current_block_n = 128 + 5;
        } else {
            /*
             * Checksum
             */
            checksum = 0;
            for (i = 3; i < 127 + 4; i++) {
                checksum += current_block[i];
            }
            current_block[127 + 4] = checksum;
            current_block_n = 128 + 4;
        }
        current_block[0] = C_SOH;
    } else {
        /*
         * 1024-byte, CRC only
         */
        DLOG2(("1024\n"));

        rc = fread(current_block + 3, 1, 1024, file);
        if (ferror(file)) {
            snprintf(notify_message, sizeof(notify_message),
                     _("Error reading from file \"%s\": %s"), filename,
                     strerror(errno));
            notify_form(notify_message, 0);
            stats_file_cancelled(_("DISK READ ERROR"));
            return Q_FALSE;
        }
        if (feof(file)) {
            DLOG(("LAST BLOCK\n"));
            state = LAST_BLOCK;
        }
        for (i = rc + 1; i <= 1024; i++) {
            /*
             * Pad the remaining space with SUB
             */
            current_block[i + 2] = C_SUB;
        }
        /*
         * CRC
         */
        if (rc <= 128) {
            /*
             * Use a small terminating block
             */
            crc = calcrc(current_block + 3, 128);
            current_block[127 + 4] = (crc >> 8) & 0xFF;
            current_block[127 + 5] = crc & 0xFF;
            current_block_n = 128 + 5;
            current_block[0] = C_SOH;
        } else {
            /*
             * Normal 1K block
             */
            crc = calcrc(current_block + 3, 1024);
            current_block[1023 + 4] = (crc >> 8) & 0xFF;
            current_block[1023 + 5] = crc & 0xFF;
            current_block_n = 1024 + 5;
            current_block[0] = C_STX;
        }
    }

    /*
     * Write the sequence number
     */
    current_block[1] = current_block_sequence_i;
    current_block[2] = 0xFF - current_block_sequence_i;
    current_block_sequence_i++;
    current_block_number++;
    return Q_TRUE;
}

/**
 * Verify that the block in current_block is valid and write to file.
 *
 * @return true if the block was valid AND the block wrote to disk OK
 */
static Q_BOOL verify_block() {
    unsigned int i;
    unsigned char checksum;
    int crc;
    int rc;
    unsigned char ch;
    unsigned char ch2;

    /*
     * First, verify block size
     */
    DLOG(("verify_block() checking block size, current_block_n = %d\n",
            current_block_n));
    DLOG2(("verify_block() input bytes: "));
    for (i = 0; i < current_block_n; i++) {
        DLOG2(("%02x ", (current_block[i] & 0xFF)));
    }
    DLOG2(("\n"));

    switch (flavor) {
    case X_RELAXED:
    case X_NORMAL:
        if (current_block_n != 128 + 4) {
            stats_increment_errors(_("SHORT/LONG BLOCK #%d"),
                                   current_block_number);
            return Q_FALSE;
        }
        break;
    case X_CRC:
        if (current_block_n != 128 + 5) {
            stats_increment_errors(_("SHORT/LONG BLOCK #%d"),
                                   current_block_number);
            return Q_FALSE;
        }
        break;
    case X_1K:
    case X_1K_G:
    case Y_NORMAL:
    case Y_G:
        if ((current_block_n != 1024 + 5) && (current_block_n != 128 + 5)) {
            stats_increment_errors(_("SHORT/LONG BLOCK #%d"),
                                   current_block_number);
            return Q_FALSE;
        }
        break;
    }

    DLOG(("verify_block() checking header for SOH/STX\n"));

    /*
     * Byte 0: SOH or STX
     */
    ch = current_block[0] & 0xFF;
    switch (flavor) {
    case X_RELAXED:
    case X_NORMAL:
    case X_CRC:
        if (ch != C_SOH) {
            stats_increment_errors(_("HEADER ERROR IN BLOCK #%d"),
                                   current_block_number);
            return Q_FALSE;
        }
        break;
    case X_1K:
    case X_1K_G:
    case Y_NORMAL:
    case Y_G:
        if ((ch != C_SOH) && (ch != C_STX)) {
            stats_increment_errors(_("HEADER ERROR IN BLOCK #%d"),
                                   current_block_number);
            return Q_FALSE;
        }
        break;
    }

    DLOG(("verify_block() checking sequence #, current_block_sequence_i = %d\n",
            current_block_sequence_i));

    /*
     * Byte 1 and 2: current block counter
     */
    ch = current_block[1];
    ch2 = current_block[2];
    if (current_block_sequence_i != ch) {
        stats_increment_errors(_("BAD BLOCK NUMBER IN BLOCK #%d"),
                               current_block_number);
        return Q_FALSE;
    }
    if (ch + ch2 != 0xFF) {
        stats_increment_errors(_("COMPLIMENT BYTE BAD IN BLOCK #%d"),
                               current_block_number);
        return Q_FALSE;
    }

    DLOG(("verify_block() checking CRC/checksum\n"));

    /*
     * Finally, check the checksum or the CRC
     */
    if ((flavor == X_NORMAL) || (flavor == X_RELAXED)) {
        checksum = 0;
        for (i = 3; i < 127 + 4; i++) {
            ch = current_block[i];
            checksum += ch;
        }
        if (checksum != (current_block[127 + 4] & 0xFF)) {
            stats_increment_errors(_("CHECKSUM ERROR IN BLOCK #%d"),
                                   current_block_number);

            DLOG(("verify_block() bad checksum: %02x\n", checksum));
            return Q_FALSE;
        }
    } else if (flavor == X_CRC) {
        /*
         * X_CRC: fixed-length blocks
         */
        crc = calcrc(current_block + 3, 128);
        if ((((crc >> 8) & 0xFF) != (current_block[127 + 4] & 0xFF)) &&
            ((crc & 0xFF) != (current_block[127 + 5] & 0xFF))
        ) {
            /*
             * CRC didn't match
             */
            stats_increment_errors(_("CRC ERROR IN BLOCK #%d"),
                                   current_block_number);

            DLOG(("verify_block() crc error: %02x %02x\n",
                    ((crc >> 8) & 0xFF), (crc & 0xFF)));
            return Q_FALSE;
        }
    } else {
        /*
         * X_1K or X_1K_G or Y_NORMAL or Y_G -- variable length blocks
         */
        if (current_block[0] == C_SOH) {
            crc = calcrc(current_block + 3, 128);
            if ((((crc >> 8) & 0xFF) != (current_block[127 + 4] & 0xFF)) &&
                ((crc & 0xFF) != (current_block[127 + 5] & 0xFF))
            ) {
                /*
                 * CRC didn't match
                 */
                stats_increment_errors(_("CRC ERROR IN BLOCK #%d"),
                                       current_block_number);

                DLOG(("verify_block() crc error: %02x %02x\n",
                        ((crc >> 8) & 0xFF), (crc & 0xFF)));
                return Q_FALSE;
            }
        } else {
            crc = calcrc(current_block + 3, 1024);
            if ((((crc >> 8) & 0xFF) != (current_block[1023 + 4] & 0xFF)) &&
                ((crc & 0xFF) != (current_block[1023 + 5] & 0xFF))
            ) {
                /*
                 * CRC didn't match
                 */
                stats_increment_errors(_("CRC ERROR IN BLOCK #%d"),
                                       current_block_number);

                DLOG(("verify_block() crc error: %02x %02x\n",
                        ((crc >> 8) & 0xFF), (crc & 0xFF)));
                return Q_FALSE;
            }
        }
    }

    /*
     * Check for duplicate
     */
    if ((current_block[1] & 0xFF) == current_block_sequence_i - 1) {
        /*
         * Duplicate block
         */
        stats_increment_errors(_("DUPLICATE BLOCK #%d"), current_block_number);
        DLOG(("verify_bock() duplicate block %d\n", current_block_number));
        return Q_FALSE;
    }

    DLOG(("verify_block() block OK, writing to file\n"));

    /*
     * Block is OK, so append to file
     */
    if (current_block[0] == C_SOH) {
        /*
         * 128 byte block
         */
        rc = fwrite(current_block + 3, 1, 128, file);
        if (rc != 128) {
            stats_increment_errors(_("FILE WRITE ERROR, IS DISK FULL?"));
            DLOG(("verify_block() only wrote %d instead of 128\n", rc));
            DLOG(("verify_block() ferror: %d\n", ferror(file)));
            return Q_FALSE;
        }
    } else {
        /*
         * 1024 byte block
         */
        rc = fwrite(current_block + 3, 1, 1024, file);
        if (rc != 1024) {
            stats_increment_errors(_("FILE WRITE ERROR, IS DISK FULL?"));
            DLOG(("verify_block() only wrote %d instead of 1024\n", rc));
            DLOG(("verify_block() ferror: %d\n", ferror(file)));
            return Q_FALSE;
        }
    }
    fflush(file);

    /*
     * Increment sequence #
     */
    current_block_sequence_i++;
    current_block_number++;

    /*
     * Block OK
     */
    DLOG(("verify_block() returning true, file size is now %ld\n",
            q_transfer_stats.bytes_transfer));
    return Q_TRUE;
}

/**
 * Receive a file via the Xmodem protocol from input.
 *
 * @param input the bytes from the remote side
 * @param input_n the number of bytes in input_n
 * @param output a buffer to contain the bytes to send to the remote side
 * @param output_n the number of bytes that this function wrote to output
 */
static void xmodem_receive(unsigned char * input, unsigned int * input_n,
                           unsigned char * output, unsigned int * output_n) {

    time_t now;
    int i;
    int rc;
    int filesize;
    unsigned char last_byte;
    char full_filename[FILENAME_SIZE];
    struct utimbuf utime_buffer;
    char notify_message[DIALOG_MESSAGE_SIZE];

    DLOG(("xmodem_receive() input_n = %d\n", *input_n));

    /*
     * INIT begins the entire transfer.  We send first_byte and immediately
     * switch to BLOCK to await the data.
     *
     * Enhanced Xmodem modes will switch to FIRST_BLOCK and begin awaiting
     * the data.  If a data block doesn't come in within the timeout period,
     * FIRST_BLOCK will downgrade to regular Xmodem, re-send the initial ACK,
     * and then switch to BLOCK just as regular Xmodem would have done.
     */
    if (state == INIT) {

        DLOG(("sending initial byte to begin transfer: "));

        /*
         * Send the first byte
         */
        output[0] = first_byte;
        *output_n = 1;
        if ((flavor == X_NORMAL) ||
            (flavor == X_RELAXED) ||
            (flavor == Y_NORMAL) ||
            (flavor == Y_G)
        ) {
            /*
             * Initial state for normal is BLOCK
             */
            state = BLOCK;
        } else {
            /*
             * Any others go to FIRST_BLOCK so they can fallback
             */
            state = FIRST_BLOCK;
        }
        DLOG2(("'%c'\n", output[0]));

        /*
         * Reset timer
         */
        reset_timer();

        /*
         * Clear input
         */
        *input_n = 0;
        return;
    }

    /*
     * FIRST_BLOCK is a special check for enhanced Xmodem support by the
     * sender.
     */
    if (state == FIRST_BLOCK) {
        DLOG(("waiting for verification of sender extended Xmodem mode\n"));

        if (*input_n == 0) {
            /*
             * Special-case timeout processing.  We try to send the enhanced
             * Xmodem first_byte ('C' or 'G') five times, with a three-second
             * timeout between each attempt.  If we still have no transfer,
             * we downgrade to regular Xmodem.
             */
            time(&now);
            if (now - timeout_begin > 3) {
                timeout_count++;

                DLOG(("First block timeout #%d\n", timeout_count));

                if (timeout_count >= 5) {
                    DLOG(("fallback to vanilla Xmodem\n"));
                    stats_increment_errors(_("FALLBACK TO NORMAL XMODEM"));

                    /*
                     * Send NAK
                     */
                    output[0] = C_NAK;
                    *output_n = 1;
                    prior_state = BLOCK;
                    state = PURGE_INPUT;

                    /*
                     * Downgrade to plain Xmodem
                     */
                    downgrade_to_vanilla_xmodem();
                } else {
                    stats_increment_errors(_("TIMEOUT"));
                }

                /*
                 * Reset timer
                 */
                reset_timer();

                /*
                 * Re-send the NAK
                 */
                output[0] = first_byte;
                *output_n = 1;
            }
            return;
        }

        DLOG(("sender appears to be OK, switching to data mode\n"));

        /*
         * We got some data using the enhanced Xmodem first_byte, so go into
         * the block processing.
         */
        state = BLOCK;
    }

    /*
     * BLOCK is the main receive data path.  We look for a data block from
     * the sender, decode it, write it to disk, and then send an ACK when all
     * of that works.
     */
    if (state == BLOCK) {
        /*
         * See if data has yet arrived.  It might not be here yet because
         * xmodem() is called as soon as we can write data out.
         */
        if (*input_n == 0) {
            DLOG(("No block yet\n"));

            /*
             * No data has arrived yet.  See if the timeout has been reached.
             */
            if (check_timeout(output, output_n) == Q_TRUE) {
                if (state != ABORT) {
                    /*
                     * Send NAK
                     */
                    output[0] = first_byte;
                    *output_n = 1;
                    set_transfer_stats_last_message(_("SENDING NAK #%d"),
                                                    current_block_number);
                    /*
                     * Special case: for the first block NEVER go to
                     * PURGE_INPUT state.
                     */
                    if (current_block_number == 1) {
                        state = prior_state;
                    }
                }
            }
            return;
        }

        /*
         * Data has indeed arrived.  See what it is.
         */
        reset_timer();
        if ((current_block_n + *input_n > XMODEM_MAX_BLOCK_SIZE) &&
            (flavor != X_1K_G) && (flavor != Y_G)
        ) {
            /*
             * Too much data was sent and this isn't 1K/G.  Only Xmodem-1K/G
             * streams blocks, so if we got more than XMODEM_MAX_BLOCK_SIZE
             * we must have encountered line noise.  Wait for the input queue
             * to clear and then have the PURGE_INPUT state send a NAK to
             * continue.
             */
            prior_state = BLOCK;
            state = PURGE_INPUT;

            /*
             * Clear input
             */
            *input_n = 0;
            return;

        } else if (((flavor == X_1K_G) || (flavor == Y_G)) &&
                   (!((current_block_sequence_i == 0) &&
                       (current_block_number == 1)))
        ) {
            /*
             * Xmodem - 1K/G case: pull in just enough to make a complete
             * block, process it, and come back for more.
             */
            unsigned int n = 1024 + 5;
            if (current_block[0] == C_SOH) {
                /*
                 * We need a short block, not a long one.
                 */
                n = 128 + 5;
            }
            if (current_block_n >= n) {
                /*
                 * We already have enough for a packet.  But this should not
                 * have happened.
                 */
                assert(current_block_n < n);
            }

            if (*input_n + current_block_n < n) {
                /*
                 * We need more data, but it is not here.  Save what we have,
                 * wait for more.
                 */
                DLOG(("Not yet enough data for a block\n"));

                memcpy(current_block + current_block_n, input, *input_n);
                current_block_n += *input_n;
                *input_n = 0;

                if ((current_block_n == 1) && (current_block[0] == C_EOT)) {
                    /*
                     * EOT, handle below
                     */
                    goto eot;
                }
                return;
            }

            DLOG(("XMODEM-G: adding up to %d bytes of data\n",
                    n - current_block_n));
            memcpy(current_block + current_block_n, input, n - current_block_n);
            *input_n -= (n - current_block_n);
            memmove(input, input + (n - current_block_n), *input_n);
            current_block_n = n;

            /*
             * We have enough data for a full block.
             */
            if (verify_block() == Q_FALSE) {
                /*
                 * In G land this is a fatal error, ABORT
                 */
                clear_block();
                stats_file_cancelled(_("Xmodem 1K/G error"));

                /*
                 * Clear input
                 */
                *input_n = 0;
                return;
            }

            DLOG(("block OK, 1K/G NOT sending ACK\n"));
            stats_increment_blocks(current_block);
            clear_block();

            /*
             * Toss the block in input and return.
             */
            return;
        } /* -G protocol handling */

        /*
         * For the non-G flavors: we've got data than can fit inside
         * current_block.  Append it to current_block.
         */
        if (current_block_n + *input_n > sizeof(current_block)) {
            /*
             * We are lost.  Throw the block away and request it again.
             */
            clear_block();
            prior_state = BLOCK;
            state = PURGE_INPUT;
            /*
             * Clear input
             */
            *input_n = 0;
            return;
        }

        memcpy(current_block + current_block_n, input, *input_n);
        current_block_n += *input_n;

        /*
         * Special case: EOT means the last block received ended the file.
         */
        if ((current_block_n == 1) && (current_block[0] == C_EOT)) {
eot:
            DLOG(("EOT, saving file...\n"));

            /*
             * Clear out current_block
             */
            clear_block();

            /*
             * Xmodem pads the file with SUBs.  We generally don't want these
             * SUBs to be in the final file image, as that leads to a corrupt
             * file.  So eliminate the SUB's in the tail.  Note we do NOT do
             * this for Ymodem.
             */
            for (; (flavor != Y_NORMAL) && (flavor != Y_G);) {
                filesize = ftell(file);
                rc = fseek(file, -1, SEEK_END);
                if (rc != 0) {
                    snprintf(notify_message, sizeof(notify_message),
                             _("Error seeking in file \"%s\": %s"), filename,
                             strerror(errno));
                    notify_form(notify_message, 0);
                }
                i = fread(&last_byte, 1, 1, file);
                if (i != 1) {
                    snprintf(notify_message, sizeof(notify_message),
                             _("Error reading from file \"%s\": %s"), filename,
                             strerror(errno));
                    notify_form(notify_message, 0);
                }
                if (last_byte == C_SUB) {
                    rc = ftruncate(fileno(file), filesize - 1);
                    if (rc != 0) {
                        snprintf(notify_message, sizeof(notify_message),
                                 _("Error truncating file \"%s\": %s"),
                                 filename, strerror(errno));
                        notify_form(notify_message, 0);
                    }
                    /*
                     * Special case: decrement the total bytes as we save the
                     * file.
                     */
                    q_transfer_stats.bytes_transfer--;
                    q_transfer_stats.bytes_total--;
                } else {
                    /*
                     * Done!  Send the ACK to end the transfer.
                     */
                    output[0] = C_ACK;
                    *output_n = 1;

                    /*
                     * Set the final transfer state.
                     */
                    stats_file_complete_ok();
                    break;
                }
            }

            if ((flavor == Y_NORMAL) || (flavor == Y_G)) {

                /*
                 * For Ymodem, we already have the file size from Block 0, so
                 * we can just truncate the file to correct size.
                 */
                ftruncate(fileno(file), q_transfer_stats.bytes_total);

                /*
                 * The file is fully written, so close it.
                 */
                fclose(file);
                file = NULL;

                /*
                 * Modify the file's times to reflect what was sent.  For
                 * POSIX systems, we will set both access and modification
                 * time to the transferred time stamp.
                 */
                sprintf(full_filename, "%s/%s", download_path, filename);
                utime_buffer.actime = download_file_modtime;
                utime_buffer.modtime = download_file_modtime;
                utime(full_filename, &utime_buffer);

                /*
                 * Not translated since it isn't a sentence.
                 */
                set_transfer_stats_last_message("EOF");

                /*
                 * Set the appropriate transfer stats state
                 */
                q_transfer_stats.state = Q_TRANSFER_STATE_FILE_DONE;

                /*
                 * The last file is completely finished.  Setup now for the
                 * next file to download.
                 */
                DLOG(("YMODEM: Sending EOT ACK and first_byte\n"));

                /*
                 * Send the ACK and the first byte again
                 */
                output[0] = C_ACK;
                output[1] = first_byte;
                *output_n = 2;

                /*
                 * Reset the Block 0 check flag
                 */
                block0_has_been_seen = Q_FALSE;
                current_block_sequence_i = 0;
                current_block_number = 1;
            }

            /*
             * Clear input
             */
            *input_n = 0;
            return;
        }

        if (((flavor == Y_NORMAL) || (flavor == Y_G)) &&
            (current_block_sequence_i == 0) &&
            (current_block_number == 1) && (block0_has_been_seen == Q_FALSE)
        ) {
            /*
             * Ymodem: look for block 0.
             */
            if (((current_block[0] == C_STX) &&
                    (current_block_n >= XMODEM_MAX_BLOCK_SIZE)) ||
                ((current_block[0] == C_SOH) && (current_block_n >= 128 + 5))
            ) {

                DLOG(("YMODEM: block 0 received, calling ymodem_decode_block0()...\n"));

                if (ymodem_decode_block_0() == Q_TRUE) {
                    /*
                     * Send the ACK and first_byte again to start the
                     * transfer.
                     */
                    output[0] = C_ACK;
                    output[1] = first_byte;
                    *output_n = 2;
                    block0_has_been_seen = Q_TRUE;
                    /*
                     * Clear the block
                     */
                    clear_block();
                } else {
                    /*
                     * Throw the block away and request it again.
                     */
                    clear_block();
                    prior_state = BLOCK;
                    state = PURGE_INPUT;
                    /*
                     * Clear input
                     */
                    *input_n = 0;
                    return;
                }

                /*
                 * We got block 0.  See if this is the terminator block, and
                 * if so end the transfer.
                 */
                if (strlen(filename) == 0) {
                    DLOG(("YMODEM: terminator block received\n"));

                    /*
                     * Send ACK and end
                     */
                    output[0] = C_ACK;
                    *output_n = 1;

                    /*
                     * Set the final transfer state.
                     */
                    stats_file_complete_ok();
                }

            } else {
                /*
                 * We are looking for block 0, but don't have enough length
                 * yet.
                 */
                DLOG(("YMODEM: need more for block0\n"));
                DLOG(("   current_block[0] == %02x\n", current_block[0]));
                DLOG(("   current_block_n %d\n", current_block_n));
            }

            /*
             * Clear input
             */
            *input_n = 0;
            return;

        } /* Ymodem block 0 handling */

        /*
         * This is a normal data block, either for Xmodem or
         * Ymodem-not-block-0.  See if there enough in current_block to
         * process it.
         */
        switch (flavor) {
        case Y_NORMAL:
        case Y_G:
        case X_1K:
        case X_1K_G:
            /*
             * Block size is 1024 + 1 + 4
             */
            if ((current_block[0] == C_STX) &&
                (current_block_n < XMODEM_MAX_BLOCK_SIZE)
            ) {
                /*
                 * Waiting for more data
                 */
                DLOG(("waiting for more data (1k block)...\n"));

                /*
                 * Clear input
                 */
                *input_n = 0;
                return;
            }
            /*
             * Fall through ...
             */
        case X_CRC:
            if (current_block_n < 128 + 5) {
                /*
                 * Waiting for more data
                 */
                DLOG(("waiting for more data (128 byte block with CRC)...\n"));

                /*
                 * Clear input
                 */
                *input_n = 0;
                return;
            }
            break;
        default:
            if (current_block_n < 128 + 4) {
                /*
                 * Waiting for more data
                 */
                DLOG(("waiting for more data (128 byte block)...\n"));

                /*
                 * Clear input
                 */
                *input_n = 0;
                return;
            }
            break;
        }

        /*
         * We have enough for a full block.
         */
        DLOG(("block received, calling verify_block()...\n"));

        /*
         * Normal case: a data block came in.  We verify the block data first
         * with verify_block() and either ACK or NAK.
         */
        if (verify_block() == Q_FALSE) {
            /*
             * verify_block() has already posted the appropriate error
             * message to the progress dialog.
             */
            if (state == ABORT) {
                /*
                 * Clear input
                 */
                *input_n = 0;
                return;
            }

            /*
             * Throw the block away and request it again
             */
            clear_block();
            prior_state = BLOCK;
            state = PURGE_INPUT;
            /*
             * Clear input
             */
            *input_n = 0;
            return;
        }

        /*
         * The data block was fine, so send an ACK and keep going...
         */
        DLOG(("block OK, sending ACK\n"));
        output[0] = C_ACK;
        *output_n = 1;
        stats_increment_blocks(current_block);
        clear_block();

        /*
         * Clear input
         */
        *input_n = 0;
        return;
    }

    /*
     * This is the general state for a receive error.  We wait until the
     * input buffer is clear, and then send a NAK to request whatever was
     * sent to be re-sent.
     *
     * We don't do this when waiting for the very first block, but that is
     * because we are still negotiating the Ymodem/Xmodem flavor.
     */
    if (state == PURGE_INPUT) {

        DLOG(("xmodem_receive PURGE INPUT\n"));

        if (*input_n == 0) {
            /*
             * Send the NAK
             */
            output[0] = C_NAK;
            *output_n = 1;
            state = prior_state;
            set_transfer_stats_last_message(_("SENDING NAK #%d"),
                                            current_block_number);
        }
    }

    DLOG(("xmodem_receive returning at the end()\n"));

    /*
     * Clear input
     */
    *input_n = 0;
    return;
}

/**
 * Send a file via the Xmodem protocol to output.
 *
 * @param input the bytes from the remote side
 * @param input_n the number of bytes in input_n
 * @param output a buffer to contain the bytes to send to the remote side
 * @param output_n the number of bytes that this function wrote to output
 */
static void xmodem_send(unsigned char * input, unsigned int * input_n,
                        unsigned char * output, unsigned int * output_n) {


    DLOG(("xmodem_send() STATE = %d input_n = %d\n", state, *input_n));

    if ((*input_n > 0) && (input[0] == C_CAN)) {
        stats_file_cancelled(_("TRANSFER CANCELLED BY RECEIVER"));
        /*
         * Clear input
         */
        *input_n = 0;
        return;
    }

    /*
     * This state is where everyone begins.  The receiver is going to send
     * first_byte, we're just marking time until we see it.
     */
    if (state == INIT) {
        DLOG(("waiting for receiver to initiate transfer...\n"));

        set_transfer_stats_last_message(_("WAITING FOR NAK"));

        /*
         * Do timeout processing
         */
        if (*input_n == 0) {
            check_timeout(output, output_n);
            return;
        }

        /*
         * We've got some data, check it out
         */
        if (*input_n >= 1) {
            DLOG(("received char: '%c'\n", input[0]));

            /*
             * It would be nice to just look for first_byte and zip off to
             * BLOCK state.  But we need to see if the receiver is using the
             * same kind of Xmodem enhancement we expect.  If not, we need to
             * downgrade.  So we have a switch for the various flavor
             * downgrade options.
             */
            switch (flavor) {
            case X_RELAXED:
            case X_NORMAL:
                if (input[0] == first_byte) {
                    /*
                     * We're good to go.
                     */
                    state = BLOCK;

                    /*
                     * Clear the last message
                     */
                    set_transfer_stats_last_message("");

                    /*
                     * Put an ACK here so the if (state == BLOCK) case can
                     * construct the first block.
                     */
                    input[0] = C_ACK;
                } else {
                    /*
                     * Error.  Wait and see if the receiver will downgrade.
                     */

                    /*
                     * Clear input
                     */
                    *input_n = 0;
                    return;
                }
                break;
            case X_CRC:
            case X_1K:
            case X_1K_G:
                if (input[0] == first_byte) {
                    /*
                     * We're good to go.
                     */
                    state = BLOCK;
                    /*
                     * Put an ACK here so the if (state == BLOCK) case can
                     * construct the first block.
                     */

                    /*
                     * Clear the last message
                     */
                    set_transfer_stats_last_message("");

                    input[0] = C_ACK;
                } else if (input[0] == C_NAK) {

                    DLOG(("fallback to vanilla Xmodem\n"));

                    /*
                     * Clear the last message
                     */
                    set_transfer_stats_last_message("");

                    /*
                     * Downgrade to plain Xmodem
                     */
                    downgrade_to_vanilla_xmodem();

                    /*
                     * Put an ACK here so the if (state == BLOCK) case can
                     * construct the first block.
                     */
                    input[0] = C_ACK;
                    state = BLOCK;
                } else {
                    /*
                     * Error, proceed to timeout case.  Just return and the
                     * next xmodem_send() will do timeout checks.
                     */

                    /*
                     * Clear input
                     */
                    *input_n = 0;
                    return;
                }
                break;
            case Y_NORMAL:
            case Y_G:
                if (input[0] == first_byte) {
                    /*
                     * We're good to go.
                     */
                    state = YMODEM_BLOCK0;
                } else {
                    /*
                     * Error, proceed to timeout case.  Just return and the
                     * next xmodem_send() will do timeout checks.
                     */
                }
                break;

            } /* switch (flavor) */

        } /* if (*input_n == 1) */

        /*
         * At this point, we've either gotten the first_byte we expect, or
         * we've downgraded to vanilla Xmodem, OR we've seen complete garbage
         * from the receiver.  In the first two cases, we've already switched
         * state to BLOCK and we have a NAK waiting on the input queue.  In
         * the last case, we're still in INIT state.
         *
         * Since we've got NAK/first_byte, we need to fall through to the
         * BLOCK state and begin sending data, so we DON'T put a return here.
         *
         * Finally, for Ymodem, when we saw first_byte we switched to
         * YMODEM_BLOCK0 state.
         */
    }

    /*
     * Ymodem has a weird startup sequence:
     *
     * 1) Wait for 'C' or 'G'.
     * 2) Send block 0
     * 3) Wait for ACK
     * 4) Wait for 'C' or 'G' AGAIN.
     * 5) Send data...
     *
     * It's that step 4 that creates this mess of YMODEM_BLOCK0 states.  We
     * might get ACK then 'C'/'G' as two separated calls to xmodem(), OR we
     * might get ACK + 'C'/'G' as one call.
     *
     * Our state machine goes like this:
     *   INIT
     *     Got first_byte       --> YMODEM_BLOCK0
     *   YMODEM_BLOCK0
     *     Send block 0         --> YMODEM_BLOCK0_ACK1
     *   YMODEM_BLOCK0_ACK1
     *     See ACK alone        --> YMODEM_BLOCK0_ACK2
     *     See ACK + first_byte --> BLOCK
     *   YMODEM_BLOCK0_ACK2
     *     See first_byte       --> BLOCK
     */
    if (state == YMODEM_BLOCK0) {
        /*
         * Send block 0
         */
        DLOG(("YMODEM: Sending block 0\n"));

        ymodem_construct_block_0();
        assert(*output_n == 0);
        memcpy(output + *output_n, current_block, current_block_n);
        *output_n += current_block_n;

        /*
         * Tell the user, but only if we're really sending another file
         */
        if (filename != NULL) {
            set_transfer_stats_last_message(_("SENDING HEADER"));
        }

        /*
         * Switch state
         */
        state = YMODEM_BLOCK0_ACK1;

        /*
         * Reset timer
         */
        reset_timer();

        /*
         * Clear input
         */
        *input_n = 0;
        return;
    }

    if (state == YMODEM_BLOCK0_ACK1) {

        if (flavor == Y_G) {

            DLOG(("YMODEM-G: Block 0 is out, now looking for 'G'\n"));

            /*
             * Special case: we can dump out immediately after the last file.
             */
            if (filename == NULL) {

                DLOG(("YMODEM-G: DONE\n"));

                /*
                 * Set the final transfer state.
                 */
                stats_file_complete_ok();

                /*
                 * Clear input
                 */
                *input_n = 0;
                return;
            }
        } else {
            DLOG(("YMODEM: Block 0 is out, now looking for ACK\n"));
        }

        if (*input_n == 0) {
            check_timeout(output, output_n);
            return;
        }

        if ((((*input_n >= 1)) && ((input[0] == C_ACK) && (flavor == Y_NORMAL)))
            || (((input[0] == 'G') && (flavor == Y_G)))
        ) {

            if (flavor == Y_G) {
                DLOG(("YMODEM-G: Block 0 'G' received\n"));
            } else {
                DLOG(("YMODEM: Block 0 ACK received\n"));
            }

            if (filename == NULL) {
                DLOG(("YMODEM: DONE\n"));

                /*
                 * Set the final transfer state.
                 */
                stats_file_complete_ok();

                /*
                 * Clear input
                 */
                *input_n = 0;
                return;
            }

            if (flavor == Y_NORMAL) {

                /*
                 * ACK received
                 */
                state = YMODEM_BLOCK0_ACK2;

                /*
                 * Check for 'C' or 'G'
                 */
                if (*input_n == 2) {
                    if (input[1] == first_byte) {

                        DLOG(("YMODEM: block 0 '%c' received\n", first_byte));

                        state = BLOCK;
                        /*
                         * Put an ACK here so the if (state == BLOCK) case
                         * can construct the first block.
                         */
                        input[0] = C_ACK;
                        *input_n = 1;
                    }
                }
            } else {
                /*
                 * Ymodem-G go straight to BLOCK
                 */
                state = BLOCK;

                /*
                 * Toss input
                 */
                *input_n = 0;
            }
        }

        if ((*input_n == 1) && (input[0] == C_NAK)) {

            DLOG(("YMODEM: NAK on block 0, re-sending\n"));

            state = YMODEM_BLOCK0;
            /*
             * Reset the sequence number
             */
            current_block_sequence_i = 0;

            /*
             * Clear input
             */
            *input_n = 0;
            return;
        }
        /*
         * Like the exit point of INIT, we might be ready for BLOCK if both
         * ACK and first_byte were seen.  So don't return, fall through to
         * send the first block.
         */

        /*
         * Clear the last message
         */
        set_transfer_stats_last_message("");
    }

    if (state == YMODEM_BLOCK0_ACK2) {
        DLOG(("YMODEM: Block 0 is out, now looking for 'C' or 'G'\n"));

        if (*input_n == 0) {
            check_timeout(output, output_n);
            return;
        }
        if ((*input_n == 1) && (input[0] == first_byte)) {
            DLOG(("YMODEM: block 0 '%c' received\n", first_byte));

            /*
             * Good to go
             */
            state = BLOCK;
            /*
             * Put an ACK here so the if (state == BLOCK) case can construct
             * the first block.
             */
            input[0] = C_ACK;
        }
        /*
         * Like the exit point of INIT, we might be ready for BLOCK if the
         * first_byte was seen.  So don't return, fall through to send the
         * first block.
         */

        /*
         * Clear the last message
         */
        set_transfer_stats_last_message("");
    }

    /*
     * This is the meat of send.  We make sure that an ACK is waiting in
     * input first to let us know that the previous block was OK.  Then we
     * construct and send out the next block.
     */
    if (state == BLOCK) {
        DLOG(("looking for ACK\n"));

        /*
         * See if ACK is here
         */
        if (((*input_n == 1) && (input[0] == C_ACK)) ||
            (flavor == X_1K_G) ||
            (flavor == Y_G)
        ) {
            /*
             * The receiver sent an ACK, so we can send a new block.
             */

            /*
             * Reset timer
             */
            reset_timer();


            if ((flavor == X_1K_G) || (flavor == Y_G)) {
                DLOG(("-G no ACK needed, keep going\n"));
            } else {
                DLOG(("ACK found, constructing block %d\n",
                        current_block_sequence_i));
            }

            /*
             * Construct block.  Note that if this is the last block state
             * will be LAST_BLOCK.
             */
            clear_block();
            if (construct_block() == Q_FALSE) {
                DLOG(("ABORT, file error\n"));

                /*
                 * construct_block() has already cancelled the transfer if it
                 * encountered an error with local I/O.
                 */

                /*
                 * Clear input
                 */
                *input_n = 0;
                return;
            }

            DLOG(("Delivering block\n"));
            DLOG(("output_n %d current_block_n %d\n", *output_n,
                    current_block_n));

            /*
             * Send the block out.
             */
            if ((flavor != X_1K_G) && (flavor != Y_G)) {
                assert(*output_n == 0);
            }

            memcpy(output + *output_n, current_block, current_block_n);
            *output_n += current_block_n;

            /*
             * Update stats on the prior block
             */
            if (state == LAST_BLOCK) {
                DLOG(("Sending last block...\n"));
                q_transfer_stats.bytes_transfer = q_transfer_stats.bytes_total;
                q_screen_dirty = Q_TRUE;
            } else {
                stats_increment_blocks(input);
            }

            /*
             * Clear input
             */
            *input_n = 0;
            return;

        } else if ((*input_n == 1) && (input[0] == C_NAK)) {
            /*
             * The receiver sent a NAK, so we have to re-send the current
             * block.
             */
            DLOG(("NAK found, resending block\n"));

            prior_state = BLOCK;
            state = PURGE_INPUT;
            /*
             * Not translated since it isn't a real sentence
             */
            stats_increment_errors("NAK");

            /*
             * Clear input
             */
            *input_n = 0;
            return;
        } else if (*input_n == 0) {
            /*
             * Still nothing from the receiver, so do timeout processing.
             */
            if (check_timeout(output, output_n) == Q_TRUE) {
                /*
                 * Re-send the block just in case.
                 */
                prior_state = BLOCK;
                state = PURGE_INPUT;
            }
            return;
        } else if (*input_n > 0) {
            /*
             * The receiver sent me some garbage, re-send the block.  But
             * first purge whatever else he sent.
             */
            prior_state = BLOCK;
            state = PURGE_INPUT;
            stats_increment_errors(_("LINE NOISE, !@#&*%U"));
            /*
             * Clear input
             */
            *input_n = 0;
            return;
        }
    }

    /*
     * The only other state using PURGE_INPUT is BLOCK and LAST_BLOCK.  We
     * get here when the receiver sent us garbage or NAK instead of a clear
     * ACK.
     */
    if (state == PURGE_INPUT) {
        /*
         * Reset timer
         */
        reset_timer();

        DLOG(("Waiting for input buffer to clear\n"));

        if (*input_n == 0) {

            DLOG(("Re-sending current block\n"));

            if ((prior_state == BLOCK) || (prior_state == LAST_BLOCK)) {
                /*
                 * Re-send the current block
                 */
                assert(*output_n == 0);
                memcpy(output + *output_n, current_block, current_block_n);
                *output_n += current_block_n;
            }
            state = prior_state;
        }

        /*
         * Clear input
         */
        *input_n = 0;
        return;
    }

    /*
     * This is the special case for when the EOT is ready to be transmitted.
     * construct_block() changed our state to LAST_BLOCK when it encountered
     * EOF.
     */
    if (state == LAST_BLOCK) {
        /*
         * See if the receiver ACK'd the last block.
         */
        if (((*input_n == 1) && (input[0] == C_ACK)) ||
            (flavor == X_1K_G) ||
            (flavor == Y_G)
        ) {
            /*
             * The receiver ACK'd the last block.  Send EOT to end the
             * transfer.
             */
            DLOG(("Sending EOT\n"));

            output[*output_n] = C_EOT;
            *output_n += 1;
            state = EOT_ACK;
            set_transfer_stats_last_message(_("SENDING EOT"));

            /*
             * Increment on the last block now that it's ACK'd
             */
            q_transfer_stats.blocks_transfer++;
            q_transfer_stats.bytes_transfer = q_transfer_stats.bytes_total;
            q_screen_dirty = Q_TRUE;

            /*
             * Reset timer
             */
            reset_timer();
            /*
             * Clear input
             */
            *input_n = 0;
            return;
        } else if ((*input_n == 1) && (input[0] == C_NAK)) {
            /*
             * Oops!  The receiver said the last block was bad.  Re-send the
             * last block.
             */

            DLOG(("NAK on LAST BLOCK, resending...\n"));

            prior_state = LAST_BLOCK;
            state = PURGE_INPUT;
            /*
             * Not translated since it isn't a real sentence
             */
            stats_increment_errors("NAK");
            /*
             * Clear input
             */
            *input_n = 0;
            return;
        } else if (*input_n == 0) {
            /*
             * Do timeout checks
             */
            check_timeout(output, output_n);
            return;
        } else {
            /*
             * The receiver sent me some garbage, re-send the block.  But
             * first purge whatever else he sent.
             */
            prior_state = LAST_BLOCK;
            state = PURGE_INPUT;
            stats_increment_errors(_("LINE NOISE, !@#&*%U"));
            return;
        }
    }

    /*
     * The transfer is done!  We are now waiting to see the receiver ACK the
     * EOT.
     */
    if (state == EOT_ACK) {
        DLOG(("Waiting for ACK to EOT\n"));

        if (*input_n == 0) {
            if (check_timeout(output, output_n) == Q_TRUE) {
                /*
                 * We got a timeout so re-send the EOT
                 */
                output[*output_n] = C_EOT;
                *output_n += 1;
            }
            return;
        }

        if ((*input_n >= 1) && (input[0] == C_ACK)) {
            /*
             * DONE
             */
            fclose(file);
            file = NULL;

            DLOG(("Received EOT ACK\n"));

            if ((flavor == Y_NORMAL) || (flavor == Y_G)) {
                /*
                 * Ymodem special case: This was just the first file!  Get it
                 * ready for the next file.
                 */
                /*
                 * Not translated since it isn't a sentence
                 */
                set_transfer_stats_last_message("EOF");
                q_transfer_stats.bytes_transfer = q_transfer_stats.bytes_total;

                /*
                 * Setup for the next file
                 */
                upload_file_list_i++;
                setup_for_next_file();
                current_block_sequence_i = 0;
                current_block_number = 1;
                timeout_count = 0;
                clear_block();

                /*
                 * Switch state
                 */
                state = INIT;

                /*
                 * Reset timer
                 */
                reset_timer();

                /*
                 * Shift input down 1 byte
                 */
                if (*input_n > 1) {
                    int n = *input_n - 1;
                    memmove(input, input + 1, n);
                    *input_n = n;
                }

                DLOG(("YMODEM: Back to INIT\n"));
                return;
            }

            /*
             * Normal Xmodem case
             */

            /*
             * Set the final transfer state.
             */
            stats_file_complete_ok();
        }
    }

    DLOG(("xmodem_send returning at the end()\n"));

    /*
     * Clear input
     */
    *input_n = 0;
    return;
}

/**
 * Process raw bytes from the remote side through the transfer protocol.  See
 * also protocol_process_data().
 *
 * @param input the bytes from the remote side
 * @param input_n the number of bytes in input_n
 * @param remaining the number of un-processed bytes that should be sent
 * through a future invocation of xmodem()
 * @param output a buffer to contain the bytes to send to the remote side
 * @param output_n the number of bytes that this function wrote to output
 * @param output_max the maximum number of bytes this function may write to
 * output
 */
void xmodem(unsigned char * input, const unsigned int input_n, int * remaining,
            unsigned char * output, unsigned int * output_n,
            const int output_max) {

    unsigned int i;

    /*
     * Check my input arguments
     */
    assert(input_n >= 0);
    assert(input != NULL);
    assert(output != NULL);
    assert(*output_n >= 0);
    assert(output_max > XMODEM_MAX_BLOCK_SIZE);

    /*
     * It's amazing how little documentation exists for Xmodem and Ymodem in
     * paper form.  My local university library only had one book with enough
     * detail in it to actually implement bare-bones Xmodem.  I've got
     * another on order that supposedly has lots of great detail.
     *
     * OTOH, in electronic form I find Chuck Forsberg's "Tower of Babel"
     * document that describes to byte-level detail exactly how Xmodem
     * (checksum, CRC, and 1K) work along with Ymodem.
     *
     * The (X/Y/Z)modem protocols really are a product of the early online
     * culture.  Today's computer bookstore or library has NO real
     * information about this most fundamental operation that almost every
     * modem program in the world has implemented.  But they've got hundreds
     * of books about Cisco routers, Oracle databases, and artificial
     * intelligence -- none of which are present on a typical home system.
     * <sigh>
     *
     */

    if (state == ABORT) {
        return;
    }

    DLOG(("state = %d current_block_n = %d\n", state, current_block_n));
    if (flavor == X_NORMAL) {
        DLOG(("flavor = X_NORMAL\n"));
    }
    if (flavor == X_CRC) {
        DLOG(("flavor = X_CRC\n"));
    }
    if (flavor == X_RELAXED) {
        DLOG(("flavor = X_RELAXED\n"));
    }
    if (flavor == X_1K) {
        DLOG(("flavor = X_1K\n"));
    }
    if (flavor == X_1K_G) {
        DLOG(("flavor = X_1K_G\n"));
    }
    if (flavor == Y_NORMAL) {
        DLOG(("flavor = Y_NORMAL\n"));
    }
    if (flavor == Y_G) {
        DLOG(("flavor = Y_G\n"));
    }

    DLOG(("%d input bytes: ", input_n));
    for (i = 0; i < input_n; i++) {
        DLOG2(("%02x ", (input[i] & 0xFF)));
    }
    DLOG2(("\n"));

    if (sending == Q_FALSE) {
        /*
         * -G protocols might see multiple packets in the receive buffer, so
         * loop this.
         */
        unsigned int n = input_n;
        do {
            xmodem_receive(input, &n, output, output_n);
        } while (n > 0);
    } else {
        unsigned int n = input_n;
        if (output_max - *output_n < XMODEM_MAX_BLOCK_SIZE) {
            /*
             * Don't send unless there is enough room for a full block
             */
            return;
        }
        /*
         * -G protocols need to construct multiple packets in the send
         * buffer, so loop this.
         */
        do {
            xmodem_send(input, &n, output, output_n);
        } while (n > 0);
    }

    /*
     * All of the input was consumed
     */
    *remaining = 0;

    DLOG(("cleared input queue\n"));
    DLOG(("%d output bytes: ", *output_n));
    for (i = 0; i < *output_n; i++) {
        DLOG2(("%02x ", (output[i] & 0xFF)));
    }
    DLOG2(("\n"));

}

/**
 * Setup for a new file transfer session.
 *
 * @param in_filename the filename to save downloaded file data to, or the
 * name of the file to upload.
 * @param send if true, this is an upload
 * @param in_flavor the type of Xmodem transfer to perform
 */
Q_BOOL xmodem_start(const char * in_filename, const Q_BOOL send,
                    const XMODEM_FLAVOR in_flavor) {
    struct stat fstats;
    char notify_message[DIALOG_MESSAGE_SIZE];

    /*
     * Assume we don't start up successfully
     */
    state = ABORT;

    DLOG(("START flavor = %d ", in_flavor));

    if (send == Q_TRUE) {
        /*
         * Pull the file size
         */
        if (stat(in_filename, &fstats) < 0) {
            return Q_FALSE;
        }

        if ((file = fopen(in_filename, "rb")) == NULL) {
            DLOG2(("false\n"));
            return Q_FALSE;
        }
        /*
         * Initialize timer for the first timeout
         */
        reset_timer();
    } else {
        if ((file = fopen(in_filename, "w+b")) == NULL) {
            DLOG2(("false\n"));
            return Q_FALSE;
        }
    }

    filename = Xstrdup(in_filename, __FILE__, __LINE__);
    sending = send;
    flavor = in_flavor;
    state = INIT;
    current_block_sequence_i = 1;
    current_block_number = 1;
    timeout_count = 0;
    timeout_max = 10;
    clear_block();

    if (sending == Q_TRUE) {
        if (stat(filename, &fstats) < 0) {
            snprintf(notify_message, sizeof(notify_message),
                     _("Error stat()'ing file \"%s\": %s"), filename,
                     strerror(errno));
            notify_form(notify_message, 0);
            return Q_FALSE;
        }

        switch (flavor) {

        case X_RELAXED:
        case X_NORMAL:
        case X_CRC:
            if (sending == Q_TRUE) {
                q_transfer_stats.bytes_total = fstats.st_size;
                q_transfer_stats.blocks = fstats.st_size / 128;
            }
            if ((q_transfer_stats.blocks % 128) > 0) {
                q_transfer_stats.blocks++;
            }
            break;
        case X_1K:
        case X_1K_G:
            if (sending == Q_TRUE) {
                q_transfer_stats.bytes_total = fstats.st_size;
                q_transfer_stats.blocks = fstats.st_size / 1024;
            }
            if ((q_transfer_stats.blocks % 1024) > 0) {
                q_transfer_stats.blocks++;
            }
            break;
        default:
            /*
             * Should never get here
             */
            abort();
        }
    }

    /*
     * Set block_size
     */
    if ((flavor == X_1K) || (flavor == X_1K_G)) {
        q_transfer_stats.block_size = 1024;
    } else {
        q_transfer_stats.block_size = 128;
    }

    /*
     * Set first byte
     */
    switch (flavor) {
    case X_RELAXED:
        timeout_max *= 10;
        /*
         * Fall through
         */
    case X_NORMAL:
        first_byte = C_NAK;
        break;
    case X_CRC:
    case X_1K:
        first_byte = 'C';
        break;
    case X_1K_G:
        first_byte = 'G';
        break;
    default:
        /*
         * Should never get here
         */
        abort();
    }

    /*
     * Clear the last message
     */
    set_transfer_stats_last_message("");

    DLOG(("true\n"));
    return Q_TRUE;
}

/**
 * Stop the file transfer.  Note that this function is only called in
 * stop_file_transfer() and save_partial is always true.  However it is left
 * in for API completeness.
 *
 * @param save_partial if true, save any partially-downloaded files.
 */
void xmodem_stop(const Q_BOOL save_partial) {
    char notify_message[DIALOG_MESSAGE_SIZE];

    DLOG(("STOP\n"));

    if ((save_partial == Q_TRUE) || (sending == Q_TRUE)) {
        if (file != NULL) {
            fflush(file);
            fclose(file);
        }
    } else {
        if (file != NULL) {
            fclose(file);
            if (unlink(filename) < 0) {
                snprintf(notify_message, sizeof(notify_message),
                         _("Error deleting file \"%s\": %s"), filename,
                         strerror(errno));
                notify_form(notify_message, 0);
            }
        }
    }
    file = NULL;
    if (filename != NULL) {
        Xfree(filename, __FILE__, __LINE__);
    }
    filename = NULL;
}

/**
 * Setup for a new file transfer session.
 *
 * @param file_list list of files to upload, or NULL if this will be a
 * download.
 * @param pathname the path to save downloaded files to
 * @param send if true, this is an upload: file_list must be valid and
 * pathname is ignored.  If false, this is a download: file_list must be NULL
 * and pathname will be used.
 * @param in_flavor the type of Ymodem transfer to perform
 */
Q_BOOL ymodem_start(struct file_info * file_list, const char * pathname,
                    const Q_BOOL send, const XMODEM_FLAVOR in_flavor) {

    /*
     * If I got here, then I know that all the files in file_list exist.
     * forms.c makes sure the files are all readable by me.
     */

    /*
     * Assume we don't start up successfully
     */
    state = ABORT;

    upload_file_list = file_list;
    upload_file_list_i = 0;


    DLOG(("YMODEM: START flavor = %d ", in_flavor));

    if (send == Q_TRUE) {
        /*
         * Set up for first file
         */
        if (setup_for_next_file() == Q_FALSE) {
            DLOG2(("false\n"));
            return Q_FALSE;
        }
    } else {
        /*
         * Save download path
         */
        download_path = Xstrdup(pathname, __FILE__, __LINE__);
        set_transfer_stats_filename("");
        set_transfer_stats_pathname(pathname);
    }

    sending = send;
    flavor = in_flavor;
    state = INIT;
    current_block_sequence_i = 0;
    current_block_number = 1;
    block0_has_been_seen = Q_FALSE;
    timeout_count = 0;
    timeout_max = 10;
    clear_block();

    /*
     * Set block size
     */
    q_transfer_stats.block_size = 1024;

    /*
     * Set first byte
     */
    if (flavor == Y_NORMAL) {
        first_byte = 'C';
    } else {
        first_byte = 'G';
    }

    /*
     * Clear the last message
     */
    set_transfer_stats_last_message("");

    DLOG2(("true\n"));
    return Q_TRUE;
}

/**
 * Stop the file transfer.  Note that this function is only called in
 * stop_file_transfer() and save_partial is always true.  However it is left
 * in for API completeness.
 *
 * @param save_partial if true, save any partially-downloaded files.
 */
void ymodem_stop(const Q_BOOL save_partial) {
    DLOG(("YMODEM: STOP\n"));

    xmodem_stop(save_partial);
    if (download_path != NULL) {
        Xfree(download_path, __FILE__, __LINE__);
    }
    download_path = NULL;
}
