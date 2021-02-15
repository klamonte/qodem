/*
 * help.c
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

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include "qodem.h"
#include "console.h"
#include "screen.h"
#include "forms.h"
#include "options.h"
#include "help.h"

/* Set this to a not-NULL value to enable debug log. */
/* static const char * DLOGNAME = "help"; */
static const char * DLOGNAME = NULL;

/* Forward declaration for raw help text.  It is located at the bottom. */
extern char * raw_help_text;

/* Entry point topic keys */
static char * HELP_NOP_KEY = "HELP_NOP";
static char * HELP_INDEX_KEY = "HELP_INDEX";
static char * HELP_PHONEBOOK_KEY = "PHONEBOOK";
static char * HELP_PHONEBOOK_REVISE_ENTRY_KEY = "PHONEBOOK_REVISE_ENTRY";
static char * HELP_CONSOLE__ALT6_BEW_KEY = "CONSOLE__ALT6_BEW";
#ifndef Q_NO_SERIAL
static char * HELP_CONSOLE__ALTO_MODEM_CFG_KEY = "CONSOLE__ALTO_MODEM_CFG";
static char * HELP_CONSOLE__ALTY_COMM_PARMS_KEY = "CONSOLE__ALTY_COMM_PARMS";
#endif
static char * HELP_CONSOLE_MENU_KEY = "CONSOLE_MENU";
static char * HELP_FILE_PROTOCOLS_KEY = "FILE_PROTOCOLS";
static char * HELP_EMULATION_KEY = "EMULATION";
static char * HELP_CONSOLE__ALTA_TRANSLATE_KEY = "CONSOLE__ALTA_TRANSLATE";
static char * HELP_CODEPAGE_KEY = "CODEPAGE";
static char * HELP_CONFIGURATION_KEY = "CONFIGURATION";
static char * HELP_FUNCTION_KEYS_KEY = "FUNCTION_KEYS";

#if __WCHAR_MAX__ > 0x10000
/*
 * 32-bit wchar_t: we use the Private Use Area Plane of Unicode to flag bold
 * text.
 */
#define HELP_BOLD 0x100000
#else
/*
 * 16-bit wchar_t: we use the last available high bit to flag bold text.
 * This means that we cannot use Unicode code points above 0x8000.  So far,
 * we do not: they are used for arrow forms and suchlike in the 0x2000-0x2FFF
 * range.
 */
#define HELP_BOLD 0x8000
#endif

/*
 * A hyperlink to another help topic.
 */
struct help_link {
    wchar_t * label;
    char * topic_key;
    int line_number;
    int x;
};

/*
 * A help topic.
 */
struct help_topic {
    char * key;
    wchar_t * title;
    wchar_t ** lines;
    int lines_n;
    struct help_link ** links;
    int links_n;
    struct help_topic * next;
};

/* The global list of help topics. */
static struct help_topic * TOPICS = NULL;

/**
 * Find a topic in the list.
 *
 * @param key the topic key
 * @return the help_topic entry
 */
static struct help_topic * find_topic(const char * key) {
    struct help_topic *topic = TOPICS;
    DLOG(("find_topic: look for %s\n", key));
    while (topic != NULL) {
        if (strcmp(topic->key, key) == 0) {
            DLOG(("find_topic: found %ls\n", topic->title));
            return topic;
        }
        topic = topic->next;
    }

    DLOG(("find_topic: NOT FOUND\n"));
    return NULL;
}

/**
 * Allocate a new help_link.
 *
 * @return the new help_link
 */
static struct help_link * new_link() {
    struct help_link * link;
    DLOG(("new_link() : "));
    link =
        (struct help_link *) Xmalloc(sizeof(struct help_link), __FILE__,
                                     __LINE__);
    memset(link, 0, sizeof(struct help_link));
    DLOG2(("%p\n", link));
    return link;
}

/**
 * Allocate a new topic and add it to the list of topics.
 *
 * @return the new help_topic
 */
struct help_topic * new_topic() {
    struct help_topic * topic;
    DLOG(("new_topic() : "));
    topic =
        (struct help_topic *) Xmalloc(sizeof(struct help_topic), __FILE__,
                                      __LINE__);
    memset(topic, 0, sizeof(struct help_topic));

    /*
     * Prepend to list of topics.
     */
    if (TOPICS == NULL) {
        TOPICS = topic;
    } else {
        topic->next = TOPICS;
        TOPICS = topic;
    }

    DLOG2(("%p\n", topic));
    return topic;
}

/**
 * Add one line to a topic.
 *
 * @param topic the topic to add to
 * @param line the new line of help text
 */
static void append_line(struct help_topic * topic, wchar_t * line) {

    if (topic == NULL) {
        DLOG(("append_line() topic = NULL, line = \'%ls\'\n", line));
    } else {
        DLOG(("append_line() topic = %s, line = \'%ls\'\n",
             topic->key, line));
    }

    if (topic == NULL) {
        /*
         * No topics yet, throw this away
         */
        return;
    }
    assert(wcslen(line) <= 76);
    if (wcslen(line) > 76) {
        line[76] = 0;
        line[77] = 0;
        line[78] = 0;
        line[79] = 0;
    }
    topic->lines = (wchar_t **) Xrealloc(topic->lines,
                                         sizeof(wchar_t *) * (topic->lines_n +
                                                              1), __FILE__,
                                         __LINE__);
    topic->lines[topic->lines_n] = Xwcsdup(line, __FILE__, __LINE__);
    topic->lines_n++;
}

/**
 * Add one link to a topic.
 *
 * @param topic the topic to add to
 * @param link the new link to another topic
 */
static void append_link(struct help_topic * topic, struct help_link * link) {

    if (topic == NULL) {
        DLOG(("append_link() topic = NULL\n"));
    } else {
        DLOG(("append_link() topic = %s\n", topic->key));
    }


    if (topic == NULL) {
        /*
         * No topics yet, throw this away
         */
        return;
    }
    assert(wcslen(link->label) <= 76);
    topic->links = (struct help_link **) Xrealloc(topic->links,
                                                  sizeof(struct help_link *) *
                                                  (topic->links_n + 1),
                                                  __FILE__, __LINE__);
    topic->links[topic->links_n] = link;
    topic->links_n++;
}

/**
 * Convert a line of help text from raw ASCII to wide char, using the &#XXXX;
 * notation for Unicode.
 *
 * @param input the ASCII text
 * @param output the wide char line to write to
 */
static void convert_unicode(char * input, wchar_t * output) {
    enum STATES {
        NORMAL,
        U_1,
        U_2,
        U_DIGITS,
        U_HEX
    } state;
    int input_i;
    int input_n;
    int output_i;
    char ch;
    wchar_t x = 0;

    DLOG(("convert_unicode() input '%s'\n", input));

    state = NORMAL;
    input_i = 0;
    output_i = 0;
    input_n = strlen(input);

    while (input_i < input_n) {
        ch = input[input_i];
        input_i++;
        DLOG(("   state %d ch %lc\n", state, ch));

        switch (state) {
        case NORMAL:
            /*
             * Looking for any char or '&'
             */
            if (ch == '&') {
                state = U_1;
            } else {
                output[output_i] = ch;
                output_i++;
            }
            break;
        case U_1:
            /*
             * Looking for '#'
             */
            if (ch == '#') {
                state = U_2;
                x = 0;
            } else {
                output[output_i] = '&';
                output_i++;
                output[output_i] = ch;
                output_i++;
                state = NORMAL;
            }
            break;
        case U_2:
            /*
             * Looking for 'x' or digits
             */
            if (tolower(ch) == 'x') {
                state = U_HEX;
            } else if ((ch >= '0') && (ch <= '9')) {
                state = U_DIGITS;
                x = ch - '0';
            } else {
                output[output_i] = '&';
                output_i++;
                output[output_i] = '#';
                output_i++;
                output[output_i] = ch;
                output_i++;
                state = NORMAL;
            }
            break;
        case U_DIGITS:
            /*
             * Looking for digits or ';'
             */
            if ((ch >= '0') && (ch <= '9')) {
                x *= 10;
                x += (ch - '0');
            } else if (ch == ';') {
                output[output_i] = x;
                output_i++;
                state = NORMAL;
            } else {
                /*
                 * This is a bug. Bail out.
                 */
                abort();
            }
            break;
        case U_HEX:
            /*
             * Looking for digits, [a-f] or ';'
             */
            if ((ch >= '0') && (ch <= '9')) {
                x *= 16;
                x += (ch - '0');
            } else if ((tolower(ch) >= 'a') && (tolower(ch) <= 'f')) {
                x *= 16;
                x += (tolower(ch) - 'a') + 10;
            } else if (ch == ';') {
                output[output_i] = x;
                output_i++;
                state = NORMAL;
            } else {
                /*
                 * This is a bug. Bail out.
                 */
                abort();
            }
            break;
        }
    }
    DLOG(("convert_unicode() output '%ls'\n", output));
}

/**
 * Emit a topic to the debug file.
 *
 * @param topic the topid
 */
static void debug_topic(struct help_topic * topic) {

    int i;
    struct help_link * link;

    DLOG(("---- HELP TOPIC DEBUG ----\n"));
    DLOG((" KEY: %s\n", topic->key));
    DLOG((" TITLE: %ls\n", topic->title));
    DLOG((" TEXT:\n"));
    for (i = 0; i < topic->lines_n; i++) {
        DLOG((" %02d | %ls |\n", i, topic->lines[i]));
    }

    DLOG((" LINKS:\n"));
    for (i = 0; i < topic->links_n; i++) {
        link = topic->links[i];
        DLOG((" (%02d,%d)  \'%ls\' --> %s\n",
                link->line_number, link->x, link->label, link->topic_key));
    }

    DLOG(("---- HELP TOPIC DEBUG ----\n"));


}

/**
 * Generate a help topic that has the text from the options configuratio
 * file.
 */
static void build_options_topic() {
    struct help_topic * topic;
    wchar_t line[80];
    char ch;
    unsigned int i;
    const char * begin;
    Q_OPTION option;
    int skip_line_count = 0;

    topic = find_topic(HELP_CONFIGURATION_KEY);
    assert(topic != NULL);
    for (option = Q_OPTION_NULL + 1; option < Q_OPTION_MAX; option++) {
        begin = get_option_key(option);
        if (begin == NULL) {
            continue;
        }

        /*
         * Newline
         */
        memset(line, 0, sizeof(line));
        append_line(topic, line);

        /*
         * Put in option key, bolded
         */
        for (i = 0; i < strlen(begin); i++) {
            line[i] = begin[i] | HELP_BOLD;
        }
        append_line(topic, line);

        /*
         * Pull default value
         */
        memset(line, 0, sizeof(line));
        begin = get_option_default(option);
        convert_unicode(_("  Default value: "), line);
        while (*begin != 0) {
            line[wcslen(line)] = *begin | HELP_BOLD;
            begin++;
        }
        append_line(topic, line);

        /*
         * Newline
         */
        memset(line, 0, sizeof(line));
        append_line(topic, line);

        /*
         * Pull description
         */
        memset(line, 0, sizeof(line));
        begin = get_option_description(option);
        ch = *begin;
        while (ch != 0) {
            if (ch == '\n') {
                if (skip_line_count > 0) {
                    memset(line, 0, sizeof(line));
                    skip_line_count--;
                } else if (wcsstr(line, L"--------") != NULL) {
                    /*
                     * Ignore lines with '--------', these are the section
                     * headers.
                     */
                    skip_line_count = 1;
                } else {
                    append_line(topic, line);
                }
                memset(line, 0, sizeof(line));
            } else if (ch == '#') {
                /*
                 * Convert the hash to indentation
                 */
                line[wcslen(line)] = ' ';
            } else {
                /*
                 * Hang onto this character
                 */
                line[wcslen(line)] = ch;
            }

            /*
             * Grab next character
             */
            begin++;
            ch = *begin;
        }
        /*
         * Last line
         */
        append_line(topic, line);
    }

}

/**
 * Generate a help index based on the existing topics and links.
 */
static void build_help_index() {
    struct help_topic * topic;
    struct help_topic * topic_index;
    struct help_link * link;
    struct help_link * link2;
    struct help_link ** index_links = NULL;
    int index_links_n = 0;
    int i, j;
    int line_number;

    /*
     * Make sure I don't accidentally create one by hand in the future.
     */
    topic = find_topic(HELP_INDEX_KEY);
    assert(topic == NULL);

    /*
     * Now begin...
     */
    topic = TOPICS;
    while (topic != NULL) {
        for (i = 0; i < topic->links_n; i++) {
            link = topic->links[i];
            link2 = new_link();
            link2->topic_key = link->topic_key;
            link2->label = NULL;
            link2->x = 0;

            /*
             * Add to index_links array
             */
            index_links = (struct help_link **) Xrealloc(index_links,
                                                         sizeof(struct help_link
                                                                *) *
                                                         (index_links_n + 1),
                                                         __FILE__, __LINE__);
            index_links[index_links_n] = link2;
            index_links_n++;
        }
        /*
         * Add the topic itself
         */
        link2 = new_link();
        link2->topic_key = topic->key;
        link2->label = NULL;
        link2->x = 0;

        /*
         * Add to index_links array
         */
        index_links = (struct help_link **) Xrealloc(index_links,
                                                     sizeof(struct help_link *)
                                                     * (index_links_n + 1),
                                                     __FILE__, __LINE__);
        index_links[index_links_n] = link2;
        index_links_n++;

        /*
         * Next topic
         */
        topic = topic->next;
    }


    DLOG(("HELP INDEX:\n"));
    for (i = 0; i < index_links_n; i++) {
        link = index_links[i];
        DLOG(("   %s\n", link->topic_key));
    }

    /*
     * Now I have an array of link pointers.  Sort and uniq it.
     */

    /*
     * In-place bubble sort
     */
    for (i = 0; i < index_links_n; i++) {
        for (j = i; j < index_links_n; j++) {
            link = index_links[i];
            link2 = index_links[j];
            if (strcmp(link->topic_key, link2->topic_key) > 0) {
                /*
                 * Swap
                 */
                index_links[i] = link2;
                index_links[j] = link;
            }
        }
    }

    DLOG(("HELP INDEX SORTED:\n"));
    for (i = 0; i < index_links_n; i++) {
        link = index_links[i];
        DLOG(("   %s\n", link->topic_key));
    }


    /*
     * In-place uniq
     */
    for (i = 0; i < index_links_n - 1; i++) {
        link = index_links[i];
        link2 = index_links[i + 1];
        if (strcmp(link->topic_key, link2->topic_key) == 0) {
            /*
             * Toss link2
             */
            memmove(&index_links[i], &index_links[i + 1],
                    sizeof(struct help_link *) * (index_links_n - i - 1));
            index_links_n--;
            if (i >= 0) {
                i--;
            }
        }
    }

    DLOG(("HELP INDEX UNIQ:\n"));
    for (i = 0; i < index_links_n; i++) {
        link = index_links[i];
        DLOG(("   %s\n", link->topic_key));
    }

    /*
     * Finally, build the index topic itself.
     */
    topic_index = new_topic();
    topic_index->key = HELP_INDEX_KEY;
    topic_index->title = L"Index";
    line_number = 0;

    for (i = 0; i < index_links_n; i++) {
        /*
         * Skip the NOP topic
         */
        if (strcmp(index_links[i]->topic_key, HELP_NOP_KEY) == 0) {
            continue;
        }

        topic = find_topic(index_links[i]->topic_key);
        if (topic != NULL) {
            link = new_link();
            link->topic_key = index_links[i]->topic_key;
            link->label = topic->title;
            link->line_number = line_number;
            line_number++;
            link->x = 5;
            append_line(topic_index, L"");
            append_link(topic_index, link);
        }
    }
    debug_topic(topic_index);
}

/**
 * Parse raw_help_text into data structures to feed help_handler().
 */
void setup_help() {

    enum STATES {
        NORMAL,
        TAG,
        UNICODEPOINT
    } state;

    char * input;
    int input_n;
    int input_i;
    int line_number;
    struct help_topic * topic = NULL;
    struct help_link * link = NULL;
    wchar_t line[80];
    int line_i;
    char ch;
    char key[32];
    char label[74];
    char title[7 * 74];
    wchar_t title_wcs[74];
    char text[7 * 74];
    wchar_t text_wcs[74];
    char x[4];
    int rc;
    unsigned int i;

    DLOG(("HELP: setup_help()\n"));

    /*
     * Translate everything at once
     */
    input = _(raw_help_text);

    DLOG(("HELP TEXT: %d bytes\n----\n%s\n----\n", (int) strlen(input), input));

    /*
     * Initial state
     */
    input_n = strlen(input);
    input_i = 0;
    memset(line, 0, sizeof(line));
    line_i = 0;
    state = NORMAL;
    line_number = 0;

    /*
     * Build each help line
     */
    while (input_i < input_n) {
        ch = input[input_i];

        /* DLOG((" STATE %d --> ch: \'%c\'\n", state, ch)); */

        switch (state) {

        case NORMAL:
            if (ch == '\n') {
                append_line(topic, line);
                memset(line, 0, sizeof(line));
                line_i = 0;
                line_number++;
            } else if (ch == '@') {
                state = TAG;
            } else if (ch == '&') {
                state = UNICODEPOINT;
            } else {
                line[line_i] = ch;
                line_i++;
            }
            break;

        case UNICODEPOINT:

            DLOG(("setup_help() Looking for Unicode...\n"));

            if (ch != '#') {
                /*
                 * False alarm
                 */
                line[line_i] = '&';
                line_i++;
                line[line_i] = ch;
                line_i++;
                state = NORMAL;
                break;
            }

            rc = sscanf(input + input_i - 1, "%[^;]%*s", &text[0]);

            DLOG(("%d Unicode: \'%s\'\n", rc, text));

            assert(rc == 1);

            /*
             * Append the ;
             */
            text[strlen(text) + 1] = 0;
            text[strlen(text)] = ';';
            input_i = strchr(input + input_i, ';') - input;

            DLOG(("  Unicode next ch: \'%c\' input_i %d\n",
                    input[input_i], input_i));


            memset(text_wcs, 0, sizeof(text_wcs));
            convert_unicode(text, text_wcs);
            for (i = 0; i < wcslen(text_wcs); i++) {
                line[line_i] = text_wcs[i];
                line_i++;
            }
            state = NORMAL;
            break;

        case TAG:

            DLOG(("setup_help() Looking for TAG...\n"));

            if (strncasecmp(input + input_i, "topic{", 6) == 0) {

                DLOG(("setup_help() TOPIC: "));

                rc = sscanf(input + input_i + 6, "%[^,],%[^}]%*s", key, title);

                DLOG2(("%d key: \'%s\' Title: \'%s\'\n", rc, key, title));

                assert(rc == 2);
                input_i = strchr(input + input_i, '}') - input;

                memset(title_wcs, 0, sizeof(title_wcs));
                convert_unicode(title, title_wcs);
                topic = new_topic();
                topic->key = Xstrdup(key, __FILE__, __LINE__);
                topic->title = Xwcsdup(title_wcs, __FILE__, __LINE__);


                DLOG(("Added topic key: \'%s\' Title: \'%ls\'\n", topic->key,
                        topic->title));

                /*
                 * Automatic new line
                 */
                memset(line, 0, sizeof(line));
                line_i = 0;
                line_number = 0;
                while (input[input_i] != '\n') {
                    input_i++;
                }

                state = NORMAL;
            } else if (strncasecmp(input + input_i, "link{", 5) == 0) {

                DLOG(("setup_help() LINK: \n"));


                rc = sscanf(input + input_i + 5, "%[^,],%[^,],%[^}]%*s", key,
                            label, x);

                DLOG(("%d LINK: key \'%s\' label \'%s\' x \'%s\'\n", rc, key,
                        label, x));

                assert(rc == 3);
                input_i = strchr(input + input_i, '}') - input;

                DLOG(("  LINK next ch: \'%c\' input_i %d\n", input[input_i],
                        input_i));

                memset(text_wcs, 0, sizeof(text_wcs));
                convert_unicode(label, text_wcs);

                link = new_link();
                link->topic_key = Xstrdup(key, __FILE__, __LINE__);
                link->label = Xwcsdup(text_wcs, __FILE__, __LINE__);
                link->line_number = line_number;
                link->x = atoi(x);

                DLOG((" -->  LINK: line_number %d key \'%s\' label \'%ls\' x %d\n",
                        link->line_number, link->topic_key, link->label,
                        link->x));

                append_link(topic, link);
                state = NORMAL;
            } else if (strncasecmp(input + input_i, "bold{", 5) == 0) {

                DLOG(("setup_help() BOLD: "));

                rc = sscanf(input + input_i + 5, "%[^}]%*s", text);

                DLOG2(("%d bolded text: \'%s\'\n", rc, text));

                assert(rc == 1);
                input_i = strchr(input + input_i, '}') - input;

                DLOG(("  BOLD next ch: \'%c\' input_i %d\n", input[input_i],
                        input_i));


                memset(text_wcs, 0, sizeof(text_wcs));
                convert_unicode(text, text_wcs);
                for (i = 0; i < wcslen(text_wcs); i++) {
                    line[line_i] = text_wcs[i] | HELP_BOLD;
                    line_i++;
                }
                state = NORMAL;
            } else {
                DLOG(("setup_help() no tag, back to NORMAL\n"));
                state = NORMAL;
                line[line_i] = '@';
                line_i++;
                line[line_i] = ch;
                line_i++;
            }

            break;
        }

        input_i++;
    }


    DLOG(("HELP Topics provided:\n"));
    topic = TOPICS;
    while (topic != NULL) {
        DLOG(("   %s \'%ls\' links: %d\n", topic->key, topic->title,
                topic->links_n));
        topic = topic->next;
    }

    /*
     * The configuration file topic is auto-generated
     */
    build_options_topic();

    /*
     * The index is auto-generated
     */
    build_help_index();
}

/**
 * This provides the UI to the help text.
 *
 * @param topic_key the topic to start on
 */
static void help_handler(char * topic_key) {
    int message_left;
    int window_left;
    int window_top;
    int window_height = HEIGHT - STATUS_HEIGHT;
    int window_length = WIDTH;
    int keystroke;
    int status_left_stop;
    Q_BOOL dirty = Q_TRUE;
    Q_BOOL done = Q_FALSE;
    int old_cursor;
    struct help_topic * topic;
    struct help_link * link;
    int selected_link = -1;
    int selected_link_x = -1;
    int current_line = 0;
    int i, j;
    wchar_t * line = NULL;
    int original_width, original_height;

    char ** topic_stack = NULL;
    int topic_stack_n = 0;
    Q_BOOL popped = Q_FALSE;
    char * status_prompt = NULL;
    wchar_t title[128];

    /*
     * Save the cursor
     */
    old_cursor = q_cursor_off();

    /*
     * We do window resizing
     */
    original_width = WIDTH;
    original_height = HEIGHT;

reload:

    DLOG(("HELP UI: RELOAD:\n"));

    /*
     * Reload when a user presses Enter or the screen is resized.
     */
    window_height = HEIGHT - STATUS_HEIGHT;
    window_length = WIDTH;
    dirty = Q_TRUE;
    done = Q_FALSE;
    selected_link = -1;
    selected_link_x = -1;
    current_line = 0;
    line = NULL;

    if ((topic_key == NULL) && (topic_stack_n > 1)) {
        /*
         * Pop from topic stack
         */
        assert(topic_stack != NULL);
        topic_stack =
            (char **) Xrealloc(topic_stack,
                               sizeof(char *) * (topic_stack_n - 1), __FILE__,
                               __LINE__);
        topic_stack_n--;
        topic_key = topic_stack[topic_stack_n - 1];

        DLOG(("HELP UI: POP NEW TOPIC: %s\n", topic_key));

        popped = Q_TRUE;
    } else if ((topic_key == NULL) && (topic_stack_n == 1)) {
        topic_key = topic_stack[0];
        popped = Q_TRUE;
    } else {
        popped = Q_FALSE;
    }

    topic = find_topic(topic_key);
    if (topic == NULL) {
        topic = find_topic(HELP_NOP_KEY);
    }
    assert(topic != NULL);

    if (popped == Q_FALSE) {
        if (topic_stack_n == 0) {

            DLOG(("HELP UI: START TOPIC STACK: %s\n", topic_key));

            /*
             * Start the topic stack
             */
            topic_stack = (char **) Xmalloc(sizeof(char *), __FILE__, __LINE__);
            topic_stack[0] = topic_key;
            topic_stack_n = 1;
        } else {

            DLOG(("HELP UI: PUSH TOPIC STACK: %s\n", topic_key));

            /*
             * Push on topic stack
             */
            assert(topic_stack != NULL);
            topic_stack =
                (char **) Xrealloc(topic_stack,
                                   sizeof(char *) * (topic_stack_n + 1),
                                   __FILE__, __LINE__);
            topic_stack[topic_stack_n] = topic_key;
            topic_stack_n++;
        }
    }


    DLOG(("HELP UI: topic_key = %s topic_stack_n = %d\n", topic_key,
            topic_stack_n));

    /*
     * Default: select the first link
     */
    if (topic->links_n > 0) {
        selected_link = 0;
    }

    status_prompt =
        _(" ^V-Select Link   Enter-Next Topic   B-Prior Topic   F1-Help Index   ESC/`-Exit ");

    memset(title, 0, sizeof(title));
#if defined(__BORLANDC__) || defined(_MSC_VER)
    /*
     * swprintf() doesn't take a length argument
     */
    swprintf(title, L" %ls - %ls ", _(L"Help"), topic->title);
#else
    swprintf(title, sizeof(title) / sizeof(wchar_t), L" %ls - %ls ",
             _(L"Help"), topic->title);
#endif

    /*
     * Window will be centered on the screen
     */
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
        window_top /= 2;
    }

    while (done == Q_FALSE) {
        assert(selected_link < topic->links_n);

        if (dirty) {

            /*
             * Draw box
             */
            screen_win_draw_box_color(stdscr, 0, 0, window_length,
                                      window_height, Q_COLOR_HELP_BORDER,
                                      Q_COLOR_HELP_BACKGROUND);

            /*
             * Place title
             */
            message_left =
                window_length - (wcslen(title) + 2);
            if (message_left < 0) {
                message_left = 0;
            } else {
                message_left /= 2;
            }
            screen_put_color_wcs_yx(0, message_left, title,
                                    Q_COLOR_HELP_BORDER);

            /*
             * Put up the status line
             */
            screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH,
                                      Q_COLOR_STATUS);
            status_left_stop = WIDTH - strlen(status_prompt);
            if (status_left_stop <= 0) {
                status_left_stop = 0;
            } else {
                status_left_stop /= 2;
            }
            screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_prompt,
                                    Q_COLOR_STATUS);
            screen_put_color_char_yx(HEIGHT - 1, status_left_stop + 1,
                                     cp437_chars[UPARROW], Q_COLOR_STATUS);
            screen_put_color_char_yx(HEIGHT - 1, status_left_stop + 2,
                                     cp437_chars[DOWNARROW], Q_COLOR_STATUS);

            /*
             * Display the current text
             */
            for (i = 0; i < HEIGHT - 4; i++) {
                if (i + current_line >= topic->lines_n) {
                    /*
                     * No more lines to display, bail out
                     */
                    break;
                }
                screen_move_yx(i + 2, 2 + ((WIDTH - 80) / 2));
                line = topic->lines[current_line + i];
                for (j = 0; j < 78; j++) {
                    if (j == wcslen(line)) {
                        /*
                         * No more characters
                         */
                        break;
                    }
                    if (line[j] & HELP_BOLD) {
                        screen_put_color_char((wchar_t) (line[j] & ~HELP_BOLD),
                                              Q_COLOR_HELP_BOLD);
                    } else {
                        screen_put_color_char((wchar_t) (line[j] & ~HELP_BOLD),
                                              Q_COLOR_HELP_BACKGROUND);
                    }
                }
                /*
                 * Now put up the links for this line
                 */
                for (j = 0; j < topic->links_n; j++) {
                    link = topic->links[j];
                    if (link->line_number == (i + current_line)) {
                        /*
                         * This link is visible on this line.  Put up a nice
                         * 1-char border, then the link.
                         */
                        screen_put_color_hline_yx(i + 2,
                                                  link->x + 2 +
                                                  ((WIDTH - 80) / 2), ' ',
                                                  wcslen(link->label) + 2,
                                                  Q_COLOR_HELP_LINK);
                        if (j == selected_link) {
                            screen_put_color_wcs_yx(i + 2,
                                                    link->x + 3 +
                                                    ((WIDTH - 80) / 2),
                                                    link->label,
                                                    Q_COLOR_HELP_LINK_SELECTED);
                        } else {
                            screen_put_color_wcs_yx(i + 2,
                                                    link->x + 3 +
                                                    ((WIDTH - 80) / 2),
                                                    link->label,
                                                    Q_COLOR_HELP_LINK);
                        }
                    }
                }
            }

            if ((i + current_line < topic->lines_n) && (current_line == 0)) {
                /*
                 * At the top and there is more below, show only "PgDn"
                 */
                screen_put_color_hline_yx(window_top + window_height - 1,
                                          window_left + window_length - 15,
                                          cp437_chars[Q_WINDOW_TOP], 5,
                                          Q_COLOR_HELP_BORDER);
                screen_put_color_str_yx(window_top + window_height - 1,
                                        window_left + window_length - 10,
                                        _(" PgDn "), Q_COLOR_HELP_BORDER);
            } else if (i + current_line < topic->lines_n) {
                /*
                 * There is more below and above
                 */
                screen_put_color_str_yx(window_top + window_height - 1,
                                        window_left + window_length - 15,
                                        _(" PgUp/PgDn "), Q_COLOR_HELP_BORDER);
            } else if (current_line > 0) {
                /*
                 * At the bottom, show "PgUp"
                 */
                screen_put_color_str_yx(window_top + window_height - 1,
                                        window_left + window_length - 15,
                                        _(" PgUp "), Q_COLOR_HELP_BORDER);
                screen_put_color_hline_yx(window_top + window_height - 1,
                                          window_left + window_length - 9,
                                          cp437_chars[Q_WINDOW_TOP], 5,
                                          Q_COLOR_HELP_BORDER);
            }

            dirty = Q_FALSE;
            screen_flush();
        }

        /*
         * Handle keystroke
         */
        qodem_win_getch(stdscr, &keystroke, NULL, Q_KEYBOARD_DELAY);

        switch (keystroke) {
        case 'B':
        case 'b':
            DLOG(("KEY B\n"));
            /*
             * Go back one topic
             */
            topic_key = NULL;
            goto reload;

        case Q_KEY_F(1):
            DLOG(("Q_KEY_F1\n"));
            /*
             * Go to Help Index
             */
            if (strcmp(topic_key, HELP_INDEX_KEY) != 0) {
                topic_key = HELP_INDEX_KEY;
                goto reload;
            }
            break;

        case ERR:
            /*
             * This was either a timeout or a resize.  Pull fresh dimensions.
             */
            if ((WIDTH != original_width) || (HEIGHT != original_height)) {

                /*
                 * Redraw/recompute everything
                 */
                window_height = HEIGHT - STATUS_HEIGHT;
                window_length = WIDTH;
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
                    window_top /= 2;
                }

                original_width = WIDTH;
                original_height = HEIGHT;

                /*
                 * Back to the top
                 */
                current_line = 0;
                dirty = Q_TRUE;
            }
            break;

        case Q_KEY_ENTER:
            DLOG(("Q_KEY_ENTER\n"));
            if (selected_link != -1) {
                /*
                 * Goto another topic
                 */
                topic_key = topic->links[selected_link]->topic_key;
                goto reload;
            }
            break;

        case Q_KEY_NPAGE:
            DLOG(("Q_KEY_NPAGE\n"));
            if (topic->lines_n - current_line > HEIGHT - 4) {
                current_line += HEIGHT - 4;
                if (topic->links_n > 0) {
                    selected_link = 0;
                    for (i = selected_link; i < topic->links_n; i++) {
                        if ((topic->links[i]->line_number >= current_line) &&
                            (topic->links[i]->line_number <=
                             current_line + HEIGHT - 4)
                            ) {
                            selected_link = i;
                            break;
                        }
                    }
                }
                dirty = Q_TRUE;
            }
            break;

        case Q_KEY_END:
            DLOG(("Q_KEY_END\n"));
            while (topic->lines_n - current_line > HEIGHT - 4) {
                current_line += HEIGHT - 4;
                dirty = Q_TRUE;
            }
            break;

        case Q_KEY_PPAGE:
            DLOG(("Q_KEY_PPAGE\n"));
            if (current_line > 0) {
                current_line -= HEIGHT - 4;
                if (topic->links_n > 0) {
                    selected_link = 0;
                    for (i = selected_link; i < topic->links_n; i++) {
                        if ((topic->links[i]->line_number >= current_line) &&
                            (topic->links[i]->line_number <=
                             current_line + HEIGHT - 4)
                            ) {
                            selected_link = i;
                            break;
                        }
                    }
                }
                dirty = Q_TRUE;
            }
            break;

        case Q_KEY_HOME:
            DLOG(("Q_KEY_HOME\n"));
            while (current_line > 0) {
                current_line -= HEIGHT - 4;
                dirty = Q_TRUE;
            }
            break;

        case Q_KEY_LEFT:
            DLOG(("Q_KEY_LEFT\n"));
            if (topic->links_n == 0) {
                /*
                 * No link movement
                 */
                break;
            }
            if (selected_link == 0) {
                /*
                 * Can't go further left
                 */
                break;
            }
            selected_link--;
            dirty = Q_TRUE;
            break;

        case Q_KEY_RIGHT:
            DLOG(("Q_KEY_RIGHT\n"));
            if (topic->links_n == 0) {
                /*
                 * No link movement
                 */
                break;
            }
            if (selected_link == (topic->links_n - 1)) {
                /*
                 * Can't go further right
                 */
                break;
            }
            selected_link++;
            dirty = Q_TRUE;
            break;

        case Q_KEY_DOWN:
            DLOG(("Q_KEY_DOWN\n"));
            if (topic->links_n == 0) {
                /*
                 * No link movement
                 */
                break;
            }
            if (selected_link == (topic->links_n - 1)) {
                /*
                 * Can't go further down
                 */
                break;
            }
            selected_link_x = topic->links[selected_link]->x;

            DLOG(("0 Q_KEY_DOWN selected_link_x %d\n", selected_link_x));

            /*
             * Look for the next link that has x within 10 of this one.  If
             * one can't be found, just go to the next link.
             */
            for (i = selected_link + 1; i < topic->links_n; i++) {

                DLOG(("1 Q_KEY_DOWN %d topic->links[i]->x %d selected_link_x %d\n",
                        i, topic->links[i]->x, selected_link_x));

                if (abs(topic->links[i]->x - selected_link_x) <= 10) {
                    break;
                }
            }

            if (i < topic->links_n) {
                DLOG(("2a Q_KEY_DOWN %d topic->links[i]->x %d selected_link_x %d\n",
                        i, topic->links[i]->x, selected_link_x));
            } else {
                DLOG(("2b Q_KEY_DOWN %d selected_link_x %d\n", i,
                    selected_link_x));
            }

            if (i < topic->links_n) {
                selected_link = i;
                DLOG(("3a Q_KEY_DOWN %d topic->links[i]->x %d selected_link_x %d\n",
                        i, topic->links[i]->x, selected_link_x));
            } else {
                DLOG(("3b Q_KEY_DOWN %d selected_link_x %d\n", i,
                        selected_link_x));
            }

            dirty = Q_TRUE;
            break;

        case Q_KEY_UP:
            DLOG(("Q_KEY_UP\n"));
            if (topic->links_n == 0) {
                /*
                 * No link movement
                 */
                break;
            }
            if (selected_link == 0) {
                /*
                 * Can't go further up
                 */
                break;
            }
            selected_link_x = topic->links[selected_link]->x;

            DLOG(("0 Q_KEY_UP selected_link_x %d\n", selected_link_x));

            /*
             * Look for the previous link that has x within 10 of this one.
             * If one can't be found, just go to the previous link.
             */
            for (i = selected_link - 1; i >= 0; i--) {

                DLOG(("1 Q_KEY_UP %d topic->links[i]->x %d selected_link_x %d\n",
                        i, topic->links[i]->x, selected_link_x));
                if (abs(topic->links[i]->x - selected_link_x) <= 10) {
                    break;
                }
            }

            if (i >= 0) {
                DLOG(("2a Q_KEY_UP %d topic->links[i]->x %d selected_link_x %d\n",
                        i, topic->links[i]->x, selected_link_x));
            } else {
                DLOG(("2b Q_KEY_UP %d selected_link_x %d\n", i,
                        selected_link_x));
            }

            if (i >= 0) {
                selected_link = i;
                DLOG(("3a Q_KEY_UP %d topic->links[i]->x %d selected_link_x %d\n",
                        i, topic->links[i]->x, selected_link_x));
            } else {
                DLOG(("3b Q_KEY_UP %d selected_link_x %d\n", i,
                        selected_link_x));
            }

            dirty = Q_TRUE;
            break;

        case '`':
            /*
             * Backtick works too
             */
        case Q_KEY_ESCAPE:
            done = Q_TRUE;
            break;

        default:
            /*
             * Ignore keystroke
             */
            break;

        }

    } /* while (done == Q_FALSE) */

    /*
     * The OK exit point
     */

    /*
     * Restore the cursor
     */
    q_cursor(old_cursor);

    q_screen_dirty = Q_TRUE;
}

/**
 * Enter the online help system.
 *
 * @param help_screen the screen to start with
 */
void launch_help(Q_HELP_SCREEN help_screen) {
    switch (help_screen) {
    case Q_HELP_PHONEBOOK:
        help_handler(HELP_PHONEBOOK_KEY);
        return;
    case Q_HELP_PHONEBOOK_REVISE_ENTRY:
        help_handler(HELP_PHONEBOOK_REVISE_ENTRY_KEY);
        return;
    case Q_HELP_BATCH_ENTRY_WINDOW:
        help_handler(HELP_CONSOLE__ALT6_BEW_KEY);
        return;
#ifndef Q_NO_SERIAL
    case Q_HELP_MODEM_CONFIG:
        help_handler(HELP_CONSOLE__ALTO_MODEM_CFG_KEY);
        return;
    case Q_HELP_COMM_PARMS:
        help_handler(HELP_CONSOLE__ALTY_COMM_PARMS_KEY);
        return;
#endif
    case Q_HELP_CONSOLE_MENU:
        help_handler(HELP_CONSOLE_MENU_KEY);
        return;
    case Q_HELP_PROTOCOLS:
        help_handler(HELP_FILE_PROTOCOLS_KEY);
        return;
    case Q_HELP_EMULATION_MENU:
        help_handler(HELP_EMULATION_KEY);
        return;
    case Q_HELP_TRANSLATE_EDITOR:
        help_handler(HELP_CONSOLE__ALTA_TRANSLATE_KEY);
        return;
    case Q_HELP_CODEPAGE:
        help_handler(HELP_CODEPAGE_KEY);
        return;
    case Q_HELP_FUNCTION_KEYS:
        help_handler(HELP_FUNCTION_KEYS_KEY);
        return;
    default:
        help_handler(HELP_NOP_KEY);
        return;
    }
}

/*
 * This defines the help text:
 *
 *   * It is ASCII-encoded.
 *
 *   * Each line is 76 columns at most - all text beyond 76 is truncated and
 *     fires an assertion.
 *
 *   * Newline (\n) starts a new line.  The last line needs a newline.
 *
 *   * Four special tokens are supported:
 *
 *     1. @TOPIC{key,title} - starts a new topic titled "title" that can be
 *        found by calling find_topic(key).  The rest of the line is
 *        discarded.
 *
 *     2. @LINK{key,label,x} - embeds a hyperref link to the topic with key
 *        "key", displayed at column x on the line, with the label "label".
 *        X starts at 0.
 *
 *     3. @BOLD{text} - makes the text bold.
 *
 *     4. &#YYYY; or $#xYYYY- inserts the Unicode character YYYY in the text.
 *        Decimal and hex OK.  This special form may be also used in the
 *        title field of @TOPIC, the label field of @LINK, and the text field
 *        of @BOLD.
 */
char * raw_help_text = \
"@TOPIC{HELP_NOP,TODO}\n"
"This topic is not yet written\n"
"\n"
"@TOPIC{CONSOLE_MENU,TERMINAL Mode Command Menu}\n"
"@BOLD{TERMINAL Mode} is the main communications screen.  In this mode most keys\n"
"typed will be passed to the remote side, with the exception of the special\n"
"commands listed on the Command Menu.\n"
"\n"
"The TERMINAL Mode commands are described below:\n"
"\n"
"@BOLD{       *       *       *       *       *       *       *       *}\n"
"\n"
"@BOLD{Alt-D} Phone Book @LINK{PHONEBOOK,Phone Book ,40}\n"
"This brings up the phone book.\n"
"\n"
"@BOLD{Alt-G} Term Emulation @LINK{EMULATION,Emulations ,40}\n"
"This brings up a menu to select the terminal emulation.  Selecting the\n"
"active terminal emulation will prompt to reset the emulation state;\n"
"this may be useful to recover from corrupted escape sequences.\n"
"\n"
"@BOLD{Alt-C} Clear Screen\n"
"This clears the screen and homes the cursor.\n"
"\n"
"@BOLD{Alt-F} Execute Script @LINK{REFERENCE_03_SCRIPT,Scripting  ,40}\n"
"This prompts for a filename, and then executes that file as a script.\n"
"Any program that reads from standard input and writes to standard\n"
"output can be run as a script.  See the section below on script\n"
"support.\n"
"\n"
"@BOLD{Alt-K} Send BREAK\n"
"When connected via serial mode or modem, this calls tcsendbreak() to\n"
"send a true \"Break Signal\" on the serial line.\n"
"\n"
"@BOLD{Alt-P} Capture File\n"
"Enable/disable capture to file.  Four capture formats are supported:\n"
"\"raw\", \"normal\", \"html\", and \"ask\".  \"Raw\" format saves every byte as\n"
"received from the other side before emulation processing; \"normal\"\n"
"saves UTF-8 characters after emulation processing; \"html\" saves in\n"
"HTML format with Unicode entities and color attributes after emulation\n"
"processing; \"ask\" will bring up a dialog to select which format to use\n"
"every time capture is enabled.  ASCII file transfers will be included\n"
"in the capture file; other file transfers (Xmodem, Ymodem, Zmodem,\n"
"Kermit) are excluded from the capture file.\n"
"\n"
"@BOLD{Alt-S} Split Screen\n"
"This actives split screen mode, in which local characters are\n"
"accumulated in a buffer before sending to the remote side.  To send\n"
"carriage return, enter \"^M\".\n"
"\n"
"@BOLD{Alt-T} Screen Dump\n"
"This prompts for a filename, and then saves the current view to that\n"
"file.  Three screen dump formats are supported: \"normal\", \"html\", and\n"
"\"ask\".  \"normal\" saves UTF-8 characters after emulation processing;\n"
"\"html\" saves in HTML format with Unicode entities and color attributes\n"
"after emulation processing; \"ask\" will bring up a dialog to select\n"
"which format to use every time the screen is dumped.\n"
"\n"
"@BOLD{Alt-Y} COM Parameters\n"
"This brings up a menu to alter the serial port settings.\n"
"\n"
"@BOLD{PgUp} Upload Files @LINK{FILE_PROTOCOLS,Protocols   ,40}\n"
"This brings up the file upload menu.  Note that @BOLD{CTRL-PgUp} and @BOLD{ALT-PgUp}\n"
"may also work depending on the terminfo for the host terminal.\n"
"\n"
"@BOLD{PgDn} Download Files @LINK{FILE_PROTOCOLS,Protocols   ,40}\n"
"This brings up the file download menu.  Note that @BOLD{CTRL-PgDn} and\n"
"@BOLD{ALT-PgDn} may also work depending on the terminfo for the host\n"
"terminal.\n"
"\n"
"@BOLD{Alt-\\} Alt Code Key\n"
"This brings up a dialog to enter the 3-digit decimal value (0-255) for\n"
"an 8-bit byte or a 4-digit hexadecimal value (0-ffff) for a 16-bit\n"
"Unicode character (L_UTF8 and X_UTF8 only).  @BOLD{Alt-\\} also works in these\n"
"other functions:\n"
"    Phone book revise entry form\n"
"    Phone book find text dialog\n"
"    Scrollback find text dialog\n"
"    Unicode translate tables find text dialog\n"
"    Keyboard macro editor\n"
"\n"
"@BOLD{Alt-;} Codepage @LINK{CODEPAGE,Codepages  ,40}\n"
"This brings up a dialog to change the current codepage.  Codepages are\n"
"limited by the current emulation.  VT52, VT100, VT102, VT220, L_UTF8,\n"
"X_UTF8, PETSCII, and ATASCII have their own codepage; LINUX, XTERM, ANSI,\n"
"AVATAR, TTY, and DEBUG emulations can be set to CP437 (DOS VGA), ISO-8859-1,\n"
"CP720 (DOS Arabic), CP737 (DOS Greek), CP775 (DOS Baltic Rim), CP850 (DOS\n"
"West European), CP852 (DOS Central European), CP857 (DOS Turkish), CP858\n"
"(DOS West European+Euro), CP860 (DOS Portuguese), CP862 (DOS Hebrew), CP863\n"
"(DOS Quebecois), CP866 (DOS Cyrillic), Windows-1250 (Central/East European),\n"
"Windows-1251 (Cyrillic), Windows-1252 (West European), KOI8-R (Russian), and\n"
"KOI8-U (Ukrainian).\n"
"\n"
"@BOLD{Alt-/} Scroll Back\n"
"This selects the scrollback buffer.  When viewing the buffer, @BOLD{S}\n"
"saves to file and @BOLD{C} clears the scrollback buffer.  By default qodem\n"
"supports up to 20000 lines of scrollback; this can be changed by\n"
"in the @BOLD{qodemrc} configuration file.  Three scrollback save formats\n"
"are supported: \"normal\", \"html\", and \"ask\".  \"normal\" saves UTF-8\n"
"characters after emulation processing; \"html\" saves in HTML format\n"
"with Unicode entities and color attributes after emulation processing;\n"
"\"ask\" will bring up a dialog to select which format to use every time\n"
"the scrollback is saved.\n"
"\n"
"@BOLD{Alt-H} Hangup/Close\n"
"This hangs up the modem (drops DTR) or closes the remote connection\n"
"(kills the child process).\n"
"\n"
"@BOLD{Alt-L} Log View\n"
"This brings the session log up in an editor.  The session log stores\n"
"information about connect, disconnect, file upload/download events,\n"
"and script messages.\n"
"\n"
"@BOLD{Alt-M} Mail Reader\n"
"This spawns the mail reader, by default @BOLD{mm}.\n"
"\n"
"@BOLD{Alt-X} Exit Qodem\n"
"This prompts to exit qodem.  When not connected, Ctrl-C will also\n"
"bring up the exit prompt.\n"
"\n"
"@BOLD{Alt-A} Translate Tables @LINK{CONSOLE__ALTA_TRANSLATE,Translate Tables,40}\n"
"This brings up the translate tables editor.  Both incoming and\n"
"outgoing bytes can be changed or stripped (set to ASCII NUL (0)).\n"
"Note that 8-bit INPUT translation occurs before both emulation\n"
"processing and UTF-8 decoding.  Unicode INPUT translation occurs\n"
"before code points are written to the scrollback buffer; Unicode\n"
"OUTPUT translation occurs after code points are read from the\n"
"keyboard.\n"
"\n"
"@BOLD{Alt-J} Function Keys @LINK{FUNCTION_KEYS,Function Keys,40}\n"
"This brings up the keyboard macro editor.  Keyboard macros support\n"
"substitutions for carriage return (\"^M\"), the phone book entry username\n"
"(\"$USERNAME\"), and the phone book entry password (\"$PASSWORD\").\n"
"\n"
"@BOLD{Alt-N} Configuration\n"
"This brings the @BOLD{qodemrc} options file up in an editor.\n"
"\n"
"@BOLD{Alt-:} Colors\n"
"This brings the @BOLD{colors.cfg} colors file up in an editor.\n"
"\n"
"@BOLD{Alt-O} Modem Config @LINK{CONSOLE__ALTO_MODEM_CFG,Modem Config,40}\n"
"This brings up the modem configuration dialog.\n"
"\n"
"@BOLD{Alt-R} OS Shell\n"
"This spawns a system shell.\n"
"\n"
"@BOLD{Alt-V} View File\n"
"This brings up a prompt to view a file in an editor.\n"
"\n"
"@BOLD{Alt-W} List Directory\n"
"This brings up a directory listing.\n"
"\n"
"@BOLD{Alt-0} Session Log\n"
"This toggles the session log on or off.\n"
"\n"
"@BOLD{Alt-1} XON/XOFF Flow Ctrl\n"
"When connected via modem or serial port, this toggles XON/XOFF on or off.\n"
"\n"
"@BOLD{Alt-2} Backspace/Del Mode\n"
"This selects whether the backspace key on the keyboard sends an ASCII\n"
"backspace (^H) or an ASCII DEL (127) character.  Alt-\\ 0 0 8 can always be\n"
"used to send true backspace; Alt-\\ 1 2 7 can be used to send true DEL.\n"
"Note that VT220 emulation always sends DEL when the backspace key is\n"
"pressed.\n"
"\n"
"@BOLD{Alt-3} Line Wrap\n"
"This toggles line wrap mode on or off.  When line wrap mode is\n"
"enabled, if a character is received when the cursor is at the right\n"
"margin it will wrap to the first column of the next line.\n"
"\n"
"@BOLD{Alt-4} Display NULL\n"
"This selects whether ASCII NUL (0) will be displayed as a blank/space\n"
"or stripped.\n"
"\n"
"@BOLD{Alt-5} Host Mode @LINK{HOST_MODE,Host Mode,40}\n"
"This switches Qodem into Host Mode.\n"
"\n"
"@BOLD{Alt-6} Batch Entry Window @LINK{CONSOLE__ALT6_BEW,Batch Entry Window,40}\n"
"This brings up the list of upload files used by Ymodem, Zmodem, and\n"
"Kermit uploads.\n"
"\n"
"@BOLD{Alt-7} Status Line Info\n"
"This selects between two formats for the status line.\n"
"\n"
"@BOLD{Alt-8} Hi-Bit Strip\n"
"This selects whether or not to clear the 8th bit of all incoming\n"
"bytes.  Note that high-bit stripping occurs before both emulation\n"
"processing and UTF-8 decoding.\n"
"\n"
"@BOLD{Alt-9} Serial Port\n"
"This opens or closes the serial port.  If already connected to a\n"
"non-serial/modem remote host, this does nothing.\n"
"\n"
"@BOLD{Alt-B} Beeps & Bells\n"
"This toggles beep support on or off.  When beep support is on, beeps\n"
"from the remote host will be played by qodem.  In LINUX emulation,\n"
"qodem supports setting the tone and duration of the beep as specified\n"
"in @BOLD{console-codes(4)}.\n"
"\n"
"@BOLD{Alt-E} Half/Full Duplex\n"
"This toggles between half and full duplex.\n"
"\n"
"@BOLD{Alt-I} Qodem Information\n"
"This displays the qodem splash screen which includes the version and\n"
"build date.\n"
"\n"
"@BOLD{Alt-U} Scrollback Record\n"
"This selects whether or not lines that scroll off the top of the\n"
"screen will be saved to the scrollback buffer.\n"
"\n"
"@BOLD{Alt-=} Doorway Mode\n"
"This selects between three doorway modes: \"Doorway OFF\", \"Doorway\n"
"MIXED\" and \"Doorway FULL\".  When doorway mode is \"Doorway OFF\",\n"
"terminal mode responds to all of its command keys as described in this\n"
"section.  When doorway mode is \"Doorway FULL\", all @BOLD{Alt-} command\n"
"keystrokes except @BOLD{Alt-=} are passed to the remote side.  When doorway\n"
"mode is \"Doorway MIXED\", terminal mode supports a few commands but\n"
"passes the majority of @BOLD{Alt-} command keystrokes to the remote side.\n"
"The default commands supported \"Doorway MIXED\" mode are:\n"
"     @BOLD{Alt-D} Phone Book\n"
"     @BOLD{Alt-P} Capture\n"
"     @BOLD{Alt-T} Screen Dump\n"
"     @BOLD{Alt-Y} COM Parameters\n"
"     @BOLD{Alt-Z} Menu\n"
"     @BOLD{Alt-/} Scrollback\n"
"     @BOLD{Alt-PgUp} or @BOLD{Ctrl-PgUp} Upload Files\n"
"     @BOLD{Alt-PgDn} or @BOLD{Ctrl-PgDn} Download Files\n"
"\n"
"@BOLD{Alt--} Status Lines\n"
"This toggles the status line on or off.\n"
"\n"
"@BOLD{Alt-+} CR/CRLF Mode\n"
"This toggles whether or not received carriage returns imply line feed\n"
"or not.\n"
"\n"
"@BOLD{Alt-,} ANSI Music\n"
"This toggles ANSI music support on or off.\n"
"\n"
"\n"
"\n"
"See Also: @LINK{CONFIGURATION,Configuration File     ,12}\n"
"\n"
"          @LINK{CODEPAGE,Codepages              ,12}\n"
"\n"
"          @LINK{EMULATION,Terminal Emulations    ,12}\n"
"\n"
"          @LINK{FILE_PROTOCOLS,File Transfer Protocols,12}\n"
"\n"
"          @LINK{FUNCTION_KEYS,Function Keys          ,12}\n"
"\n"
"          @LINK{HOST_MODE,Host Mode              ,12}\n"
"\n"
"          @LINK{REFERENCE_99_QMODEM,Differences From Qmodem,12}\n"
"\n"
"@TOPIC{CONSOLE__ALT6_BEW,Batch Entry Window}\n"
"The Batch Entry Window is used to select the files to upload for file\n"
"transfers that use the Ymodem, Zmodem, or Kermit protocols.\n"
"\n"
"The Batch Entry Window supports uploading up to twenty files at one time.\n"
"The @BOLD{&#x2191;} and @BOLD{&#x2193;} arrow keys can be used to switch between lines.  Each line shows\n"
"the fully-qualified path and filename of the file to upload on left, and the\n"
"file size in kilobytes on the right.  A filename can be entered directly in\n"
"the text field, or a file selection dialog can be brought up by pressing @BOLD{F2}\n"
"or @BOLD{Enter}.  When all files have been selected, pressing @BOLD{F10} will start the\n"
"file transfer.  To clear the entire list, press @BOLD{F4}.\n"
"\n"
"\n"
"\n"
"See Also: @LINK{FILE_PROTOCOLS,File Transfer Protocols,12}\n"
"\n"
"          @LINK{REFERENCE_99_QMODEM,Differences From Qmodem,12}\n"
"\n"
"@TOPIC{CONSOLE__ALTA_TRANSLATE,Translate Tables}\n"
"The Translate Tables can be used to automatically change bytes received and\n"
"and sent to the remote system, and also Unicode code points written to\n"
"screen and read from keyboard.  This can be useful when communicating with\n"
"systems that use different conventions for end-of-line behavior, or that\n"
"do not use ASCII, or for canonicalizing equivalent Unicode code points.\n"
"\n"
"The 8-bit INPUT table changes bytes that are received from the remote side,\n"
"and occurs before the bytes are processed by the terminal emulation.  It is\n"
"possible for the 8-bit INPUT table to completely break the emulation, e.g.\n"
"if the ESC byte (ASCII 27) is changed.\n"
"\n"
"The 8-bit OUTPUT table changes bytes that are sent to the remote side, and\n"
"occurs after keystrokes have been translated by the terminal emulation.  It\n"
"is possible for the 8-bit OUTPUT table to break UTF-8 encoding, e.g. if byte\n"
"226 (0xE2) is changed then the box-drawing characters will not encode\n"
"correctly.\n"
"\n"
"The Unicode INPUT table changes code points (glyphs) that are displayed from\n"
"the scrollback buffer to the screen.\n"
"\n"
"The Unicode OUTPUT table changes code points (glyphs) that are received from\n"
"the keyboard, and occurs before keystrokes are encoded in UTF-8 for sending\n"
"to the remote side.\n"
"\n"
"Use the arrow keys (@BOLD{&#x2191;}, @BOLD{&#x2193;}, @BOLD{&#x2190;}, and @BOLD{&#x2192;}) to select a byte or code point to\n"
"change.  Press @BOLD{Enter} to bring up an edit textbox, and enter the decimal\n"
"value of the byte (8-bit) or the hexadecimal value of the code point\n"
"(Unicode) to change it to.  Pressing @BOLD{PgUp} or @BOLD{PgDn} switches betweeen the\n"
"ASCII characters (0 to 127) and the high-bit characters (128 to 255) in the\n"
"8-bit tables; in the Unicode tables it selects the next block of 96 code\n"
"points.  In the Unicode tables, pressing @BOLD{F} brings up a search box.  Enter a\n"
"hexadecimal value to go directly to that code point.\n"
"\n"
"\n"
"\n"
"See Also: @LINK{CODEPAGE,Codepages              ,12}\n"
"\n"
"          @LINK{EMULATION,Terminal Emulations    ,12}\n"
"\n"
"@TOPIC{CONSOLE__ALTY_COMM_PARMS,Serial Port Settings}\n"
"The Serial Port Settings screen is used to configure the serial port\n"
"properties. To change a property, press a letter from @BOLD{A} to @BOLD{W}. Press @BOLD{Enter}\n"
"to change the serial port settings, or @BOLD{ESCAPE} or @BOLD{`} to abandon changes.\n"
"\n"
"Most properties are self-explanatory.  Not all combinations of baud rate,\n"
"data bits, stop bits, and parity are supported on all platforms.  @BOLD{MARK} and\n"
"@BOLD{SPACE} parity in particular are unlikely to work correctly on POSIX (Linux,\n"
"BSD, Mac OS X, etc.) systems.\n"
"\n"
"\n"
"\n"
"See Also: @LINK{CONFIGURATION,Configuration File     ,12}\n"
"\n"
"          @LINK{REFERENCE_99_QMODEM,Differences From Qmodem,12}\n"
"\n"
"@TOPIC{CONSOLE__ALTO_MODEM_CFG,Modem Config}\n"
"The Modem Configuration screen is used to configure the serial port\n"
"properties and Hayes AT-compatible strings sent to the modem.  To change a\n"
"property, first press a number from @BOLD{1} to @BOLD{8} to select that field, and\n"
"then enter the new value in the highlighted editing text field or popup\n"
"dialog.\n"
"\n"
"For the Init, Hangup, and Dial strings, the tilde (@BOLD{~}) character inserts a\n"
"1/2 second delay.  The carat (@BOLD{^}) character can be used to enter a control\n"
"character, most commonly @BOLD{^M} for carriage return but can be used for any\n"
"control character.\n"
"\n"
"\n"
"\n"
"See Also: @LINK{CONFIGURATION,Configuration File     ,12}\n"
"\n"
"          @LINK{REFERENCE_99_QMODEM,Differences From Qmodem,12}\n"
"\n"
"@TOPIC{PHONEBOOK,Phone Book}\n"
"Connection information and other details for local and remote systems are\n"
"stored in the @BOLD{Phone Book}.  A phone book may have an unlimited number of\n"
"entries.  Internally a phone book file contains simple UTF-8 encoded text.\n"
"\n"
"The phone book display is organized into two main windows:\n"
"\n"
"    * The upper window shows a list of phone book entries and provides\n"
"      a scroll bar to navigate the list.  The scroll bar can be moved\n"
"      through the list using the up and down arrow keys (@BOLD{&#x2191;} and @BOLD{&#x2193;}),\n"
"      @BOLD{PgUp}, @BOLD{PgDn}, @BOLD{Home}, and @BOLD{End}.  The headings above the list show the\n"
"      fully-qualified filename of the current phone book, the number of\n"
"      tagged entries, and column headings for the current view mode.\n"
"\n"
"    * The bottom window shows a list of phone book commands, or when\n"
"      dialing the modem provides information about the current call\n"
"      status.\n"
"\n"
"The phone book commands are described below.  Commands may operate on the\n"
"phone book itself, on the highlighted entry, or on the currently-tagged\n"
"entries.\n"
"\n"
"An entry can be tagged by moving the scroll bar to it and pressing\n"
"@BOLD{Space Bar}.\n"
"\n"
"Pressing @BOLD{ESCAPE} or @BOLD{`} exits the phone book and returns to @BOLD{TERMINAL Mode}.\n"
"\n"
"@BOLD{Phone Book Dialing}\n"
"\n"
"  Two commands are available to dial or connect: @BOLD{M}anual Dial and @BOLD{Enter}.\n"
"\n"
"  If no entries are tagged, then pressing @BOLD{Enter} will connect to the\n"
"  highlighted entry.\n"
"\n"
"  If at least one entry is tagged, then pressing @BOLD{Enter} will move the scroll\n"
"  bar to the next entry and attempt to connect, trying each tagged entry\n"
"  until a successful connection is established.\n"
"\n"
"  @BOLD{M}anual Dial brings up a dialog to enter a phone number and immediately\n"
"  begin dialing.  Manual dialing is only supported for modem connections.\n"
"\n"
"@BOLD{Phone Book Commands}\n"
"\n"
"  The following commands operate on the phone book as a whole:\n"
"\n"
"    @BOLD{F} Find Text\n"
"\n"
"    This brings up a dialog to enter a search string.  Enter the string\n"
"    and press @BOLD{Enter}, and Qodem will move the scroll bar to the first entry\n"
"    that matches the search string.  The fields checked for a match are the\n"
"    name, address, and notes.  If the match occurs in the notes, a popup\n"
"    message is displayed to indicate that the match was in the notes.  A\n"
"    popup is also display if the text is not found at all.\n"
"\n"
"    @BOLD{A} Find Again\n"
"\n"
"    This is nearly identical to @BOLD{F}ind, however it will continue searching\n"
"    for a match from the current scroll bar location onward.\n"
"\n"
"    @BOLD{L} Load\n"
"\n"
"    This brings up a directory view to load a different phone book file\n"
"\n"
"    @BOLD{O} Other Info\n"
"\n"
"    This changes the visible columns.  There are five different views\n"
"    available, between them all most of the fields can be seen in tabular\n"
"    form.  Passwords in the password field are replaced by asterisks (@BOLD{*}).\n"
"\n"
"    @BOLD{^P/P} Print 132/80\n"
"\n"
"    This brings up a dialog to select a printer to output the phone book to.\n"
"    The default value sends the phone book to the system printer.  Entering\n"
"    a different value will print the phone book to a file.  @BOLD{^P} selects 132-\n"
"    column format, @BOLD{P} selects 80-column format.  THIS COMMAND EXPOSES\n"
"    THE PHONE BOOK PASSWORDS.\n"
"\n"
"    @BOLD{S} Sort\n"
"\n"
"    This brings up a selection box to sort the phone book by one of six\n"
"    different methods.\n"
"\n"
"    @BOLD{^U} Undo\n"
"\n"
"    This reloads the phone book from the backup file.  This undo can ONLY\n"
"    restore the phone book from very recent edits, it is not a general-\n"
"    purpose undo feature.\n"
"\n"
"@BOLD{Phone Book Entry Commands}\n"
"\n"
"  The following commands operate on the highlighted or tagged entry:\n"
"\n"
"    @BOLD{Space Bar} Tag/Untag\n"
"\n"
"    This tags (or untags) the highlighted entry.  If the entry has a\n"
"    script file associated with it and the file does not exist, then\n"
"    tagging it will also enable QuickLearn.\n"
"\n"
"    @BOLD{I-Ins} Insert New Entry\n"
"\n"
"    This inserts a new entry and switches to the Revise Entry screen.\n"
"    The new entry is inserted before the highlighted entry.  Both @BOLD{I} and\n"
"    @BOLD{Insert} will work.\n"
"\n"
"    @BOLD{^D/D-Del} Delete Tagged/Bar\n"
"\n"
"    This deletes either the highlighted entry only (@BOLD{D} or @BOLD{Delete}) or all\n"
"    tagged entries (@BOLD{^D}).  A prompt is displayed to select either the notes\n"
"    only or the entire entry.\n"
"\n"
"    @BOLD{^R/R} Revise Tagged/Bar\n"
"\n"
"    This brings up either the highlighted entry only (@BOLD{R}) or all tagged\n"
"    entries (@BOLD{^R}) in the phone book revise entry form.  After editing, the\n"
"    changes can be saved by pressing @BOLD{F10} to return to the phone book, or\n"
"    abandoned by pressing @BOLD{ESCAPE}.  If multiple entries were selected to be\n"
"    edited with @BOLD{^R}, then each tagged entry will be brought up in the edit\n"
"    window in sequence.\n"
"\n"
"    @BOLD{T} Tag Multiple\n"
"\n"
"    This brings up an entry box to tag multiple entries.  Entries can be\n"
"    tagged in two ways, each tag separated by space:\n"
"        1. By entry number.\n"
"        2. By text search: precede with the letter @BOLD{T} or @BOLD{t}.\n"
"    Examples:\n"
"        1. To select entries 1, 5, and 16, enter the tag string \"1 5 16\".\n"
"        2. To select entries that match \"web\", enter the tag string\n"
"           \"Tweb\".\n"
"        3. To select entries that match \"bbs\" and also entries 10 and 23,\n"
"           enter the tag string \"Tbbs 10 23\".\n"
"\n"
"    @BOLD{U} Untag All\n"
"\n"
"    This untags all tagged entries.\n"
"\n"
"    @BOLD{Q} QuickLearn\n"
"\n"
"    This flags the highlighted entry for QuickLearn.  The entry must have a\n"
"    script associated with it.  That script will be overwritten with a new\n"
"    script the next time the entry is connected.\n"
"\n"
"\n"
"\n"
"See Also: @LINK{PHONEBOOK_REVISE_ENTRY,Phone Book Revise Entry,12}\n"
"\n"
"@LINK{CONSOLE_MENU,TERMINAL Mode Commands , 12}\n"
"\n"
"@TOPIC{PHONEBOOK_REVISE_ENTRY,Phone Book - Revise Entry}\n"
"Phone book entries are changed in the revise entry screen.  The up and down\n"
"arrow keys (@BOLD{&#x2191;} and @BOLD{&#x2193;}) select between fields.  Fields are changed by either\n"
"typing in a new value, or pressing @BOLD{F2} or @BOLD{Space} to select from a list.\n"
"\n"
"@BOLD{F10} or @BOLD{Alt-Enter} saves changes, @BOLD{ESCAPE} abandons changes.\n"
"\n"
"Each entry has several fields described in detail below:\n"
"\n"
"@BOLD{Name}\n"
"This is the name that is recorded in the session log (when logging is\n"
"enabled) and shown on the alternate status line (@BOLD{Alt--}) in TERMINAL mode.\n"
"\n"
"@BOLD{Command Line}/@BOLD{Address}/@BOLD{Phone #}\n"
"The second field contains the information needed to connect to the remote\n"
"system.  For MODEM connections, this is a phone number; for CMDLINE\n"
"connections, this is a raw command line; for all other connections this is\n"
"a host name.\n"
"\n"
"@BOLD{Port}\n"
"For TELNET and SSH connections, this is the port to connect to.  This field\n"
"is not shown for any other connection types.\n"
"\n"
"@BOLD{Method}\n"
"Qodem supports several connection methods:\n"
"    @BOLD{MODEM}   - Calls the remote system by dialing a phone number.\n"
"    @BOLD{LOCAL}   - Spawns a local shell in a pseudo-tty.\n"
"    @BOLD{RLOGIN}  - Connects to the remote system using the rlogin protocol.\n"
"    @BOLD{SSH}     - Connects to the remote system using the ssh protocol.\n"
"    @BOLD{TELNET}  - Connects to the remote system using the telnet protocol.\n"
"    @BOLD{SOCKET}  - Connects to the remote system using a raw TCP socket.\n"
"    @BOLD{CMDLINE} - Spawns the command line in a pseudo-tty.\n"
"\n"
"@BOLD{Username}\n"
"This stores the logon username for the remote system.  This value can be\n"
"assigned to a keyboard macro using the @BOLD{$USERNAME} substitution string.  For\n"
"RLOGIN and SSH connections, if the username is not specified then the local\n"
"operating system user name is used.\n"
"\n"
"@BOLD{Password}\n"
"This stores the logon password for the remote system.  This value can be\n"
"assigned to a keyboard macro using the @BOLD{$PASSWORD} substitution string.\n"
"\n"
"@BOLD{Script}\n"
"This is the filename for a script to execute as soon as the connection is\n"
"established.  If the filename is specified but the script does not exist,\n"
"then upon the next connection the file will be created using QuickLearn.\n"
"\n"
"@BOLD{Emulation}\n"
"This is the terminal emulation to use on the remote system.\n"
"\n"
"@BOLD{Codepage}\n"
"This is the codepage for 8-bit characters used by some of the terminal\n"
"emulations.\n"
"\n"
"@BOLD{Capture File}\n"
"This is the filename to start capturing data to as soon as the connection is\n"
"established.\n"
"\n"
"@BOLD{Key File}\n"
"This is the filename of keyboard macros to load as soon as the connection is\n"
"established.\n"
"\n"
"@BOLD{Doorway}\n"
"This is the DOORWAY mode to set as soon as the connection is established.\n"
"Qodem can use the default setting specified in the global configuration file\n"
"for this entry, or it can override the global default to always use one of\n"
"the three supported DOORWAY modes (FULL, OFF, and MIXED).\n"
"\n"
"@BOLD{Port Settings}\n"
"This is the serial port settings to use to establish the connection.  Qodem\n"
"can use the default port settings specified in the modem configuration\n"
"screen, or it can use specific settings for this entry.\n"
"\n"
"@BOLD{Toggles}\n"
"This field is used to override the various TERMINAL Mode toggles for this\n"
"entry.  For example, one remote system may require line wrap to be OFF and\n"
"half duplex, while another requires line wrap to be ON and full duplex.\n"
"\n"
"@BOLD{Clear Call Info}\n"
"Pressing @BOLD{Enter} while this field is highlighted will reset the @BOLD{Times On}\n"
"to 0 and clear the @BOLD{Last Call} time.\n"
"\n"
"\n"
"\n"
"See Also: @LINK{CONFIGURATION,Configuration File     ,12}\n"
"\n"
"          @LINK{CODEPAGE,Codepages              ,12}\n"
"\n"
"          @LINK{EMULATION,Terminal Emulations    ,12}\n"
"\n"
"          @LINK{FUNCTION_KEYS,Function Keys          ,12}\n"
"\n"
"          @LINK{REFERENCE_99_QMODEM,Differences From Qmodem,12}\n"
"\n"
"@TOPIC{REFERENCE_97_COPYRIGHT,Reference - Copyright}\n"
"@BOLD{Qodem Terminal Emulator}\n"
"Written 2003-2021 by Autumn Lamonte ⚧ Trans Liberation Now\n"
"\n"
"To the extent possible under law, the author(s) have dedicated all\n"
"copyright and related and neighboring rights to this software to the\n"
"public domain worldwide. This software is distributed without any\n"
"warranty.\n"
"\n"
"Qodem has benefited from the contributions of several other people, and\n"
"thanks specifically:\n"
"\n"
"    @BOLD{John Friel III} for writing @BOLD{Qmodem} which was the inspiration\n"
"    for this project.\n"
"\n"
"    Paul Williams, for his excellent work documenting the DEC VT\n"
"    terminals at @BOLD{http://www.vt100.net/emu/dec_ansi_parser}.\n"
"\n"
"    Thomas E. Dickey, for his work on the xterm emulator and the\n"
"    ncurses library.\n"
"\n"
"    Both Mr. Williams and Mr. Dickey have answered numerous\n"
"    questions over the years in @BOLD{comp.terminals} that were archived\n"
"    and greatly aided the development of Qodem's emulation layer.\n"
"\n"
"    Bjorn Larsson, William McBrine, and the many developers involved in\n"
"    @BOLD{PDCurses} who dedicated their work to the public domain.\n"
"\n"
"    Miquel van Smoorenburg and the many developers involved in\n"
"    @BOLD{minicom} who licensed their work under the GNU General Public\n"
"    License.\n"
"\n"
"    Thomas BERNARD and the developers involved in @BOLD{miniupnpc} who\n"
"    licensed their work under a BSD-like license.\n"
"\n"
"    Jeff Gustafson for creating the Fedora RPM build script.\n"
"\n"
"    Martin Godisch for help in packaging for the deb build.\n"
"\n"
"    Jason Scott for creating @BOLD{BBS: The Documentary}.\n"
"\n"
"    Peter Gutmann for developing @BOLD{cryptlib} and licensing it under an open\n"
"    source compatible license.\n"
"\n"
"    Nathanael Culver for obtaining Qmodem 2.3, 4.2f, and QmodemPro 1.50.\n"
"\n"
"@TOPIC{REFERENCE_98_VERSIONS,Reference - Differences Between Text, X11, and Win32 Versions}\n"
"@BOLD{Qodem} comes in three flavors:\n"
"\n"
"  @BOLD{Text}  - The text version is a typical Unix-like curses-based\n"
"          application.  It can be run on the raw Linux console,\n"
"          inside an X11-based terminal emulator, or under Mac\n"
"          OS X Terminal.app.  This version will look different\n"
"          depending on what fonts are supplied with the end-user\n"
"          terminal.\n"
"\n"
"  @BOLD{X11}   - The X11 version is a native X11-based application.\n"
"          It provides its own font to look consistent on all systems.\n"
"\n"
"  @BOLD{Win32} - The Win32 version is a native Windows-based application\n"
"          that runs on most Win32-based systems including Windows 2000, XP\n"
"          Vista, and Windows 7.\n"
"\n"
" @BOLD{Text Version Notes}\n"
"\n"
" Compiling Qodem from source will build the text version by default.\n"
"\n"
" @BOLD{X11 Version Notes}\n"
"\n"
" The X11 version can be activated by passing --enable-x11 to configure.\n"
" Due to how the curses libraries are linked, a single qodem binary\n"
" cannot currently support both interfaces.  The X11 binary is built\n"
" as 'qodem-x11', and its man page is accessed by 'man qodem-x11'.\n"
"\n"
" The Debian package is 'qodem-x11'.  It can be installed entirely\n"
" independently of the normal 'qodem' package.\n"
"\n"
" When spawning other processes such as editors (@BOLD{Alt-L}, @BOLD{Alt-N}, @BOLD{Alt-V},\n"
" and editing files in the phone book), the mail reader (@BOLD{Alt-M}), or\n"
" shelling to the OS (@BOLD{Alt-R}), qodem spawns them inside a separate\n"
" X11-based terminal window, and displays the message \"Waiting On X11\n"
" Terminal To Exit...\" until the other terminal closes.  The default\n"
" terminal program is 'x-terminal-emulator'; this can be changed in\n"
" qodemrc.\n"
"\n"
" Mouse motion events do not work due to limitation in the PDCurses\n"
" mouse API.  Mouse clicks however do work.\n"
"\n"
" @BOLD{Win32 Version Notes}\n"
"\n"
" When spawning other processes such as editors (Alt-L, Alt-N, Alt-V,\n"
" and editing files in the phone book), the mail reader (Alt-M), or\n"
" shelling to the OS (Alt-R), qodem waits for the program to exit.\n"
"\n"
" Quicklearn scripts are written in Perl.  Strawberry Perl for Windows\n"
" is available at @BOLD{http://strawberryperl.com} .\n"
"\n"
" The left ALT key does not produce all combinations correctly in the\n"
" PDCurses-3.4 build due to Windows conventions.  The right Alt key\n"
" appears to work normally.  The win32a build appears to work\n"
" correctly.\n"
"\n"
" The Windows build uses Beep() rather than SDL for sounds.  This might\n"
" not work on Windows Vista and 64-bit XP systems.\n"
"\n"
" SSH connections (client or host) using cryptlib do not work when\n"
" compiled with the Borland compiler, nor when compiled with the Visual\n"
" C++ compiler and running on Windows 2000 or earlier.\n"
"\n"
" Mouse motion events do not work due to limitation in the PDCurses\n"
" mouse API.  Mouse clicks however do work.\n"
"\n"
"@TOPIC{REFERENCE_03_SCRIPT,Reference - Script Support}\n"
"Qodem does not have its own scripting language.  Instead, any\n"
"program that reads and writes to the standard input and output can\n"
"be run as a Qodem script:\n"
"\n"
"  * Characters sent from the remote connection are visible to the\n"
"    script in its standard input.\n"
"\n"
"  * Characters the script emits to its standard output are passed on\n"
"    the remote connection.\n"
"\n"
"  * Messages to the standard error are reported to the user and also\n"
"    recorded in the session log.\n"
"\n"
"Since scripts are communicating with the remote system and not Qodem\n"
"itself, they are unable to script Qodem's behavior, e.g. change the\n"
"terminal emulation, hangup and dial another phone book entry,\n"
"download a file, etc.  However, they can be written in any language,\n"
"and they can be tested outside Qodem.\n"
"\n"
"Scripts replace the user, and as such have similar constraints:\n"
"\n"
"  * Script standard input, output, and error must all be in UTF-8\n"
"    encoding.\n"
"\n"
"  * Scripts should send carriage return (0x0D, or \\r) instead of new\n"
"    line (0x0A, or \\n) to the remote side - the same as if a user\n"
"    pressed the Enter key.  They should expect to see either bare\n"
"    carriage return (0x0D, or \\r) or carriage return followed by\n"
"    newline (0x0D 0x0A, or \\r\\n) from the remote side.\n"
"\n"
"  * Input and output translate byte translation (the @BOLD{Alt-A} Translate\n"
"    Tables) are honored for scripts.\n"
"\n"
"  * While a script is running:\n"
"        - Zmodem and Kermit autostart are disabled.\n"
"        - Keyboard function key macros are disabled.\n"
"        - Qodem functions accessed through the @BOLD{Alt-}character\n"
"          combinations and PgUp/PgDn are unavailable.\n"
"        - Pressing @BOLD{Alt-P} will pause the script.\n"
"\n"
"  * While a script is paused:\n"
"        - The script will receive nothing on its standard input.\n"
"        - Anything in the script's standard output will be held\n"
"          until the script is resumed.\n"
"        - The script process will not be signaled; it may continue\n"
"          running in its own process.\n"
"        - The only @BOLD{Alt-}character function recognized is @BOLD{Alt-P} to\n"
"          resume the script.  All other @BOLD{Alt-} keys will be ignored.\n"
"        - Keys pressed will be sent directly to the remote system.\n"
"        - Keyboard function key macros will work.\n"
"\n"
"Scripts are launched in two ways:\n"
"\n"
"  * In TERMINAL mode, press @BOLD{Alt-F} and enter the script filename.\n"
"    The script will start immediately.\n"
"\n"
"  * In the phone book, add a script filename to a phone book entry.\n"
"    The script will start once that entry is connected.\n"
"\n"
"Script command-line arguments can be passed directly in both the\n"
"@BOLD{Alt-F} script dialog and the phone book linked script field.  For\n"
"example, pressing @BOLD{Alt-F} and entering \"my_script.pl arg1\" will launch\n"
"my_script.pl and with its first command-line argument ($ARGV[0] in\n"
"Perl) set to \"arg1\".\n"
"\n"
"@TOPIC{REFERENCE_04_TRANSLATE,Reference - Translate Tables}\n"
"Qodem has a slightly different method for translating bytes and\n"
"Unicode code points than Qmodem's @BOLD{Alt-A} Translate Table function.\n"
"The data flow is as follows:\n"
"\n"
"  * Bytes received from the wire are converted according to the 8-bit\n"
"    INPUT table before any other processing.  Similarly, bytes are\n"
"    converted through the 8-bit OUTPUT table before being written to\n"
"    the wire.\n"
"\n"
"  * Code points written to the screen are converted according to the\n"
"    Unicode INPUT table.  Code points read from the keyboard are\n"
"    converted through the Unicode OUTPUT table before being converted\n"
"    to UTF-8.\n"
"\n"
"  * When using 8-bit codepages, Qodem attempts to convert code points\n"
"    read from the keyboard back to the correct 8-bit codepage value\n"
"    based on several strategies.  If no values can be found, @BOLD{?} is\n"
"    sent instead.\n"
"\n"
"  * Capture, scrollback, screen dump, and keyboard macro files are\n"
"    stored in untranslated formats where possible.  \'raw\' capture\n"
"    records bytes before the 8-bit tables are applied; \'normal\'\n"
"    capture and other files record code points after 8-bit tables are\n"
"    applied but before Unicode tables are applied.\n"
"\n"
"  * 8-bit and Unicode tables can be specified for each phonebook\n"
"    entry.\n"
"\n"
"  * An EBCDIC-to-CP437 table is provided, but is largely untested.\n"
"\n"
"\n"
"\n"
"@TOPIC{REFERENCE_99_QMODEM,Reference - Differences From Qmodem}\n"
"Qodem strives to be as faithful as possible to Qmodem, however\n"
"sometimes it must deviate due to modern system constraints or in\n"
"order to go beyond Qmodem with entirely new features.  This\n"
"help topic describes those changes.\n"
"\n"
"@BOLD{       *       *       *       *       *       *       *       *}\n"
"\n"
"The default emulation for raw serial and command line connections is\n"
"VT102.\n"
"\n"
"The IBM PC Alt and Ctrl + <function key> combinations do not work\n"
"through the curses terminal library.  Ctrl-Home, Ctrl-End,\n"
"Ctrl-PgUp, Ctrl-PgDn, Shift-Tab, and Alt-Up have been given new key\n"
"combinations.\n"
"\n"
"The @BOLD{F2}, @BOLD{F4} and @BOLD{F10} function keys are often co-opted by modern\n"
"desktop environments and unavailable for qodem.  @BOLD{F2} and @BOLD{F10} are\n"
"still supported, but also have additional keys depending on\n"
"function.  Most of the time space bar can be used for @BOLD{F2} and the\n"
"Enter key for @BOLD{F10}.  The status bar will show the alternate\n"
"keystrokes.  Since @BOLD{F4} is currently only used to clear the Batch\n"
"Entry Window, no alternative keystroke is provided.\n"
"\n"
"The ESCAPE key can have a long delay (up to 1 second) under some\n"
"installations of curses.  It is still supported, but the backtick\n"
"(`) can also be used for faster response time.  See ESCDELAY in the\n"
"curses documentation.\n"
"\n"
"The program settings are stored in a text file usually called\n"
#ifdef Q_PDCURSES_WIN32
"qodemrc.txt .  They are hand-edited by the user rather than\n"
#else
"$HOME/.qodem/qodemrc.  They are hand-edited by the user rather than\n"
#endif
"another executable ala QINSTALL.EXE.  The @BOLD{Alt-N} Configuration\n"
"command loads the file into an editor for convenience.\n"
"\n"
"The batch entry window is a simple form permitting up to twenty\n"
"entries, each with a long filename.  Next to each entry is the file\n"
"size.  The Qmodem screen was limited to three directories each\n"
"containing up to twenty 8.3 DOS filenames, and did not report file\n"
"sizes.  The @BOLD{F3} \"Last Found\" function is not supported since many\n"
"systems use long filenames.\n"
"\n"
"The upload window for Ymodem, Zmodem, and Kermit contains a second\n"
"progress indicator for the batch percentage complete.\n"
"\n"
"@BOLD{Alt-X} Exit has only two options yes and no.  Qmodem offers a\n"
"third (exit with DTR up) that cannot be implemented using Linux-ish\n"
"termios.\n"
"\n"
"External protocols are not yet supported.\n"
"\n"
"Some functions are different in TERMINAL mode:\n"
"\n"
"    @BOLD{Key      Qodem function         Qmodem function}\n"
"    ----------------------------------------------------------\n"
"    @BOLD{Alt-L}    Log View               Change drive\n"
"    @BOLD{Alt-O}    Modem Config           Change directory\n"
"    @BOLD{Alt-2}    Backspace/Del Mode     80x25 (EGA/VGA)\n"
"    @BOLD{Alt-3}    Line Wrap              Debug Status Info\n"
"    @BOLD{Alt-4}    Display NULL           80x43/50 (EGA/VGA)\n"
"    @BOLD{Alt-9}    Serial Port            Printer Echo\n"
"    @BOLD{Alt-P}    Capture File           COM Parameters\n"
"    @BOLD{Alt-K}    Send BREAK             Change COM Port\n"
"    @BOLD{Alt-Y}    COM Parameters         -\n"
"    @BOLD{Alt-,}    ANSI Music             -\n"
"    @BOLD{Alt-\\}    Alt Code Key           -\n"
"    @BOLD{Alt-;}    Codepage               -\n"
"    @BOLD{Alt-:}    Colors                 -\n"
"\n"
"The Phone Book stores an arbitrary number of entries, not the\n"
"hard-coded 200 of Qmodem.\n"
"\n"
"The directory view popup window allows up to 20 characters for\n"
"filename, and the Unix file permissions are displayed in the\n"
"rightmost column.\n"
"\n"
"The directory browse window behaves differently.  Scrolling occurs a\n"
"full page at a time and the first selected entry is the first entry\n"
"rather than the first file.  Also, @BOLD{F4} can be used to toggle between\n"
"showing and hiding \"hidden files\" (dotfiles) - by default dotfiles\n"
"are hidden.\n"
"\n"
"The phone book displays the fully-qualified filename rather than the\n"
"base filename.\n"
"\n"
"VT100 escape sequences may change terminal settings, such as line\n"
"wrap, local echo, and duplex.  The original settings are not\n"
"restored after leaving VT100 emulation.\n"
"\n"
"DEBUG_ASCII and DEBUG_HEX emulations are not supported.  Qodem\n"
"instead offers a DEBUG emulation that resembles the output of a\n"
"programmer's hex editor: a byte offset, hexadecimal codes, and a\n"
"region of printable characters.\n"
"\n"
"TTY emulation is actually a real emulation.  The following control\n"
"characters are recognized: ENQ, BEL, BS, HT, LF, VT, FF, CR.  Also,\n"
"underlines that would overwrite characters in a typical character\n"
"cell display will actually underline the characters.  For example,\n"
"A^H_ ('A' backspace underline) will draw an underlined 'A' on a\n"
"console that can render underlined characters.\n"
"\n"
"ANSI emulation supports more codes than ANSI.SYS.  Specifically, it\n"
"responds to DSR 6 (Cursor Position) which many BBSes used to\n"
"\"autodetect\" ANSI, and it also supports the following ANSI X3.64\n"
"functions: ICH, DCH, IL, DL, VPA, CHA, CHT, and REP.  It detects and\n"
"discards the RIPScript auto-detection code (CSI !) to maintain a\n"
"cleaner display.\n"
"\n"
"The \"Tag Multiple\" command in the phone book does not support the\n"
"\"P<prefix><number><suffix>\" form of tagging.  Number prefixes and\n"
"suffixes in general are not supported.  Also, text searching in both\n"
"\"Tag Multiple\" and \"Find Text/Find Again\" is case-insensitive.\n"
"\n"
"The \"Set Emulation\" function has the ability to reset the current\n"
"emulation.  For example, if Qodem is in ANSI emulation, and you try\n"
"to change to ANSI emulation, a prompt will appear asking if you want\n"
"to reset the current emulation.  If you respond with 'Y' or 'y', the\n"
"emulation will be reset, otherwise nothing will change.  This is\n"
"particularly useful to recover from a flash.c-style of attack.\n"
"\n"
"\"WideView\" mode in the function key editor is not supported.\n"
"\n"
"\"Status Line Info\" changes the status line to show the\n"
"online/offline state, the name of the remote system (in the\n"
"phone book), and the current time.  Qmodem showed the name of the\n"
"system, the phone number, and the connect time.\n"
"\n"
"The scripting language is entirely different.  Qodem has no plans to\n"
"support Qmodem or QmodemPro scripts.\n"
"\n"
"Qmodem had several options to control Zmodem behavior: overwrite\n"
"files, crash recovery, etc.  Qodem does not expose these to the\n"
"user; Qodem's Zmodem implementation will always use crash recovery\n"
"or rename files to prevent overwrite when appropriate.\n"
"\n"
"Qodem always prompts for a filename for capture, screen dump, saved\n"
"scrollback, and session log.  (Qmodem only prompts if the files\n"
"do not already exist.)  Exception: if session log is specified on a\n"
"phone book entry toggle, qodem will not prompt for the filename but\n"
"use the default session log filename specified in qodemrc.\n"
"\n"
"Qodem supports two kinds of DOORWAY mode: \"Doorway FULL\" and\n"
"\"Doorway MIXED\".  \"Doorway FULL\" matches the behavior of\n"
"Qmodem's DOORWAY mode.  \"Doorway MIXED\" behaves like DOORWAY\n"
"EXCEPT for a list of commands to honor.  These commands are stored\n"
"in the qodemrc 'doorway_mixed_mode_commands' option.  \"Doorway\n"
"MIXED\" allows one to use @BOLD{PgUp}/@BOLD{PgDn} and @BOLD{Alt-X} (M-X) in Emacs yet\n"
"still have @BOLD{Alt-PgUp}/@BOLD{Alt-PgDn}, scrollback, capture, etc.\n"
"\n"
"Qodem includes a Alt Code Key function (@BOLD{Alt-\\}) for entering a raw\n"
"decimal byte value (0-255) or a 16-bit Unicode value (0-FFFF).\n"
"\n"
"Capture, screen dump, and saving scrollback can be saved in several\n"
"formats (configured in qodemrc).  \"normal\" behaves like Qmodem:\n"
"colors and emulation commands are stripped out, leaving a UTF-8\n"
"encoded black-and-white text file.  \"html\" saves in an HTML format\n"
"that includes colors.  For capture only, \"raw\" saves the raw\n"
"incoming byte stream before any UTF-8 decoding or emulation\n"
"processing.  For all save formats, \"ask\" will bring up a dialog to\n"
"select the save format every time the save is requested.  For\n"
"phone book entries that specify a capture file, if the capture type\n"
"is \"ask\" it will be saved in \"normal\" format.\n"
"\n"
"Host mode behaves differently.  It uses simple ASCII menus rather\n"
"than CP437 menus, provides no \"Optional Activities\", and has fewer\n"
"features than Qmodem's Host Mode implementation.  However, in\n"
"addition to listening on the modem, it can also listen on TCP ports\n"
"for raw socket, telnet, and ssh connections; optionally the TCP port\n"
"can be exposed via UPnP to the general Internet.\n"
"\n"
"The @BOLD{Alt-A} Translate Tables behave differently.  Instead of two\n"
"8-bit translate tables (INPUT and OUTPUT), Qodem has four tables: two\n"
"for 8-bit conversions on the wire and two for Unicode glyph conversions\n"
"to the screen and keyboard.  See the Translate Tables reference page for\n"
"more information.\n"
"\n"
"\n"
"\n"
"See Also: @LINK{CONFIGURATION,Configuration File     ,12}\n"
"\n"
"          @LINK{CODEPAGE,Codepages              ,12}\n"
"\n"
"          @LINK{EMULATION,Terminal Emulations    ,12}\n"
"\n"
"          @LINK{FILE_PROTOCOLS,File Transfer Protocols,12}\n"
"\n"
"          @LINK{FUNCTION_KEYS,Function Keys          ,12}\n"
"\n"
"          @LINK{CONSOLE_MENU,TERMINAL Mode Menu     ,12}\n"
"\n"
"@TOPIC{EMULATION,Terminal Emulations}\n"
"Qodem supports several common BBS-era and contemporary terminal emulations.\n"
"The details of each emulation is provided below.\n"
"\n"
"If the TERMINAL mode display is unresponsive or looks weird, you can always\n"
"reset the emulation by pressing @BOLD{Alt-G}, and then choosing the current\n"
"emulation.  A dialog will come up asking if you wish to reset the emulation,\n"
"choose @BOLD{Y} and the screen will be fixed.\n"
"\n"
"TTY, DEBUG, ANSI.SYS, AVATAR, LINUX, and XTERM emulations can use many\n"
"codepages for 8-bit characters including CP437 (PC VGA) and ISO-8859-1\n"
"glyphs.  When 8-bit characters are used, the C0 control characters\n"
"(0x00 - 0x1F, 0x7F) are mapped to the equivalent CP437 glyphs when there\n"
"are no glyphs defined in that range for the set codepage.\n"
"\n"
"@BOLD{ANSI}    - This is the DOS-based \"ANSI.SYS\" emulation plus a few more codes\n"
"than the original ANSI.SYS.  It supports DSR 6 (Cursor Position) which many\n"
"BBSes used to \"autodetect\" ANSI, and also the following ANSI X3.64\n"
"functions: ICH, DCH, IL, DL, VPA, CHA, CHT, and REP.  It also supports\n"
"\"ANSI Music\" sequences that follow the \"PLAY\" command syntax.  It can play\n"
"these tones either directly on the Linux console using its console beep, or\n"
"the Simple DirectMedia Layer (SDL) library.\n"
"\n"
"@BOLD{AVATAR}  - This is the BBS-era Avatar (\"Advanced Video Attribute Terminal\n"
"Assembler and Recreator\") emulation.  It supports all of the \"Extended\n"
"AVT/0\" commands as per George A. Stanislav's 1 May 1989 document except for\n"
"transmitting PC keyboard scan codes.  It also includes ANSI fallback\n"
"capability.\n"
"\n"
"@BOLD{VT52}    - This is a fairly complete VT52.  It does not support HOLD SCREEN\n"
"mode.  Since the original specified some graphics mode glyphs that do not\n"
"have direct Unicode equivalents (yet), those characters are rendered as a\n"
"hatch (&#x2592;).\n"
"\n"
"@BOLD{VT100}   - This is identical to @BOLD{VT102} except in how it responds to the\n"
"Device Attributes function.\n"
"\n"
"@BOLD{VT102}   - This is a fairly complete VT102.  The text version displays\n"
"double-width and double-height characters correctly when run under xterm,\n"
"X11, and Win32, but when run on the raw Linux console Qodem displays\n"
"double-width / double-height with a character followed by a space.  Qodem\n"
"also does not support smooth scrolling, printing, keyboard locking, and\n"
"hardware tests.  On the text Qodem build, some numeric keypad characters\n"
"also do not work correctly due to the console NUM LOCK handling.  132-\n"
"column output is only supported if the host console / window is already 132\n"
"columns or wider; Qodem does not issue resize commands to the host console\n"
"for 80/132 column switching codes.\n"
"\n"
"@BOLD{VT220}   - This is fairly complete VT220.  It converts the National\n"
"Replacement Character sets and DEC Supplemental Graphics characters to\n"
"Unicode.  In addition to the limitations of VT102, also the following VT220\n"
"features are not supported: user-defined keys (DECUDK), downloadable fonts\n"
"(DECDLD), VT100/ANSI compatibility mode (DECSCL).\n"
"\n"
"@BOLD{TTY}     - This emulation supports bare control character handling\n"
"(backspace, newline, etc.) and litte else.  Characters that would be\n"
"overwritten with underscores are instead made underlined as an old teletype\n"
"would do.\n"
"\n"
"@BOLD{DEBUG}   - This emulation displays all incoming characters in a format\n"
"similar to a programmer's hex dump.\n"
"\n"
"@BOLD{LINUX}   - This emulation has two modes: PC VGA (@BOLD{LINUX}) and UTF-8 (@BOLD{L_UTF8}).\n"
"This emulation is similar to VT102 but also recognizes the Linux private\n"
"mode sequences and ECMA-48 sequences VPA, CNL, CPL, ECH, CHA, VPA, VPR, and\n"
"HPA.  In addition to VT102 limitations, also the following Linux console\n"
"features are not supported: selecting ISO 646/ISO 8859-1/UTF-8, character\n"
"sets, X11 mouse reporting, and setting the scroll/caps/numlock leds.\n"
"\n"
"@BOLD{XTERM}   - This emulation has two modes: PC VGA (@BOLD{XTERM}) and UTF-8 (@BOLD{X_UTF8}).\n"
"It recognizes everything in LINUX, VT220, and a few more XTerm sequences.\n"
"It does not support most of the advanced features unique to XTerm such\n"
"as Tektronix 4014 mode, alternate screen buffer, and many more.  It is\n"
"intended to support XTerm applications that only use the sequences in\n"
"the 'xterm' terminfo entry.\n"
"\n"
"@BOLD{PETSCII} - This very loosely emulates a 40-column Commodore 64/128.  Colors\n"
"are not quite right due to the IBM CGA colors being unable to match the\n"
"Commodore colors.  Uppercase/lowercase affect new characters rather than\n"
"the whole screen.  This emulation requires the C64 Pro Mono font from Style,\n"
"available at @BOLD{http://style64.org/c64-truetype/} .  (Note that this font is\n"
"only licensed to software packages that are \"freely provided to end users\".\n"
"Entities wishing to ship packages that are not \"freely provided to end\n"
"users\" will need to either remove this font, or negotiate a separate\n"
"license agreement by contacting Style at @BOLD{http://style64.org/contact-style} .)\n"
"\n"
"@BOLD{ATASCII} - This emulates the 8-bit Atari 400/800 series.\n"
"\n"
"\n"
"\n"
"See Also: @LINK{CODEPAGE,Codepages              ,12}\n"
"\n"
"@LINK{REFERENCE_99_QMODEM,Differences From Qmodem,12}\n"
"\n"
"@TOPIC{CODEPAGE,Codepages}\n"
"A @BOLD{codepage} refers to the graphics that the user is supposed to see when\n"
"the remote system uses 8-bit bytes.  Before Unicode, most DOS, Unix, and\n"
"and Windows computers used different codepages to support different\n"
"languages.  For example, DOS codepage 437 (CP437) was used in the United\n"
"States by the IBM PC, while DOS codepage 500 (CP850) was used in western\n"
"Europe due to its better support for accented vowels, currency symbols,\n"
"etc.\n"
"\n"
"In Qodem, codepages are impacted by the terminal emulation used:\n"
"\n"
"TTY, DEBUG, ANSI, Avatar, LINUX, and XTERM emulations can use one\n"
"of many 8-bit codepages including both CP437 and ISO-8859-1.\n"
"VT52, VT100, and VT102 emulations are defined by their standards\n"
"to be 7-bit only, so only ASCII characters and DEC Special Graphics\n"
"characters will be seen.  PETSCII and ATASCII also have fixed codepages.\n"
"\n"
"VT220 emulation uses 8-bit characters but has multiple defined\n"
"codepages for the high-bit characters (DEC multinational\n"
"characters).\n"
"\n"
"L_UTF8 and X_UTF8 use UTF-8 encoded characters throughout.\n"
"\n"
"When Qodem connects to a remote Unix-like system, the LANG environment\n"
"variable must be set correctly to get the remote system to send\n"
"either 8-bit characters or UTF-8 encoded characters.\n"
"\n"
"\n"
"\n"
"See Also: @LINK{EMULATION,Emulation              ,12}\n"
"\n"
"@LINK{REFERENCE_99_QMODEM,Differences From Qmodem,12}\n"
"\n"
"@TOPIC{FILE_PROTOCOLS,File Transfer Protocols}\n"
"Qodem supports several common file transfer protocols.  The details of\n"
"each protocol is provided below.\n"
"\n"
"@BOLD{Xmodem} - Qodem supports original Xmodem, Xmodem-1k, Xmodem-CRC16, and\n"
"Xmodem-G.  Qodem also supports \"Xmodem Relaxed\", a variant of original\n"
"Xmodem with longer timeouts.\n"
"\n"
"@BOLD{Ymodem} - Qodem supports both original Ymodem and Ymodem-G.  During\n"
"a Ymodem batch download, if a file already exists it will be appended to.\n"
"\n"
"@BOLD{Zmodem} - Qodem supports Zmodem including resume (crash recovery) and\n"
"Zmodem auto-start.\n"
"\n"
"@BOLD{Kermit} - Qodem supports the original robust (slower) Kermit plus\n"
"streaming and autostart.  On reliable connections with streaming Kermit\n"
"should perform reasonably well.  Qodem does not yet support long or\n"
"extra-long packets, RESEND/REGET, server mode, or sliding windows.\n"
"\n"
"\n"
"\n"
"See Also: @LINK{REFERENCE_99_QMODEM,Differences From Qmodem,12}\n"
"\n"
"@TOPIC{FUNCTION_KEYS,Function Keys}\n"
"Qodem can send a custom sequence of characters instead of the normal\n"
"keystroke when certain keys are pressed.  This is known generally as a\n"
"keyboard macro or a re-mapped key.  An example of a keyboard macro is to\n"
"send the logon username to the remote side when the user presses @BOLD{F9}.\n"
"\n"
"Qodem can re-map the 12 function keys in four shifted states (normal,\n"
"Shifted, Alt-shifted, and Ctrl-shifted), the numeric keypad keys, and the\n"
"navigation keys (@BOLD{PgUp}, @BOLD{PgDn}, arrow keys, etc.).\n"
"\n"
"Keyboard mappings are organized according to terminal emulation.  A new set\n"
"of re-mappings is loaded when the terminal emulation is changed, either\n"
"through the @BOLD{Alt-G} @BOLD{Set Emulation} dialog or by connecting to a system through\n"
"the @BOLD{Phone Book}.  The @BOLD{default.key} mapping file can be used to set a macro for\n"
"all emulations.\n"
"\n"
"Within the @BOLD{Function Key Assignment} dialog, pressing @BOLD{S} will save any changes\n"
"made and exit the dialog.  Pressing @BOLD{Esc} or @BOLD{`} will exit the editor and\n"
"abandon any changes.  Pressing @BOLD{L} will bring up a dialog to load the mapping\n"
"file for a different emulation.\n"
"\n"
"A keyboard macro can have have any number of letters, numbers, punctuation,\n"
"etc.  To add a high-bit or Unicode character, press @BOLD{Alt-\\} to bring up the\n"
"Alt Code Key dialog.  Control characters can be added using the carat: @BOLD{^M}\n"
"(Control-M) is carriage return, @BOLD{^J} is line feed, etc.  To enter a bare\n"
"carat, use two carats @BOLD{^^}.\n"
"\n"
"In addition to regular characters and control characters, the following\n"
"special sequences can be used:\n"
"\n"
"    @BOLD{$USERNAME} - This will be replaced with the username field from the\n"
"                most recent connection.\n"
"\n"
"    @BOLD{$PASSWORD} - This will be replaced with the password field from the\n"
"                most recent connection.\n"
"\n"
"\n"
"\n"
"See Also: @LINK{EMULATION,Emulation              ,12}\n"
"\n"
"@LINK{PHONEBOOK,Phone Book             ,12}\n"
"\n"
"@LINK{PHONEBOOK_REVISE_ENTRY,Phone Book Revise Entry,12}\n"
"\n"
"@LINK{REFERENCE_99_QMODEM,Differences From Qmodem,12}\n"
"\n"
"@TOPIC{REFERENCE_10_OPTIONS,Reference - Command Line Options}\n"
"Qodem supports several command-line options:\n"
"\n"
"@BOLD{--capfile} FILENAME\n"
"    Capture the entire session and save to FILENAME.\n"
"\n"
"@BOLD{--logfile} FILENAME\n"
"    Start the session log and log to FILENAME.\n"
"\n"
"@BOLD{--keyfile} FILENAME\n"
"    Load keyboard macros from FILENAME.\n"
"\n"
"@BOLD{--xl8file} FILENAME\n"
"    Load 8-bit translate tables from FILENAME.\n"
"\n"
"@BOLD{--xlufile} FILENAME\n"
"    Load Unicode translate tables from FILENAME.\n"
"\n"
"@BOLD{--scrfile} FILENAME\n"
"    Start script FILENAME after connect.\n"
"\n"
"@BOLD{--config} FILENAME\n"
"    Load options from FILENAME (only).\n"
"\n"
"@BOLD{--create-config} FILENAME\n"
"    Write a new options file to FILENAME and exit.\n"
"\n"
"@BOLD{--read-only}\n"
"    Disable all writes to disk.  No config files will be saved, downloads\n"
"    are disabled, scripts are disabled, no screen dump, capture, logging,\n"
"    etc.\n"
"\n"
"@BOLD{--dotqodem-dir} DIRNAME\n"
"    Use DIRNAME instead of $HOME/.qodem for config/data files.\n"
"\n"
"@BOLD{-x}, @BOLD{--exit-on-completion}\n"
"    Exit after connection/command finishes.\n"
"\n"
"@BOLD{--doorway} MODE\n"
"    Select doorway MODE.  Valid values for MODE are \"doorway\", \"mixed\",\n"
"    and \"off\".\n"
"\n"
"@BOLD{--codepage} CODEPAGE\n"
"    Select codepage CODEPAGE.  See @BOLD{Alt-;} list for valid codepages.\n"
"    Example: \"CP437\", \"CP850\", \"Windows-1252\", etc.\n"
"\n"
"@BOLD{--emulation} EMULATION\n"
"    Select emulation EMULATION.  Valid values are \"ansi\", \"avatar\",\n"
"    \"debug\", \"vt52\", \"vt100\", \"vt102\", \"vt220\", \"linux\", \"l_utf8\",\n"
"    \"xterm\", \"x_utf8\", \"petscii\", and \"atascii\".\n"
"\n"
"@BOLD{--status-line} { on | off }\n"
"    If \"on\" enable status line.  If \"off\" disable status line.\n"
"\n"
"@BOLD{--play} MUSIC\n"
"    Play the MUSIC string as ANSI Music.  For more information on ANSI\n"
"    Music, see @BOLD{http://www.textfiles.com/artscene/ansimusic/}, or the\n"
"    GWBASIC PLAY statement.\n"
"\n"
"@BOLD{--play-exit}\n"
"    If @BOLD{--play} was specified, exit immediately after playing MUSIC.\n"
"\n"
"@BOLD{--geometry} COLSxROWS\n"
"    Request text window size COLS x ROWS.\n"
"\n"
"@BOLD{--dial} N\n"
"    Immediately open a connection to the phone book entry number N.  The\n"
"    first phone book entry has n=1.\n"
"\n"
"@BOLD{--connect} HOST\n"
"    Immediately open a connection to HOST.  The default connection method\n"
"    is \"ssh\" unless specified with the @BOLD{--connect-method} option.\n"
"\n"
"@BOLD{--connect-method} METHOD\n"
"    Use METHOD to connect for the @BOLD{--connect} option.  Valid values are\n"
"    \"ssh\", \"rlogin\", \"telnet\", and \"shell\".\n"
"\n"
"@BOLD{--username} USERNAME\n"
"    Use USERNAME when connecting with the @BOLD{--connect} option.  This value\n"
"    is passed on the command line to @BOLD{ssh}, and @BOLD{rlogin}.\n"
"\n"
"@BOLD{--xterm}\n"
"    Enables X11 terminal mode: phonebook and serial port are disabled,\n"
"    status line is turned off at startup, doorway is set to MIXED mode, a\n"
"    shell is opened, and Qodem exits when the shell session exits.\n"
"\n"
"@BOLD{args...}\n"
"    Spawn a local shell and pass args to it.\n"
"\n"
"@BOLD{--version}\n"
"Display program version and brief public domain dedication statement.\n"
"\n"
"@BOLD{--help}, @BOLD{-h}, @BOLD{-?}\n"
"    Display usage screen.\n"
"\n"
"@TOPIC{CONFIGURATION,Qodem Configuration File}\n"
#ifdef Q_PDCURSES_WIN32
"Qodem stores its configuration options in @BOLD{qodemrc.txt}.  In TERMINAL\n"
#else
"Qodem stores its configuration options in @BOLD{~/.qodem/qodemrc}.  In TERMINAL\n"
#endif
"Mode, @BOLD{Alt-N} will bring up the configuration file in an editor.  Each option\n"
"is described below:\n"
"\n"
"@BOLD{       *       *       *       *       *       *       *       *}\n"
"@TOPIC{HOST_MODE,Host Mode}\n"
"Qodem can act as a host system in which it becomes a miniature BBS system\n"
"for other users.  This can be extremely useful as a quick-and-dirty file and\n"
"message server.  @BOLD{Host Mode} is entered by pressing @BOLD{Alt-5} from the TERMINAL\n"
"mode screen and selecting the server listening option.  Qodem supports the\n"
"following listening options:\n" "\n"
#ifndef Q_NO_SERIAL
"    @BOLD{Modem}       - Qodem will attempt to answer the modem when it RINGs.\n"
"                  This is directly equivalent to the Qmodem(tm) Host Mode.\n"
"\n"
"    @BOLD{Serial Port} - Qodem will listen for users on the serial port.\n"
"\n"
#endif
"    @BOLD{Socket}      - Qodem accepts a raw TCP connection.  The @BOLD{TCP listen Port}.\n"
"                  dialog will appear to select the port.\n"
"\n"
"    @BOLD{telnetd}     - Qodem will listen for a TCP connection and use the telnet\n"
"                  protocol to set options to ensure clean file transfers.\n"
"                  The @BOLD{TCP Listen Port} dialog will appear to select the port.\n"
#ifdef Q_SSH_CRYPTLIB
"\n"
"    @BOLD{sshd}        - Qodem will listen for a TCP connection and use the ssh\n"
"                  protocol.  Users may login to the host using any ssh name\n"
"                  or password.  After logging in through ssh, the host login\n"
"                  prompt will ask for the same username/password\n"
"                  specified in the Qodem preferences file.  Like telnetd and\n"
"                  socket, the @BOLD{TCP Listen Port} dialog will appear to select\n"
"                  the port.\n"
#endif
"\n"
"The @BOLD{TCP Listen Port} dialog is used to select the listening port.  The\n"
"@BOLD{Next Available} option chooses any random available port.  The @BOLD{Enter Port}\n"
"option allows you to explicitly set the listening port."
#ifdef Q_UPNP
"  If the @BOLD{UPnP} option\n"
"is chosen, Qodem will attempt to open a port on the local router;  the\n"
"Internet-visible IP address and port will be displayed on the TERMINAL\n"
"screen.\n"
#else
"\n"
#endif
"\n"
"While waiting for a connection, pressing @BOLD{L} starts a local logon.  In local\n"
"logon mode, you can read and enter messages, but not do file transfers.\n"
"During either a local logon or remote connection, you can press @BOLD{Alt-C} to\n"
"enter chat mode, or press @BOLD{Alt-H} to forcibly hangup the user (terminate the\n"
"connection).\n"
"\n"
"\n"
"\n"
"See Also: @LINK{CONSOLE_MENU,TERMINAL Mode Commands , 12}\n"
"\n"
"@LINK{REFERENCE_99_QMODEM,Differences From Qmodem,12}\n"
"\n";
