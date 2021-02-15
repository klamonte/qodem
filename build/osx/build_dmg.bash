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

# This creates a DMG file.  It is expected to be run from
# qodem/build/osx.  It leaves behind the 'dmg' directory which can be
# removed.

VERSION=1.0.1

if [ ! -f Qodem.app/Contents/MacOS/qodem-bin ]; then
    echo "Qodem binary not found.  Build it on a Mac first!"
    return -1;
fi

mkdir dmg
cp -r Qodem.app dmg
cp ../../README.md dmg/README.txt
cp ../../ChangeLog dmg/ChangeLog.txt
cp ../../CREDITS dmg/CREDITS.txt
cp ../../misc/ibbs0517.txt dmg/ibbs0517.txt

genisoimage -V qodem -D -R -apple -no-pad -o qodem-${VERSION}.dmg dmg
