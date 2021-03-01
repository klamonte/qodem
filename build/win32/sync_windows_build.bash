#!/bin/bash

# qodem - Qodem Terminal Emulator
#
# Written 2003-2021 by Autumn Lamonte ⚧ Trans Liberation Now
#
# To the extent possible under law, the author(s) have dedicated all
# copyright and related and neighboring rights to this software to the
# public domain worldwide. This software is distributed without any
# warranty.
#
# You should have received a copy of the CC0 Public Domain Dedication
# along with this software. If not, see
# <http://creativecommons.org/publicdomain/zero/1.0/>.

QODEM_DIR=/home/autumn/code/qodem/git/qodem
WINDOWS_BUILD_DIR=/home/autumn/shared/qodem2k/git/qodem
# RSYNCN=-n
RSYNCN=

# Sync the C source files
rsync $RSYNCN -av --include='*/' --include='**/*.[ch]' --exclude='*' \
      $QODEM_DIR/* $WINDOWS_BUILD_DIR

# Copy and convert the text files
TEXTFILES="ChangeLog README.md CREDITS COPYING FILE_ID.DIZ"
for i in $TEXTFILES ; do
    cat $QODEM_DIR/$i | unix2dos > $WINDOWS_BUILD_DIR/$i
done

# Sync the build directory
rsync $RSYNCN -av --exclude=sync_windows_build.bash $QODEM_DIR/build $WINDOWS_BUILD_DIR
