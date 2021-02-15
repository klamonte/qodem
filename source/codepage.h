/*
 * codepage.h
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

#ifndef __CODEPAGE_H__
#define __CODEPAGE_H__

/* Includes --------------------------------------------------------------- */

#include <stddef.h>             /* wchar_t */
#include <stdint.h>             /* uint32_t */
#include "qcurses.h"            /* attr_t */
#include "common.h"             /* Q_BOOL */

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

/* Control characters used in lots of places */
#define C_NUL   0x00
#define C_SOH   0x01
#define C_STX   0x02
#define C_EOT   0x04
#define C_ACK   0x06
#define C_BS    0x08
#define C_TAB   0x09
#define C_LF    0x0A
#define C_CR    0x0D
#define C_XON   0x11            /* DC1 */
#define C_XOFF  0x13            /* DC3 */
#define C_NAK   0x15
#define C_CAN   0x18
#define C_SUB   0x1A
#define C_ESC   0x1B

/* Globals ---------------------------------------------------------------- */

/* CP437 characters!  Yee! */

/*
 * I've been using the medium-density hatch forever, but I think it's
 * actually the low-density hatch that Qmodem used.
 */
/* #define HATCH                        0xB1 */

#define HATCH                           0xB0
#define DOUBLE_BAR                      0xCD
#define BOX                             0xFE
#define CHECK                           0xFB
#define TRIPLET                         0xF0
#define OMEGA                           0xEA
#define PI                              0xE3
#define UPARROW                         0x18
#define DOWNARROW                       0x19
#define RIGHTARROW                      0x1A
#define LEFTARROW                       0x1B
#define SINGLE_BAR                      0xC4
#define BACK_ARROWHEAD                  0x11
#define LRCORNER                        0xD9
#define DEGREE                          0xF8
#define PLUSMINUS                       0xF1

#define Q_WINDOW_TOP                    DOUBLE_BAR
#define Q_WINDOW_LEFT_TOP               0xD5
#define Q_WINDOW_RIGHT_TOP              0xB8
#define Q_WINDOW_SIDE                   0xB3
#define Q_WINDOW_LEFT_BOTTOM            0xD4
#define Q_WINDOW_RIGHT_BOTTOM           0xBE
#define Q_WINDOW_LEFT_TEE               0xC6
#define Q_WINDOW_RIGHT_TEE              0xB5
#define Q_WINDOW_LEFT_TOP_DOUBLESIDE    0xD6
#define Q_WINDOW_RIGHT_TOP_DOUBLESIDE   0xB7

/* VT52 uses "blank" in the docs, so I figured I'll leave a spot for it. */
#define BLANK                           0x20

/**
 * CP437 translation map.
 */
extern wchar_t cp437_chars[256];

/**
 * VT52 drawing characters.
 */
extern wchar_t vt52_special_graphics_chars[128];

/* DEC VT100/VT220 translation maps --------------------------------------- */

/**
 * US - Normal "international" (ASCII).
 */
extern wchar_t dec_us_chars[128];

/**
 * VT100 drawing characters.
 */
extern wchar_t dec_special_graphics_chars[128];

/**
 * Dec Supplemental (DEC multinational).
 */
extern wchar_t dec_supplemental_chars[128];

/**
 * UK.
 */
extern wchar_t dec_uk_chars[128];

/**
 * DUTCH.
 */
extern wchar_t dec_nl_chars[128];

/**
 * FINNISH.
 */
extern wchar_t dec_fi_chars[128];

/**
 * FRENCH.
 */
extern wchar_t dec_fr_chars[128];

/**
 * FRENCH_CA.
 */
extern wchar_t dec_fr_CA_chars[128];

/**
 * GERMAN.
 */
extern wchar_t dec_de_chars[128];

/**
 * ITALIAN.
 */
extern wchar_t dec_it_chars[128];

/**
 * NORWEGIAN.
 */
extern wchar_t dec_no_chars[128];

/**
 * SPANISH.
 */
extern wchar_t dec_es_chars[128];

/**
 * SWEDISH.
 */
extern wchar_t dec_sv_chars[128];

/**
 * SWISS.
 */
extern wchar_t dec_swiss_chars[128];

/**
 * Available codepages.  Note that Q_CODEPAGE_DEC is a special case for the
 * selectable character sets of the VT100 line.
 */
typedef enum Q_CODEPAGES {
    Q_CODEPAGE_CP437,           /* PC VGA */
    Q_CODEPAGE_ISO8859_1,       /* ISO-8859-1 */

    /*
     * DOS codepages
     */
    Q_CODEPAGE_CP720,           /* Arabic */
    Q_CODEPAGE_CP737,           /* Greek */
    Q_CODEPAGE_CP775,           /* Baltic Rim */
    Q_CODEPAGE_CP850,           /* Western European */
    Q_CODEPAGE_CP852,           /* Central European */
    Q_CODEPAGE_CP857,           /* Turkish */
    Q_CODEPAGE_CP858,           /* Western European with euro */
    Q_CODEPAGE_CP860,           /* Portuguese */
    Q_CODEPAGE_CP862,           /* Hebrew */
    Q_CODEPAGE_CP863,           /* Quebec French */
    Q_CODEPAGE_CP866,           /* Cyrillic */

    /*
     * Windows codepages
     */
    Q_CODEPAGE_CP1250,          /* Central/Eastern European */
    Q_CODEPAGE_CP1251,          /* Cyrillic */
    Q_CODEPAGE_CP1252,          /* Western European */

    /*
     * Other codepages
     */
    Q_CODEPAGE_KOI8_R,          /* Russian */
    Q_CODEPAGE_KOI8_U,          /* Ukrainian */

    /*
     * Non-selectable codepages in the phonebook picklist.
     */
    Q_CODEPAGE_DEC,             /* DEC character sets for VT10x/VT220 */
    Q_CODEPAGE_PETSCII,         /* PETSCII, treated like DEC */
    Q_CODEPAGE_ATASCII,         /* ATASCII, treated like DEC */

} Q_CODEPAGE;
#define Q_CODEPAGE_PHONEBOOK_MAX (Q_CODEPAGE_KOI8_U + 1)
#define Q_CODEPAGE_MAX (Q_CODEPAGE_PETSCII + 1)

#define UTF8_ACCEPT 0
#define UTF8_REJECT 1

/* Functions -------------------------------------------------------------- */

/**
 * Encode a single Unicode code point to a buffer.
 *
 * @param ch the code point to encode.
 * @param utf8_buffer the buffer to encode to.  There must be at least 4
 * bytes available.
 * @return the number of encoded bytes.
 */
extern int utf8_encode(const wchar_t ch, char * utf8_buffer);

/**
 * Decode a UTF-8 encoded byte into a code point.
 *
 * @param state the current decoder state.
 * @param codep if the return is UTF8_ACCEPT, then codep contains a decoded
 * code point.
 * @param byte the byte to read.
 * @return if decoding was complete, then codep will have a code point and
 * this function returns UTF8_ACCEPT.  If decoding is incomplete, or there is
 * an error, then codep is undefined and this function returns UTF8_REJECT.
 */
extern uint32_t utf8_decode(uint32_t * state, uint32_t * codep, uint32_t byte);

/**
 * Convert a Q_CODEPAGE enum into a printable string.
 *
 * @param codepage the Q_CODEPAGE value.
 * @return the printable string.
 */
extern char * codepage_string(Q_CODEPAGE codepage);

/**
 * Convert a printable string into a Q_CODEPAGE enum.
 *
 * @param string the printable string, case insensitive.
 * @return the Q_CODEPAGE value.
 */
extern Q_CODEPAGE codepage_from_string(const char * string);

/**
 * Map a character in q_current_codepage's character set to its equivalent
 * Unicode code point / glyph.
 *
 * @param ch the 8-bit character in one of the 8-bit codepages.
 * @return the Unicode code point.
 */
extern wchar_t codepage_map_char(const unsigned char ch);

/**
 * Map a Unicode code point / glyph to a byte in a codepage.
 *
 * @param ch the Unicode code point.
 * @param codepage the codepage to look through.
 * @param success if true, the reverse mapping worked.
 * @return the 8-bit character in one of the 8-bit codepages.
 */
extern wchar_t codepage_unmap_byte(const wchar_t ch, const Q_CODEPAGE codepage,
                                   Q_BOOL * success);

/**
 * Keyboard handler for the codepage selection dialog.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void codepage_keyboard_handler(const int keystroke, const int flags);

/**
 * Draw screen for the codepage selection dialog.
 */
extern void codepage_refresh();

#ifdef __cplusplus
}
#endif

#endif /* __CODEPAGE_H__ */
