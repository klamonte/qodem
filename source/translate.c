/*
 * translate.c
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
#include <errno.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include "qodem.h"
#include "screen.h"
#include "forms.h"
#include "console.h"
#include "states.h"
#include "field.h"
#include "help.h"
#include "translate.h"

/*
 * Maximum length of a line in a translation file on disk.
 */
#define TRANSLATE_TABLE_LINE_SIZE       128

/*
 * The filename containing EBCDIC to CP437 mappings.
 */
#define EBCDIC_FILENAME                 "ebcdic.xl8"

/*
 * The filename containing global default 8-bit mappings.
 */
#define DEFAULT_8BIT_FILENAME           "default.xl8"

/*
 * The filename containing global default Unicode mappings.
 */
#define DEFAULT_UNICODE_FILENAME        "default.xlu"

/* Translate tables -------------------------------------------------------- */

/**
 * An 8-bit translation table.
 */
struct table_8bit_struct {
    unsigned char map_to[256];
};

/**
 * A <wchar_t, wchar_t> tuple.
 */
struct q_wchar_tuple {
    wchar_t key;
    wchar_t value;
};

/**
 * A Unicode translation table.  This is currently a very stupid list of
 * tuples.
 */
struct table_unicode_struct {
    struct q_wchar_tuple * mappings;
    size_t mappings_n;
};

/**
 * The 8-bit input translation table.
 */
static struct table_8bit_struct table_8bit_input;

/**
 * The 8-bit output translation table.
 */
static struct table_8bit_struct table_8bit_output;

/**
 * The Unicode input translation table.
 */
static struct table_unicode_struct table_unicode_input;

/**
 * The Unicode output translation table.
 */
static struct table_unicode_struct table_unicode_output;

/* UI Editor fields -------------------------------------------------------- */

/*
 * UI is provided for both kinds of tables (8-bit and Unicode).  The editor
 * only changes one part of a table pair, but the save/load works on both
 * parts.  The editing_table enum keeps track of which of the four available
 * parts are being edited, and each call to save/load uses that to figure out
 * the other half.
 */

/**
 * A copy of the 8-bit table that is currently being edited.
 */
static struct table_8bit_struct editing_table_8bit;

/**
 * A copy of the Unicode table that is currently being edited.
 */
static struct table_unicode_struct editing_table_unicode;

/**
 * The 8-bit table filename that is currently being edited.
 */
static char * editing_table_8bit_filename = NULL;

/**
 * The Unicode table filename that is currently being edited.
 */
static char * editing_table_unicode_filename = NULL;

/**
 * Which table entry is currently being edited.
 */
static int selected_entry = 0;

/**
 * Whether we have changed the table mapping in the editor.
 */
static Q_BOOL saved_changes = Q_TRUE;

/**
 * Type of translate table.
 */
typedef enum {
    INPUT_8BIT,                 /* Input table 8-bit */
    OUTPUT_8BIT,                /* Output table 8-bit */
    INPUT_UNICODE,              /* Input table Unicode */
    OUTPUT_UNICODE,             /* Output table Unicode */
} EDITING_TABLE;

/**
 * Which table is being edited.
 */
static EDITING_TABLE editing_table = INPUT_8BIT;

/**
 * Load an 8-bit translate table pair from a file into the global translate
 * table structs.
 *
 * @param filename the basename of a file in the data directory to read from
 * @param table_input the input table to write the [input] values to
 * @param table_output the output table to write the [output] values to
 */
void load_translate_tables_8bit(const char * filename,
    struct table_8bit_struct * table_input,
    struct table_8bit_struct * table_output) {

    FILE * file;
    char * full_filename;
    char line[TRANSLATE_TABLE_LINE_SIZE];
    char * key;
    char * value;
    char * endptr;
    unsigned char map_to;
    unsigned char map_from;
    int i;

    enum {
        SCAN_INPUT,
        SCAN_INPUT_VALUES,
        SCAN_OUTPUT_VALUES
    };

    int state = SCAN_INPUT;

    assert(filename != NULL);

    file = open_datadir_file(filename, &full_filename, "r");
    if (file == NULL) {
        /*
         * Quietly exit.
         */
        /*
         * No leak
         */
        Xfree(full_filename, __FILE__, __LINE__);
        return;
    }

    /*
     * Set defaults to do no translation.
     */
    for (i = 0; i < 256; i++) {
        table_input->map_to[i] = i;
        table_output->map_to[i] = i;
    }

    memset(line, 0, sizeof(line));
    while (!feof(file)) {

        if (fgets(line, sizeof(line), file) == NULL) {
            /*
             * This should cause the outer while's feof() check to fail
             */
            continue;
        }
        line[sizeof(line) - 1] = 0;

        if ((strlen(line) == 0) || (line[0] == '#')) {
            /*
             * Empty or comment line
             */
            continue;
        }

        /*
         * Nix trailing whitespace
         */
        while ((strlen(line) > 0) && q_isspace(line[strlen(line) - 1])) {
            line[strlen(line) - 1] = 0;
        }

        if (state == SCAN_INPUT) {
            /*
             * Looking for "[input]"
             */
            if (strcmp(line, "[input]") == 0) {
                state = SCAN_INPUT_VALUES;
            }
            continue;
        }

        /*
         * Looking for "[output]"
         */
        if (strcmp(line, "[output]") == 0) {
            state = SCAN_OUTPUT_VALUES;
            continue;
        }

        key = line;
        while ((strlen(key) > 0) && (q_isspace(*key))) {
            key++;
        }

        value = strchr(key, '=');
        if (value == NULL) {
            /*
             * Invalid line
             */
            continue;
        }

        *value = 0;
        value++;
        while ((strlen(value) > 0) && (q_isspace(*value))) {
            value++;
        }
        if (*value == 0) {
            /*
             * Invalid mapping
             */
            continue;
        }

        map_to = (unsigned char) strtoul(value, &endptr, 10);
        if (*endptr != 0) {
            /*
             * Invalid mapping
             */
            continue;
        }

        while ((strlen(key) > 0) && (q_isspace(key[strlen(key) - 1]))) {
            key[strlen(key) - 1] = 0;
        }

        map_from = (unsigned char) strtoul(key, &endptr, 10);
        if (*endptr != 0) {
            /*
             * Invalid mapping
             */
            continue;
        }

        /*
         * map_from and map_to are both valid unsigned integers
         */
        if (state == SCAN_INPUT_VALUES) {
            table_input->map_to[map_from] = map_to;
        } else {
            table_output->map_to[map_from] = map_to;
        }

    }

    Xfree(full_filename, __FILE__, __LINE__);
    fclose(file);

    /*
     * Note that we have no outstanding changes to save
     */
    saved_changes = Q_TRUE;
}

/**
 * Save an 8-bit translate table pair to a file.
 *
 * @param filename the basename of a file in the data directory to write to
 * @param table_input the input table to read the [input] values from
 * @param table_output the output table to read the [output] values from
 */
static void save_translate_tables_8bit(const char * filename,
    struct table_8bit_struct * table_input,
    struct table_8bit_struct * table_output) {

    char notify_message[DIALOG_MESSAGE_SIZE];
    char * full_filename;
    FILE * file;
    int i;

    if (q_status.read_only == Q_TRUE) {
        return;
    }

    assert(filename != NULL);

    file = open_datadir_file(filename, &full_filename, "w");
    if (file == NULL) {
        snprintf(notify_message, sizeof(notify_message),
                 _("Error opening file \"%s\" for writing: %s"),
                 filename, strerror(errno));
        notify_form(notify_message, 0);
        /*
         * No leak
         */
        Xfree(full_filename, __FILE__, __LINE__);
        return;
    }

    /*
     * Header
     */
    fprintf(file, "# Qodem ASCII translate tables file\n");
    fprintf(file, "# ---------------------------------\n");
    fprintf(file, "#\n");
    fprintf(file, "# The default is an identity mapping.  Entries in this\n");
    fprintf(file, "# file record overrides from that default.\n");
    fprintf(file, "#\n");
    fprintf(file, "# This is the list of 8-bit overrides.  Entries use\n");
    fprintf(file, "# base 10 (0-255).  For example, to make all incoming\n");
    fprintf(file, "# '$' bytes turn into 'o', add a line that has:\n");
    fprintf(file, "#     36 = 111\n");
    fprintf(file, "# ...in the [input] section.\n");
    fprintf(file, "#\n");

    /*
     * Input
     */
    fprintf(file, "\n[input]\n");
    for (i = 0; i < 256; i++) {
        if (table_input->map_to[i] != i) {
            fprintf(file, "%d = %d\n", i, table_input->map_to[i]);
        }
    }

    /*
     * Output
     */
    fprintf(file, "\n[output]\n");
    for (i = 0; i < 256; i++) {
        if (table_output->map_to[i] != i) {
            fprintf(file, "%d = %d\n", i, table_output->map_to[i]);
        }
    }

    Xfree(full_filename, __FILE__, __LINE__);
    fclose(file);

    /*
     * Note that we have no outstanding changes to save
     */
    saved_changes = Q_TRUE;
}

/**
 * Load a Unicode translate table pair from a file into the global translate
 * table structs.
 *
 * @param filename the basename of a file in the data directory to read from
 * @param table_input the input table to read the [input] values from
 * @param table_output the output table to read the [output] values from
 */
void load_translate_tables_unicode(const char * filename,
    struct table_unicode_struct * table_input,
    struct table_unicode_struct * table_output) {

    FILE * file;
    char * full_filename;
    char line[TRANSLATE_TABLE_LINE_SIZE];
    char * key;
    char * value;
    char * endptr;
    wchar_t map_to;
    wchar_t map_from;

    enum {
        SCAN_INPUT,
        SCAN_INPUT_VALUES,
        SCAN_OUTPUT_VALUES
    };

    int state = SCAN_INPUT;

    assert(filename != NULL);

    file = open_datadir_file(filename, &full_filename, "r");
    if (file == NULL) {
        /*
         * Quietly exit.
         */
        /*
         * No leak
         */
        Xfree(full_filename, __FILE__, __LINE__);
        return;
    }

    /*
     * Set defaults to do no translation.
     */
    if (table_input->mappings_n > 0) {
        assert(table_input->mappings != NULL);
        Xfree(table_input->mappings, __FILE__, __LINE__);
        table_input->mappings = NULL;
        table_input->mappings_n = 0;
    }
    assert(table_input->mappings == NULL);
    assert(table_input->mappings_n == 0);

    if (table_output->mappings_n > 0) {
        assert(table_output->mappings != NULL);
        Xfree(table_output->mappings, __FILE__, __LINE__);
        table_output->mappings = NULL;
        table_output->mappings_n = 0;
    }
    assert(table_output->mappings == NULL);
    assert(table_output->mappings_n == 0);

    /*
     * Read the data file.
     */
    memset(line, 0, sizeof(line));
    while (!feof(file)) {

        if (fgets(line, sizeof(line), file) == NULL) {
            /*
             * This should cause the outer while's feof() check to fail
             */
            continue;
        }
        line[sizeof(line) - 1] = 0;

        if ((strlen(line) == 0) || (line[0] == '#')) {
            /*
             * Empty or comment line
             */
            continue;
        }

        /*
         * Nix trailing whitespace
         */
        while ((strlen(line) > 0) && q_isspace(line[strlen(line) - 1])) {
            line[strlen(line) - 1] = 0;
        }

        if (state == SCAN_INPUT) {
            /*
             * Looking for "[input]"
             */
            if (strcmp(line, "[input]") == 0) {
                state = SCAN_INPUT_VALUES;
            }
            continue;
        }

        /*
         * Looking for "[output]"
         */
        if (strcmp(line, "[output]") == 0) {
            state = SCAN_OUTPUT_VALUES;
            continue;
        }

        key = line;
        while ((strlen(key) > 0) && (q_isspace(*key))) {
            key++;
        }

        value = strchr(key, '=');
        if (value == NULL) {
            /*
             * Invalid line
             */
            continue;
        }

        *value = 0;
        value++;
        while ((strlen(value) > 0) && (q_isspace(*value))) {
            value++;
        }
        if (*value == 0) {
            /*
             * Invalid mapping
             */
            continue;
        }
        if (strlen(value) <= 2) {
            /*
             * Invalid mapping
             */
            continue;
        }
        if ((value[0] != '\\') && (value[1] != 'u')) {
            /*
             * Invalid mapping
             */
            continue;
        }

        map_to = (wchar_t) strtoul(value + 2, &endptr, 16);
        if (*endptr != 0) {
            /*
             * Invalid mapping
             */
            continue;
        }

        while ((strlen(key) > 0) && (q_isspace(key[strlen(key) - 1]))) {
            key[strlen(key) - 1] = 0;
        }

        if (strlen(key) <= 2) {
            /*
             * Invalid mapping
             */
            continue;
        }
        if ((key[0] != '\\') && (key[1] != 'u')) {
            /*
             * Invalid mapping
             */
            continue;
        }
        map_from = (wchar_t) strtoul(key + 2, &endptr, 16);
        if (*endptr != 0) {
            /*
             * Invalid mapping
             */
            continue;
        }

        /*
         * map_from and map_to are both valid unsigned integers
         */
        if (state == SCAN_INPUT_VALUES) {
            table_input->mappings = (struct q_wchar_tuple *) Xrealloc(
                table_input->mappings,
                sizeof(struct q_wchar_tuple) * (table_input->mappings_n + 1),
                __FILE__, __LINE__);
            table_input->mappings[table_input->mappings_n].key = map_from;
            table_input->mappings[table_input->mappings_n].value = map_to;
            table_input->mappings_n++;
        } else {
            table_output->mappings = (struct q_wchar_tuple *) Xrealloc(
                table_output->mappings,
                sizeof(struct q_wchar_tuple) * (table_output->mappings_n + 1),
                __FILE__, __LINE__);
            table_output->mappings[table_output->mappings_n].key = map_from;
            table_output->mappings[table_output->mappings_n].value = map_to;
            table_output->mappings_n++;
        }

    } /* while (!feof(file)) */

    Xfree(full_filename, __FILE__, __LINE__);
    fclose(file);

    /*
     * Note that we have no outstanding changes to save
     */
    saved_changes = Q_TRUE;
}

/**
 * Save a Unicode translate table pair to a file.
 *
 * @param filename the basename of a file in the data directory to read from
 * @param table_input the input table to read the [input] values from
 * @param table_output the output table to read the [output] values from
 */
static void save_translate_tables_unicode(const char * filename,
    struct table_unicode_struct * table_input,
    struct table_unicode_struct * table_output) {

    char notify_message[DIALOG_MESSAGE_SIZE];
    char * full_filename;
    FILE * file;
    int i;

    if (q_status.read_only == Q_TRUE) {
        return;
    }

    assert(filename != NULL);

    file = open_datadir_file(filename, &full_filename, "w");
    if (file == NULL) {
        snprintf(notify_message, sizeof(notify_message),
                 _("Error opening file \"%s\" for writing: %s"),
                 filename, strerror(errno));
        notify_form(notify_message, 0);
        /*
         * No leak
         */
        Xfree(full_filename, __FILE__, __LINE__);
        return;
    }

    /*
     * Header
     */
    fprintf(file, "# Qodem ASCII translate tables file\n");
    fprintf(file, "# ---------------------------------\n");
    fprintf(file, "#\n");
    fprintf(file, "# The default is an identity mapping.  Entries in this\n");
    fprintf(file, "# file record overrides from that default.\n");
    fprintf(file, "#\n");
    fprintf(file, "# This is the list of Unicode overrides.  Entries use\n");
    fprintf(file, "# 16-bit base 16 (0x0000-0xFFFF).  For example, to make\n");
    fprintf(file, "# all incoming '$' bytes turn into 'o', add a line that\n");
    fprintf(file, "# has:\n");
    fprintf(file, "#     \\u0024 = \\u006F\n");
    fprintf(file, "# ...in the [input] section.\n");
    fprintf(file, "#\n");

    /*
     * Input
     */
    fprintf(file, "\n[input]\n");
    for (i = 0; i < table_unicode_input.mappings_n; i++) {
        if (table_unicode_input.mappings[i].key !=
            table_unicode_input.mappings[i].value) {
            fprintf(file, "\\u%04x = \\u%04x\n",
                (uint16_t) table_unicode_input.mappings[i].key,
                (uint16_t) table_unicode_input.mappings[i].value);
        }
    }

    /*
     * Output
     */
    fprintf(file, "\n[output]\n");
    for (i = 0; i < table_unicode_output.mappings_n; i++) {
        if (table_unicode_output.mappings[i].key !=
            table_unicode_output.mappings[i].value) {
            fprintf(file, "\\u%04x = \\u%04x\n",
                (uint16_t) table_unicode_output.mappings[i].key,
                (uint16_t) table_unicode_output.mappings[i].value);
        }
    }

    Xfree(full_filename, __FILE__, __LINE__);
    fclose(file);

    /*
     * Note that we have no outstanding changes to save
     */
    saved_changes = Q_TRUE;
}

/**
 * Sets a translate table mapping to do nothing.
 *
 * @param table the table to reset
 */
static void reset_table_8bit(struct table_8bit_struct * table) {
    int i;

    for (i = 0; i < 256; i++) {
        table->map_to[i] = i;
    }
}

/**
 * Sets a translate table mapping to do nothing.
 *
 * @param table the table to reset
 */
static void reset_table_unicode(struct table_unicode_struct * table) {
    if (table->mappings != NULL) {
        assert(table->mappings_n > 0);
        Xfree(table->mappings, __FILE__, __LINE__);
    }
    table->mappings = NULL;
    table->mappings_n = 0;
}

/**
 * Copies a translate table mapping to another.
 *
 * @param src the table to copy from
 * @param dest the table to copy to
 */
static void copy_table_8bit(struct table_8bit_struct * src,
                            struct table_8bit_struct * dest) {

    int i;

    for (i = 0; i < 256; i++) {
        dest->map_to[i] = src->map_to[i];
    }
}

/**
 * Copies a translate table mapping to another.
 *
 * @param src the table to copy from
 * @param dest the table to copy to
 */
static void copy_table_unicode(struct table_unicode_struct * src,
                               struct table_unicode_struct * dest) {
    int i;

    reset_table_unicode(dest);
    if (src->mappings_n > 0) {
        dest->mappings = (struct q_wchar_tuple *) Xmalloc(sizeof(
            struct q_wchar_tuple) * (src->mappings_n), __FILE__, __LINE__);
        for (i = 0; i < src->mappings_n; i++) {
            dest->mappings[i].key   = src->mappings[i].key;
            dest->mappings[i].value = src->mappings[i].value;
        }
        dest->mappings_n = src->mappings_n;
    }
}

/**
 * Translate an 8-bit byte using the input table read via
 * use_translate_table_8bit().
 *
 * @param in the byte to translate
 * @return the translated byte
 */
unsigned char translate_8bit_in(const unsigned char in) {
    return table_8bit_input.map_to[in];
}

/**
 * Translate an 8-bit byte using the output table read via
 * use_translate_table_8bit().
 *
 * @param in the byte to translate
 * @return the translated byte
 */
unsigned char translate_8bit_out(const unsigned char in) {
    return table_8bit_output.map_to[in];
}

/**
 * Translate a Unicode code point using the input tables read via
 * use_translate_table_unicode().
 *
 * @param in the code point to translate
 * @return the translated code point
 */
wchar_t translate_unicode_in(const wchar_t in) {
    int i = 0;

    while (i < table_unicode_input.mappings_n) {
        if (table_unicode_input.mappings[i].key == in) {
            return table_unicode_input.mappings[i].value;
        }
        i++;
    }

    /*
     * No overrides found.
     */
    return in;
}

/**
 * Translate a Unicode code point using the output tables read via
 * use_translate_table_unicode().
 *
 * @param in the code point to translate
 * @return the translated code point
 */
wchar_t translate_unicode_out(const wchar_t in) {
    int i = 0;

    while (i < table_unicode_output.mappings_n) {
        if (table_unicode_output.mappings[i].key == in) {
            return table_unicode_output.mappings[i].value;
        }
        i++;
    }

    /*
     * No overrides found.
     */
    return in;
}

/**
 * Get the value part of a Unicode mapping.
 *
 * @param table the table to search
 * @param key the mapping key
 * @return the mapping value
 */
static wchar_t unicode_table_get(struct table_unicode_struct * table,
                                 const wchar_t key) {
    int i = 0;

    while (i < table->mappings_n) {
        if (table->mappings[i].key == key) {
            return table->mappings[i].value;
        }
        i++;
    }

    /*
     * No overrides found.
     */
    return key;
}

/**
 * Set a new (key, value) Unicode mapping into a table.
 *
 * @param table the table to modify
 * @param key the mapping key
 * @param value the new mapping value
 */
static void unicode_table_set(struct table_unicode_struct * table,
                              const wchar_t key,
                              const wchar_t value) {
    int i = 0;

    while (i < table->mappings_n) {
        if (table->mappings[i].key == key) {
            table->mappings[i].value = value;
            return;
        }
        i++;
    }

    /*
     * No overrides found, add this one to the end.
     */
    table->mappings = (struct q_wchar_tuple *) Xrealloc(table->mappings,
        sizeof(struct q_wchar_tuple) * (table->mappings_n + 1),
        __FILE__, __LINE__);
    table->mappings[table->mappings_n].key = key;
    table->mappings[table->mappings_n].value = value;
    table->mappings_n++;
}

/**
 * Try to map a Unicode code point to an 8-bit byte of a codepage.  Use both
 * of the input/output tables read via use_translate_table_unicode(), and the
 * codepage mappings.
 *
 * @param in the code point to translate
 * @param codepage the 8-bit codepage to map to
 * @return the mapped byte, or '?' if nothing can be mapped to it
 */
unsigned char translate_unicode_to_8bit(const wchar_t in,
                                        const Q_CODEPAGE codepage) {

    wchar_t utf8_char;
    unsigned char ch;
    Q_BOOL success;

    if (in < 0x80) {
        /*
         * 7-bit ASCII: use the bottom half of the 8-bit translate table.
         */
        return table_8bit_output.map_to[in & 0x7F];
    }

    if (codepage == Q_CODEPAGE_DEC) {
        /*
         * This is a gap that is hard to close.  DEC character sets (NRC,
         * VT100 Special Graphics, and VT52 Graphics) are converted to
         * Unicode on the way to the display, but converting Unicode back
         * requires knowing which character set is currently selected in the
         * VT52/VT100/xterm AND whether or not the terminal is in graphics
         * mode.  Rather than a complicated scheme that will only sometimes
         * work and requires the application on the other end be aware of the
         * terminal state (which if they use curses should never be true
         * anyway), I will simply return the unknown character.
         *
         * Users who really need to send non-ASCII or graphics characters in
         * a DEC emulation should just use one of the UTF-8 emulations.
         */
        return '?';
    }

    if (codepage == Q_CODEPAGE_PETSCII) {
        /*
         * PETSCII is specifically hardcoded to the C64 Pro Mono font
         * developed by Style, which uses the private use area of Unicode to
         * provide pixel-perfect reproduction of the actual C64/128 glyphs.
         * Users who try to put in a Unicode char (e.g. via Alt Code Key) are
         * just out of luck.
         */
        if (in < 0x100) {
            /* 8-bit byte, send it exactly as-is. */
            return (in & 0xFF);
        }
        /* Unicode code point: sorry, you are out of luck. */
        return '?';
    }

    /*
     * We have a Unicode code point.
     *
     * We must try to map it back to something in the codepage.  We try the
     * following tactics:
     *
     * 1. Apply the Unicode output map, and then see if the codepage map has
     * the reverse mapping.
     *
     * 2. See if there are any mapping from the Unicode input map that have
     * in as their output, and if so use that input to unmap to.
     */
    utf8_char = translate_unicode_out(in);
    ch = codepage_unmap_byte(utf8_char, codepage, &success);
    if (success == Q_TRUE) {
        /*
         * We have a reasonable mapping, use it.
         */
        return ch;
    }

    /*
     * TODO: implement strategy 2: reverse the Unicode input map, look for
     * equivalents in the 8-bit codepage map.
     */
    return '?';
}

/**
 * Create an 8-bit translate table pair for EBCDIC-to-CP437.
 *
 * @param filename the basename of a file in the data directory to read from
 */
static void create_ebcdic_file(const char * filename) {
    char * full_filename;
    FILE * file;
    struct table_8bit_struct ebcdic_to_cp437 = {
        {
            0x00, 0x01, 0x02, 0x03, 0xEC, 0x09, 0xCA, 0x1C,
            0xE2, 0xD2, 0xD3, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
            0x10, 0x11, 0x12, 0x13, 0xEF, 0xC5, 0x08, 0xCB,
            0x18, 0x19, 0xDC, 0xD8, 0x1A, 0x1D, 0x1E, 0x1F,
            0xB7, 0xB8, 0xB9, 0xBB, 0xC4, 0x0A, 0x17, 0x1B,
            0xCC, 0xCD, 0xCF, 0xD0, 0xD1, 0x05, 0x06, 0x07,
            0xD9, 0xDA, 0x16, 0xDD, 0xDE, 0xDF, 0xE0, 0x04,
            0xE3, 0xE5, 0xE9, 0xEB, 0xB0, 0xB1, 0x9E, 0x7F,
            0x20, 0xFF, 0x83, 0x84, 0x85, 0xA0, 0xF2, 0x86,
            0x87, 0xA4, 0x5B, 0x2E, 0x3C, 0x28, 0x2B, 0x21,
            0x26, 0x82, 0x88, 0x89, 0x8A, 0xA1, 0x8C, 0x8B,
            0x8D, 0xE1, 0x5D, 0x24, 0x2A, 0x29, 0x3B, 0x5E,
            0x2D, 0x2F, 0xB2, 0x8E, 0xB4, 0xB5, 0xB6, 0x8F,
            0x80, 0xA5, 0xB3, 0x2C, 0x25, 0x5F, 0x3E, 0x3F,
            0xBA, 0x90, 0xBC, 0xBD, 0xBE, 0xF3, 0xC0, 0xC1,
            0xC2, 0x60, 0x3A, 0x23, 0x40, 0x27, 0x3D, 0x22,
            0xC3, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
            0x68, 0x69, 0xAE, 0xAF, 0xC6, 0xC7, 0xC8, 0xF1,
            0xF8, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70,
            0x71, 0x72, 0xA6, 0xA7, 0x91, 0xCE, 0x92, 0xA9,
            0xE6, 0x7E, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
            0x79, 0x7A, 0xAD, 0xA8, 0xD4, 0xD5, 0xD6, 0xD7,
            0x9B, 0x9C, 0x9D, 0xFA, 0x9F, 0x15, 0x14, 0xAC,
            0xAB, 0xFC, 0xAA, 0x7C, 0xE4, 0xFE, 0xBF, 0xE7,
            0x7B, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
            0x48, 0x49, 0xE8, 0x93, 0x94, 0x95, 0xA2, 0xED,
            0x7D, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50,
            0x51, 0x52, 0xEE, 0x96, 0x81, 0x97, 0xA3, 0x98,
            0x5C, 0xF6, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
            0x59, 0x5A, 0xFD, 0xF5, 0x99, 0xF7, 0xF0, 0xF9,
            0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
            0x38, 0x39, 0xDB, 0xFB, 0x9A, 0xF4, 0xEA, 0xC9
        }
    };

    struct table_8bit_struct cp437_to_ebcdic = {
        {
            0x00, 0x01, 0x02, 0x03, 0x37, 0x2D, 0x2E, 0x2F,
            0x16, 0x05, 0x25, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
            0x10, 0x11, 0x12, 0x13, 0xB6, 0xB5, 0x32, 0x26,
            0x18, 0x19, 0x1C, 0x27, 0x07, 0x1D, 0x1E, 0x1F,
            0x40, 0x4F, 0x7F, 0x7B, 0x5B, 0x6C, 0x50, 0x7D,
            0x4D, 0x5D, 0x5C, 0x4E, 0x6B, 0x60, 0x4B, 0x61,
            0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
            0xF8, 0xF9, 0x7A, 0x5E, 0x4C, 0x7E, 0x6E, 0x6F,
            0x7C, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
            0xC8, 0xC9, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6,
            0xD7, 0xD8, 0xD9, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6,
            0xE7, 0xE8, 0xE9, 0x4A, 0xE0, 0x5A, 0x5F, 0x6D,
            0x79, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
            0x88, 0x89, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96,
            0x97, 0x98, 0x99, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
            0xA7, 0xA8, 0xA9, 0xC0, 0xBB, 0xD0, 0xA1, 0x3F,
            0x68, 0xDC, 0x51, 0x42, 0x43, 0x44, 0x47, 0x48,
            0x52, 0x53, 0x54, 0x57, 0x56, 0x58, 0x63, 0x67,
            0x71, 0x9C, 0x9E, 0xCB, 0xCC, 0xCD, 0xDB, 0xDD,
            0xDF, 0xEC, 0xFC, 0xB0, 0xB1, 0xB2, 0x3E, 0xB4,
            0x45, 0x55, 0xCE, 0xDE, 0x49, 0x69, 0x9A, 0x9B,
            0xAB, 0x9F, 0xBA, 0xB8, 0xB7, 0xAA, 0x8A, 0x8B,
            0x3C, 0x3D, 0x62, 0x6A, 0x64, 0x65, 0x66, 0x20,
            0x21, 0x22, 0x70, 0x23, 0x72, 0x73, 0x74, 0xBE,
            0x76, 0x77, 0x78, 0x80, 0x24, 0x15, 0x8C, 0x8D,
            0x8E, 0xFF, 0x06, 0x17, 0x28, 0x29, 0x9D, 0x2A,
            0x2B, 0x2C, 0x09, 0x0A, 0xAC, 0xAD, 0xAE, 0xAF,
            0x1B, 0x30, 0x31, 0xFA, 0x1A, 0x33, 0x34, 0x35,
            0x36, 0x59, 0x08, 0x38, 0xBC, 0x39, 0xA0, 0xBF,
            0xCA, 0x3A, 0xFE, 0x3B, 0x04, 0xCF, 0xDA, 0x14,
            0xEE, 0x8F, 0x46, 0x75, 0xFD, 0xEB, 0xE1, 0xED,
            0x90, 0xEF, 0xB3, 0xFB, 0xB9, 0xEA, 0xBD, 0x41
        }
    };

    assert(filename != NULL);

    file = open_datadir_file(filename, &full_filename, "r");
    /*
     * Avoid leak
     */
    Xfree(full_filename, __FILE__, __LINE__);

    if (file != NULL) {
        /*
         * The file containing mappings already exists.  Bail out.
         */
        fclose(file);
        return;
    }

    /*
     * Need to create the file now.
     */
    save_translate_tables_8bit(filename, &ebcdic_to_cp437, &cp437_to_ebcdic);

}

/**
 * Loads the default translate table pairs.
 */
void initialize_translate_tables() {
    reset_table_8bit(&table_8bit_input);
    reset_table_8bit(&table_8bit_output);
    table_unicode_input.mappings    = NULL;
    table_unicode_input.mappings_n  = 0;
    table_unicode_output.mappings   = NULL;
    table_unicode_output.mappings_n = 0;
    reset_table_unicode(&table_unicode_input);
    reset_table_unicode(&table_unicode_output);
    use_translate_table_8bit(DEFAULT_8BIT_FILENAME);
    use_translate_table_unicode(DEFAULT_UNICODE_FILENAME);
    create_ebcdic_file(EBCDIC_FILENAME);
}

/**
 * Load an 8-bit translate table pair from a file and begin using it for
 * translate_8bit().
 *
 * @param filename the basename of a file in the data directory to read from
 */
void use_translate_table_8bit(const char * filename) {
    char * full_filename;
    FILE * file;

    assert(filename != NULL);

    file = open_datadir_file(filename, &full_filename, "r");
    /*
     * Avoid leak
     */
    Xfree(full_filename, __FILE__, __LINE__);

    if (file == NULL) {
        /*
         * The file containing mappings doesn't exist.  Reset globals to
         * default no-mapping case, and then create a new file so that the
         * editor will work.
         */
        reset_table_8bit(&table_8bit_input);
        reset_table_8bit(&table_8bit_output);
        save_translate_tables_8bit(filename, &table_8bit_input,
            &table_8bit_output);
    } else {
        fclose(file);

        /*
         * File exists, let's load it.
         */
        load_translate_tables_8bit(filename, &table_8bit_input,
            &table_8bit_output);
    }

    /*
     * Make sure this is what shows in the editor.
     */
    if (editing_table_8bit_filename != NULL) {
        Xfree(editing_table_8bit_filename, __FILE__, __LINE__);
    }
    editing_table_8bit_filename = Xstrdup(filename, __FILE__, __LINE__);
}

/**
 * Load a Unicode translate table pair from a file and begin using it for
 * translate_unicode().
 *
 * @param filename the basename of a file in the data directory to read from
 */
void use_translate_table_unicode(const char * filename) {
    char * full_filename;
    FILE * file;

    assert(filename != NULL);

    file = open_datadir_file(filename, &full_filename, "r");
    /*
     * Avoid leak
     */
    Xfree(full_filename, __FILE__, __LINE__);

    if (file == NULL) {
        /*
         * The file containing mappings doesn't exist.  Reset globals to
         * default no-mapping case, and then create a new file so that the
         * editor will work.
         */
        reset_table_unicode(&table_unicode_input);
        reset_table_unicode(&table_unicode_output);
        save_translate_tables_unicode(filename, &table_unicode_input,
            &table_unicode_output);
    } else {
        fclose(file);

        /*
         * File exists, let's load it.
         */
        load_translate_tables_unicode(filename, &table_unicode_input,
            &table_unicode_output);
    }

    /*
     * Make sure this is what shows in the editor.
     */
    if (editing_table_unicode_filename != NULL) {
        Xfree(editing_table_unicode_filename, __FILE__, __LINE__);
    }
    editing_table_unicode_filename = Xstrdup(filename, __FILE__, __LINE__);
}

/**
 * Draw screen for the Alt-A translation table selection dialog.
 */
void translate_table_menu_refresh() {
    char * status_string;
    int status_left_stop;
    char * message;
    int message_left;
    int window_left;
    int window_top;
    int window_height = 11;
    int window_length;

    if (q_screen_dirty == Q_FALSE) {
        return;
    }

    /*
     * Clear screen for when it resizes
     */
    console_refresh(Q_FALSE);

    window_length = 24;
    window_left = WIDTH - 1 - window_length;
    if (window_left < 0) {
        window_left = 0;
    } else {
        window_left /= 2;
    }
    window_top = HEIGHT - 1 - window_height;
    if (window_top < 0) {
        window_top = 0;
    } else {
        window_top /= 10;
    }

    screen_draw_box(window_left, window_top, window_left + window_length,
                    window_top + window_height);
    screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH,
                              Q_COLOR_STATUS);

    status_string = _(" Select the Strip/Replace Table to Edit   ESC/`-Exit ");
    status_left_stop = WIDTH - strlen(status_string);
    if (status_left_stop <= 0) {
        status_left_stop = 0;
    } else {
        status_left_stop /= 2;
    }
    screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string,
                            Q_COLOR_STATUS);

    message = _("Table Selection");
    message_left = window_length - (strlen(message) + 2);
    if (message_left < 0) {
        message_left = 0;
    } else {
        message_left /= 2;
    }
    screen_put_color_printf_yx(window_top + 0, window_left + message_left,
                               Q_COLOR_WINDOW_BORDER, " %s ", message);

    screen_put_color_str_yx(window_top + 2, window_left + 2,
                            _("Select Table to Edit"), Q_COLOR_MENU_TEXT);

    screen_put_color_str_yx(window_top + 4, window_left + 2, "1",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, " - %s", _("INPUT  (8-Bit)"));
    screen_put_color_str_yx(window_top + 5, window_left + 2, "2",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, " - %s", _("OUTPUT (8-Bit)"));

    screen_put_color_str_yx(window_top + 6, window_left + 2, "3",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, " - %s", _("INPUT  (Unicode)"));
    screen_put_color_str_yx(window_top + 7, window_left + 2, "4",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, " - %s", _("OUTPUT (Unicode)"));

    screen_put_color_str_yx(window_top + 9, window_left + 2,
                            _("Your Choice ? "), Q_COLOR_MENU_COMMAND);

    screen_flush();
    q_screen_dirty = Q_FALSE;
}

/*
 * When true, the user is editing the high bit range of the 8-bit table.
 */
static Q_BOOL editing_high_128 = Q_FALSE;

/**
 * Keyboard handler for the Alt-A translation table selection dialog.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
void translate_table_menu_keyboard_handler(const int keystroke,
                                           const int flags) {

    Q_PROGRAM_STATE next_state = Q_STATE_TRANSLATE_EDITOR_8BIT;

    switch (keystroke) {

    case '1':
        copy_table_8bit(&table_8bit_input, &editing_table_8bit);
        next_state = Q_STATE_TRANSLATE_EDITOR_8BIT;
        editing_table = INPUT_8BIT;
        if (editing_table_8bit_filename == NULL) {
            editing_table_8bit_filename = Xstrdup(DEFAULT_8BIT_FILENAME,
                __FILE__, __LINE__);
        }
        break;

    case '2':
        copy_table_8bit(&table_8bit_output, &editing_table_8bit);
        next_state = Q_STATE_TRANSLATE_EDITOR_8BIT;
        editing_table = OUTPUT_8BIT;
        if (editing_table_8bit_filename == NULL) {
            editing_table_8bit_filename = Xstrdup(DEFAULT_8BIT_FILENAME,
                __FILE__, __LINE__);
        }
        break;

    case '3':
        copy_table_unicode(&table_unicode_input, &editing_table_unicode);
        next_state = Q_STATE_TRANSLATE_EDITOR_UNICODE;
        editing_table = INPUT_UNICODE;
        if (editing_table_unicode_filename == NULL) {
            editing_table_unicode_filename = Xstrdup(DEFAULT_UNICODE_FILENAME,
                __FILE__, __LINE__);
        }
        break;

    case '4':
        copy_table_unicode(&table_unicode_output, &editing_table_unicode);
        next_state = Q_STATE_TRANSLATE_EDITOR_UNICODE;
        editing_table = OUTPUT_UNICODE;
        if (editing_table_unicode_filename == NULL) {
            editing_table_unicode_filename = Xstrdup(DEFAULT_UNICODE_FILENAME,
                __FILE__, __LINE__);
        }
        break;

    case '`':
        /*
         * Backtick works too
         */
    case Q_KEY_ESCAPE:
        /*
         * ESC return to TERMINAL mode
         */
        switch_state(Q_STATE_CONSOLE);

        /*
         * The ABORT exit point
         */
        return;

    default:
        /*
         * Ignore keystroke
         */
        return;
    }

    /*
     * The OK exit point
     */
    selected_entry = 0;
    editing_high_128 = Q_FALSE;
    q_screen_dirty = Q_TRUE;
    console_refresh(Q_FALSE);
    switch_state(next_state);
}

/* A form + fields to handle the editing of a given key binding value */
static void * edit_table_entry_window;
static struct fieldset * edit_table_entry_form;
static struct field * edit_table_entry_field;
static Q_BOOL editing_entry = Q_FALSE;
static int window_left;
static int window_top;
static int window_length = 80;
static int window_height = 24;

/* 8-bit translate table editor -------------------------------------------- */

/**
 * Keyboard handler for the Alt-A translation table editor screen.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
void translate_table_editor_8bit_keyboard_handler(const int keystroke,
                                                  const int flags) {
    int row;
    int col;
    int new_keystroke;
    wchar_t * value;
    char buffer[5];
    struct file_info * new_file;

    col = (selected_entry % 128) / 16;
    row = (selected_entry % 128) % 16;

    switch (keystroke) {

    case 'S':
    case 's':
        if (editing_entry == Q_FALSE) {
            /*
             * Save
             */
            if (editing_table == INPUT_8BIT) {
                copy_table_8bit(&editing_table_8bit,
                    &table_8bit_input);
            } else {
                copy_table_8bit(&editing_table_8bit,
                    &table_8bit_output);
            }
            save_translate_tables_8bit(editing_table_8bit_filename,
                &table_8bit_input, &table_8bit_output);

            /*
             * Editing form is already deleted, so just escape out
             */
            q_screen_dirty = Q_TRUE;
            console_refresh(Q_FALSE);
            switch_state(Q_STATE_TRANSLATE_MENU);
        }
        return;

    case 'L':
    case 'l':
        if (editing_entry == Q_FALSE) {
            /*
             * Load a new translation pair from file
             */
            new_file = view_directory(q_home_directory, "*.xl8");
            /*
             * Explicitly freshen the background console image
             */
            q_screen_dirty = Q_TRUE;
            console_refresh(Q_FALSE);
            if (new_file != NULL) {
                /*
                 * We call basename() which is normally a bad thing to do.
                 * But we're only one line away from tossing new_filename
                 * anyway.
                 */
                use_translate_table_8bit(basename(new_file->name));
                if (editing_table == INPUT_8BIT) {
                    copy_table_8bit(&table_8bit_input, &editing_table_8bit);
                } else {
                    copy_table_8bit(&table_8bit_output, &editing_table_8bit);
                }
                Xfree(new_file->name, __FILE__, __LINE__);
                Xfree(new_file, __FILE__, __LINE__);
            } else {
                /*
                 * Nothing to do.
                 */
            }
            q_screen_dirty = Q_TRUE;
        }
        return;

    default:
        break;
    }

    switch (keystroke) {

    case Q_KEY_F(1):
        launch_help(Q_HELP_TRANSLATE_EDITOR);
        console_refresh(Q_FALSE);
        q_screen_dirty = Q_TRUE;
        return;

    case '`':
        /*
         * Backtick works too
         */
    case Q_KEY_ESCAPE:
        /*
         * ESC return to TERMINAL mode
         */
        if (editing_entry == Q_TRUE) {
            editing_entry = Q_FALSE;
            q_cursor_off();

            /*
             * Delete the editing form
             */
            fieldset_free(edit_table_entry_form);
            screen_delwin(edit_table_entry_window);

        } else {

            if (saved_changes == Q_FALSE) {
                /*
                 * Ask if the user wants to save changes.
                 */
                new_keystroke = notify_prompt_form(_("Attention!"),
                    _("Changes have been made!  Save them? [Y/n] "),
                    _(" Y-Save Changes   N-Exit "),
                    Q_TRUE, 0.0,
                    "YyNn\r");
                new_keystroke = q_tolower(new_keystroke);

                /*
                 * Save if the user said so
                 */
                if ((new_keystroke == 'y') || (new_keystroke == Q_KEY_ENTER)) {
                    if (editing_table == INPUT_8BIT) {
                        copy_table_8bit(&editing_table_8bit,
                            &table_8bit_input);
                    } else {
                        copy_table_8bit(&editing_table_8bit,
                            &table_8bit_output);
                    }
                    save_translate_tables_8bit(editing_table_8bit_filename,
                        &table_8bit_input, &table_8bit_output);
                } else {
                    /*
                     * Abandon changes, do nothing.
                     */
                }

            }

            /*
             * Editing form is already deleted, so just escape out
             */
            q_screen_dirty = Q_TRUE;
            console_refresh(Q_FALSE);
            switch_state(Q_STATE_TRANSLATE_MENU);
        }

        q_screen_dirty = Q_TRUE;
        return;

    case Q_KEY_DOWN:
        if (editing_entry == Q_FALSE) {
            if (row < 15) {
                selected_entry++;
            }
            q_screen_dirty = Q_TRUE;
        }
        return;

    case Q_KEY_UP:
        if (editing_entry == Q_FALSE) {
            if (row > 0) {
                selected_entry--;
            }
            q_screen_dirty = Q_TRUE;
        }
        return;

    case Q_KEY_LEFT:
        if (editing_entry == Q_FALSE) {
            if (col > 0) {
                selected_entry -= 16;
            }
            q_screen_dirty = Q_TRUE;
        } else {
            fieldset_left(edit_table_entry_form);
        }
        return;

    case Q_KEY_RIGHT:
        if (editing_entry == Q_FALSE) {
            if (col < 7) {
                selected_entry += 16;
            }
            q_screen_dirty = Q_TRUE;
        } else {
            fieldset_right(edit_table_entry_form);
        }
        return;

    case Q_KEY_BACKSPACE:
        if (editing_entry == Q_TRUE) {
            fieldset_backspace(edit_table_entry_form);
        }
        return;

    case Q_KEY_HOME:
        if (editing_entry == Q_TRUE) {
            fieldset_home_char(edit_table_entry_form);
        }
        return;

    case Q_KEY_END:
        if (editing_entry == Q_TRUE) {
            fieldset_end_char(edit_table_entry_form);
        }
        return;

    case Q_KEY_DC:
        if (editing_entry == Q_TRUE) {
            fieldset_delete_char(edit_table_entry_form);
        }
        return;

    case Q_KEY_IC:
        if (editing_entry == Q_TRUE) {
            fieldset_insert_char(edit_table_entry_form);
        }
        return;

    case Q_KEY_ENTER:
        if (editing_entry == Q_FALSE) {
            /*
             * ENTER - Begin editing
             */
            editing_entry = Q_TRUE;
            break;
        } else {
            /*
             * The OK exit point
             */
            value = field_get_value(edit_table_entry_field);
            editing_table_8bit.map_to[selected_entry] =
                        (unsigned char) wcstoul(value, NULL, 10);
            Xfree(value, __FILE__, __LINE__);

            saved_changes = Q_FALSE;

            fieldset_free(edit_table_entry_form);
            screen_delwin(edit_table_entry_window);
            editing_entry = Q_FALSE;
            q_cursor_off();
        }
        q_screen_dirty = Q_TRUE;
        return;

    case Q_KEY_PPAGE:
    case Q_KEY_NPAGE:
        /*
         * Switch editing tables
         */
        if (editing_entry == Q_FALSE) {
            if (editing_high_128 == Q_TRUE) {
                editing_high_128 = Q_FALSE;
                selected_entry = 0;
            } else {
                editing_high_128 = Q_TRUE;
                selected_entry = 128;
            }
            q_screen_dirty = Q_TRUE;
            return;
        }
        break;

    case ' ':
        /*
         * Ignore.  We either switched into editing mode, was already editing
         * and spacebar should not be passed to the form field anyway.
         */
        return;

    default:
        /*
         * Pass to form handler
         */
        if (editing_entry == Q_TRUE) {
            if (!q_key_code_yes(keystroke)) {
                if (q_isdigit(keystroke)) {
                    /*
                     * Pass only digit keys to field
                     */
                    fieldset_keystroke(edit_table_entry_form, keystroke);
                }
            }
        }

        /*
         * Return here.  The logic below the switch is all about switching
         * the editing key.
         */
        return;
    }

    /*
     * If we got here, the user hit Enter to begin editing a key.
     */
    if (selected_entry < 10) {
        edit_table_entry_window =
            screen_subwin(1, 3, window_top + window_height - 3,
                          window_left + 49);
    } else if (selected_entry < 100) {
        edit_table_entry_window =
            screen_subwin(1, 3, window_top + window_height - 3,
                          window_left + 50);
    } else {
        edit_table_entry_window =
            screen_subwin(1, 3, window_top + window_height - 3,
                          window_left + 51);
    }
    if (check_subwin_result(edit_table_entry_window) == Q_FALSE) {
        editing_entry = Q_FALSE;
        q_cursor_off();
        q_screen_dirty = Q_TRUE;
        return;
    }

    edit_table_entry_field = field_malloc(3, 0, 0, Q_TRUE,
                                          Q_COLOR_WINDOW_FIELD_TEXT_HIGHLIGHTED,
                                          Q_COLOR_WINDOW_FIELD_HIGHLIGHTED);
    edit_table_entry_form =
        fieldset_malloc(&edit_table_entry_field, 1, edit_table_entry_window);

    screen_put_color_printf_yx(window_top + window_height - 3, window_left + 25,
                               Q_COLOR_MENU_COMMAND,
                               _("Enter new value for %d >"), selected_entry);

    snprintf(buffer, sizeof(buffer), "%d",
        editing_table_8bit.map_to[selected_entry]);
    field_set_char_value(edit_table_entry_field, buffer);

    /*
     * Render everything above the edit field.
     */
    screen_flush();

    /*
     * Render the field.  Must be called AFTER screen_flush() to put the
     * cursor on the right spot.
     */
    q_cursor_on();
    fieldset_render(edit_table_entry_form);

    screen_flush();
}

/**
 * Draw screen for the Alt-A translation table editor screen.
 */
void translate_table_editor_8bit_refresh() {
    char * status_string;
    int status_left_stop;
    int title_left;
    char * title;
    int i, end_i;
    int row, col;
    unsigned char selected_mapped;

    window_left = (WIDTH - window_length) / 2;
    window_top = (HEIGHT - window_height) / 2;

    if (q_screen_dirty == Q_FALSE) {
        return;
    }

    /*
     * Clear screen for when it resizes
     */
    console_refresh(Q_FALSE);

    screen_draw_box(window_left, window_top, window_left + window_length,
                    window_top + window_height);

    if (editing_table == INPUT_8BIT) {
        title = _("8-Bit INPUT Strip/Replace Table");
    } else {
        assert(editing_table == OUTPUT_8BIT);
        title = _("8-Bit OUTPUT Strip/Replace Table");
    }
    title_left = window_length - (strlen(title) + 2);
    if (title_left < 0) {
        title_left = 0;
    } else {
        title_left /= 2;
    }
    screen_put_color_printf_yx(window_top, window_left + title_left,
                               Q_COLOR_WINDOW_BORDER, " %s ", title);
    screen_put_color_str_yx(window_top + window_height - 1,
                            window_left + window_length - 10, _("F1 Help"),
                            Q_COLOR_WINDOW_BORDER);

    screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH,
                              Q_COLOR_STATUS);
    if (editing_entry == Q_FALSE) {
        status_string = _(" ARROWS/PgUp/PgDn-Movement   ENTER-Change   L-Load   S-Save   ESC/`-Exit ");
    } else {
        status_string = _(" ENTER-Save Changes  ESC/`-Exit ");
    }
    status_left_stop = WIDTH - strlen(status_string);
    if (status_left_stop <= 0) {
        status_left_stop = 0;
    } else {
        status_left_stop /= 2;
    }
    screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string,
                            Q_COLOR_STATUS);

    screen_put_color_str_yx(window_top + 1, window_left + 3,
                            _("File: "), Q_COLOR_MENU_TEXT);
    screen_put_color_str(editing_table_8bit_filename, Q_COLOR_MENU_COMMAND);

    screen_put_color_str_yx(window_top + 2, window_left + 3,
                            _("Display Codepage: "), Q_COLOR_MENU_TEXT);
    if (q_status.codepage == Q_CODEPAGE_DEC) {
        screen_put_color_str(codepage_string(Q_CODEPAGE_CP437),
                             Q_COLOR_MENU_COMMAND);
    } else {
        screen_put_color_str(codepage_string(q_status.codepage),
                             Q_COLOR_MENU_COMMAND);
    }

    screen_put_color_str_yx(window_top + 3, window_left + 21,
                            _("In Character | |  Out Character | |"),
                            Q_COLOR_MENU_TEXT);
    if ((selected_entry < 0x20) ||
        ((selected_entry >= 0x7F) && (selected_entry < 0xA0)) ||
        (q_status.codepage == Q_CODEPAGE_DEC)
    ) {
        /*
         * Always use CP437 for C0 and C1 control characters, or when
         * codepage is DEC.
         */
        screen_put_color_char_yx(window_top + 3, window_left + 21 + 14,
                                 cp437_chars[selected_entry & 0xFF],
                                 Q_COLOR_MENU_COMMAND);
    } else {
        screen_put_color_char_yx(window_top + 3, window_left + 21 + 14,
                                 codepage_map_char(selected_entry & 0xFF),
                                 Q_COLOR_MENU_COMMAND);
    }

    selected_mapped = editing_table_8bit.map_to[selected_entry & 0xFF];
    if ((selected_mapped < 0x20) ||
        ((selected_mapped >= 0x7F) && (selected_mapped < 0xA0)) ||
        (q_status.codepage == Q_CODEPAGE_DEC)
    ) {
        /*
         * Always use CP437 for C0 and C1 control characters, or when
         * codepage is DEC.
         */
        screen_put_color_char_yx(window_top + 3, window_left + 21 + 33,
                                 cp437_chars[selected_mapped],
                                 Q_COLOR_MENU_COMMAND);
    } else {
        screen_put_color_char_yx(window_top + 3, window_left + 21 + 33,
                                 codepage_map_char(selected_mapped),
                                 Q_COLOR_MENU_COMMAND);
    }


    if (editing_high_128) {
        i = 128;
    } else {
        i = 0;
    }
    end_i = i + 128;

    for (; i < end_i; i++) {
        col = (i % 128) / 16;
        row = (i % 128) % 16;

        if (i == selected_entry) {
            screen_put_color_printf_yx(window_top + 4 + row,
                                       window_left + 3 + (col * 9),
                                       Q_COLOR_MENU_COMMAND, "[%3d-%3d]", i,
                                       editing_table_8bit.map_to[i]);
        } else {
            screen_put_color_printf_yx(window_top + 4 + row,
                                       window_left + 3 + (col * 9),
                                       Q_COLOR_MENU_TEXT, " %3d-%3d ", i,
                                       editing_table_8bit.map_to[i]);
        }
    }

    q_screen_dirty = Q_FALSE;
    screen_flush();
}

/* Unicode translate table editor ------------------------------------------ */

/**
 * Keyboard handler for the Alt-A translation table editor screen.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
void translate_table_editor_unicode_keyboard_handler(const int keystroke,
                                                     const int flags) {
    int row;
    int col;
    int new_keystroke;
    wchar_t * value;
    char buffer[16];
    struct file_info * new_file;
    static wchar_t * search_string = NULL;
    char * search_string_char = NULL;
    wchar_t search_value;
    char * endptr;
    int i;

    col  = (selected_entry % 96) / 16;
    row  = (selected_entry % 96) % 16;

    switch (keystroke) {

    case 'S':
    case 's':
        /*
         * Save
         */
        if (editing_entry == Q_FALSE) {
            /*
             * Save
             */
            if (editing_table == INPUT_UNICODE) {
                copy_table_unicode(&editing_table_unicode,
                    &table_unicode_input);
            } else {
                copy_table_unicode(&editing_table_unicode,
                    &table_unicode_output);
            }
            save_translate_tables_unicode(editing_table_unicode_filename,
                &table_unicode_input, &table_unicode_output);

            /*
             * Editing form is already deleted, so just escape out
             */
            q_screen_dirty = Q_TRUE;
            console_refresh(Q_FALSE);
            switch_state(Q_STATE_TRANSLATE_MENU);
        }
        break;

    case 'L':
    case 'l':
        if (editing_entry == Q_FALSE) {
            /*
             * Load a new translation pair from file
             */
            new_file = view_directory(q_home_directory, "*.xlu");
            /*
             * Explicitly freshen the background console image
             */
            q_screen_dirty = Q_TRUE;
            console_refresh(Q_FALSE);
            if (new_file != NULL) {
                /*
                 * We call basename() which is normally a bad thing to do.
                 * But we're only one line away from tossing new_filename
                 * anyway.
                 */
                use_translate_table_unicode(basename(new_file->name));
                if (editing_table == INPUT_UNICODE) {
                    copy_table_unicode(&table_unicode_input,
                        &editing_table_unicode);
                } else {
                    copy_table_unicode(&table_unicode_output,
                        &editing_table_unicode);
                }
                Xfree(new_file->name, __FILE__, __LINE__);
                Xfree(new_file, __FILE__, __LINE__);
            } else {
                /*
                 * Nothing to do.
                 */
            }
            q_screen_dirty = Q_TRUE;
        }
        return;

    case 'F':
    case 'f':
        if (editing_entry == Q_FALSE) {
            /*
             * Find
             */
            if (search_string != NULL) {
                Xfree(search_string, __FILE__, __LINE__);
                search_string = NULL;
            }
            q_cursor_on();
            search_string = pick_find_string();
            q_cursor_off();
            if (search_string == NULL) {
                break;
            }

            assert(search_string_char == NULL);
            search_string_char = (char *) Xmalloc(
                sizeof(char) * (wcslen(search_string) + 1), __FILE__, __LINE__);
            for (i = 0; i < wcslen(search_string); i++) {
                search_string_char[i] = search_string[i] & 0x7F;
            }
            search_string_char[wcslen(search_string)] = 0;

            /*
             * Search for the first matching entry
             */
            search_value = (wchar_t) strtoul(search_string_char, &endptr, 16);
            if (*endptr != 0) {
                /*
                 * Invalid number.
                 */
            } else {
                selected_entry = search_value;
            }
            Xfree(search_string_char, __FILE__, __LINE__);
            search_string_char = NULL;

            q_screen_dirty = Q_TRUE;
            return;
        }

        /*
         * Break here so that the form handler can see 'f', it is a valid hex
         * character.
         */
        break;

    default:
        break;
    }

    switch (keystroke) {

    case Q_KEY_F(1):
        launch_help(Q_HELP_TRANSLATE_EDITOR);
        console_refresh(Q_FALSE);
        q_screen_dirty = Q_TRUE;
        return;

    case '`':
        /*
         * Backtick works too
         */
    case Q_KEY_ESCAPE:
        /*
         * ESC return to TERMINAL mode
         */
        if (editing_entry == Q_TRUE) {
            editing_entry = Q_FALSE;
            q_cursor_off();

            /*
             * Delete the editing form
             */
            fieldset_free(edit_table_entry_form);
            screen_delwin(edit_table_entry_window);

        } else {

            if (saved_changes == Q_FALSE) {
                /*
                 * Ask if the user wants to save changes.
                 */
                new_keystroke = notify_prompt_form(_("Attention!"),
                    _("Changes have been made!  Save them? [Y/n] "),
                    _(" Y-Save Changes   N-Exit "),
                    Q_TRUE, 0.0,
                    "YyNn\r");
                new_keystroke = q_tolower(new_keystroke);

                /*
                 * Save if the user said so
                 */
                if ((new_keystroke == 'y') || (new_keystroke == Q_KEY_ENTER)) {
                    if (editing_table == INPUT_UNICODE) {
                        copy_table_unicode(&editing_table_unicode,
                            &table_unicode_input);
                    } else {
                        copy_table_unicode(&editing_table_unicode,
                            &table_unicode_output);
                    }
                    save_translate_tables_unicode(
                        editing_table_unicode_filename,
                        &table_unicode_input, &table_unicode_output);
                } else {
                    /*
                     * Abandon changes
                     */
                }

            }

            /*
             * Editing form is already deleted, so just escape out
             */
            q_screen_dirty = Q_TRUE;
            console_refresh(Q_FALSE);
            switch_state(Q_STATE_TRANSLATE_MENU);
        }

        q_screen_dirty = Q_TRUE;
        return;

    case Q_KEY_DOWN:
        if (editing_entry == Q_FALSE) {
            if (row < 15) {
                selected_entry++;
            }
            q_screen_dirty = Q_TRUE;
        }
        return;

    case Q_KEY_UP:
        if (editing_entry == Q_FALSE) {
            if (row > 0) {
                selected_entry--;
            }
            q_screen_dirty = Q_TRUE;
        }
        return;

    case Q_KEY_LEFT:
        if (editing_entry == Q_FALSE) {
            if (col > 0) {
                selected_entry -= 16;
            }
            q_screen_dirty = Q_TRUE;
        } else {
            fieldset_left(edit_table_entry_form);
        }
        return;

    case Q_KEY_RIGHT:
        if (editing_entry == Q_FALSE) {
            if ((col < 5) && (selected_entry <= 0xFFFF - 16)) {
                selected_entry += 16;
            }
            q_screen_dirty = Q_TRUE;
        } else {
            fieldset_right(edit_table_entry_form);
        }
        return;

    case Q_KEY_NPAGE:
        if (editing_entry == Q_FALSE) {
            if (selected_entry + 96 > 0xFFFF) {
                selected_entry = 0;
            } else {
                selected_entry += 96;
            }
            q_screen_dirty = Q_TRUE;
        }
        return;

    case Q_KEY_PPAGE:
        if (editing_entry == Q_FALSE) {
            if (selected_entry - 96 < 0) {
                selected_entry = 0x10000 - (0x10000 % 96);
            } else {
                selected_entry -= 96;
            }
            q_screen_dirty = Q_TRUE;
        }
        return;

    case Q_KEY_BACKSPACE:
        if (editing_entry == Q_TRUE) {
            fieldset_backspace(edit_table_entry_form);
        }
        return;

    case Q_KEY_HOME:
        if (editing_entry == Q_TRUE) {
            fieldset_home_char(edit_table_entry_form);
        }
        return;

    case Q_KEY_END:
        if (editing_entry == Q_TRUE) {
            fieldset_end_char(edit_table_entry_form);
        }
        return;

    case Q_KEY_DC:
        if (editing_entry == Q_TRUE) {
            fieldset_delete_char(edit_table_entry_form);
        }
        return;

    case Q_KEY_IC:
        if (editing_entry == Q_TRUE) {
            fieldset_insert_char(edit_table_entry_form);
        }
        return;

    case Q_KEY_ENTER:
        if (editing_entry == Q_FALSE) {
            /*
             * ENTER - Begin editing
             */
            editing_entry = Q_TRUE;
            break;
        } else {
            /*
             * The OK exit point
             */
            value = field_get_value(edit_table_entry_field);
            unicode_table_set(&editing_table_unicode, selected_entry,
                wcstoul(value, NULL, 16));
            Xfree(value, __FILE__, __LINE__);

            saved_changes = Q_FALSE;

            fieldset_free(edit_table_entry_form);
            screen_delwin(edit_table_entry_window);
            editing_entry = Q_FALSE;
            q_cursor_off();
        }
        q_screen_dirty = Q_TRUE;
        return;

    case ' ':
        /*
         * Ignore.  We either switched into editing mode, was already editing
         * and spacebar should not be passed to the form field anyway.
         */
        return;

    default:
        /*
         * Pass to form handler
         */
        if (editing_entry == Q_TRUE) {
            if (!q_key_code_yes(keystroke)) {
                if (q_isdigit(keystroke) ||
                    ((keystroke >= 'A') && (keystroke <= 'F')) ||
                    ((keystroke >= 'a') && (keystroke <= 'f'))
                ) {
                    /*
                     * Pass only digit keys to field
                     */
                    fieldset_keystroke(edit_table_entry_form, keystroke);
                }
            }
        }

        /*
         * Return here.  The logic below the switch is all about switching
         * the editing key.
         */
        return;
    }

    /*
     * If we got here, the user hit Enter to begin editing a key.
     */
    edit_table_entry_window = screen_subwin(1, 4,
        window_top + window_height - 3, window_left + 52);
    if (check_subwin_result(edit_table_entry_window) == Q_FALSE) {
        editing_entry = Q_FALSE;
        q_cursor_off();
        q_screen_dirty = Q_TRUE;
        return;
    }

    edit_table_entry_field = field_malloc(4, 0, 0, Q_TRUE,
                                          Q_COLOR_WINDOW_FIELD_TEXT_HIGHLIGHTED,
                                          Q_COLOR_WINDOW_FIELD_HIGHLIGHTED);
    edit_table_entry_form =
        fieldset_malloc(&edit_table_entry_field, 1, edit_table_entry_window);

    screen_put_color_printf_yx(window_top + window_height - 3, window_left + 25,
                               Q_COLOR_MENU_COMMAND,
                               _("Enter new value for %04x >"), selected_entry);

    snprintf(buffer, sizeof(buffer), "%04x",
        (uint16_t) unicode_table_get(&editing_table_unicode, selected_entry));
    field_set_char_value(edit_table_entry_field, buffer);

    /*
     * Render everything above the edit field.
     */
    screen_flush();

    /*
     * Render the field.  Must be called AFTER screen_flush() to put the
     * cursor on the right spot.
     */
    q_cursor_on();
    fieldset_render(edit_table_entry_form);

    screen_flush();
}

/**
 * Draw screen for the Alt-A translation table editor screen.
 */
void translate_table_editor_unicode_refresh() {
    char * status_string;
    int status_left_stop;
    int title_left;
    char * title;
    int i, end_i;
    int row, col;
    wchar_t selected_mapped;

    window_left = (WIDTH - window_length) / 2;
    window_top = (HEIGHT - window_height) / 2;

    if (q_screen_dirty == Q_FALSE) {
        return;
    }

    /*
     * Clear screen for when it resizes
     */
    console_refresh(Q_FALSE);

    screen_draw_box(window_left, window_top, window_left + window_length,
                    window_top + window_height);

    if (editing_table == INPUT_UNICODE) {
        title = _("Unicode INPUT Strip/Replace Table");
    } else {
        assert(editing_table == OUTPUT_UNICODE);
        title = _("Unicode OUTPUT Strip/Replace Table");
    }
    title_left = window_length - (strlen(title) + 2);
    if (title_left < 0) {
        title_left = 0;
    } else {
        title_left /= 2;
    }
    screen_put_color_printf_yx(window_top, window_left + title_left,
                               Q_COLOR_WINDOW_BORDER, " %s ", title);
    screen_put_color_str_yx(window_top + window_height - 1,
                            window_left + window_length - 10, _("F1 Help"),
                            Q_COLOR_WINDOW_BORDER);

    screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH,
                              Q_COLOR_STATUS);
    if (editing_entry == Q_FALSE) {
        status_string = _(" ARROWS/PgUp/PgDn-Movement  F-Find  ENTER-Change  L-Load  S-Save  ESC/`-Exit ");
    } else {
        status_string = _(" ENTER-Save Changes  ESC/`-Exit ");
    }
    status_left_stop = WIDTH - strlen(status_string);
    if (status_left_stop <= 0) {
        status_left_stop = 0;
    } else {
        status_left_stop /= 2;
    }
    screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string,
                            Q_COLOR_STATUS);

    screen_put_color_str_yx(window_top + 1, window_left + 3,
                            _("File: "), Q_COLOR_MENU_TEXT);
    screen_put_color_str(editing_table_unicode_filename, Q_COLOR_MENU_COMMAND);

    screen_put_color_str_yx(window_top + 2, window_left + 3,
                            _("Display Codepage: "), Q_COLOR_MENU_TEXT);
    /* Note "UNICODE" is not translated. */
    screen_put_color_str("UNICODE", Q_COLOR_MENU_COMMAND);

    screen_put_color_str_yx(window_top + 3, window_left + 21,
                            _("In Character | |  Out Character | |"),
                            Q_COLOR_MENU_TEXT);

    if ((selected_entry < 0x20) ||
        ((selected_entry >= 0x7F) && (selected_entry < 0xA0))
    ) {
        /*
         * Always use CP437 for C0 and C1 control characters.
         */
        screen_put_color_char_yx(window_top + 3, window_left + 21 + 14,
                                 cp437_chars[selected_entry & 0xFF],
                                 Q_COLOR_MENU_COMMAND);
    } else {
        screen_put_color_char_yx(window_top + 3, window_left + 21 + 14,
                                 selected_entry, Q_COLOR_MENU_COMMAND);
    }

    selected_mapped = unicode_table_get(&editing_table_unicode, selected_entry);
    if ((selected_mapped < 0x20) ||
        ((selected_mapped >= 0x7F) && (selected_mapped < 0xA0))
    ) {
        /*
         * Always use CP437 for C0 and C1 control characters.
         */
        screen_put_color_char_yx(window_top + 3, window_left + 21 + 33,
                                 cp437_chars[selected_mapped & 0xFF],
                                 Q_COLOR_MENU_COMMAND);
    } else {
        screen_put_color_char_yx(window_top + 3, window_left + 21 + 33,
                                 selected_mapped, Q_COLOR_MENU_COMMAND);
    }

    i = selected_entry - (selected_entry % 96);
    if (selected_entry >= (0x10000 - 64)) {
        end_i = i + 64;
    } else {
        end_i = i + 96;
    }

    for (; i < end_i; i++) {
        col  = (i % 96) / 16;
        row  = (i % 96) % 16;
        selected_mapped = unicode_table_get(&editing_table_unicode, i);

        if (i == selected_entry) {
            screen_put_color_printf_yx(window_top + 4 + row,
                                       window_left + 3 + (col * 12),
                                       Q_COLOR_MENU_COMMAND, "[%04x-%04x]",
                                       (uint16_t) i,
                                       (uint16_t) selected_mapped);
        } else {
            screen_put_color_printf_yx(window_top + 4 + row,
                                       window_left + 3 + (col * 12),
                                       Q_COLOR_MENU_TEXT, " %04x-%04x ",
                                       (uint16_t) i,
                                       (uint16_t) selected_mapped);
        }
    }

    q_screen_dirty = Q_FALSE;
    screen_flush();
}
