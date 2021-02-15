/*
 * music.h
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

#ifndef __MUSIC_H__
#define __MUSIC_H__

/* Includes --------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

/**
 * Qodem has several events that can generate a music sequence.
 */
typedef enum {
    Q_MUSIC_CONNECT,            /* Connected to system */
    Q_MUSIC_CONNECT_MODEM,      /* Connected to system over modem */
    Q_MUSIC_UPLOAD,             /* Successfully uploaded file(s) */
    Q_MUSIC_DOWNLOAD,           /* Successfully downloaded file(s) */
    Q_MUSIC_PAGE_SYSOP          /* Page sysop in host mode */
} Q_MUSIC_SEQUENCE;

/**
 * A music sequence is a list of tones.
 */
struct q_music_struct {
    int hertz;                  /* Hertz of tone (middle A = 440 Hz) */
    int duration;               /* Duration of tone in milliseconds */

    struct q_music_struct * next;
};

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

/**
 * This must be called to initialize the sound system.
 */
extern void music_init();

/**
 * Shut down the sound system.
 */
extern void music_teardown();

/**
 * Parse an "ANSI Music" sequence and play it.  ANSI music has two different
 * "standards", one of which is the GWBASIC PLAY statement and the other is
 * detailed at
 * http://www.textfiles.com/artscene/ansimusic/information/dybczak.txt .
 *
 * @param buffer buffer containing the music sequence
 * @param buffer_n length of buffer
 * @param interruptible if true, the user can press a key to stop the
 * sequence
 */
extern void play_ansi_music(const unsigned char * buffer, const int buffer_n,
                            const Q_BOOL interruptible);

/**
 * Play the tones that correspond to one of the qodem music events.
 *
 * @param sequence the sequence to play
 */
extern void play_sequence(const Q_MUSIC_SEQUENCE sequence);

/**
 * Play a list of tones.
 *
 * @param music the tones to play
 * @param interruptible if true, the user can press a key to stop the
 * sequence
 */
extern void play_music(const struct q_music_struct * music,
                       const Q_BOOL interruptible);

#ifdef __cplusplus
}
#endif

#endif /* __MUSIC_H__ */
