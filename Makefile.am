if Q_X11_BUILD
bin_PROGRAMS = qodem-x11
man_MANS = docs/qodem-x11.1
else
bin_PROGRAMS = qodem
bin_SCRIPTS = bin/xqodem
man_MANS = docs/qodem.1 docs/xqodem.1
endif

qodem_SOURCES = \
source/ansi.c \
source/atascii.c \
source/avatar.c \
source/codepage.c \
source/colors.c \
source/common.c \
source/console.c \
source/dialer.c \
source/emulation.c \
source/field.c \
source/forms.c \
source/help.c \
source/host.c \
source/input.c \
source/kermit.c \
source/keyboard.c \
source/modem.c \
source/music.c \
source/netclient.c \
source/options.c \
source/petscii.c \
source/phonebook.c \
source/protocols.c \
source/qodem.c \
source/screen.c \
source/script.c \
source/scrollback.c \
source/states.c \
source/translate.c \
source/vt100.c \
source/vt52.c \
source/xmodem.c \
source/zmodem.c \
source/ansi.h \
source/atascii.h \
source/avatar.h \
source/codepage.h \
source/colors.h \
source/common.h \
source/console.h \
source/dialer.h \
source/emulation.h \
source/field.h \
source/forms.h \
source/help.h \
source/host.h \
source/input.h \
source/kermit.h \
source/keyboard.h \
source/modem.h \
source/music.h \
source/netclient.h \
source/options.h \
source/petscii.h \
source/phonebook.h \
source/protocols.h \
source/qodem.h \
source/screen.h \
source/script.h \
source/scrollback.h \
source/states.h \
source/translate.h \
source/vt100.h \
source/vt52.h \
source/xmodem.h \
source/zmodem.h \
source/qcurses.h
qodem_x11_SOURCES = $(qodem_SOURCES)

AM_CPPFLAGS = -I. -I@srcdir@
DEFS = @DEFS@

SUBDIRS = @subdirs@ .

EXTRA_DIST = \
@srcdir@/build \
@srcdir@/bin/xqodem \
@srcdir@/docs/qodem.1 \
@srcdir@/docs/qodem-x11.1 \
@srcdir@/docs/xqodem.1 \
@srcdir@/docs/TODO.md \
@srcdir@/docs/worklog.md \
@srcdir@/docs/pd_announcement2.txt \
@srcdir@/CREDITS \
@srcdir@/FILE_ID.DIZ \
@srcdir@/README.md \
@srcdir@/Doxyfile \
@srcdir@/qodem.ide \
@srcdir@/vc6qodem \
@srcdir@/include \
@srcdir@/misc/fonts \
@srcdir@/misc/screenshots \
@srcdir@/misc/convert_ibbs.pl \
@srcdir@/misc/ibbs0321.txt \
@srcdir@/misc/ibbs0321.zip \
@srcdir@/lib/c \
@srcdir@/lib/upnp \
@srcdir@/lib/pdcurses \
@srcdir@/lib/cryptlib

dist-hook:
	rm -rf `find $(distdir)/lib -type f -name "*.o"`
	rm -rf `find $(distdir)/lib -type f -name "*.obj"`
	rm -rf `find $(distdir)/lib/cryptlib -type d -name "*-obj"`
	rm -f $(distdir)/lib/upnp/libminiupnpc.a
	rm -f $(distdir)/lib/cryptlib/libcl.a
	rm -f $(distdir)/lib/cryptlib/tools/endian

ACLOCAL_AMFLAGS =
