/*
 * qcurses.h
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
 * This loads the header for the correct curses library (ncursesw or PDCurses
 * widechar).  It is a bit finicky because of conflicts between ncurses.h,
 * PDCurses curses.h, and windows.h.
 */

/* Support wide char ncurses */
#ifdef Q_PDCURSES_WIN32
#  define PDC_WIDE
#  include "curses.h"
/*
 * MOUSE_MOVED has a conflict with windows.h, so redefine it to
 * Q_MOUSE_MOVED.
 */
#  undef MOUSE_MOVED
#  define Q_MOUSE_MOVED         (Mouse_status.changes & PDC_MOUSE_MOVED)
#  include "term.h"
#else
#  ifdef Q_PDCURSES
#    define PDC_WIDE
#    define XCURSES
#    include "curses.h"
#    include "term.h"
#  else
#    if defined(__FreeBSD__) || \
        defined(__OpenBSD__) || \
        defined(__NetBSD__) || \
        defined(__APPLE__) || \
        defined(__HAIKU__)
#      ifndef _XOPEN_SOURCE_EXTENDED
#        define _XOPEN_SOURCE_EXTENDED
#      endif
#      include <curses.h>
#    else
#      ifdef HAVE_CONFIG_H
#        include "config.h"
#      endif
#      ifdef HAVE_NCURSESW_CURSES_H
         /*
          * Autoconf detected wide-char curses as ncursesw/curses.h.  This is
          * the most common route on Linux systems.
          */
#        define _GNU_SOURCE
#        include <ncursesw/curses.h>
#      else
         /*
          * Some versions of Linux (e.g. Arch) use the wide-char ncurses as
          * plain old curses.h, like the BSD's do.
          */
#        ifndef _XOPEN_SOURCE_EXTENDED
#          define _XOPEN_SOURCE_EXTENDED
#        endif
#        include <curses.h>
#      endif /* HAVE_CURSES_NCURSES_H */
#    endif /* BSD __APPLE__ __HAIKU__ */
#  endif /* Q_PDCURSES */
#endif /* Q_PDCURSES_WIN32 */
