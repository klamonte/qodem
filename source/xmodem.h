/*
 * xmodem.h
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

#ifndef __XMODEM_H__
#define __XMODEM_H__

/* Includes --------------------------------------------------------------- */

#include "common.h"             /* Q_BOOL */
#include "forms.h"              /* struct file_info */

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

/**
 * The various flavors of Xmodem and Ymodem that are supported.
 */
typedef enum {
    X_NORMAL,                   /* Regular Xmodem */
    X_CRC,                      /* Xmodem CRC */
    X_RELAXED,                  /* Xmodem Relaxed */
    X_1K,                       /* Xmodem-1k */
    X_1K_G,                     /* Xmodem-1k/G */
    Y_NORMAL,                   /* Regular Ymodem */
    Y_G                         /* Ymodem/G */
} XMODEM_FLAVOR;

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

/**
 * Setup for a new file transfer session.
 *
 * @param in_filename the filename to save downloaded file data to, or the
 * name of the file to upload.
 * @param send if true, this is an upload
 * @param in_flavor the type of Xmodem transfer to perform
 * @return true if successful
 */
extern Q_BOOL xmodem_start(const char * in_filename, const Q_BOOL send,
                           const XMODEM_FLAVOR in_flavor);

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
extern void xmodem(unsigned char * input, const unsigned int input_n,
                   int * remaining, unsigned char * output,
                   unsigned int * output_n, const int output_max);

/**
 * Stop the file transfer.  Note that this function is only called in
 * stop_file_transfer() and save_partial is always true.  However it is left
 * in for API completeness.
 *
 * @param save_partial if true, save any partially-downloaded files.
 */
extern void xmodem_stop(const Q_BOOL save_partial);

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
extern Q_BOOL ymodem_start(struct file_info * file_list, const char * pathname,
                           const Q_BOOL send, const XMODEM_FLAVOR in_flavor);

/**
 * Stop the file transfer.  Note that this function is only called in
 * stop_file_transfer() and save_partial is always true.  However it is left
 * in for API completeness.
 *
 * @param save_partial if true, save any partially-downloaded files.
 */
extern void ymodem_stop(const Q_BOOL save_partial);

#ifdef __cplusplus
}
#endif

#endif /* __XMODEM_H__ */
