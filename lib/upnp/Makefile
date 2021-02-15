# $Id: Makefile,v 1.125 2015/07/29 22:05:23 nanard Exp $
# MiniUPnP Project
# http://miniupnp.free.fr/
# http://miniupnp.tuxfamily.org/
# https://github.com/miniupnp/miniupnp
# (c) 2005-2015 Thomas Bernard
#
# ----
#
# This makefile was written 2015 by Autumn Lamonte.  To the extent
# possible under law, the author(s) have dedicated all copyright and
# related and neighboring rights to this Makefile to the public domain
# worldwide. This Makefile is distributed without any warranty.  You
# should have received a copy of the CC0 Public Domain Dedication
# along with this Makefile. If not, see
# <http://creativecommons.org/publicdomain/zero/1.0/>.
#
# This Makefile produces a barebones miniupnpc library for use with
# qodem.

all: default

default: libminiupnpc

# ----------------------------------------------------------------------------
# C compiler and linker options
# ----------------------------------------------------------------------------

MINIUPNPC_SRC_DIR = .
MINIUPNPC_OBJS_DIR = objs
MINIUPNPC_BIN_DIR = .
MINIUPNPC_LIB = $(MINIUPNPC_BIN_DIR)/libminiupnpc.a
INC = -I$(MINIUPNPC_SRC_DIR)
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
CFLAGS = -O0 -Wall -ggdb $(INC) -I$(EXTRA_INC)
LDLIBS =



# ----------------------------------------------------------------------------
# What's below this line should not require modifications
# ----------------------------------------------------------------------------

CFLAGS += -DMINIUPNPC_SET_SOCKET_TIMEOUT
CFLAGS += -DMINIUPNPC_GET_SRC_ADDR
CFLAGS += -D_BSD_SOURCE -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200112L

.SUFFIXES: .o .c

$(MINIUPNPC_OBJS_DIR):
	-mkdir $(MINIUPNPC_OBJS_DIR)

MINIUPNPC_SRC = \
$(MINIUPNPC_SRC_DIR)/connecthostport.c \
$(MINIUPNPC_SRC_DIR)/igd_desc_parse.c \
$(MINIUPNPC_SRC_DIR)/minisoap.c \
$(MINIUPNPC_SRC_DIR)/minissdpc.c \
$(MINIUPNPC_SRC_DIR)/miniupnpc.c \
$(MINIUPNPC_SRC_DIR)/miniwget.c \
$(MINIUPNPC_SRC_DIR)/minixml.c \
$(MINIUPNPC_SRC_DIR)/portlistingparse.c \
$(MINIUPNPC_SRC_DIR)/receivedata.c \
$(MINIUPNPC_SRC_DIR)/upnpcommands.c \
$(MINIUPNPC_SRC_DIR)/upnperrors.c \
$(MINIUPNPC_SRC_DIR)/upnpreplyparse.c

MINIUPNPC_OBJS = \
$(MINIUPNPC_OBJS_DIR)/connecthostport.o \
$(MINIUPNPC_OBJS_DIR)/igd_desc_parse.o \
$(MINIUPNPC_OBJS_DIR)/minisoap.o \
$(MINIUPNPC_OBJS_DIR)/minissdpc.o \
$(MINIUPNPC_OBJS_DIR)/miniupnpc.o \
$(MINIUPNPC_OBJS_DIR)/miniwget.o \
$(MINIUPNPC_OBJS_DIR)/minixml.o \
$(MINIUPNPC_OBJS_DIR)/portlistingparse.o \
$(MINIUPNPC_OBJS_DIR)/receivedata.o \
$(MINIUPNPC_OBJS_DIR)/upnpcommands.o \
$(MINIUPNPC_OBJS_DIR)/upnperrors.o \
$(MINIUPNPC_OBJS_DIR)/upnpreplyparse.o

clean:
	-rm $(MINIUPNPC_LIB) core $(MINIUPNPC_OBJS)

distclean: clean

dist:

distdir:

install:

libminiupnpc: $(MINIUPNPC_LIB)

$(MINIUPNPC_LIB): $(MINIUPNPC_BIN_DIR) $(MINIUPNPC_OBJS_DIR) $(MINIUPNPC_OBJS) $(EXTRA_LIBS)
	$(AR) crs $(MINIUPNPC_LIB) $(MINIUPNPC_OBJS)

$(MINIUPNPC_OBJS_DIR)/%.o : $(MINIUPNPC_SRC_DIR)/%.c
	$(CC) $(CFLAGS) -o $@ -c $<
