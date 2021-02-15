/*
 * common.c
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

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <stdarg.h>
#ifdef Q_PDCURSES_WIN32
#  include <tchar.h>
#  include <windows.h>
#  include <shlobj.h>
#else
#  include <sys/poll.h>
#  include <unistd.h>
#endif /* Q_PDCURSES_WIN32 */

/**
 * The name to pair with the next dlogprintf() call.
 */
char * dlogname = NULL;

/**
 * When true, emit the timestamp on the next dlogprintf() call.
 */
Q_BOOL dlogtimestamp = Q_TRUE;

/**
 * The debug log file handle.
 */
FILE * dlogfile = NULL;

/**
 * Emit a timestamped message to the debug log.
 *
 * @param format the format string
 */
void dlogprintf(const char * format, ...) {
    char timestring[80];
    struct tm * timestamp;
    struct timeval now;
    va_list arglist;

    if (dlogfile == NULL) {
        dlogfile = fopen("debug-qodem.txt", "wt");
        if (dlogfile == NULL) {
            /* Some error opening it, bail out. */
            return;
        }
    }

    if (dlogtimestamp == Q_TRUE) {
        gettimeofday(&now, NULL);
        timestamp = localtime(&now.tv_sec);
        strftime(timestring, sizeof(timestring),
                 "[%Y-%m-%d %H:%M:%S.%%03d] %%s ",
                 timestamp);
        fprintf(dlogfile, timestring, (now.tv_usec / 1000), dlogname);
    }

    va_start(arglist, format);
    vfprintf(dlogfile, format, arglist);
    va_end(arglist);

    fflush(dlogfile);

}

/**
 * wcsdup() equivalent that plugs into the Hans-Boehm GC if it is enabled, if
 * not it just passes through to the default wcsdup().
 *
 * @param ptr the string to copy
 * @param file the filename calling this function
 * @param line the line number at the call point.
 */
wchar_t * Xwcsdup(const wchar_t * ptr, const char * file, const int line) {

    wchar_t * local_ptr;
#if defined(Q_GC_BOEHM) && defined(HAVE_LIBGC)
    int length;
    int i;
#endif

    assert(ptr != NULL);

#if defined(Q_GC_BOEHM) && defined(HAVE_LIBGC)
    length = wcslen(ptr) + 1;
#ifdef DEBUG
    local_ptr =
        (wchar_t *) GC_debug_malloc(length * sizeof(wchar_t), file, line);
#else
    local_ptr = (wchar_t *) GC_malloc(length * sizeof(wchar_t));
#endif

    if (local_ptr != NULL) {
        for (i = 0; i < length; i++) {
            local_ptr[i] = ptr[i];
        }
    }
    local_ptr[length - 1] = 0;

#else

#if __linux

    local_ptr = wcsdup(ptr);

#else

    if (wcslen(ptr) > 0) {
        local_ptr =
            (wchar_t *) Xcalloc(1, (sizeof(wchar_t) + 1) * (wcslen(ptr) + 1),
                                __FILE__, __LINE__);
        wcsncpy(local_ptr, ptr, wcslen(ptr));
    } else {
        local_ptr =
            (wchar_t *) Xcalloc(1, (sizeof(wchar_t) + 1) * 1, __FILE__,
                                __LINE__);
    }

#endif

#endif

#if 0
    /* For debugging: */
    fprintf(stderr, "Xwcsdup: ptr=%p local_ptr=%p file=%s line=%d\n", ptr,
            local_ptr, file, line);
#endif

    return local_ptr;
}

/**
 * A narrow-char to wide-char string duplicate function that plugs into the
 * Hans-Boehm GC if it is enabled, if not it just passes through to the
 * default mbstowcs().
 *
 * @param ptr the string to copy
 * @param file the filename calling this function
 * @param line the line number at the call point.
 */
wchar_t * Xstring_to_wcsdup(const char * ptr, const char * file,
                            const int line) {

    wchar_t * local_ptr;
    int length;

#if defined(Q_GC_BOEHM) && defined(HAVE_LIBGC)
    int i;
#endif

    assert(ptr != NULL);

    length = mbstowcs(NULL, ptr, strlen(ptr)) + 1;

#if defined(Q_GC_BOEHM) && defined(HAVE_LIBGC)
#ifdef DEBUG
    local_ptr =
        (wchar_t *) GC_debug_malloc((sizeof(wchar_t)) * length, file, line);
#else
    local_ptr = (wchar_t *) GC_malloc((sizeof(wchar_t)) * length);
#endif

#else

    local_ptr = (wchar_t *) malloc((sizeof(wchar_t)) * length);

#endif

    if (length > 1) {
        mbstowcs(local_ptr, ptr, length - 1);
        local_ptr[length - 1] = 0;
    } else {
        if (length == 1) {
            local_ptr[0] = 0;
        } else {
            local_ptr = Xwcsdup(L"", file, line);
        }
    }

#if 0
    /* For debugging: */
    fprintf(stderr, "Xstring_to_wcsdup: ptr=%p local_ptr=%p file=%s line=%d\n",
            ptr, local_ptr, file, line);
#endif

    return local_ptr;
}

/**
 * strdup() equivalent that plugs into the Hans-Boehm GC if it is enabled, if
 * not it just passes through to the default strdup().
 *
 * @param ptr the string to copy
 * @param file the filename calling this function
 * @param line the line number at the call point.
 */
char * Xstrdup(const char * ptr, const char * file, const int line) {

    char * local_ptr;
#if defined(Q_GC_BOEHM) && defined(HAVE_LIBGC)
    int length;
    int i;
#endif

    assert(ptr != NULL);

#if defined(Q_GC_BOEHM) && defined(HAVE_LIBGC)
    length = strlen(ptr) + 1;
#ifdef DEBUG
    local_ptr = (char *) GC_debug_malloc(length, file, line);
#else
    local_ptr = (char *) GC_malloc(length);
#endif /* DEBUG */

    if (local_ptr != NULL) {
        for (i = 0; i < length; i++) {
            local_ptr[i] = ptr[i];
        }
    }

#else

    local_ptr = strdup(ptr);

#endif

#if 0
    /* For debugging: */
    fprintf(stderr, "Xstrdup: ptr=%p local_ptr=%p file=%s line=%d\n", ptr,
            local_ptr, file, line);
#endif

    return local_ptr;
}

#ifdef Q_PDCURSES_WIN32

/* Saved copy of path pointing to My Documents  */
static char * win32_docs_path = NULL;

#endif

/**
 * Returns the home directory where ~/.qodem (My Documents\\qodem\\prefs) and
 * ~/qodem (My Documents\\qodem) are stored.
 *
 * @return the path string pointing to qodem's home directory, usually
 * ~/.qodem on POSIX or My Documents\\qodem on Windows.
 */
char * get_home_directory() {
    char * env_string;

#ifdef Q_PDCURSES_WIN32

    TCHAR myDocsPath[MAX_PATH];

    if (win32_docs_path != NULL) {
        return win32_docs_path;
    }

#ifdef __BORLANDC__

    /*
     * We go through some work to make %USERPROFILE%\\My Documents because
     * swprintf() is weird when mixing char * and wchar_t * with Borland.
     */
    env_string = getenv("USERPROFILE");
    int i, j;

    memset(myDocsPath, 0, sizeof(myDocsPath));
    for (i = 0; i < strlen(env_string); i++) {
        myDocsPath[i] = env_string[i];
    }
    myDocsPath[i] = '\\';
    i++;
    wchar_t * myDocumentsString = _(L"My Documents");
    for (j = 0; j < wcslen(myDocumentsString); j++) {
        myDocsPath[i] = myDocumentsString[j];
        i++;
    }

    if (1) {

#else

    /*
     * Windows: try the CSIDL function, if that fails just return
     * %USERPROFILE% .
     */

    /*
     * VC6 has SHGetSpecialFolderPath, not SHGetFolderPath.
     * if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, 0, myDocsPath))) {
     */
    if (SUCCEEDED(SHGetSpecialFolderPath(NULL, myDocsPath, CSIDL_PERSONAL, 0))) {

#endif /* __BORLANDC__ */

        if (sizeof(TCHAR) == sizeof(char)) {
            /*
             * Direct copy
             */
            win32_docs_path = Xstrdup((char *) myDocsPath, __FILE__, __LINE__);
        } else {
            /*
             * TCHAR is wchar_t, copy each byte.  This would be like a
             * Xwcs_to_strdup but no one else needs that function.
             */
            unsigned int i;
            win32_docs_path =
                (char *) Xmalloc(wcslen((wchar_t *) myDocsPath) + 1, __FILE__,
                                 __LINE__);
            memset(win32_docs_path, 0, wcslen((wchar_t *) myDocsPath) + 1);
            for (i = 0; i < wcslen((wchar_t *) myDocsPath); i++) {
                win32_docs_path[i] = myDocsPath[i];
            }
        }
        return win32_docs_path;
    } else {
        env_string = getenv("USERPROFILE");
    }

#else

    /*
     * Everyone else in the world: $HOME
     */
    env_string = getenv("HOME");

#endif /* Q_PDCURSES_WIN32 */

    return env_string;
}

#ifndef Q_PDCURSES_WIN32

/**
 * Function to clean out any characters waiting in stdin.
 */
void purge_stdin() {
    struct pollfd pfd;
    int rc = 0;

    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;

    rc = poll(&pfd, 1, 10);
    if (rc < 0) {
        /*
         * Error
         */
    } else if (rc > 0) {

        /*
         * Flush stdin
         */
        do {
            pfd.fd = STDIN_FILENO;
            pfd.events = POLLIN;
            getchar();
        } while ((rc = poll(&pfd, 1, 0)) > 0);
    }
}

#endif

/**
 * Returns Q_TRUE if the named file already exists.
 *
 * @param filename the filename to check
 * @return true if the file exists
 */
Q_BOOL file_exists(const char * filename) {
    int rc;
    struct stat fstats;
    rc = stat(filename, &fstats);
    if (rc < 0) {
        if (errno == ENOENT) {
            /*
             * File definitely does not exist.
             */
            return Q_FALSE;
        }
    }

    /*
     * For all I/O errors, or a successful stat(), assume that the file
     * exists.
     */
    return Q_TRUE;
}

/**
 * Returns Q_TRUE if path exists and is a directory.
 *
 * @param path the directory name to check
 * @return true if the directory exists
 */
Q_BOOL directory_exists(const char * path) {

#ifdef Q_PDCURSES_WIN32
    DWORD dwAttrib = GetFileAttributesA(path);

    if ((dwAttrib != 0xFFFFFFFF) && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
        return Q_TRUE;
    }
#else

    struct stat fstats;
    int rc;

    rc = stat(path, &fstats);
    if (rc < 0) {
        if (errno == ENOENT) {
            /*
             * Directory definitely does not exist.
             */
            return Q_FALSE;
        }
    }
    if (S_ISDIR(fstats.st_mode) == 0) {
        /*
         * This is not a directory.
         */
        return Q_FALSE;
    }

    if (access(path, F_OK) == 0) {
        return Q_TRUE;
    }

#endif

    /*
     * Directory does not exist
     */
    return Q_FALSE;
}

/**
 * Truncate a string longer than length to "blah...".
 *
 * @param string the string to potentially truncate.  String is destructively
 * modified.
 * @param the maximum length of string
 */
void shorten_string(char * string, const int length) {
    if (string == NULL) {
        return;
    }

    if (length > strlen(string) + 4) {
        return;
    }

    string[length] = '\0';
    string[length - 1] = '.';
    string[length - 2] = '.';
    string[length - 3] = '.';

}

/**
 * Convert ASCII 'A'-'Z' to 'a'-'z'.
 *
 * @param ch the character to convert
 * @return (ch - 0x20), IF ch is between 'A' and 'Z'.  Otherwise, ch.
 */
int q_tolower(const int ch) {
    if ((ch >= 'A') && (ch <= 'Z')) {
        return (ch - 0x20);
    }
    return ch;
}

#if defined(__BORLANDC__)

/**
 * wmemmove() implementation to make up for Borland C++ not having it.
 * Wide-char equivalent of memmove().
 *
 * @param dest the destination location
 * @param src the source wide-chars to copy
 * @param n the number of wide-chars to copy
 * @return dest
 */
wchar_t * wmemmove(wchar_t * dest, const wchar_t * src, size_t n) {
    return (wchar_t *) memmove(dest, src, n * sizeof(wchar_t));
}

#endif

#if defined(_MSC_VER) && !defined(_DEBUG)

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
wchar_t * wmemset(wchar_t * wcs, const wchar_t wc, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        wcs[i] = wc;
    }
    return wcs;
}

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
wchar_t * wmemcpy(wchar_t * dest, const wchar_t * src, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        dest[i] = src[i];
    }
    return dest;
}

#endif
