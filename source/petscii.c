/*
 * petscii.c
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

#include "common.h"
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "qodem.h"
#include "screen.h"
#include "scrollback.h"
#include "options.h"
#include "ansi.h"
#include "petscii.h"

/* Set this to a not-NULL value to enable debug log. */
/* static const char * DLOGNAME = "petscii"; */
static const char * DLOGNAME = NULL;

/**
 * Scan states for the parser state machine.
 */
typedef enum SCAN_STATES {
    SCAN_NONE,
    SCAN_ESC,
    SCAN_CSI,
    SCAN_CSI_PARAM,
    SCAN_ANSI_FALLBACK,
    DUMP_UNKNOWN_SEQUENCE
} SCAN_STATE;

/* Current scanning state. */
static SCAN_STATE scan_state;

/**
 * State change flags for the Commodore keyboard/screen.
 */
struct commodore_state {
    /**
     * If true, the system is in uppercase / graphics mode.
     */
    Q_BOOL uppercase;

    /**
     * If true, reverse video is enabled.
     */
    Q_BOOL reverse;
};

/**
 * The current keyboard/screen state.
 */
static struct commodore_state state = {
    Q_FALSE,
    Q_FALSE
};

/**
 * ANSI fallback: the unknown escape sequence is copied here and then run
 * through the ANSI emulator.
 */
static unsigned char ansi_buffer[sizeof(q_emul_buffer)];
static int ansi_buffer_n;
static int ansi_buffer_i;

/**
 * The C64/128 characters in uppercase / graphics mode, no reverse.
 */
static wchar_t c64_uppercase_normal_chars[] = {
    /* Non-printable C0 set */
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    /* Private use area matching C64 Pro Mono STYLE font */
    0x0020, 0xE021, 0xE022, 0xE023, 0xE024, 0xE025, 0xE026, 0xE027,
    0xE028, 0xE029, 0xE02A, 0xE02B, 0xE02C, 0xE02D, 0xE02E, 0xE02F,
    0xE030, 0xE031, 0xE032, 0xE033, 0xE034, 0xE035, 0xE036, 0xE037,
    0xE038, 0xE039, 0xE03A, 0xE03B, 0xE03C, 0xE03D, 0xE03E, 0xE03F,
    0xE040, 0xE041, 0xE042, 0xE043, 0xE044, 0xE045, 0xE046, 0xE047,
    0xE048, 0xE049, 0xE04A, 0xE04B, 0xE04C, 0xE04D, 0xE04E, 0xE04F,
    0xE050, 0xE051, 0xE052, 0xE053, 0xE054, 0xE055, 0xE056, 0xE057,
    0xE058, 0xE059, 0xE05A, 0xE05B, 0xE05C, 0xE05D, 0xE05E, 0xE05F,
    0xE060, 0xE061, 0xE062, 0xE063, 0xE064, 0xE065, 0xE066, 0xE067,
    0xE068, 0xE069, 0xE06A, 0xE06B, 0xE06C, 0xE06D, 0xE06E, 0xE06F,
    0xE070, 0xE071, 0xE072, 0xE073, 0xE074, 0xE075, 0xE076, 0xE077,
    0xE078, 0xE079, 0xE07A, 0xE07B, 0xE07C, 0xE07D, 0xE07E, 0xE07F,
    /* Non-printable C1 set */
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    /* Private use area matching C64 Pro Mono STYLE font */
    0x0020, 0xE0A1, 0xE0A2, 0xE0A3, 0xE0A4, 0xE0A5, 0xE0A6, 0xE0A7,
    0xE0A8, 0xE0A9, 0xE0AA, 0xE0AB, 0xE0AC, 0xE0AD, 0xE0AE, 0xE0AF,
    0xE0B0, 0xE0B1, 0xE0B2, 0xE0B3, 0xE0B4, 0xE0B5, 0xE0B6, 0xE0B7,
    0xE0B8, 0xE0B9, 0xE0BA, 0xE0BB, 0xE0BC, 0xE0BD, 0xE0BE, 0xE0BF,
    0xE0C0, 0xE0C1, 0xE0C2, 0xE0C3, 0xE0C4, 0xE0C5, 0xE0C6, 0xE0C7,
    0xE0C8, 0xE0C9, 0xE0CA, 0xE0CB, 0xE0CC, 0xE0CD, 0xE0CE, 0xE0CF,
    0xE0D0, 0xE0D1, 0xE0D2, 0xE0D3, 0xE0D4, 0xE0D5, 0xE0D6, 0xE0D7,
    0xE0D8, 0xE0D9, 0xE0DA, 0xE0DB, 0xE0DC, 0xE0DD, 0xE0DE, 0xE0DF,
    0xE0E0, 0xE0E1, 0xE0E2, 0xE0E3, 0xE0E4, 0xE0E5, 0xE0E6, 0xE0E7,
    0xE0E8, 0xE0E9, 0xE0EA, 0xE0EB, 0xE0EC, 0xE0ED, 0xE0EE, 0xE0EF,
    0xE0F0, 0xE0F1, 0xE0F2, 0xE0F3, 0xE0F4, 0xE0F5, 0xE0F6, 0xE0F7,
    0xE0F8, 0xE0F9, 0xE0FA, 0xE0FB, 0xE0FC, 0xE0FD, 0xE0FE, 0xE0FF
};

/**
 * The C64/128 characters in uppercase / graphics mode, reverse on.
 */
static wchar_t c64_uppercase_reverse_chars[] = {
    /* Non-printable C0 set */
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    /* Private use area matching C64 Pro Mono STYLE font */
    0xE220, 0xE221, 0xE222, 0xE223, 0xE224, 0xE225, 0xE226, 0xE227,
    0xE228, 0xE229, 0xE22A, 0xE22B, 0xE22C, 0xE22D, 0xE22E, 0xE22F,
    0xE230, 0xE231, 0xE232, 0xE233, 0xE234, 0xE235, 0xE236, 0xE237,
    0xE238, 0xE239, 0xE23A, 0xE23B, 0xE23C, 0xE23D, 0xE23E, 0xE23F,
    0xE240, 0xE241, 0xE242, 0xE243, 0xE244, 0xE245, 0xE246, 0xE247,
    0xE248, 0xE249, 0xE24A, 0xE24B, 0xE24C, 0xE24D, 0xE24E, 0xE24F,
    0xE250, 0xE251, 0xE252, 0xE253, 0xE254, 0xE255, 0xE256, 0xE257,
    0xE258, 0xE259, 0xE25A, 0xE25B, 0xE25C, 0xE25D, 0xE25E, 0xE25F,
    0xE260, 0xE261, 0xE262, 0xE263, 0xE264, 0xE265, 0xE266, 0xE267,
    0xE268, 0xE269, 0xE26A, 0xE26B, 0xE26C, 0xE26D, 0xE26E, 0xE26F,
    0xE270, 0xE271, 0xE272, 0xE273, 0xE274, 0xE275, 0xE276, 0xE277,
    0xE278, 0xE279, 0xE27A, 0xE27B, 0xE27C, 0xE27D, 0xE27E, 0xE27F,
    /* Non-printable C1 set */
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    /* Private use area matching C64 Pro Mono STYLE font */
    0xE2A0, 0xE2A1, 0xE2A2, 0xE2A3, 0xE2A4, 0xE2A5, 0xE2A6, 0xE2A7,
    0xE2A8, 0xE2A9, 0xE2AA, 0xE2AB, 0xE2AC, 0xE2AD, 0xE2AE, 0xE2AF,
    0xE2B0, 0xE2B1, 0xE2B2, 0xE2B3, 0xE2B4, 0xE2B5, 0xE2B6, 0xE2B7,
    0xE2B8, 0xE2B9, 0xE2BA, 0xE2BB, 0xE2BC, 0xE2BD, 0xE2BE, 0xE2BF,
    0xE2C0, 0xE2C1, 0xE2C2, 0xE2C3, 0xE2C4, 0xE2C5, 0xE2C6, 0xE2C7,
    0xE2C8, 0xE2C9, 0xE2CA, 0xE2CB, 0xE2CC, 0xE2CD, 0xE2CE, 0xE2CF,
    0xE2D0, 0xE2D1, 0xE2D2, 0xE2D3, 0xE2D4, 0xE2D5, 0xE2D6, 0xE2D7,
    0xE2D8, 0xE2D9, 0xE2DA, 0xE2DB, 0xE2DC, 0xE2DD, 0xE2DE, 0xE2DF,
    0xE2E0, 0xE2E1, 0xE2E2, 0xE2E3, 0xE2E4, 0xE2E5, 0xE2E6, 0xE2E7,
    0xE2E8, 0xE2E9, 0xE2EA, 0xE2EB, 0xE2EC, 0xE2ED, 0xE2EE, 0xE2EF,
    0xE2F0, 0xE2F1, 0xE2F2, 0xE2F3, 0xE2F4, 0xE2F5, 0xE2F6, 0xE2F7,
    0xE2F8, 0xE2F9, 0xE2FA, 0xE2FB, 0xE2FC, 0xE2FD, 0xE2FE, 0xE2FF
};

/**
 * The C64/128 characters in lowercase mode, no reverse.
 */
static wchar_t c64_lowercase_normal_chars[] = {
    /* Non-printable C0 set */
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    /* Private use area matching C64 Pro Mono STYLE font */
    0x0020, 0xE121, 0xE122, 0xE123, 0xE124, 0xE125, 0xE126, 0xE127,
    0xE128, 0xE129, 0xE12A, 0xE12B, 0xE12C, 0xE12D, 0xE12E, 0xE12F,
    0xE130, 0xE131, 0xE132, 0xE133, 0xE134, 0xE135, 0xE136, 0xE137,
    0xE138, 0xE139, 0xE13A, 0xE13B, 0xE13C, 0xE13D, 0xE13E, 0xE13F,
    0xE140, 0xE141, 0xE142, 0xE143, 0xE144, 0xE145, 0xE146, 0xE147,
    0xE148, 0xE149, 0xE14A, 0xE14B, 0xE14C, 0xE14D, 0xE14E, 0xE14F,
    0xE150, 0xE151, 0xE152, 0xE153, 0xE154, 0xE155, 0xE156, 0xE157,
    0xE158, 0xE159, 0xE15A, 0xE15B, 0xE15C, 0xE15D, 0xE15E, 0xE15F,
    0xE160, 0xE161, 0xE162, 0xE163, 0xE164, 0xE165, 0xE166, 0xE167,
    0xE168, 0xE169, 0xE16A, 0xE16B, 0xE16C, 0xE16D, 0xE16E, 0xE16F,
    0xE170, 0xE171, 0xE172, 0xE173, 0xE174, 0xE175, 0xE176, 0xE177,
    0xE178, 0xE179, 0xE17A, 0xE17B, 0xE17C, 0xE17D, 0xE17E, 0xE17F,
    /* Non-printable C1 set */
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    /* Private use area matching C64 Pro Mono STYLE font */
    0x0020, 0xE1A1, 0xE1A2, 0xE1A3, 0xE1A4, 0xE1A5, 0xE1A6, 0xE1A7,
    0xE1A8, 0xE1A9, 0xE1AA, 0xE1AB, 0xE1AC, 0xE1AD, 0xE1AE, 0xE1AF,
    0xE1B0, 0xE1B1, 0xE1B2, 0xE1B3, 0xE1B4, 0xE1B5, 0xE1B6, 0xE1B7,
    0xE1B8, 0xE1B9, 0xE1BA, 0xE1BB, 0xE1BC, 0xE1BD, 0xE1BE, 0xE1BF,
    0xE1C0, 0xE1C1, 0xE1C2, 0xE1C3, 0xE1C4, 0xE1C5, 0xE1C6, 0xE1C7,
    0xE1C8, 0xE1C9, 0xE1CA, 0xE1CB, 0xE1CC, 0xE1CD, 0xE1CE, 0xE1CF,
    0xE1D0, 0xE1D1, 0xE1D2, 0xE1D3, 0xE1D4, 0xE1D5, 0xE1D6, 0xE1D7,
    0xE1D8, 0xE1D9, 0xE1DA, 0xE1DB, 0xE1DC, 0xE1DD, 0xE1DE, 0xE1DF,
    0xE1E0, 0xE1E1, 0xE1E2, 0xE1E3, 0xE1E4, 0xE1E5, 0xE1E6, 0xE1E7,
    0xE1E8, 0xE1E9, 0xE1EA, 0xE1EB, 0xE1EC, 0xE1ED, 0xE1EE, 0xE1EF,
    0xE1F0, 0xE1F1, 0xE1F2, 0xE1F3, 0xE1F4, 0xE1F5, 0xE1F6, 0xE1F7,
    0xE1F8, 0xE1F9, 0xE1FA, 0xE1FB, 0xE1FC, 0xE1FD, 0xE1FE, 0xE1FF
};

/**
 * The C64/128 characters in lowercase mode, reverse on.
 */
static wchar_t c64_lowercase_reverse_chars[] = {
    /* Non-printable C0 set */
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    /* Private use area matching C64 Pro Mono STYLE font */
    0xE320, 0xE321, 0xE322, 0xE323, 0xE324, 0xE325, 0xE326, 0xE327,
    0xE328, 0xE329, 0xE32A, 0xE32B, 0xE32C, 0xE32D, 0xE32E, 0xE32F,
    0xE330, 0xE331, 0xE332, 0xE333, 0xE334, 0xE335, 0xE336, 0xE337,
    0xE338, 0xE339, 0xE33A, 0xE33B, 0xE33C, 0xE33D, 0xE33E, 0xE33F,
    0xE340, 0xE341, 0xE342, 0xE343, 0xE344, 0xE345, 0xE346, 0xE347,
    0xE348, 0xE349, 0xE34A, 0xE34B, 0xE34C, 0xE34D, 0xE34E, 0xE34F,
    0xE350, 0xE351, 0xE352, 0xE353, 0xE354, 0xE355, 0xE356, 0xE357,
    0xE358, 0xE359, 0xE35A, 0xE35B, 0xE35C, 0xE35D, 0xE35E, 0xE35F,
    0xE360, 0xE361, 0xE362, 0xE363, 0xE364, 0xE365, 0xE366, 0xE367,
    0xE368, 0xE369, 0xE36A, 0xE36B, 0xE36C, 0xE36D, 0xE36E, 0xE36F,
    0xE370, 0xE371, 0xE372, 0xE373, 0xE374, 0xE375, 0xE376, 0xE377,
    0xE378, 0xE379, 0xE37A, 0xE37B, 0xE37C, 0xE37D, 0xE37E, 0xE37F,
    /* Non-printable C1 set */
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    /* Private use area matching C64 Pro Mono STYLE font */
    0xE3A0, 0xE3A1, 0xE3A2, 0xE3A3, 0xE3A4, 0xE3A5, 0xE3A6, 0xE3A7,
    0xE3A8, 0xE3A9, 0xE3AA, 0xE3AB, 0xE3AC, 0xE3AD, 0xE3AE, 0xE3AF,
    0xE3B0, 0xE3B1, 0xE3B2, 0xE3B3, 0xE3B4, 0xE3B5, 0xE3B6, 0xE3B7,
    0xE3B8, 0xE3B9, 0xE3BA, 0xE3BB, 0xE3BC, 0xE3BD, 0xE3BE, 0xE3BF,
    0xE3C0, 0xE3C1, 0xE3C2, 0xE3C3, 0xE3C4, 0xE3C5, 0xE3C6, 0xE3C7,
    0xE3C8, 0xE3C9, 0xE3CA, 0xE3CB, 0xE3CC, 0xE3CD, 0xE3CE, 0xE3CF,
    0xE3D0, 0xE3D1, 0xE3D2, 0xE3D3, 0xE3D4, 0xE3D5, 0xE3D6, 0xE3D7,
    0xE3D8, 0xE3D9, 0xE3DA, 0xE3DB, 0xE3DC, 0xE3DD, 0xE3DE, 0xE3DF,
    0xE3E0, 0xE3E1, 0xE3E2, 0xE3E3, 0xE3E4, 0xE3E5, 0xE3E6, 0xE3E7,
    0xE3E8, 0xE3E9, 0xE3EA, 0xE3EB, 0xE3EC, 0xE3ED, 0xE3EE, 0xE3EF,
    0xE3F0, 0xE3F1, 0xE3F2, 0xE3F3, 0xE3F4, 0xE3F5, 0xE3F6, 0xE3F7,
    0xE3F8, 0xE3F9, 0xE3FA, 0xE3FB, 0xE3FC, 0xE3FD, 0xE3FE, 0xE3FF
};

/**
 * The C64/128 characters in uppercase / graphics mode, mapped to Unicode.
 */
static wchar_t c64_uppercase_unicode_chars[] = {
    /* Non-printable C0 set */
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    /* Printable 7-bit ASCII */
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
    0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005A, 0x005B, 0x00A3, 0x005D, 0x2191, 0x2190,
    0x2500, 0x2660, 0x2502, 0x2500, 0xE064, 0xE065, 0xE066, 0xE067,
    0xE068, 0x256E, 0x2570, 0x256F, 0xE06C, 0x2572, 0x2571, 0xE06F,
    0xE070, 0x2022, 0xE072, 0x2665, 0xE074, 0x256D, 0x2573, 0x25CB,
    0x2663, 0xE079, 0x2666, 0x253C, 0xE07C, 0x2502, 0x03C0, 0x25E5,
    /* Non-printable C1 set */
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    /* Printable 8-bit non-ASCII */
    0x0020, 0x258C, 0x2584, 0x2594, 0x2581, 0x258E, 0x2592, 0xE0A7,
    0xE0A8, 0x25E4, 0xE0AA, 0x251C, 0x2597, 0x2514, 0x2510, 0x2582,
    0x250C, 0x2534, 0x252C, 0x2524, 0x258E, 0x258D, 0xE0B6, 0xE0B7,
    0xE0B8, 0x2583, 0xE0BA, 0x2596, 0x259C, 0x2518, 0x2598, 0x259A,
    0x2500, 0x2660, 0x2502, 0x2500, 0xE0C4, 0xE0C5, 0xE0C6, 0xE0C7,
    0xE0C8, 0x256E, 0x2570, 0x256F, 0xE0CC, 0x2572, 0x2571, 0xE0CF,
    0xE0D0, 0x2022, 0xE0D2, 0x2665, 0xE0D4, 0x256D, 0x2573, 0x25CB,
    0x2663, 0xE0D9, 0x2666, 0x253C, 0xE0DC, 0x2502, 0x03C0, 0x25E5,
    0x0020, 0x258C, 0x2584, 0x2594, 0x2581, 0x258E, 0x2592, 0xE0E7,
    0xE0E8, 0x25E4, 0xE0EA, 0x251C, 0x2597, 0x2514, 0x2510, 0x2582,
    0x250C, 0x2534, 0x252C, 0x2524, 0x258E, 0x258D, 0xE0F6, 0xE0F7,
    0xE0F8, 0x2583, 0xE0FA, 0x2596, 0x259C, 0x2518, 0x2598, 0x03C0
};

/**
 * The C64/128 characters in lowercase mode, mapped to Unicode.
 */
static wchar_t c64_lowercase_unicode_chars[] = {
    /* Non-printable C0 set */
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    /* Printable 7-bit ASCII */
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
    0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
    0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
    0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
    0x0078, 0x0079, 0x007A, 0x005B, 0x00A3, 0x005D, 0x2191, 0x2190,
    0x2500, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005A, 0x253C, 0xE17C, 0x2502, 0xE17E, 0xE17F,
    /* Non-printable C1 set */
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    /* Printable 8-bit non-ASCII */
    0x0020, 0x258C, 0x2584, 0x2594, 0x2581, 0x258E, 0x2592, 0xE1A7,
    0xE1A8, 0xE1A9, 0xE1AA, 0x251C, 0x2597, 0x2514, 0x2510, 0x2582,
    0x250C, 0x2534, 0x252C, 0x2524, 0x258E, 0x258D, 0xE1B6, 0xE1B7,
    0xE1B8, 0x2583, 0x2713, 0x2596, 0x259C, 0x2518, 0x2598, 0x259A,
    0x2500, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005A, 0x253C, 0xE17C, 0x2502, 0xE17E, 0xE17F,
    0x2663, 0xE179, 0x2666, 0x253C, 0xE17C, 0x2502, 0xE1DE, 0xE1DF,
    0x0020, 0x258C, 0x2584, 0x2594, 0x2581, 0x258E, 0x2592, 0xE1E7,
    0xE1E8, 0xE1E9, 0xE1EA, 0x251C, 0x2597, 0x2514, 0x2510, 0x2582,
    0x250C, 0x2534, 0x252C, 0x2524, 0x258E, 0x258D, 0xE1F6, 0xE1F7,
    0xE1F8, 0x2583, 0x2713, 0x2596, 0x259C, 0x2518, 0x2598, 0xE1FF
};

/**
 * Reset the emulation state.
 */
void petscii_reset() {
    DLOG(("petscii_reset()\n"));

    scan_state = SCAN_NONE;
    state.uppercase = Q_FALSE;
    state.reverse = Q_FALSE;
}

/**
 * Reset the scan state for a new sequence.
 *
 * @param to_screen one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
static void clear_state(wchar_t * to_screen) {
    q_status.insert_mode = Q_FALSE;
    q_emul_buffer_n = 0;
    q_emul_buffer_i = 0;
    memset(q_emul_buffer, 0, sizeof(q_emul_buffer));
    scan_state = SCAN_NONE;
    *to_screen = 1;
}

/**
 * Hang onto one character in the buffer.
 *
 * @param keep_char the character to save into q_emul_buffer
 * @param to_screen one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
static void save_char(unsigned char keep_char, wchar_t * to_screen) {
    q_emul_buffer[q_emul_buffer_n] = keep_char;
    q_emul_buffer_n++;
    *to_screen = 1;
}

/**
 * Process a control character.
 *
 * @param control_char a byte in the C0 or C1 range.
 */
static void petscii_handle_control_char(const unsigned char control_char) {
    short foreground, background;
    short curses_color;
    attr_t attributes = q_current_color & NO_COLOR_MASK;

    /*
     * Pull the current foreground and background.
     */
    curses_color = color_from_attr(q_current_color);
    foreground = (curses_color & 0x38) >> 3;
    background = curses_color & 0x07;

    switch (control_char) {
    case 0x05:
        /*
         * Change color to white.
         */
        foreground = Q_COLOR_WHITE;
        attributes |= Q_A_BOLD;
        break;

    case 0x07:
        if (q_status.petscii_is_c64 == Q_FALSE) {
            /*
             * C128: BEL
             */
            screen_beep();
        }
        break;

    case 0x08:
        if (q_status.petscii_is_c64 == Q_TRUE) {
            /*
             * Lock case.  TODO.
             */
        }
        break;

    case 0x09:
        if (q_status.petscii_is_c64 == Q_TRUE) {
            /*
             * C64: Unlock case.  TODO.
             */

        } else {
            /*
             * C128: Tab.
             */
            while (q_status.cursor_x < 80) {
                print_character(' ');
                if (q_status.cursor_x % 8 == 0) {
                    break;
                }
            }
        }
        break;

    case 0x0A:
        if (q_status.petscii_is_c64 == Q_FALSE) {
            /*
             * C128: Linefeed.
             */
            cursor_linefeed(Q_FALSE);
        }
        break;

    case 0x0B:
        if (q_status.petscii_is_c64 == Q_FALSE) {
            /*
             * C128: Lock case.  TODO.
             */
        }
        break;

    case 0x0C:
        if (q_status.petscii_is_c64 == Q_FALSE) {
            /*
             * C128: Unlock case.  TODO.
             */
        }
        break;

    case 0x0D:
        /*
         * Carriage return + linefeed.
         */
        cursor_linefeed(Q_TRUE);
        break;

    case 0x0E:
        /*
         * Switch to lowercase.  The real C64/128 changes every visible
         * character.  Qodem will not do so, it will only change newly
         * incoming characters.
         */
        state.uppercase = Q_FALSE;
        break;

    case 0x11:
        /*
         * Cursor down.  Do it like a linefeed though, so that the screen
         * scrolls.
         */
        cursor_linefeed(Q_FALSE);
        break;

    case 0x12:
        /*
         * Reverse on.
         */
        state.reverse = Q_TRUE;
        break;

    case 0x13:
        /*
         * Home cursor.  This does not clear the screen.
         */
        cursor_position(0, 0);
        break;

    case 0x14:
        /*
         * Delete.
         */
        delete_character(1);
        break;

    case 0x18:
        if (q_status.petscii_is_c64 == Q_FALSE) {
            /*
             * C128: Tab set/clear.  TODO.
             */
        }
        break;

    case 0x1B:
        if (q_status.petscii_is_c64 == Q_FALSE) {
            /*
             * C16/C128: ESC.
             *
             * According to Compute's "Programming the Commodore 64: The
             * Definitive Guide" on page 159:
             *
             *   An especially useful combination is CTRL-[ (open bracket, or
             *   SHIFTed :), which is CHR$(27).  This is a special printer
             *   code, called ESCape, which triggers features like underline,
             *   double strike, and emphasized.
             *
             * So...what do I do here?
             */
        }
        break;

    case 0x1C:
        /*
         * Red.
         */
        foreground = Q_COLOR_RED;
        break;

    case 0x1D:
        /*
         * Cursor right.
         */
        if (q_status.cursor_x == 39) {
            /* Newline, including scrolling the screen */
            cursor_linefeed(Q_TRUE);
        } else {
            cursor_right(1, Q_FALSE);
        }
        break;

    case 0x1E:
        /*
         * Green.
         */
        foreground = Q_COLOR_GREEN;
        break;

    case 0x1F:
        /*
         * Blue.
         */
        foreground = Q_COLOR_BLUE;
        break;

    case 0x81:
        /*
         * Orange.  Can't quite match it, so try bold red.
         */
        foreground = Q_COLOR_RED;
        attributes |= Q_A_BOLD;
        break;

    case 0x85:
        /*
         * F1.  Ignore.
         */
        break;

    case 0x86:
        /*
         * F3.  Ignore.
         */
        break;

    case 0x87:
        /*
         * F5.  Ignore.
         */
        break;

    case 0x88:
        /*
         * F7.  Ignore.
         */
        break;

    case 0x89:
        /*
         * F2.  Ignore.
         */
        break;

    case 0x8A:
        /*
         * F4.  Ignore.
         */
        break;

    case 0x8B:
        /*
         * F6.  Ignore.
         */
        break;

    case 0x8C:
        /*
         * F8.  Ignore.
         */
        break;

    case 0x8D:
        /*
         * Shift-Return.  This is supposed to move to the next line in BASIC
         * but not execute it.  For now, do nothing.  We will see if BBSes
         * use it later.
         */
        break;

    case 0x8E:
        /*
         * Uppercase on.
         */
        state.uppercase = Q_TRUE;
        break;

    case 0x90:
        /*
         * Black.
         */
        foreground = Q_COLOR_BLACK;
        break;

    case 0x91:
        /*
         * Cursor up.
         */
        cursor_up(1, Q_FALSE);
        break;

    case 0x92:
        /*
         * Reverse off.
         */
        state.reverse = Q_FALSE;
        break;

    case 0x93:
        /*
         * Clear.  Erase screen and home cursor.
         */
        erase_screen(0, 0, HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1, Q_FALSE);
        cursor_position(0, 0);
        break;

    case 0x94:
        /*
         * INST: insert.
         */
        insert_blanks(1);
        break;

    case 0x95:
        /*
         * Brown.
         */
        foreground = Q_COLOR_YELLOW;
        attributes &= ~Q_A_BOLD;
        break;

    case 0x96:
        /*
         * Pink.  Try bold magenta.
         */
        foreground = Q_COLOR_MAGENTA;
        attributes |= Q_A_BOLD;
        break;

    case 0x97:
        /*
         * Dark grey.  Try bold black.
         */
        foreground = Q_COLOR_BLACK;
        attributes |= Q_A_BOLD;
        break;

    case 0x98:
        /*
         * Medium grey.
         */
        foreground = Q_COLOR_WHITE;
        attributes &= ~Q_A_BOLD;
        break;

    case 0x99:
        /*
         * Light green.
         */
        foreground = Q_COLOR_GREEN;
        attributes |= Q_A_BOLD;
        break;

    case 0x9A:
        /*
         * Light blue.
         */
        foreground = Q_COLOR_BLUE;
        attributes |= Q_A_BOLD;
        break;

    case 0x9B:
        /*
         * Light grey.  This is the same as medium grey.
         */
        foreground = Q_COLOR_WHITE;
        attributes &= ~Q_A_BOLD;
        break;

    case 0x9C:
        /*
         * Purple.
         */
        foreground = Q_COLOR_MAGENTA;
        attributes &= ~Q_A_BOLD;
        break;

    case 0x9D:
        /*
         * Cursor left.
         */
        if ((q_status.cursor_x == 0) && (q_status.cursor_y > 0)) {
            /* Go to the previous line, last column */
            cursor_position(q_status.cursor_y - 1, 39);
        } else {
            cursor_left(1, Q_FALSE);
        }
        break;

    case 0x9E:
        /*
         * Yellow.
         */
        foreground = Q_COLOR_YELLOW;
        attributes |= Q_A_BOLD;
        break;

    case 0x9F:
        /*
         * Cyan.
         */
        foreground = Q_COLOR_CYAN;
        attributes &= ~Q_A_BOLD;
        break;

    }

    /* Change to whatever attribute was selected. */
    curses_color = (foreground << 3) | background;
    attributes |= color_to_attr(curses_color);
    q_current_color = attributes;

}

/**
 * Push one byte through the PETSCII emulator.
 *
 * @param from_modem one byte from the remote side.
 * @param to_screen if the return is Q_EMUL_FSM_ONE_CHAR or
 * Q_EMUL_FSM_MANY_CHARS, then to_screen will have a character to display on
 * the screen.
 * @return one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
Q_EMULATION_STATUS petscii(const unsigned char from_modem,
                           wchar_t * to_screen) {

    static unsigned char * count;
    static attr_t attributes;
    Q_EMULATION_STATUS rc;

    DLOG(("STATE: %d CHAR: 0x%02x '%c'\n", scan_state, from_modem, from_modem));

    if (q_status.petscii_has_wide_font == Q_FALSE) {
        /*
         * We don't think our font is double-width, so ask xterm/X11 to make
         * it bigger for us.
         */
        set_double_width(Q_TRUE);
    }

petscii_start:

    switch (scan_state) {

    /* ANSI Fallback ------------------------------------------------------- */

    case SCAN_ANSI_FALLBACK:

        /*
         * From here on out we pass through ANSI until we don't get
         * Q_EMUL_FSM_NO_CHAR_YET.
         */

        DLOG(("ANSI FALLBACK ansi_buffer_i %d ansi_buffer_n %d\n",
                ansi_buffer_i, ansi_buffer_n));
        DLOG(("              q_emul_buffer_i %d q_emul_buffer_n %d\n",
                q_emul_buffer_i, q_emul_buffer_n));

        if (ansi_buffer_n == 0) {
            assert(ansi_buffer_i == 0);
            /*
             * We have already cleared the old buffer, now push one byte at a
             * time through ansi until it is finished with its state machine.
             */
            ansi_buffer[ansi_buffer_n] = from_modem;
            ansi_buffer_n++;
        }

        DLOG(("ANSI FALLBACK ansi()\n"));

        rc = Q_EMUL_FSM_NO_CHAR_YET;
        while (rc == Q_EMUL_FSM_NO_CHAR_YET) {
            rc = ansi(ansi_buffer[ansi_buffer_i], to_screen);

            DLOG(("ANSI FALLBACK ansi() RC %d\n", rc));

            if (rc != Q_EMUL_FSM_NO_CHAR_YET) {
                /*
                 * We can be ourselves again now.
                 */
                DLOG(("ANSI FALLBACK END\n"));
                scan_state = SCAN_NONE;
            }

            ansi_buffer_i++;
            if (ansi_buffer_i == ansi_buffer_n) {
                /*
                 * No more characters to send through ANSI.
                 */
                ansi_buffer_n = 0;
                ansi_buffer_i = 0;
                break;
            }
        }

        if (rc == Q_EMUL_FSM_MANY_CHARS) {
            /*
             * ANSI is dumping q_emul_buffer.  Finish the job.
             */
            scan_state = DUMP_UNKNOWN_SEQUENCE;
        }

        return rc;

    case DUMP_UNKNOWN_SEQUENCE:

        DLOG(("DUMP_UNKNOWN_SEQUENCE q_emul_buffer_i %d q_emul_buffer_n %d\n",
                q_emul_buffer_i, q_emul_buffer_n));

        /*
         * Dump the string in q_emul_buffer
         */
        assert(q_emul_buffer_n > 0);

        *to_screen = codepage_map_char(q_emul_buffer[q_emul_buffer_i]);
        q_emul_buffer_i++;
        if (q_emul_buffer_i == q_emul_buffer_n) {
            /*
             * This was the last character.
             */
            q_emul_buffer_n = 0;
            q_emul_buffer_i = 0;
            memset(q_emul_buffer, 0, sizeof(q_emul_buffer));
            scan_state = SCAN_NONE;
            return Q_EMUL_FSM_ONE_CHAR;

        } else {
            return Q_EMUL_FSM_MANY_CHARS;
        }

    case SCAN_ESC:
        save_char(from_modem, to_screen);

        if (from_modem == '[') {
            if (q_status.petscii_color == Q_TRUE) {
                /*
                 * Fall into SCAN_CSI only if PETSCII_COLOR is enabled.
                 */
                scan_state = SCAN_CSI;
                return Q_EMUL_FSM_NO_CHAR_YET;
            }
        }

        /*
         * Fall-through to ANSI fallback.
         */
        break;

    case SCAN_CSI:
        save_char(from_modem, to_screen);

        /*
         * We are only going to support CSI Pn [ ; Pn ... ] m a.k.a. ANSI
         * Select Graphics Rendition.  We can see only a digit or 'm'.
         */
        if (q_isdigit(from_modem)) {
            /*
             * Save the position for the counter.
             */
            count = q_emul_buffer + q_emul_buffer_n - 1;
            scan_state = SCAN_CSI_PARAM;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'm') {
            /*
             * ESC [ m mean ESC [ 0 m, all attributes off.
             */
            q_current_color =
                Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * Fall-through to ANSI fallback.
         */
        break;

    case SCAN_CSI_PARAM:
        save_char(from_modem, to_screen);
        /*
         * Following through on the SGR code, we are now looking only for a
         * digit, semicolon, or 'm'.
         */
        if ((q_isdigit(from_modem)) || (from_modem == ';')) {
            scan_state = SCAN_CSI_PARAM;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'm') {

            DLOG(("ANSI SGR: change text attributes\n"));

            /*
             * Text attributes
             */
            if (ansi_color(&attributes, &count) == Q_TRUE) {
                q_current_color = attributes;
            } else {
                break;
            }

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * Fall-through to ANSI fallback.
         */
        break;

    /* PETSCII ------------------------------------------------------------- */

    case SCAN_NONE:
        /*
         * ESC
         */
        if (from_modem == C_ESC) {
            if ((q_status.petscii_color == Q_TRUE) ||
                (q_status.petscii_ansi_fallback == Q_TRUE)
            ) {
                /* Permit parsing of ANSI escape sequences. */
                save_char(from_modem, to_screen);
                scan_state = SCAN_ESC;
                return Q_EMUL_FSM_NO_CHAR_YET;
            }
        }

        if ((from_modem < 0x20) ||
            ((from_modem >= 0x80) && (from_modem < 0xA0))
        ) {
            /* This is a C0/C1 control character, process it there. */
            petscii_handle_control_char(from_modem);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /* This is a printable character, send it out. */
        if (q_status.petscii_use_unicode == Q_TRUE) {
            if (state.reverse == Q_TRUE) {
                q_current_color |= Q_A_REVERSE;
            } else {
                q_current_color &= ~Q_A_REVERSE;
            }
            if (state.uppercase == Q_TRUE) {
                *to_screen = c64_uppercase_unicode_chars[from_modem];
            } else {
                *to_screen = c64_lowercase_unicode_chars[from_modem];
            }
        } else {
            if ((state.uppercase == Q_TRUE) && (state.reverse == Q_TRUE)) {
                *to_screen = c64_uppercase_reverse_chars[from_modem];
            } else if ((state.uppercase == Q_TRUE) &&
                (state.reverse == Q_FALSE)
            ) {
                *to_screen = c64_uppercase_normal_chars[from_modem];
            } else if ((state.uppercase == Q_FALSE) &&
                (state.reverse == Q_TRUE)
            ) {
                *to_screen = c64_lowercase_reverse_chars[from_modem];
            } else {
                assert((state.uppercase == Q_FALSE) &&
                    (state.reverse == Q_FALSE));
                *to_screen = c64_lowercase_normal_chars[from_modem];
            }
        }
        return Q_EMUL_FSM_ONE_CHAR;

    } /* switch (scan_state) */

    if (q_status.petscii_ansi_fallback == Q_TRUE) {
        /*
         * Process through ANSI fallback code.
         *
         * This is UGLY AS HELL, but lots of BBSes assume that every emulator
         * will "fallback" to ANSI for sequences they don't understand.
         */
        scan_state = SCAN_ANSI_FALLBACK;
        DLOG(("ANSI FALLBACK BEGIN\n"));

        /*
         * From here on out we pass through ANSI until we don't get
         * Q_EMUL_FSM_NO_CHAR_YET.
         */
        memcpy(ansi_buffer, q_emul_buffer, q_emul_buffer_n);
        ansi_buffer_i = 0;
        ansi_buffer_n = q_emul_buffer_n;
        q_emul_buffer_i = 0;
        q_emul_buffer_n = 0;

        DLOG(("ANSI FALLBACK ansi()\n"));

        /*
         * Run through the emulator again
         */
        assert(ansi_buffer_n > 0);
        goto petscii_start;

    } else {

        DLOG(("Unknown sequence, and no ANSI fallback\n"));
        scan_state = DUMP_UNKNOWN_SEQUENCE;

        /*
         * This point means we got most, but not all, of a sequence.
         */
        *to_screen = codepage_map_char(q_emul_buffer[q_emul_buffer_i]);
        q_emul_buffer_i++;

        /*
         * Special case: one character returns Q_EMUL_FSM_ONE_CHAR.
         */
        if (q_emul_buffer_n == 1) {
            q_emul_buffer_i = 0;
            q_emul_buffer_n = 0;
            return Q_EMUL_FSM_ONE_CHAR;
        }

        /*
         * Tell the emulator layer that I need to be called many more times
         * to dump the string in q_emul_buffer.
         */
        return Q_EMUL_FSM_MANY_CHARS;
    }

    /*
     * Should never get here.
     */
    abort();
    return Q_EMUL_FSM_NO_CHAR_YET;
}

/**
 * Generate a sequence of bytes to send to the remote side that correspond to
 * a keystroke.
 *
 * @param keystroke one of the Q_KEY values, OR a Unicode code point.  See
 * input.h.
 * @return a wide string that is appropriate to send to the remote side.
 * Note that ANSI emulation is an 8-bit emulation: only the bottom 8 bits are
 * transmitted to the remote side.  See post_keystroke().
 */
wchar_t * petscii_keystroke(const int keystroke) {

    switch (keystroke) {

    case Q_KEY_ESCAPE:
        return L"\033";

    case Q_KEY_TAB:
        return L"\011";

    case Q_KEY_BACKSPACE:
        return L"\024";

    case Q_KEY_LEFT:
        return L"\235";

    case Q_KEY_RIGHT:
        return L"\035";

    case Q_KEY_UP:
        return L"\221";

    case Q_KEY_DOWN:
        return L"\021";

    case Q_KEY_PPAGE:
    case Q_KEY_NPAGE:
        return L"";
    case Q_KEY_IC:
        return L"\224";
    case Q_KEY_DC:
        return L"\024";
    case Q_KEY_SIC:
    case Q_KEY_SDC:
        return L"";
    case Q_KEY_HOME:
        return L"\023";
    case Q_KEY_END:
        return L"";
    case Q_KEY_F(1):
        return L"\205";
    case Q_KEY_F(2):
        return L"\211";
    case Q_KEY_F(3):
        return L"\206";
    case Q_KEY_F(4):
        return L"\212";
    case Q_KEY_F(5):
        return L"\207";
    case Q_KEY_F(6):
        return L"\213";
    case Q_KEY_F(7):
        return L"\210";
    case Q_KEY_F(8):
        return L"\214";
    case Q_KEY_F(9):
    case Q_KEY_F(10):
    case Q_KEY_F(11):
    case Q_KEY_F(12):
    case Q_KEY_F(13):
    case Q_KEY_F(14):
    case Q_KEY_F(15):
    case Q_KEY_F(16):
    case Q_KEY_F(17):
    case Q_KEY_F(18):
    case Q_KEY_F(19):
    case Q_KEY_F(20):
    case Q_KEY_F(21):
    case Q_KEY_F(22):
    case Q_KEY_F(23):
    case Q_KEY_F(24):
    case Q_KEY_F(25):
    case Q_KEY_F(26):
    case Q_KEY_F(27):
    case Q_KEY_F(28):
    case Q_KEY_F(29):
    case Q_KEY_F(30):
    case Q_KEY_F(31):
    case Q_KEY_F(32):
    case Q_KEY_F(33):
    case Q_KEY_F(34):
    case Q_KEY_F(35):
    case Q_KEY_F(36):
    case Q_KEY_PAD0:
    case Q_KEY_C1:
    case Q_KEY_PAD1:
    case Q_KEY_C2:
    case Q_KEY_PAD2:
    case Q_KEY_C3:
    case Q_KEY_PAD3:
    case Q_KEY_B1:
    case Q_KEY_PAD4:
    case Q_KEY_B2:
    case Q_KEY_PAD5:
    case Q_KEY_B3:
    case Q_KEY_PAD6:
    case Q_KEY_A1:
    case Q_KEY_PAD7:
    case Q_KEY_A2:
    case Q_KEY_PAD8:
    case Q_KEY_A3:
    case Q_KEY_PAD9:
    case Q_KEY_PAD_STOP:
    case Q_KEY_PAD_SLASH:
    case Q_KEY_PAD_STAR:
    case Q_KEY_PAD_MINUS:
    case Q_KEY_PAD_PLUS:
        return L"";

    case Q_KEY_PAD_ENTER:
    case Q_KEY_ENTER:
        return L"\015";

    default:
        break;

    }

    return NULL;
}

/**
 * Convert a printable ASCII character into a PETSCII printable character.
 *
 * @param ascii a value in the range of 0x20 - 0x7E, inclusive
 * @return a byte that can be transmitted to a PETSCII system, reflecting the
 * current shift state
 */
unsigned char petscii_ascii_to_petscii(const unsigned char ascii) {
    if ((ascii < 0x20) || (ascii >= 0x7F)) {
        return ascii;
    }

    if ((ascii >= ' ') && (ascii <= '?')) {
        return ascii;
    }

    switch (ascii) {
    case '[':
    case ']':
        return ascii;
    default:
        break;
    }

    if (state.uppercase == Q_TRUE) {
        /*
         * Uppercase is present, but lowercase letters are out of luck.
         */
        if ((ascii >= 'A') && (ascii <= 'Z')) {
            return ascii;
        }
    } else {
        /*
         * Uppercase and lowercase are both present, but in reversed
         * position.
         */
        if ((ascii >= 'A') && (ascii <= 'Z')) {
            return ascii + 0x20;
        } else if ((ascii >= 'a') && (ascii <= 'z')) {
            return ascii - 0x20;
        }
    }

    /*
     * The rest of the ASCII characters have no counterpart in PETSCII.
     * These are:
     *
     *   \\ ^ _ ` { | } ~
     *
     * Return them as-is, and let the user see the wrong thing.
     */
    return ascii;
}
