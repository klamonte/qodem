/*
 * forms.h
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

#ifndef __FORMS_H__
#define __FORMS_H__

/* Includes --------------------------------------------------------------- */

#include <sys/stat.h>           /* stat() */
#include <dirent.h>             /* mode_t */
#include "modem.h"              /* Q_BAUD_RATE, Q_PARITY, etc. */
#include "qodem.h"              /* Q_CAPTURE_TYPE */
#include "host.h"               /* Q_HOST_TYPE */

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

/**
 * view_directory() and batch_entry_window() need to return both the file
 * name and size, so we have a struct to combine these.
 */
struct file_info {
    char * name;
    struct stat fstats;
};

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

/**
 * Display a message in a modal screen-centered dialog, and get a selection
 * response from the user.
 *
 * @param message the text to display inside the box
 * @param prompt the title on the top edge of the box
 * @param status_prompt the text to display on the status line while the
 * dialog is up
 * @param visible_cursor if true, make the cursor visible
 * @param timeout the number of seconds to wait before closing the dialog and
 * returning ERR
 * @param allowed_chars a list of valid characters to return.  Usually this
 * is something like "YyNn" to capture yes and no.
 * @return the keystroke the user selected, or ERR if the timeout was reached
 * before they hit anything.
 */
extern int notify_prompt_form(const char * message, const char * prompt,
                              const char * status_prompt,
                              const Q_BOOL visible_cursor, const double timeout,
                              const char * allowed_chars);

/**
 * Display a message in a modal screen-centered dialog, and have it disappear
 * after a timeout or the user presses a key.  The title will always be
 * "Status".
 *
 * @param message the text to display inside the box
 * @param timeout the number of seconds to wait before closing the dialog
 */
extern void notify_form(const char * message, const double timeout);

/**
 * Display a message in a modal screen-centered dialog, and have it disappear
 * after a timeout or the user presses a key.  The title will always be
 * "Status".
 *
 * @param message an array of strings to display inside the box, one string
 * for each line.
 * @param timeout the number of seconds to wait before closing the dialog
 * @param lines the number of strings in message
 */
extern void notify_form_long(char ** message, const double timeout,
                             const int lines);

/**
 * Display a message in a modal screen-centered dialog, and get a selection
 * response from the user.
 *
 * @param message an array of strings to display inside the box, one string
 * for each line.
 * @param prompt the title on the top edge of the box
 * @param status_prompt the text to display on the status line while the
 * dialog is up
 * @param visible_cursor if true, make the cursor visible
 * @param timeout the number of seconds to wait before closing the dialog and
 * returning ERR
 * @param allowed_chars a list of valid characters to return.  Usually this
 * is something like "YyNn" to capture yes and no.
 * @param lines the number of strings in message
 * @return the keystroke the user selected, or ERR if the timeout was reached
 * before they hit anything.
 */
extern int notify_prompt_form_long(char ** message, const char * prompt,
                                   const char * status_prompt,
                                   const Q_BOOL visible_cursor,
                                   const double timeout,
                                   const char * allowed_chars,
                                   const unsigned int lines);

/**
 * Ask the user for a location to save a file to.  This will be a dialog box
 * with a single text entry field, centered horizontally but 2/3 down
 * vertically.
 *
 * @param title the title on the top edge of the box
 * @param initial_value the starting value of the text field
 * @param is_directory if true, then the returned value can be a directory
 * name.  If false, then the returned value must not be an existing directory
 * name; pressing enter to save the value will bring up a view_directory()
 * window to switch directories.
 * @param warn_overwrite if true, ask the user if they want to overwrite an
 * existing file.
 * @return the selected filename or path name
 */
extern char * save_form(const char * title, char * initial_value,
                        const Q_BOOL is_directory, const Q_BOOL warn_overwrite);

/**
 * Display a navigatable directory listing dialog.
 *
 * @param initial_directory the starting point for navigation
 * @param filter a wildcard filter that files must match
 * @return the name and stats for the selected directory, or NULL if the user
 * cancelled.
 */
extern struct file_info * view_directory(const char * initial_directory,
                                         const char * filter);

/**
 * Display the batch entry window dialog.
 *
 * @param initial_directory the starting point for navigation
 * @param upload if true, use the text for a file upload box.  If false, just
 * save the entries to disk.
 * @return an array of the name+stats for the files selected, or NULL if the
 * user cancelled.
 */
extern struct file_info * batch_entry_window(const char * initial_directory,
                                             const Q_BOOL upload);

/**
 * Convert a mode value into a displayable string similar to the first column
 * of the ls long format (-l).  Note that the string returned is a single
 * static buffer, i.e. this is NOT thread-safe.
 *
 * @param mode the file mode returned by a stat() call
 * @return a string like "drw-r--r--"
 */
extern char * file_mode_string(mode_t mode);

#ifndef Q_NO_SERIAL

/**
 * Display the Alt-Y serial port settings dialog.  Returns true if the user
 * changed something.
 *
 * @param title the title on the top edge of the box
 * @param baud the selected baud rate value: 300bps, 19200bps, etc
 * @param data_bits the selected data_bits value: 5, 6, 7, or 8
 * @param parity the selected parity value: none, odd, even, mark, space
 * @param stop_bits the selected stop_bits value: 1 or 2
 * @param xonxoff whether or not to use XON/XOFF software flow control
 * @param rtscts whether or not to use RTS/CTS hardware flow control
 * @return if true, the user made a change to some value
 */
extern Q_BOOL comm_settings_form(const char * title, Q_BAUD_RATE * baud,
                                 Q_DATA_BITS * data_bits, Q_PARITY * parity,
                                 Q_STOP_BITS * stop_bits, Q_BOOL * xonxoff,
                                 Q_BOOL * rtscts);

#endif

/**
 * Display the alt code key dialog.
 *
 * @param utf8 if true, ask for a 16-bit value as four hex digits, otherwise
 * ask for an 8-bit value as a base-10 decimal number (0-255).
 * @return the value the user entered, or -1 if they cancelled
 */
extern int alt_code_key(Q_BOOL utf8);

/**
 * Display the "Find" or "Find Again" entry dialog.
 *
 * @return the string the user selected, or NULL if they cancelled.
 */
extern wchar_t * pick_find_string();

/**
 * Ask the user for their preferred capture type.
 *
 * @return the user's selection, or Q_CAPTURE_TYPE_ASK if they cancelled.
 */
extern Q_CAPTURE_TYPE ask_capture_type();

/**
 * Ask the user for their preferred save type for scrollback and screen
 * dumps.
 *
 * @return the user's selection, or Q_SAVE_TYPE_ASK if they cancelled.
 */
extern Q_CAPTURE_TYPE ask_save_type();

/**
 * Ask the user for the type of host to start: socket, telnetd, etc.
 *
 * @param type the user's selection
 * @return true if the user made a choice, false if they cancelled.
 */
extern Q_BOOL ask_host_type(Q_HOST_TYPE * type);

/**
 * Ask the user for the type of host listening port: next available, specific
 * number, or UPnP.
 *
 * @param port a pointer to a string to record the user's selection
 * @return true if the user made a choice, false if they cancelled.
 */
extern Q_BOOL prompt_listen_port(char ** port);

/**
 * See if the screen is big enough to display a new window.  If it isn't,
 * display a request for 80x25 and cancel whatever dialog was trying to be
 * displayed.
 *
 * @param window the WINDOW returned by a call to subwin()
 * @return true if the screen is big enough to show the window
 */
extern Q_BOOL check_subwin_result(void * window);

#ifdef __cplusplus
}
#endif

#endif /* __FORMS_H__ */
