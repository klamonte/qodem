/*
 * music.c
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
#include <string.h>
#include <ctype.h>
#ifdef Q_PDCURSES_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <assert.h>
#include "input.h"
#include "options.h"
#include "qodem.h"
#include "music.h"

#ifdef Q_SOUND_SDL
#include <SDL/SDL.h>
#include <SDL/SDL_audio.h>
#include <math.h>
#endif /* Q_SOUND_SDL */

/* Set this to a not-NULL value to enable debug log. */
/* static const char * DLOGNAME = "music"; */
static const char * DLOGNAME = NULL;

/*
 * Each note is one semitone times the note prior.  The semitone value is the
 * 12th root of 2, i.e. 2^(1/12) .
 */
#define SEMITONE 1.05946309436

/**
 * The frequencies in Hz for the seven octaves (12 notes each) defined by the
 * PLAY statement.
 *
 * A chart showing these values is at:
 * http://www.textfiles.com/artscene/ansimusic/information/dybczak.txt
 *
 * Sound CODE Musical Note Frequency Values (7 Octives):
 *
 *            C    C#   D    D#   E    F    F#   G    G#   A    A#   B
 *
 *            65   69   73   78   82   87   92   98  104  110  116  123
 *           131  139  147  156  165  175  185  196  208  220  233  247
 *           262  278  294  312  330  350  370  392  416  440  466  494
 *           524  556  588  624  660  700  740  784  832  880  932  988
 *          1048 1112 1176 1248 1320 1400 1480 1568 1664 1760 1864 1976
 *          2096 2224 2352 2496 2640 2800 2960 3136 3328 3520 3728 3952
 *          4192 4448 4704 4992 5280 5600 5920 6272 6656 7040 7456 7904
 *
 */
static float frequency_table[7][12];

#ifdef Q_SOUND_SDL

/**
 * When true, SDL was initialized successfully.
 */
static Q_BOOL sdl_ok = Q_FALSE;

/**
 * The frequency at the speakers = 22 kHz.
 */
static int output_frequency = 11025 * 2;

/**
 * Begin sine wave at x = 0.
 */
static int sdl_sine_wave_x = 0;

/**
 * Frequency of the currently-playing note.
 */
static double sdl_Hz = 0;

/**
 * Compute a discrete sine wave to send to the speakers.
 *
 * @param userdata SDL optional user data, ignored
 * @param output the audio output buffer
 * @param output_max the number of bytes that must be filled to send to the
 * speakers
 */
static void sdl_callback(void * userdata, Uint8 * output, int output_max) {
    double pi = 3.1415926535;           /* Obviously pi */
    double A = 20;                      /* Amplitude */
    double SR = output_frequency;       /* Frequency at the speakers */
    double F = 2 * pi * sdl_Hz / SR;    /* Ultimate output frequency */
    int i;

    for (i = 0; i < output_max; i++) {
        output[i] = (Uint8) (A * sin(F * sdl_sine_wave_x) + 128) & 0xFF;
        sdl_sine_wave_x++;
    }
}

#endif /* Q_SOUND_SDL */

/**
 * This must be called to initialize the sound system.
 */
void music_init() {
    int i, j;
    float current_tone;

    DLOG(("music_init()\n"));

    /*
     * Middle C is at the beginning of the third octave,
     * i.e. frequency_table[2][0].
     *
     * Let's count down from two octaves below middle A:
     *     A G# G F# F E D# D C# C  ==> 9 semitones down
     */
    current_tone = 110.0f;
    for (i = 0; i < 9; i++) {
        current_tone /= SEMITONE;
    }

    DLOG(("music_init(): first tone = %f hz\n", current_tone));

    /*
     * Now count up the seven octaves with twelve semitones each.
     */
    for (i = 0; i < 7; i++) {
        for (j = 0; j < 12; j++) {
            frequency_table[i][j] = current_tone;
            current_tone *= SEMITONE;
        }
    }

    /* Emit frequency table to debug log */
    if (DLOGNAME != NULL) {
        for (i = 0; i < 7; i++) {
            for (j = 0; j < 12; j++) {
                DLOG(("music_init(): [%d][%d] = %f hz\n", i, j,
                        frequency_table[i][j]));
            }
        }
    }

#ifdef Q_SOUND_SDL

    /*
     * Initialize the SDL system.
     */
    if (SDL_Init(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec spec;
        sdl_ok = Q_TRUE;
        spec.freq = output_frequency;
        spec.format = AUDIO_U8;
        spec.channels = 1;
        spec.silence = 0;
        spec.samples = 4 * output_frequency;
        spec.padding = 0;
        spec.size = 0;
        spec.userdata = 0;
        spec.callback = sdl_callback;
        if (SDL_OpenAudio(&spec, NULL) < 0) {
            DLOG(("music_init(): SDL error: %s\n", SDL_GetError()));
            SDL_Quit();
            sdl_ok = Q_FALSE;
        }
    } else {
        sdl_ok = Q_FALSE;
    }
    SDL_PauseAudio(1);

#endif /* Q_SOUND_SDL */

}

/**
 * Shut down the sound system.
 */
void music_teardown() {
    DLOG(("music_teardown()\n"));

#ifdef Q_SOUND_SDL
    SDL_PauseAudio(1);

    if (sdl_ok == Q_TRUE) {
        SDL_CloseAudio();
        SDL_Quit();
    }
#endif

}

/**
 * Play a list of tones.
 *
 * @param music the tones to play
 * @param interruptible if true, the user can press a key to stop the
 * sequence
 */
void play_music(const struct q_music_struct * music,
                const Q_BOOL interruptible) {

#ifndef Q_PDCURSES_WIN32
    int keystroke;
    static Q_BOOL on_linux = Q_FALSE;
#endif

    static time_t ban_time = 0;
    time_t now;

    if (q_status.sound == Q_FALSE) {
        return;
    }

    time(&now);
    if (now - ban_time < 5) {
        /*
         * User banned music for five seconds, do nothing.
         */
        return;
    }

    while (music != NULL) {
        DLOG(("play_music(): hertz = %d hz duration = %d millis\n",
              music->hertz, music->duration));

#ifdef Q_SOUND_SDL

        sdl_Hz = music->hertz;
        sdl_sine_wave_x = 0;

        /*
         * Begin sound
         */
        if (sdl_ok == Q_TRUE) {
            SDL_PauseAudio(0);
        }

#endif

#ifdef Q_PDCURSES_WIN32

        /*
         * Windows: use the Beep() function.  This should work everywhere
         * except Vista and 64-bit XP.
         *
         * This will actually pause until the beep is complete, so is not
         * actually interruptible.
         */
        Beep(music->hertz, music->duration);

#else

        static Q_BOOL first = Q_FALSE;
        if (first == Q_TRUE) {
            char * term = getenv("TERM");
            if (strstr(term, "linux") != NULL) {
                on_linux = Q_TRUE;
            }
            first = Q_FALSE;
        }

#ifdef Q_SOUND_SDL
        if ((on_linux == Q_FALSE) && (sdl_ok == Q_FALSE)) {
#else
        if (on_linux == Q_FALSE) {
#endif
            /*
             * No SDL, no console, no output.
             */
            return;
        }
        /*
         * Linux can set the console beep with a weird CSI string.
         */
        if (music->hertz > 0) {
            /*
             * Play a "note": set duration and frequency and emit BEL.
             */
            fprintf(stdout, "\033[10;%d]\033[11;%d]\007", music->hertz,
                    music->duration);
            fflush(stdout);
        }

#endif

        /*
         * At this point a tone is playing (or has already played).
         */
        assert(music->duration >= 0);

#ifndef Q_PDCURSES_WIN32

        if (interruptible == Q_TRUE) {
            /*
             * Use ncurses to timeout - any key will stop this sequence.
             */
            timeout(music->duration);
            keystroke = getch();
            if ((keystroke == '`') || (keystroke == Q_KEY_ESCAPE)) {
                /*
                 * Ban all music for five seconds.
                 */
                time(&ban_time);
            }
            if (keystroke != Q_ERR) {
                /*
                 * Terminate this sequence.
                 */
                break;
            }
        } else {
            /*
             * Must wait for the tone to finish.
             */
            usleep(music->duration * 1000);
        }

#endif

        /*
         * On to the next note...
         */
        music = music->next;

    } /* while (music != NULL) */

    /*
     * Reset timeout so the UI won't be sluggish forever
     */
    timeout(0);

#ifdef Q_SOUND_SDL
    /*
     * Cease sound
     */
    if (sdl_ok == Q_TRUE)
        SDL_PauseAudio(1);
#endif

#ifndef Q_PDCURSES_WIN32
#ifdef Q_SOUND_SDL
    if ((on_linux == Q_TRUE) && (sdl_ok == Q_FALSE)) {
#else
    if (on_linux == Q_TRUE) {
#endif
	/*
         * Restore the console beep.  The linux defaults are in
         * drivers/char/console.c, as of 2.4.22 it's 750 Hz 250 milliseconds.
         */
        fprintf(stdout, "\033[10;750]\033[11;250]");
        fflush(stdout);
    }
#endif

}

/*
 * PLAY Statement
 *
 * Purpose:
 *
 * To play music by embedding a music macro language into the string data type.
 *
 * Syntax:
 *
 * PLAY string expression
 *
 * Comments:
 *
 * The single-character commands in PLAY are as follows:
 *
 *  A-G [#,+,-]   A-G are notes. # or + following a note produces a sharp;
 *                - produces a flat.  Any note followed by #,+,or - must refer
 *                to a black key on a piano.
 *
 *  L(n)          Sets the length of each note. L4 is a quarter note, L1 is a
 *                whole note, and so on. n may be from 1 to 64.
 *
 *                Length may also follow the note to change the length for that
 *                note only. A16 is equivalent to L16A.
 *
 *  MF            Music foreground. PLAY and SOUND statements are to run in
 *                foreground. That is, each subsequent note or sound is not
 *                started until the previous note or sound is finished. This
 *                is the initial default.
 *
 *  MB            Music background. PLAY and SOUND statements are to run in
 *                background. That is, each note or sound is placed in a buffer
 *                allowing the BASIC program to continue execution while music
 *                plays in the background. As many as 32 notes (or rests) can
 *                be played in background at one time.
 *
 *  MN            Music normal. Each note plays seven-eighths of the time
 *                determined by L (length).
 *
 *  ML            Music legato. Each note plays the full period set by L.
 *
 *  MS            Music staccato. Each note plays three-quarters of the time
 *                determined by L.
 *
 *  N(n)          Play note n. n may range from 0 to 84. In the 7 possible
 *                octaves, there are 84 notes. n set to 0 indicates a rest.
 *
 *  O(n)          Octave 0 sets the current octave. There are 7 octaves
 *                (0 through 6). Default is 4. Middle C is at the beginning
 *                of octave 3.
 *
 *  P(n)          Pause. P may range from 1-64.
 *
 *  T(n)          Tempo. T sets the number of L4s in a minute. n may range
 *                from 32-255. Default is 120.
 *
 *  . (period)    A period after a note increases the playing time of the note
 *                by 3/2 times the period determined by L (length of note)
 *                times T (tempo). Multiple periods can appear after  a note,
 *                and the playing time is scaled accordingly. For example, A.
 *                will cause the note A to play one and half times the playing
 *                time determined by L (length of the note) times T (the tempo);
 *                two periods placed after A (A..) will cause the note to be
 *                played at 9/4 times its ascribed value;  an A with three
 *                periods (A...) at 27/8, etc.
 *
 *                Periods may also appear after a P (pause), and increase the
 *                pause length as described above.
 *
 *  Xstring;      Executes a substring, where string is a variable assigned to a
 *                string of PLAY commands.
 *
 * Because of the slow clock interrupt rate, some notes do not play at higher
 * tempos;  for example, 1.64 at T255. These note/tempo combinations must be
 * determined through experimentation.
 *
 * >n             A greater-than symbol preceding the note n plays the note in
 *                the next higher octave.
 *
 * <n             A less-than symbol preceding the note n plays the note in the
 *                next lower octave.
 *
 * Note
 *
 * Numeric arguments follow the same syntax described under the DRAW  statement.
 *
 */

/**
 * The states for the music string parser.
 */
typedef enum {
    MUSIC_NONE,

    MUSIC_SOUND,

    MUSIC_L,
    MUSIC_M,
    MUSIC_N,
    MUSIC_O,
    MUSIC_P,
    MUSIC_T,

    MUSIC_DIGITAL_FREQ,
    MUSIC_DIGITAL_DURATION,
    MUSIC_DIGITAL_CYCLES,
    MUSIC_DIGITAL_CYCLEDELAY,
    MUSIC_DIGITAL_VARIATION,

} MUSIC_STATE;

/**
 * Determine a duration in millis from a tempo and note length.
 *
 * @param tempo the musical tempo, 60 = 1 beat per second
 * @param note_length the music note length, 4 is a quarter note, 1 is a
 * whole note, etc.
 * @return the duration in millis of the note
 */
static int duration_millis(const int tempo, const int note_length) {

    /*
     * Duration (millis) = 1 /
     *
     *  (tempo/60) beat | (note_length/4) note | second
     *  ------------------------------------------------------
     *        second    |        beat          | 1000 millis
     *
     */
    return (int) ((float) 1000.0f /
        (((float) tempo / 60.0f) * ((float) note_length / 4.0f)));
}

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
void play_ansi_music(const unsigned char * buffer, const int buffer_n,
                     const Q_BOOL interruptible) {

    /*
     * Default music options
     */
    static int tempo = 120;
    static int octave = 4;
    static int length = 4;
    /*
     * normal, legato, stacatto
     */
    static float style = 0.875;

    /*
     * There is a second ANSI music style of mapping frequency, length,
     * etc. to 16-bit signed and unsigned integer values.
     */
    static int digital_freq = 0;
    static int digital_duration = 0;
    static int digital_cycles = 0;
    static int digital_cycledelay = 0;
    static int digital_variation = 0;

    char * digits_end;

    int i;
    MUSIC_STATE state = MUSIC_NONE;

    struct q_music_struct music;
    struct q_music_struct * p;
    struct q_music_struct * q;

    /*
     * For the '>' and '<' options
     */
    int note_octave = -1;

    /*
     * For digits past the note
     */
    int note_length = 4;

    float note_length_multiplier = 1.0;

    /*
     * Index into the second column of frequency_table (1..12)
     */
    int current_note = 0;

    if (q_status.sound == Q_FALSE) {
        return;
    }

    memset(&music, 0, sizeof(music));
    p = &music;

    /*
     * Parse the GWBASIC/BASICA PLAY command string using a finite state
     * machine.
     */
    for (i = 0; i < buffer_n; i++) {
        DLOG(("play_ansi_music(): state = %d note_octave = %d buffer[i] = %c\n",
              state, note_octave, buffer[i]));

music_top:

        /*
         * Skip whitespace
         */
        if (q_isspace(buffer[i])) {
            continue;
        }

        switch (state) {

        case MUSIC_NONE:
            note_length = length;
            if (note_octave < 0) {
                note_octave = octave;
            }
            note_length_multiplier = 1.0;

            /*
             * Looking for L, M, N, O, P, T, >, <
             */

            if (toupper(buffer[i]) == 'L') {
                state = MUSIC_L;
                continue;
            } else if (toupper(buffer[i]) == 'M') {
                state = MUSIC_M;
                continue;
            } else if (toupper(buffer[i]) == 'N') {
                state = MUSIC_N;
                continue;
            } else if (toupper(buffer[i]) == 'O') {
                state = MUSIC_O;
                continue;
            } else if (toupper(buffer[i]) == 'P') {
                state = MUSIC_P;
                continue;
            } else if (toupper(buffer[i]) == 'T') {
                state = MUSIC_T;
                continue;
            } else if (buffer[i] == '<') {
                note_octave = octave - 1;
                if (note_octave < 0) {
                    note_octave = 0;
                }
                continue;
            } else if (buffer[i] == '>') {
                note_octave = octave + 1;
                if (note_octave > 6) {
                    note_octave = 6;
                }
                continue;
            }

            if ((tolower(buffer[i]) >= 'a') && (tolower(buffer[i]) <= 'g')) {

                /*
                 * Set current note
                 */
                switch (tolower(buffer[i])) {

                case 'c':
                    current_note = 0;
                    break;

                case 'd':
                    current_note = 2;
                    break;

                case 'e':
                    current_note = 4;
                    break;

                case 'f':
                    current_note = 5;
                    break;

                case 'g':
                    current_note = 7;
                    break;

                case 'a':
                    current_note = 9;
                    break;

                case 'b':
                    current_note = 11;
                    break;

                }

                state = MUSIC_SOUND;
            }

            if (q_isdigit(buffer[i])) {
                /*
                 * This is an integer sequence of:
                 * Freq; Duration; Cycles; CycleDelay; Variation
                 *
                 * See: http://www.textfiles.com/artscene/ansimusic/information/dybczak.txt
                 */
                state = MUSIC_DIGITAL_FREQ;
                i--;
            }
            if (buffer[i] == ';') {
                /*
                 * This is an integer sequence of:
                 * Freq; Duration; Cycles; CycleDelay; Variation
                 *
                 * See: http://www.textfiles.com/artscene/ansimusic/information/dybczak.txt
                 */
                state = MUSIC_DIGITAL_DURATION;
            }
            break;

        case MUSIC_SOUND:

            if (((tolower(buffer[i]) >= 'a') && (tolower(buffer[i]) <= 'g')) ||
                (buffer[i] == '>') || (buffer[i] == '<')) {

                /*
                 * Play the old note
                 */
                /*
                 * Play this note
                 */
                p->hertz = frequency_table[note_octave][current_note];
                p->duration = duration_millis(tempo, note_length);
                p->duration *= note_length_multiplier;

                q = (struct q_music_struct *)
                    Xmalloc(sizeof(struct q_music_struct), __FILE__, __LINE__);
                memset(q, 0, sizeof(struct q_music_struct));
                p->next = q;
                q->hertz = 0;
                q->duration = p->duration * (1 - style);
                p->duration *= style;
                p = q;
                q = (struct q_music_struct *)
                    Xmalloc(sizeof(struct q_music_struct), __FILE__, __LINE__);
                memset(q, 0, sizeof(struct q_music_struct));
                p->next = q;
                p = q;

                /*
                 * Reset the octave
                 */
                note_octave = octave;
                note_length = length;
                note_length_multiplier = 1.0;

                if ((buffer[i] == '<') && (i + 1 == buffer_n)) {
                    /*
                     * Error: string terminated on '<'
                     */
                    state = MUSIC_NONE;
                    continue;
                } else if ((buffer[i] == '>') && (i + 1 == buffer_n)) {
                    /*
                     * Error: string terminated on '>'
                     */
                    state = MUSIC_NONE;
                    continue;
                } else if (buffer[i] == '<') {
                    note_octave = octave - 1;
                    if (note_octave < 0) {
                        note_octave = 0;
                    }
                    /*
                     * Seek the next byte
                     */
                    i++;
                } else if (buffer[i] == '>') {
                    note_octave = octave + 1;
                    if (note_octave > 6) {
                        note_octave = 6;
                    }
                    /*
                     * Seek the next byte
                     */
                    i++;
                }

                /*
                 * Set current_note to the new note
                 */
                switch (tolower(buffer[i])) {

                case 'c':
                    current_note = 0;
                    break;

                case 'd':
                    current_note = 2;
                    break;

                case 'e':
                    current_note = 4;
                    break;

                case 'f':
                    current_note = 5;
                    break;

                case 'g':
                    current_note = 7;
                    break;

                case 'a':
                    current_note = 9;
                    break;

                case 'b':
                    current_note = 11;
                    break;

                }

                continue;
            }

            /*
             * Looking for #, +, -
             */
            if (buffer[i] == '#') {
                DLOG(("SHARP\n"));
                current_note++;
                continue;
            } else if (buffer[i] == '+') {
                DLOG(("SHARP\n"));
                current_note++;
                continue;
            } else if (buffer[i] == '-') {
                DLOG(("FLAT\n"));
                current_note--;
                continue;
            } else if (buffer[i] == '.') {
                DLOG(("DOT\n"));
                /*
                 * Increase length by 50%
                 */
                note_length_multiplier *= 1.5;
                continue;
            } else if ((buffer[i] >= '0') && (buffer[i] <= '9')) {
                /*
                 * Change duration of the note
                 */
                note_length = strtol((char *) buffer + i, &digits_end, 10);
                DLOG(("new note length: %d\n", note_length));

                i = ((unsigned char *) digits_end - buffer) - 1;
                continue;
            }

            /*
             * This must be some other option, so re-parse it.  First add the
             * original note.
             */
            p->hertz = frequency_table[note_octave][current_note];
            p->duration = duration_millis(tempo, note_length);
            p->duration *= note_length_multiplier;

            q = (struct q_music_struct *) Xmalloc(sizeof(struct q_music_struct),
                                                  __FILE__, __LINE__);
            memset(q, 0, sizeof(struct q_music_struct));
            p->next = q;
            q->hertz = 0;
            q->duration = p->duration * (1 - style);
            p->duration *= style;
            p = q;
            q = (struct q_music_struct *) Xmalloc(sizeof(struct q_music_struct),
                                                  __FILE__, __LINE__);
            memset(q, 0, sizeof(struct q_music_struct));
            p->next = q;
            p = q;

            /*
             * Reset the octave
             */
            note_octave = octave;
            note_length = length;
            note_length_multiplier = 1.0;

            state = MUSIC_NONE;
            goto music_top;

        case MUSIC_M:

            /*
             * Looking for F, B, L, N, S
             */
            if ((tolower(buffer[i]) == 'f') || (tolower(buffer[i]) == 'b')) {
                DLOG(("MUSIC FOREGROUND/BACKGROUND\n"));
                state = MUSIC_NONE;
                break;
            }

            /*
             * Normal
             */
            if (tolower(buffer[i]) == 'n') {
                DLOG(("MUSIC NORMAL\n"));
                style = 0.875;
                state = MUSIC_NONE;
                break;
            }

            /*
             * Legato
             */
            if (tolower(buffer[i]) == 'l') {
                DLOG(("MUSIC LEGATO\n"));
                style = 1.000;
                state = MUSIC_NONE;
                break;
            }

            /*
             * Staccato
             */
            if (tolower(buffer[i]) == 's') {
                DLOG(("MUSIC STACATTO\n"));
                style = 0.750;
                state = MUSIC_NONE;
                break;
            }

            if (q_isdigit(buffer[i])) {
                /*
                 * This is an integer sequence of:
                 * Freq; Duration; Cycles; CycleDelay; Variation
                 *
                 * See: http://www.textfiles.com/artscene/ansimusic/information/dybczak.txt
                 */
                state = MUSIC_DIGITAL_FREQ;
                i--;
            }
            if (buffer[i] == ';') {
                /*
                 * This is an integer sequence of:
                 * Freq; Duration; Cycles; CycleDelay; Variation
                 *
                 * See: http://www.textfiles.com/artscene/ansimusic/information/dybczak.txt
                 */
                state = MUSIC_DIGITAL_DURATION;
                break;
            }

            /*
             * This must be some other option, so re-parse it
             */
            state = MUSIC_NONE;
            goto music_top;

        case MUSIC_L:

            /*
             * Looking for digits
             */
            if ((buffer[i] >= '0') && (buffer[i] <= '9')) {
                length = strtol((char *) buffer + i, &digits_end, 10);

                DLOG(("new length: %d\n", length));
                i = ((unsigned char *) digits_end - buffer) - 1;

            } else {
                /*
                 * Syntax error, bail out
                 */
                goto music_abort;
            }

            state = MUSIC_NONE;
            break;

        case MUSIC_N:

            /*
             * Looking for digits
             */
            if ((buffer[i] >= '0') && (buffer[i] <= '9')) {
                current_note = strtol((char *) buffer + i, &digits_end, 10);

                DLOG(("RAW current_note: %d\n", current_note));
                i = ((unsigned char *) digits_end - buffer) - 1;

                /*
                 * Play this note
                 */
                p->hertz =
                    frequency_table[current_note / 12][current_note % 12];
                p->duration = duration_millis(tempo, note_length);
                p->duration *= note_length_multiplier;

                q = (struct q_music_struct *)
                    Xmalloc(sizeof(struct q_music_struct), __FILE__, __LINE__);
                memset(q, 0, sizeof(struct q_music_struct));
                p->next = q;
                q->hertz = 0;
                q->duration = p->duration * (1 - style);
                p->duration *= style;
                p = q;
                q = (struct q_music_struct *)
                    Xmalloc(sizeof(struct q_music_struct), __FILE__, __LINE__);
                memset(q, 0, sizeof(struct q_music_struct));
                p->next = q;
                p = q;

            } else {
                /*
                 * Syntax error, bail out
                 */
                goto music_abort;
            }

            state = MUSIC_NONE;
            break;

        case MUSIC_O:

            /*
             * Looking for digits
             */
            if ((buffer[i] >= '0') && (buffer[i] <= '9')) {
                octave = strtol((char *) buffer + i, &digits_end, 10);
                DLOG(("new octave: %d\n", octave));
                i = ((unsigned char *) digits_end - buffer) - 1;
                if ((octave < 0) || (octave > 6)) {
                    /*
                     * Invalid octave, reset to default
                     */
                    octave = 4;
                }
                note_octave = octave;

            } else {
                /*
                 * Syntax error, bail out
                 */
                goto music_abort;
            }

            state = MUSIC_NONE;
            break;

        case MUSIC_P:

            /*
             * Looking for . or digits
             */
            if (buffer[i] == '.') {
                DLOG(("DOT\n"));
                /*
                 * Increase length by 50%
                 */
                note_length_multiplier *= 1.5;
                continue;

            } else if ((buffer[i] >= '0') && (buffer[i] <= '9')) {
                /*
                 * Change duration of the note
                 */
                note_length = strtol((char *) buffer + i, &digits_end, 10);
                DLOG(("new note length: %d\n", note_length));
                i = ((unsigned char *) digits_end - buffer) - 1;
                continue;
            }

            /*
             * Play the pause
             */
            p->hertz = 0;
            p->duration = duration_millis(tempo, note_length);
            p->duration *= note_length_multiplier;
            q = (struct q_music_struct *) Xmalloc(sizeof(struct q_music_struct),
                                                  __FILE__, __LINE__);
            memset(q, 0, sizeof(struct q_music_struct));
            p->next = q;
            p = q;

            /*
             * This must be some other option, so re-parse it
             */
            state = MUSIC_NONE;
            goto music_top;

        case MUSIC_T:

            /*
             * Looking for digits
             */
            if ((buffer[i] >= '0') && (buffer[i] <= '9')) {
                tempo = strtol((char *) buffer + i, &digits_end, 10);
                DLOG(("new tempo: %d\n", tempo));
                i = ((unsigned char *) digits_end - buffer) - 1;

            } else {
                /*
                 * Syntax error, bail out
                 */
                goto music_abort;
            }

            state = MUSIC_NONE;
            break;

        case MUSIC_DIGITAL_FREQ:
            /*
             * Looking for digits, '.', '+', or '-'
             */
            if (((buffer[i] >= '0') && (buffer[i] <= '9')) ||
                (buffer[i] == '.') || (buffer[i] == '+') || (buffer[i] == '-')
            ) {
                digital_freq = (int) strtod((char *) buffer + i, &digits_end);

                DLOG(("new frequency: %d\n", digital_freq));
                i = ((unsigned char *) digits_end - buffer) - 1;
            } else if (buffer[i] == ';') {
                /*
                 * Next state
                 */
                state = MUSIC_DIGITAL_DURATION;
            } else {
                /*
                 * Syntax error, bail out
                 */
                goto music_abort;
            }
            break;

        case MUSIC_DIGITAL_DURATION:
            /*
             * Looking for digits, '.', '+', or '-'
             */
            if (((buffer[i] >= '0') && (buffer[i] <= '9')) ||
                (buffer[i] == '.') || (buffer[i] == '+') || (buffer[i] == '-')
            ) {
                digital_duration =
                    (int) strtod((char *) buffer + i, &digits_end);

                DLOG(("new duration: %d\n", digital_duration));
                i = ((unsigned char *) digits_end - buffer) - 1;
            } else if (buffer[i] == ';') {
                /*
                 * Next state
                 */
                state = MUSIC_DIGITAL_CYCLES;
            } else {
                /*
                 * Syntax error, bail out
                 */
                goto music_abort;
            }
            break;

        case MUSIC_DIGITAL_CYCLES:
            /*
             * Looking for digits, '.', '+', or '-'
             */
            if (((buffer[i] >= '0') && (buffer[i] <= '9')) ||
                (buffer[i] == '.') || (buffer[i] == '+') || (buffer[i] == '-')
            ) {
                digital_cycles = (int) strtod((char *) buffer + i, &digits_end);

                DLOG(("new cycles: %d\n", digital_cycles));
                i = ((unsigned char *) digits_end - buffer) - 1;
            } else if (buffer[i] == ';') {
                /*
                 * Next state
                 */
                state = MUSIC_DIGITAL_CYCLEDELAY;
            } else {
                /*
                 * Syntax error, bail out
                 */
                goto music_abort;
            }
            break;

        case MUSIC_DIGITAL_CYCLEDELAY:
            /*
             * Looking for digits, '.', '+', or '-'
             */
            if (((buffer[i] >= '0') && (buffer[i] <= '9')) ||
                (buffer[i] == '.') || (buffer[i] == '+') || (buffer[i] == '-')
            ) {
                digital_cycledelay =
                    (int) strtod((char *) buffer + i, &digits_end);

                DLOG(("new cycledelay: %d\n", digital_cycledelay));
                i = ((unsigned char *) digits_end - buffer) - 1;
            } else if (buffer[i] == ';') {
                /*
                 * Next state
                 */
                state = MUSIC_DIGITAL_VARIATION;
            } else {
                /*
                 * Syntax error, bail out
                 */
                goto music_abort;
            }
            break;

        case MUSIC_DIGITAL_VARIATION:
            /*
             * Looking for digits, '.', '+', or '-'
             */
            if (((buffer[i] >= '0') && (buffer[i] <= '9')) ||
                (buffer[i] == '.') || (buffer[i] == '+') || (buffer[i] == '-')
            ) {
                digital_variation =
                    (int) strtod((char *) buffer + i, &digits_end);

                DLOG(("new variation: %d\n", digital_variation));
                i = ((unsigned char *) digits_end - buffer);
                DLOG(("ANSI Music style 2: i = %d buffer_n = %d\n", i,
                      buffer_n));
            } else {
                /*
                 * Syntax error, bail out
                 */
                goto music_abort;
            }

            if (i == buffer_n) {
                DLOG(("ANSI Music style 2: prepare to play:\n"));
                DLOG((" digital_freq       : %d\n", digital_freq));
                DLOG((" digital_duration   : %d\n", digital_duration));
                DLOG((" digital_cycles     : %d\n", digital_cycles));
                DLOG((" digital_cycledelay : %d\n", digital_cycledelay));
                DLOG((" digital_variation  : %d\n", digital_variation));

                /*
                 * This was it, get ready for a sound to come out
                 */

                /*
                 * Check arguments for validity
                 */
                if (digital_freq <= 0) {
                    goto music_abort;
                }
                if (digital_duration <= 0) {
                    goto music_abort;
                }
                /*
                 * Max of three seconds per note
                 */
                if (digital_duration > 3000) {
                    goto music_abort;
                }
                if (digital_cycles <= 0) {
                    goto music_abort;
                }
                if (digital_cycledelay < 0) {
                    goto music_abort;
                }

                DLOG(("ANSI Music style 2: ARGS OK\n"));

                /*
                 * Convert this into a music structure...
                 */
                if (digital_cycles > 0) {
                    /*
                     * Put the information in p first
                     */
                    /*
                     * Frequency and duration
                     */
                    p->hertz = digital_freq;
                    p->duration = digital_duration;
                    /*
                     * Cycle delay
                     */
                    if (digital_cycledelay > 0) {
                        q = (struct q_music_struct *)
                            Xmalloc(sizeof(struct q_music_struct), __FILE__,
                                    __LINE__);
                        memset(q, 0, sizeof(struct q_music_struct));
                        p->next = q;
                        p = q;
                        p->hertz = 0;
                        p->duration = digital_cycledelay;
                    }
                    /*
                     * Variation
                     */
                    digital_freq += digital_variation;
                    digital_cycles--;
                }

                while (digital_cycles > 0) {
                    q = (struct q_music_struct *)
                        Xmalloc(sizeof(struct q_music_struct), __FILE__,
                                __LINE__);
                    memset(q, 0, sizeof(struct q_music_struct));
                    p->next = q;
                    p = q;

                    /*
                     * Frequency and duration
                     */
                    p->hertz = digital_freq;
                    p->duration = digital_duration;
                    /*
                     * Cycle delay
                     */
                    if (digital_cycledelay > 0) {
                        q = (struct q_music_struct *)
                            Xmalloc(sizeof(struct q_music_struct), __FILE__,
                                    __LINE__);
                        memset(q, 0, sizeof(struct q_music_struct));
                        p->next = q;
                        p = q;
                        p->hertz = 0;
                        p->duration = digital_cycledelay;
                    }
                    /*
                     * Variation
                     */
                    digital_freq += digital_variation;
                    digital_cycles--;
                }

                goto music_done;
            }
            break;

        } /* switch (state) */
    }

    /*
     * See if we've got one more to go
     */
    if (state == MUSIC_SOUND) {

        /*
         * Play the old note
         */
        p->hertz = frequency_table[note_octave][current_note];
        p->duration = duration_millis(tempo, note_length);
        p->duration *= note_length_multiplier;

        q = (struct q_music_struct *) Xmalloc(sizeof(struct q_music_struct),
                                              __FILE__, __LINE__);
        memset(q, 0, sizeof(struct q_music_struct));
        p->next = q;
        q->hertz = 0;
        q->duration = p->duration * (1 - style);
        p->duration *= style;
        p = q;
        q = (struct q_music_struct *) Xmalloc(sizeof(struct q_music_struct),
                                              __FILE__, __LINE__);
        memset(q, 0, sizeof(struct q_music_struct));
        p->next = q;
    }

music_done:

    /*
     * Play the sequence
     */
    play_music(&music, interruptible);

music_abort:

    /*
     * Cleanup memory
     */
    p = music.next;
    while (p != NULL) {
        q = p->next;
        Xfree(p, __FILE__, __LINE__);
        p = q;
    }

}

/**
 * Play the tones that correspond to one of the qodem music events.
 *
 * @param sequence the sequence to play
 */
void play_sequence(const Q_MUSIC_SEQUENCE sequence) {
    unsigned char music_sequence[OPTIONS_LINE_SIZE];

    if (q_status.sound == Q_FALSE) {
        return;
    }

    switch (sequence) {

    case Q_MUSIC_CONNECT:
        strncpy((char *) music_sequence, get_option(Q_OPTION_MUSIC_CONNECT),
                sizeof(music_sequence) - 1);
        music_sequence[sizeof(music_sequence) - 1] = 0;
        if (strcmp((char *) music_sequence, "none") != 0) {
            play_ansi_music(music_sequence, strlen((char *) music_sequence),
                            Q_TRUE);
        }
        break;

    case Q_MUSIC_CONNECT_MODEM:
        strncpy((char *) music_sequence,
                get_option(Q_OPTION_MUSIC_CONNECT_MODEM),
                sizeof(music_sequence) - 1);
        music_sequence[sizeof(music_sequence) - 1] = 0;
        if (strcmp((char *) music_sequence, "none") != 0) {
            play_ansi_music(music_sequence, strlen((char *) music_sequence),
                            Q_TRUE);
        }
        break;

    case Q_MUSIC_UPLOAD:
        strncpy((char *) music_sequence, get_option(Q_OPTION_MUSIC_UPLOAD),
                sizeof(music_sequence) - 1);
        music_sequence[sizeof(music_sequence) - 1] = 0;
        if (strcmp((char *) music_sequence, "none") != 0) {
            play_ansi_music(music_sequence, strlen((char *) music_sequence),
                            Q_TRUE);
        }
        break;

    case Q_MUSIC_DOWNLOAD:
        strncpy((char *) music_sequence, get_option(Q_OPTION_MUSIC_DOWNLOAD),
                sizeof(music_sequence) - 1);
        music_sequence[sizeof(music_sequence) - 1] = 0;
        if (strcmp((char *) music_sequence, "none") != 0) {
            play_ansi_music(music_sequence, strlen((char *) music_sequence),
                            Q_TRUE);
        }
        break;

    case Q_MUSIC_PAGE_SYSOP:
        strncpy((char *) music_sequence, get_option(Q_OPTION_MUSIC_PAGE_SYSOP),
                sizeof(music_sequence) - 1);
        music_sequence[sizeof(music_sequence) - 1] = 0;
        if (strcmp((char *) music_sequence, "none") != 0) {
            play_ansi_music(music_sequence, strlen((char *) music_sequence),
                            Q_FALSE);
        }
        break;

    }

}
