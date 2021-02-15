/*
 * avatar.h
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

#ifndef __AVATAR_H__
#define __AVATAR_H__

/* Includes --------------------------------------------------------------- */

#include "emulation.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

/**
 * Push one byte through the AVATAR emulator.
 *
 * @param from_modem one byte from the remote side.
 * @param to_screen if the return is Q_EMUL_FSM_ONE_CHAR or
 * Q_EMUL_FSM_MANY_CHARS, then to_screen will have a character to display on
 * the screen.
 * @return one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
extern Q_EMULATION_STATUS avatar(const unsigned char from_modem,
                                 wchar_t * to_screen);

/**
 * Reset the emulation state.
 */
extern void avatar_reset();

#ifdef __cplusplus
}
#endif

#endif /* __AVATAR_H__ */
