# This makefile was written 2015 by Autumn Lamonte.  To the extent
# possible under law, the author(s) have dedicated all copyright and
# related and neighboring rights to this Makefile to the public domain
# worldwide. This Makefile is distributed without any warranty.  You
# should have received a copy of the CC0 Public Domain Dedication
# along with this Makefile. If not, see
# <http://creativecommons.org/publicdomain/zero/1.0/>.
#
# This Makefile produces a PDCurses X11 library for use with qodem.

all: default

default: libXCurses

dist:

distdir:

# ----------------------------------------------------------------------------
# C compiler and linker options
# ----------------------------------------------------------------------------

PDCURSES_SRC_DIR = pdcurses
PDCURSES_OBJS_DIR = objs
PDCURSESX11_SRC_DIR = x11
PDCURSESX11_OBJS_DIR = objs
PDCURSESX11_BIN_DIR = .
PDCURSESX11_LIB = $(PDCURSESX11_BIN_DIR)/libXCurses.a
INC = -Iinclude
LDFLAGS =

# Compiler
CC = cc

# Archiver
AR = ar

# Normal settings
# EXTRA_LIBS =
# EXTRA_INC =
# CFLAGS = -O2 -Wall $(INC)
# LDLIBS =

# Debug settings
EXTRA_LIBS =
EXTRA_INC =
CFLAGS = -O0 -Wall -ggdb $(INC)
LDLIBS =



# ----------------------------------------------------------------------------
# What's below this line should not require modifications
# ----------------------------------------------------------------------------

CFLAGS += -DXCURSES -DPDC_WIDE -DPDC_FORCE_UTF8 -DPDC_XIM
CFLAGS += -DHAVE_XPM_H -DHAVE_UNISTD_H -DHAVE_SYS_SELECT_H
CFLAGS += -DRETSIGTYPE=void -DPDC_MAX_SIGNALS=NSIG
CFLAGS += -DHAVE_USLEEP

.SUFFIXES: .o .c

$(PDCURSESX11_OBJS_DIR):
	-mkdir $(PDCURSESX11_OBJS_DIR)

PDCURSESX11_SRC = \
$(PDCURSES_SRC_DIR)/addch.c \
$(PDCURSES_SRC_DIR)/addchstr.c \
$(PDCURSES_SRC_DIR)/addstr.c \
$(PDCURSES_SRC_DIR)/attr.c \
$(PDCURSES_SRC_DIR)/beep.c \
$(PDCURSES_SRC_DIR)/bkgd.c \
$(PDCURSES_SRC_DIR)/border.c \
$(PDCURSES_SRC_DIR)/clear.c \
$(PDCURSES_SRC_DIR)/color.c \
$(PDCURSES_SRC_DIR)/debug.c \
$(PDCURSES_SRC_DIR)/delch.c \
$(PDCURSES_SRC_DIR)/deleteln.c \
$(PDCURSES_SRC_DIR)/deprec.c \
$(PDCURSES_SRC_DIR)/getch.c \
$(PDCURSES_SRC_DIR)/getstr.c \
$(PDCURSES_SRC_DIR)/getyx.c \
$(PDCURSES_SRC_DIR)/inch.c \
$(PDCURSES_SRC_DIR)/inchstr.c \
$(PDCURSES_SRC_DIR)/initscr.c \
$(PDCURSES_SRC_DIR)/inopts.c \
$(PDCURSES_SRC_DIR)/insch.c \
$(PDCURSES_SRC_DIR)/insstr.c \
$(PDCURSES_SRC_DIR)/instr.c \
$(PDCURSES_SRC_DIR)/kernel.c \
$(PDCURSES_SRC_DIR)/keyname.c \
$(PDCURSES_SRC_DIR)/mouse.c \
$(PDCURSES_SRC_DIR)/move.c \
$(PDCURSES_SRC_DIR)/outopts.c \
$(PDCURSES_SRC_DIR)/overlay.c \
$(PDCURSES_SRC_DIR)/pad.c \
$(PDCURSES_SRC_DIR)/panel.c \
$(PDCURSES_SRC_DIR)/printw.c \
$(PDCURSES_SRC_DIR)/refresh.c \
$(PDCURSES_SRC_DIR)/scanw.c \
$(PDCURSES_SRC_DIR)/scr_dump.c \
$(PDCURSES_SRC_DIR)/scroll.c \
$(PDCURSES_SRC_DIR)/slk.c \
$(PDCURSES_SRC_DIR)/termattr.c \
$(PDCURSES_SRC_DIR)/terminfo.c \
$(PDCURSES_SRC_DIR)/touch.c \
$(PDCURSES_SRC_DIR)/util.c \
$(PDCURSES_SRC_DIR)/window.c \
$(PDCURSESX11_SRC_DIR)/pdcclip.c \
$(PDCURSESX11_SRC_DIR)/pdcdisp.c \
$(PDCURSESX11_SRC_DIR)/pdcgetsc.c \
$(PDCURSESX11_SRC_DIR)/pdckbd.c \
$(PDCURSESX11_SRC_DIR)/pdcscrn.c \
$(PDCURSESX11_SRC_DIR)/pdcsetsc.c \
$(PDCURSESX11_SRC_DIR)/pdcutil.c \
$(PDCURSESX11_SRC_DIR)/pdcx11.c \
$(PDCURSESX11_SRC_DIR)/sb.c \
$(PDCURSESX11_SRC_DIR)/ScrollBox.c \
$(PDCURSESX11_SRC_DIR)/x11.c

PDCURSESX11_OBJS = \
$(PDCURSES_OBJS_DIR)/addch.o \
$(PDCURSES_OBJS_DIR)/addchstr.o \
$(PDCURSES_OBJS_DIR)/addstr.o \
$(PDCURSES_OBJS_DIR)/attr.o \
$(PDCURSES_OBJS_DIR)/beep.o \
$(PDCURSES_OBJS_DIR)/bkgd.o \
$(PDCURSES_OBJS_DIR)/border.o \
$(PDCURSES_OBJS_DIR)/clear.o \
$(PDCURSES_OBJS_DIR)/color.o \
$(PDCURSES_OBJS_DIR)/debug.o \
$(PDCURSES_OBJS_DIR)/delch.o \
$(PDCURSES_OBJS_DIR)/deleteln.o \
$(PDCURSES_OBJS_DIR)/deprec.o \
$(PDCURSES_OBJS_DIR)/getch.o \
$(PDCURSES_OBJS_DIR)/getstr.o \
$(PDCURSES_OBJS_DIR)/getyx.o \
$(PDCURSES_OBJS_DIR)/inch.o \
$(PDCURSES_OBJS_DIR)/inchstr.o \
$(PDCURSES_OBJS_DIR)/initscr.o \
$(PDCURSES_OBJS_DIR)/inopts.o \
$(PDCURSES_OBJS_DIR)/insch.o \
$(PDCURSES_OBJS_DIR)/insstr.o \
$(PDCURSES_OBJS_DIR)/instr.o \
$(PDCURSES_OBJS_DIR)/kernel.o \
$(PDCURSES_OBJS_DIR)/keyname.o \
$(PDCURSES_OBJS_DIR)/mouse.o \
$(PDCURSES_OBJS_DIR)/move.o \
$(PDCURSES_OBJS_DIR)/outopts.o \
$(PDCURSES_OBJS_DIR)/overlay.o \
$(PDCURSES_OBJS_DIR)/pad.o \
$(PDCURSES_OBJS_DIR)/panel.o \
$(PDCURSES_OBJS_DIR)/printw.o \
$(PDCURSES_OBJS_DIR)/refresh.o \
$(PDCURSES_OBJS_DIR)/scanw.o \
$(PDCURSES_OBJS_DIR)/scr_dump.o \
$(PDCURSES_OBJS_DIR)/scroll.o \
$(PDCURSES_OBJS_DIR)/slk.o \
$(PDCURSES_OBJS_DIR)/termattr.o \
$(PDCURSES_OBJS_DIR)/terminfo.o \
$(PDCURSES_OBJS_DIR)/touch.o \
$(PDCURSES_OBJS_DIR)/util.o \
$(PDCURSES_OBJS_DIR)/window.o \
$(PDCURSESX11_OBJS_DIR)/pdcclip.o \
$(PDCURSESX11_OBJS_DIR)/pdcdisp.o \
$(PDCURSESX11_OBJS_DIR)/pdcgetsc.o \
$(PDCURSESX11_OBJS_DIR)/pdckbd.o \
$(PDCURSESX11_OBJS_DIR)/pdcscrn.o \
$(PDCURSESX11_OBJS_DIR)/pdcsetsc.o \
$(PDCURSESX11_OBJS_DIR)/pdcutil.o \
$(PDCURSESX11_OBJS_DIR)/pdcx11.o \
$(PDCURSESX11_OBJS_DIR)/sb.o \
$(PDCURSESX11_OBJS_DIR)/ScrollBox.o \
$(PDCURSESX11_OBJS_DIR)/x11.o

clean:
	-rm $(PDCURSESX11_LIB) core $(PDCURSESX11_OBJS)

distclean: clean

dist:

distdir:

install:

libXCurses: $(PDCURSESX11_LIB)

$(PDCURSESX11_LIB): $(PDCURSESX11_BIN_DIR) $(PDCURSESX11_OBJS_DIR) $(PDCURSESX11_OBJS) $(EXTRA_LIBS)
	$(AR) crs $(PDCURSESX11_LIB) $(PDCURSESX11_OBJS)

$(PDCURSES_OBJS_DIR)/%.o : $(PDCURSES_SRC_DIR)/%.c
	$(CC) $(CFLAGS) -o $@ -c $<

$(PDCURSESX11_OBJS_DIR)/%.o : $(PDCURSESX11_SRC_DIR)/%.c
	$(CC) $(CFLAGS) -o $@ -c $<
