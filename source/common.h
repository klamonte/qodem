/*
 * common.h
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

#ifndef __COMMON_H__
#define __COMMON_H__

/* Hans-Boehm garbage collector integration ------------------------------- */
#if defined(Q_GC_BOEHM) && defined(HAVE_LIBGC)

#include <gc.h>

#ifdef DEBUG
#define Xmalloc(X, Y, Z)                GC_debug_malloc(X, Y, Z)
#define Xcalloc(W, X, Y, Z)             GC_debug_calloc(W, X, Y, Z)
#define Xrealloc(W, X, Y, Z)            GC_debug_realloc(W, X, Y, Z)
#define Xfree(X, Y, Z)                  GC_debug_free(X)
#else
#define Xmalloc(X, Y, Z)                GC_malloc(X)
#define Xcalloc(W, X, Y, Z)             GC_calloc(W, X)
#define Xrealloc(W, X, Y, Z)            GC_realloc(W, X)
#define Xfree(X, Y, Z)                  GC_free(X)
#endif

#else

#define Xmalloc(X, Y, Z)                malloc(X)
#define Xcalloc(W, X, Y, Z)             calloc(W, X)
#define Xrealloc(W, X, Y, Z)            realloc(W, X)
#define Xfree(X, Y, Z)                  free(X)

#endif /* defined(Q_GC_BOEHM) && defined(HAVE_LIBGC) */

/* Includes --------------------------------------------------------------- */

#define _GNU_SOURCE
#include <wchar.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */
#define RELEASE
/* #undef RELEASE */

#define Q_VERSION               "1.0.1"
#define Q_AUTHOR                "Autumn Lamonte"

#ifdef RELEASE
#define Q_VERSION_BRANCH        "Production"
#else
#define Q_VERSION_BRANCH        "Development"
#endif

/* Exit codes */
#define EXIT_ERROR_CURSES               10
#define EXIT_ERROR_SETLOCALE            12
#define EXIT_ERROR_SELECT_FAILED        20
#define EXIT_ERROR_SERIAL_FAILED        21
#define EXIT_ERROR_COMMANDLINE          30
#define EXIT_HELP                       1
#define EXIT_VERSION                    2
#define EXIT_OK                         0

#define TIME_STRING_LENGTH      64

/* HH:MM:SS */
#define SHORT_TIME_SIZE         9

/* Length of a command line string */
#define COMMAND_LINE_SIZE       1024

/* Maximum length of any filename */
#define FILENAME_SIZE           256

/*
 * Maximum length of a message generated for a dialog box (e.g. file transfer
 * dialog)
 */
#define DIALOG_MESSAGE_SIZE     128

/* Maximum length of a message generated for the session log */
#define SESSION_LOG_LINE_SIZE   512

/* Maximum line length in the options file */
#define OPTIONS_LINE_SIZE       128

/* Localization / Internationalization */
#include <locale.h>

/*
 * Bug #3528357 - The "proper" solution is to add LIBINTL to LDFLAGS and have
 * configure built the intl directory.  But until we get actual non-English
 * translations it doesn't matter.  We may as well just disable gettext().
 */
#if defined(ENABLE_NLS) && defined(HAVE_GETTEXT)
#include <libintl.h>
#define _(String) gettext(String)
#else
#define _(String) (String)
#endif

/*
 * Boolean support.  Can't use bool because PDCurses defines that.  Can't use
 * BOOL because ncurses conflicts with windows.h.  So I end up with my own
 * bool type.
 */
#define Q_BOOL int
#define Q_FALSE 0
#define Q_TRUE 1

/*
 * Debug logger function.  DLOGNAME is a const char * defined in each source
 * file that uses DLOG().  So all a source has to do is define DLOGNAME to
 * non-NULL and logging will be active for that translation unit.
 */
#define DLOG(A)                         \
do {                                    \
if (DLOGNAME != NULL) {                 \
    dlogtimestamp = Q_TRUE;             \
    dlogname = (char *) DLOGNAME;       \
    dlogprintf A;                       \
} else {                                \
    /* Do nothing */                    \
}}  while (0);

/*
 * Continue a previous DLOG message, i.e. do not emit the timestamp.
 */
#define DLOG2(A)                        \
do {                                    \
if (DLOGNAME != NULL) {                 \
    dlogtimestamp = Q_FALSE;            \
    dlogname = (char *) DLOGNAME;       \
    dlogprintf A;                       \
} else {                                \
    /* Do nothing */                    \
}}  while (0);

/*
 * A whitespace check that only looks for space, carriage return, and
 * newline.  This is used by qodem's file readers.
 */
#define q_isspace(x) ((x == ' ') || (x == '\r') || (x == '\n'))

/*
 * A digit check that only looks for '0' through '9' (i.e. ignores locale).
 */
#define q_isdigit(x) ((x >= '0') && (x <= '9'))

/* Globals ---------------------------------------------------------------- */

/**
 * The name to pair with the next dlogprintf() call.  It needs to be exposed
 * so that DLOG/DLOG2 can set it.
 */
extern char * dlogname;

/**
 * When true, emit the timestamp on the next dlogprintf() call.  It needs to
 * be exposed so that DLOG/DLOG2 can set it.
 */
extern Q_BOOL dlogtimestamp;

/* Functions -------------------------------------------------------------- */

/**
 * Emit a timestamped message to the debug log.
 *
 * @param format the format string
 */
extern void dlogprintf(const char * format, ...);

/**
 * strdup() equivalent that plugs into the Hans-Boehm GC if it is enabled, if
 * not it just passes through to the default strdup().
 *
 * @param ptr the string to copy
 * @param file the filename calling this function
 * @param line the line number at the call point.
 */
extern char * Xstrdup(const char * ptr, const char * file, const int line);

/**
 * wcsdup() equivalent that plugs into the Hans-Boehm GC if it is enabled, if
 * not it just passes through to the default wcsdup().
 *
 * @param ptr the string to copy
 * @param file the filename calling this function
 * @param line the line number at the call point.
 */
extern wchar_t * Xwcsdup(const wchar_t * ptr, const char * file,
                         const int line);

/**
 * A narrow-char to wide-char string duplicate function that plugs into the
 * Hans-Boehm GC if it is enabled, if not it just passes through to the
 * default mbstowcs().
 *
 * @param ptr the string to copy
 * @param file the filename calling this function
 * @param line the line number at the call point.
 */
extern wchar_t * Xstring_to_wcsdup(const char * ptr, const char * file,
                                   const int line);

#ifndef Q_PDCURSES_WIN32

/**
 * Function to clean out any characters waiting in stdin.
 */
extern void purge_stdin();

#endif

/**
 * Returns the home directory where ~/.qodem (My Documents\\qodem\\prefs) and
 * ~/qodem (My Documents\\qodem) are stored.
 *
 * @return the path string pointing to qodem's home directory, usually
 * ~/.qodem on POSIX or My Documents\\qodem on Windows.
 */
extern char * get_home_directory();

/**
 * Returns Q_TRUE if the named file already exists.
 *
 * @param filename the filename to check
 * @return true if the file exists
 */
extern Q_BOOL file_exists(const char * filename);

/**
 * Returns Q_TRUE if path exists and is a directory.
 *
 * @param path the directory name to check
 * @return true if the directory exists
 */
extern Q_BOOL directory_exists(const char * path);

/**
 * Truncate a string longer than length to "blah...".
 *
 * @param string the string to potentially truncate.  String is destructively
 * modified.
 * @param the maximum length of string
 */
extern void shorten_string(char * string, const int length);

/**
 * Convert ASCII 'A'-'Z' to 'a'-'z'.
 *
 * @param ch the character to convert
 * @return (ch - 0x20), IF ch is between 'A' and 'Z'.  Otherwise, ch.
 */
extern int q_tolower(const int ch);

/* Borland C 5.02 and Visual C++ 6.0 support ------------------------------ */

/**/

#if defined(__BORLANDC__) || defined(_MSC_VER)

#include <snprintf.h>

/**
 * wmemmove() implementation to make up for Borland C++ not having it.
 * Wide-char equivalent of memmove().  (Visual C++ actually has a wmemmove(),
 * but it is not visible in C89-only compiles, so leave this declaration
 * where it can see it.)
 *
 * @param dest the destination location
 * @param src the source wide-chars to copy
 * @param n the number of wide-chars to copy
 * @return dest
 */
extern wchar_t * wmemmove(wchar_t * dest, const wchar_t * src, size_t n);

typedef int pid_t;

#endif

#ifdef _MSC_VER

#define strcasecmp _stricmp
#define strncasecmp _strnicmp

/**
 * Wide-char equivalent of memset().  Visual C++ actually has a wmemset(),
 * but it is not visible in C89-only compiles, so leave this declaration
 * where it can see it.
 *
 * @param wcs the destination location
 * @param wc the source wide-char to copy
 * @param n the number of wide-chars to copy
 * @return wcs
 */
extern wchar_t * wmemset(wchar_t * wcs, const wchar_t wc, size_t n);

/**
 * Wide-char equivalent of memcpy().  Visual C++ actually has a wmemcpy(),
 * but it is not visible in C89-only compiles, so leave this declaration
 * where it can see it.
 *
 * @param dest the destination location
 * @param src the source wide-chars to copy
 * @param n the number of wide-chars to copy
 * @return dest
 */
extern wchar_t * wmemcpy(wchar_t * dest, const wchar_t * src, size_t n);

#endif /* _MSC_VER */

#ifdef __BORLANDC__

#define strcasecmp strcmpi
#define strncasecmp strncmpi
#define wmemset _wmemset
#define wmemcpy _wmemcpy

#endif /* __BORLANDC__ */

#ifdef __cplusplus
}
#endif

#endif /* __COMMON_H__ */
