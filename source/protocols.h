/*
 * protocols.h
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

#ifndef __PROTOCOLS_H__
#define __PROTOCOLS_H__

/* Includes --------------------------------------------------------------- */

#include <time.h>
#include <sys/types.h>
#include "forms.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

/**
 * The maximum size a single block might need on the wire.  We use this to
 * increase the throughput of uploads.
 */
#define Q_PROTOCOL_MAX_BLOCK_SIZE       2048

/**
 * The supported download protocols.
 */
typedef enum Q_PROTOCOLS {
    Q_PROTOCOL_ASCII,           /* ASCII */
    Q_PROTOCOL_KERMIT,          /* Kermit */
    Q_PROTOCOL_XMODEM,          /* Xmodem */
    Q_PROTOCOL_XMODEM_CRC,      /* Xmodem CRC */
    Q_PROTOCOL_XMODEM_RELAXED,  /* Xmodem Relaxed */
    Q_PROTOCOL_XMODEM_1K,       /* Xmodem-1K */
    Q_PROTOCOL_YMODEM,          /* Ymodem Batch */
    Q_PROTOCOL_ZMODEM,          /* Zmodem Batch */
    Q_PROTOCOL_XMODEM_1K_G,     /* Xmodem-1K/G */
    Q_PROTOCOL_YMODEM_G         /* Ymodem/G Batch */
} Q_PROTOCOL;

/**
 * The transfer state as exposed to the user in the file transfer dialog
 * screen.
 */
typedef enum Q_TRANSFER_STATES {
    /**
     * Initial state.
     */
    Q_TRANSFER_STATE_INIT,

    /**
     * Waiting for file information.
     */
    Q_TRANSFER_STATE_FILE_INFO,

    /**
     * Transferrring a file.
     */
    Q_TRANSFER_STATE_TRANSFER,

    /**
     * Completed with a file, maybe waiting for another file info.
     */
    Q_TRANSFER_STATE_FILE_DONE,

    /**
     * Transfer aborted, displaying completion screen.
     */
    Q_TRANSFER_STATE_ABORT,

    /**
     * Displaying completion screen.
     */
    Q_TRANSFER_STATE_END
} Q_TRANSFER_STATE;

/**
 * The data behind the file transfer dialog screen.
 */
struct q_transfer_stats_struct {
    Q_TRANSFER_STATE state;
    Q_PROTOCOL protocol;
    char * protocol_name;
    char * filename;
    char * pathname;
    char * last_message;
    unsigned long bytes_total;
    unsigned long bytes_transfer;
    unsigned long blocks;
    unsigned long block_size;
    unsigned long blocks_transfer;
    unsigned long error_count;

    /**
     * The total bytes to send for a batch.
     */
    unsigned long batch_bytes_total;

    /**
     * The amount of bytes sent so far for a batch.
     */
    unsigned long batch_bytes_transfer;

    time_t batch_start_time;
    time_t file_start_time;
    time_t end_time;
};

/*
 * Zmodem auto-start code:
 *
 * 2A2A             ZPAD
 * 18               ZDLE
 * ^^----- but I saw 01 from lrzsz
 * 42               Format type
 * 3030             ZRQINIT (Zmodem hex 1 byte)
 * 3030303030303030 Flags   (Zmodem hex 4 bytes)
 * ????????         CRC check bytes (Zmodem hex 2 bytes)
 * <CR><LF><XON>    End of packet
 *
 * In ASCII this looks like:
 * "**<CAN>B0000000000????<CR><LF><XON>"
 */
#define ZRQINIT_STRING "\x2A\x2A?\x42\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30"

/*
 * Kermit auto-start code:
 *
 * 01               MARK
 * ??               LEN
 * 20               SEQ
 * 'S'              TYPE
 * ??               MAXL
 * ??               TIME
 * ??               NPAD
 * 00               PADC
 * 0d               EOL
 * 23               QCTL
 * ??               QBIN
 */
#define KERMIT_AUTOSTART_STRING "\x01?\x20\x53???\x40\x2d\x23"

/* Globals ---------------------------------------------------------------- */

/**
 * Download location file or directory.
 */
extern char * q_download_location;

/**
 * Transfer statistics.  Lots of places need to peek into this structure.
 */
extern struct q_transfer_stats_struct q_transfer_stats;

/* Functions -------------------------------------------------------------- */

/**
 * Set the exposed protocol name.  Allocates a copy of the string which is
 * freed when the program state is switched to Q_STATE_CONSOLE.
 *
 * @param new_string the new protocol name
 */
extern void set_transfer_stats_protocol_name(const char * new_string);

/**
 * Set the exposed filename.  Allocates a copy of the string which is freed
 * when the program state is switched to Q_STATE_CONSOLE.
 *
 * @param new_string the new filname
 */
extern void set_transfer_stats_filename(const char * new_string);

/**
 * Set the exposed path name.  Allocates a copy of the string which is freed
 * when the program state is switched to Q_STATE_CONSOLE.
 *
 * @param new_string the new path name
 */
extern void set_transfer_stats_pathname(const char * new_string);

/**
 * Set the exposed message.  Allocates a copy of the string which is freed
 * when the program state is switched to Q_STATE_CONSOLE.
 *
 * @param new_string the new message
 */
extern void set_transfer_stats_last_message(const char * format, ...);

/**
 * End the file transfer.
 *
 * @param new_state the state to switch to after a brief display pause
 */
extern void stop_file_transfer(const Q_TRANSFER_STATE new_state);

/**
 * Start a file transfer.  Usually this is done in one of the protocol
 * dialogs, but the console can do it for Zmodem/Kermit autostart, and host
 * mode can do it.
 */
extern void start_file_transfer();

/**
 * Save a list of files to the batch upload window data file.  This is used
 * by host mode to perform an upload ("download" to the remote side).
 *
 * @param upload the new list of files
 */
extern void set_batch_upload(struct file_info * upload);

/**
 * Keyboard handler for the protocol selection dialog.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void protocol_menu_keyboard_handler(const int keystroke,
                                           const int flags);

/**
 * Draw screen for the protocol selection dialog.
 */
extern void protocol_menu_refresh();

/**
 * Keyboard handler for the protocol path to save file dialog.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void protocol_pathdialog_keyboard_handler(const int keystroke,
                                                 const int flags);

/**
 * Draw screen for the protocol path to save file dialog.
 */
extern void protocol_pathdialog_refresh();

/**
 * Keyboard handler for the transferring file screen.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void protocol_transfer_keyboard_handler(const int keystroke,
                                               const int flags);

/**
 * Draw screen for the transferring file screen.
 */
extern void protocol_transfer_refresh();

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
extern void protocol_process_data(unsigned char * input,
                                  const unsigned int input_n,
                                  int * remaining, unsigned char * output,
                                  unsigned int * output_n,
                                  const unsigned int output_max);

#ifdef __cplusplus
}
#endif

#endif /* __PROTOCOLS_H__ */
