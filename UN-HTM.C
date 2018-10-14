
/* **********************************************************************
   * UN-HTM.C                                                           *
   *                                                                    *
   * Converts an HTML file into an ASCII text file.                     *
   *                                                                    *
   ********************************************************************** */

#include <alloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dir.h>
#include <dos.h>

/* **********************************************************************
   * Any defines for our program are placed here.                       *
   *                                                                    *
   ********************************************************************** */

#define TRUE                    1
#define FALSE                   0
#define The_Version             "1.0"
#define MAXIMUM_CONTROL_WORDS   200

/* **********************************************************************
   * Define our exit errorlevel values                                  *
   *                                                                    *
   ********************************************************************** */

#define No_Problem              0
#define No_Arguments            10
#define Cant_Find_Input_File    11
#define Cant_Create_Work_File   12
#define Strange_HTML_File       13
#define No_Memory               14

/* **********************************************************************
   * Define the enumerated data type for the state transitions.         *
   *                                                                    *
   ********************************************************************** */

    static enum State_Machine {
        Uninitialized,
        Clear_Text,
        Underscore_Text,
        Cite_Text,
        Evaluate,
    } current_state;

/* **********************************************************************
   * Define global data storage                                         *
   *                                                                    *
   ********************************************************************** */

    static FILE *fin, *fout;
    static unsigned char want_line_length;
    static unsigned char control_text[201];
    static unsigned long input_byte_count;
    static unsigned long output_byte_count;
    static unsigned long control_word_count;
    static unsigned long unknown_control_word_count;
    static unsigned long input_line_count;
    static unsigned long output_line_count;
    static unsigned short line_byte_count;
    static unsigned short reference_count;

/* **********************************************************************
   * Define function prototypes which are referenced before they are    *
   * defined.  We do this because we include function pointers in our   *
   * array of structures.                                               *
   *                                                                    *
   ********************************************************************** */

    static void add_underscore(void);
    static void process_reference(void);
    static void process_link_rev(void);

/* **********************************************************************
   * Command text strings and the state they drive us to is placed here *
   * into this array.                                                   *
   *                                                                    *
   ********************************************************************** */

    static struct State_Driver {
        unsigned char *text;
        unsigned char more_parameters;
        unsigned char test_length;
        enum State_Machine new_state;
        void (*aux_function)(void);
    } ;

    static struct State_Driver state_driver[] = {
        "cite",        FALSE,  4, Underscore_Text,    add_underscore,
        "/cite",       FALSE,  5, Clear_Text,         add_underscore,
        "u",           FALSE,  1, Underscore_Text,    add_underscore,
        "/u",          FALSE,  2, Clear_Text,         add_underscore,
        "ul",          FALSE,  2, Underscore_Text,    add_underscore,
        "/ul",         FALSE,  3, Clear_Text,         add_underscore,
        "i",           FALSE,  1, Underscore_Text,    add_underscore,
        "/i",          FALSE,  2, Clear_Text,         add_underscore,
        "p",           FALSE,  1, Clear_Text,         NULL,
        "ol",          FALSE,  2, Clear_Text,         NULL,
        "/ol",         FALSE,  3, Clear_Text,         NULL,
        "li",          FALSE,  2, Clear_Text,         NULL,
        "title",       FALSE,  5, Clear_Text,         NULL,
        "/title",      FALSE,  6, Clear_Text,         NULL,
        "html",        FALSE,  4, Clear_Text,         NULL,
        "/html",       FALSE,  5, Clear_Text,         NULL,
        "head",        FALSE,  4, Clear_Text,         NULL,
        "/head",       FALSE,  5, Clear_Text,         NULL,
        "body",        FALSE,  4, Clear_Text,         NULL,
        "/body",       FALSE,  5, Clear_Text,         NULL,
        "center",      FALSE,  6, Clear_Text,         NULL,
        "/center",     FALSE,  7, Clear_Text,         NULL,
        "a",           FALSE,  1, Clear_Text,         NULL,
        "/a",          FALSE,  2, Clear_Text,         NULL,
        "b",           FALSE,  1, Clear_Text,         NULL,
        "/b",          FALSE,  2, Clear_Text,         NULL,
        "hr",          FALSE,  2, Clear_Text,         NULL,
        "br",          FALSE,  2, Clear_Text,         NULL,
        "h1",          FALSE,  2, Clear_Text,         NULL,
        "/h1",         FALSE,  3, Clear_Text,         NULL,
        "h2",          FALSE,  2, Clear_Text,         NULL,
        "/h2",         FALSE,  3, Clear_Text,         NULL,
        "h3",          FALSE,  2, Clear_Text,         NULL,
        "/h3",         FALSE,  3, Clear_Text,         NULL,
        "h4",          FALSE,  2, Clear_Text,         NULL,
        "/h4",         FALSE,  3, Clear_Text,         NULL,
        "address",     FALSE,  7, Clear_Text,         NULL,
        "/address",    FALSE,  8, Clear_Text,         NULL,
        "em",          FALSE,  2, Clear_Text,         NULL,
        "/em",         FALSE,  3, Clear_Text,         NULL,
        "pre",         FALSE,  3, Clear_Text,         NULL,
        "/pre",        FALSE,  4, Clear_Text,         NULL,
        "blockquote",  FALSE, 10, Clear_Text,         NULL,
        "/blockquote", FALSE, 11, Clear_Text,         NULL,
        "strong",      FALSE,  6, Clear_Text,         NULL,
        "/strong",     FALSE,  7, Clear_Text,         NULL,
        "meta name",   TRUE,   9, Clear_Text,         NULL,
        "!doctype",    TRUE,   8, Clear_Text,         NULL,
        "img ",        TRUE,   4, Clear_Text,         process_reference,
        "ahref",       TRUE,   5, Clear_Text,         process_reference,
        "a href",      TRUE,   6, Clear_Text,         process_reference,
        "link rev",    TRUE,   8, Clear_Text,         process_link_rev,
        "base href",   TRUE,   9, Clear_Text,         process_reference,
        "a name",      TRUE,   6, Clear_Text,         NULL,
        "* end *",     FALSE,  0, Uninitialized,      NULL
    } ;

/* **********************************************************************
   * When we have a reference, we add it to the linked list of          *
   * references so that it can be found later.                          *
   *                                                                    *
   ********************************************************************** */

    static struct Reference_List {
        char text[101];
        struct Reference_List *next;
    } *rl_first, *rl_last, *rl_next, *rl_point;

/* **********************************************************************
   * Add an underscore to the current output.                           *
   *                                                                    *
   ********************************************************************** */

static void add_underscore(void)
{
    (void)fputc('_', fout);
    output_byte_count++;
    line_byte_count++;
}

/* **********************************************************************
   * Extract the reference and include it into the linked list of       *
   * references.  Then place a reference designator into the output     *
   * file.                                                              *
   *                                                                    *
   ********************************************************************** */

static void process_reference(void)
{
    unsigned char c_count = 0;
    unsigned char *atpoint;
    unsigned char report[101];
    unsigned char output_length;

/*
 * Make sure we have a quote in it.  If we don't then we
 * should ignore the text.
 */

    if (NULL == strchr(control_text, '"')) {
        (void)printf("Reference missing quotes\n");
        return;
    }

/*
 * Allocate memory for the data structure of the linked list
 */

    rl_point = (struct Reference_List *)
        farmalloc(sizeof(struct Reference_List));

    if (rl_point == (struct Reference_List *)NULL) {
        (void)printf("I ran out of memory!\n");
        fcloseall();
        exit(No_Memory);
    }

/*
 * Copy it over
 */

    atpoint = control_text;

    while (*atpoint && *atpoint != '"') {
        atpoint++;
    }

    atpoint++;

    while (*atpoint && *atpoint != '"') {
        rl_point->text[c_count++] = *atpoint++;
    }

    rl_point->text[c_count] = (unsigned char)NULL;

/*
 * Append the entry in the linked list.
 */

    rl_point->next = (struct Reference_List *)NULL;

    if (rl_first == (struct Reference_List *)NULL) {
        rl_first = rl_point;
    }
    else {
        rl_last->next = rl_point;
    }

    rl_last = rl_point;

    reference_count++;

    output_length = sprintf(report, "[ref%03d]", reference_count);
    (void)fputs(report, fout);
    output_byte_count += output_length;
    line_byte_count += output_length;
}

/* **********************************************************************
   * Process link rev                                                   *
   *                                                                    *
   ********************************************************************** */

static void process_link_rev(void)
{
    unsigned char *atpoint;
    unsigned char report[101];

/*
 * Make sure an HREF exists.  If not, ignore it
 */

    if ((atpoint = strstr(control_text, "HREF")) == NULL) {
        if ((atpoint = strstr(control_text, "href")) == NULL) {
            return;
        }
        else {
            (void)strcpy(report, atpoint);
            (void)strcpy(control_text, report);
        }
    }
    else {
        (void)strcpy(report, atpoint);
        (void)strcpy(control_text, report);
    }

/*
 * We've skipped the first part, now process the rest
 */

    process_reference();
}

/* **********************************************************************
   * Initialize this program                                            *
   *                                                                    *
   ********************************************************************** */

static void initialize(void)
{
    current_state = Clear_Text;
    input_byte_count = 0L;
    output_byte_count = 0L;
    control_word_count = 0L;
    unknown_control_word_count = 0L;
    input_line_count = 0L;
    output_line_count = 0L;
    want_line_length = FALSE;
    line_byte_count = 0;
    reference_count = 0;
    rl_first = rl_last = rl_next = rl_point = (struct Reference_List *)NULL;
}

/* **********************************************************************
   * Throw away the reference linked list                               *
   *                                                                    *
   ********************************************************************** */

static void throw_away_accumulated_linked_list(void)
{
    struct Reference_List *hold;

    rl_point = rl_first;

    while (rl_point) {
        hold = rl_point->next;
        farfree(rl_point);
        rl_point = hold;
    }     

    rl_first = rl_last = rl_point = (struct Reference_List *)NULL;
}

/* **********************************************************************
   * Say hello.                                                         *
   *                                                                    *
   ********************************************************************** */

static void say_hello(void)
{
    (void)printf("\n\nUN-HTM Version %s\n\n", The_Version);
}

/* **********************************************************************
   * We have a control text being pointed to.  Evaluate what the        *
   * command text is and either transit to a new state else discard the *
   * command entirely.                                                  *
   *                                                                    *
   * Many of the commands are checked for here and then dismissed.  The *
   * reason why they're checked for is so that we can add functionality *
   * to this conversion program later on if needed.                     *
   *                                                                    *
   ********************************************************************** */

static void transit_states(char *control_text)
{
    unsigned short loop;
    unsigned char lengths_match;
    unsigned char control_length;

/*
 * We have another control word so incriment the counter
 */

    control_word_count++;

/*
 * Find out how long our control word is
 */

    control_length = strlen(control_text);

/*
 * Search for it in the table
 */

    for (loop = 0; state_driver[loop].test_length; loop++) {
        if (control_length == state_driver[loop].test_length) {
            lengths_match = TRUE;
        }
        else {
            lengths_match = FALSE;
        }

        if (! strnicmp(state_driver[loop].text, control_text, state_driver[loop].test_length)) {

            if (lengths_match) {
                current_state = state_driver[loop].new_state;

                if (state_driver[loop].aux_function != NULL) {
                    state_driver[loop].aux_function();
                }

                return;
            }

            if (state_driver[loop].more_parameters) {
                current_state = state_driver[loop].new_state;

                if (state_driver[loop].aux_function != NULL) {
                    state_driver[loop].aux_function();
                }

                return;
            }
        }
    }

/*
 * If we get here it means we encountered a command word
 * that we don't know about.  Report the string and then transit
 * back to the Clear_Text state
 */

    (void)printf("Unknown control word: [%s] length %d\n",
        control_text, control_length);

    current_state = Clear_Text;
    unknown_control_word_count++;
}

/* **********************************************************************
   * Process the input file.                                            *
   *                                                                    *
   ********************************************************************** */

static void process_file(void)
{
    unsigned char byte;
    unsigned char tc_count;

/*
 * Initialize this function
 */

    tc_count = 0;
    line_byte_count = 0;

/*
 * Go through the file
 */

    while (! feof(fin)) {
        byte = fgetc(fin);
        input_byte_count++;

        if (! feof(fin)) {
            switch (current_state) {
                case Evaluate:
                {
                    switch (byte) {
                        case '>':
                        {
                            control_text[tc_count] = (unsigned char)NULL;
                            transit_states(control_text);
                            tc_count = 0;
                            break;
                        }

                        case 0x0a:
                        {
                            input_line_count++;
                            break;
                        }

                        case 0x0d:
                        {
                            break;
                        }

                        default:
                        {
                            control_text[tc_count++] = byte;

                            if (tc_count >= 200) {
                                (void)printf("ERROR: Command text exceeds 200 characters!\n");
                                fcloseall();
                                exit(Strange_HTML_File);
                            }
                            break;
                        }
                    }
                    break;
                }

                case Clear_Text:
                {
                    switch (byte) {
                        case '<':
                        {
                            current_state = Evaluate;
                            break;
                        }

                        case 0x0a:
                        {
                            input_line_count++;
                            (void)fputc(0x0d, fout);
                            (void)fputc(0x0a, fout);
                            output_byte_count += 2;
                            output_line_count++;
                            line_byte_count = 0;
                            break;
                        }

                        case 0x0d:
                        {
                            break;
                        }

                        case ' ':
                        {
                            (void)fputc(byte, fout);
                            output_byte_count++;
                            line_byte_count++;

                            if (want_line_length) {
                                if (line_byte_count > 65) {
                                    (void)fputc(0x0d, fout);
                                    (void)fputc(0x0a, fout);
                                    output_byte_count += 2;
                                    output_line_count++;
                                    line_byte_count = 0;
                                }
                            }
                            break;
                        }

                        default:
                        {
                            (void)fputc(byte, fout);
                            output_byte_count++;
                            line_byte_count++;
                            break;
                        }
                    }
                    break;
                }

                case Underscore_Text:
                case Cite_Text:
                {
                    switch (byte) {
                        case '<':
                        {
                            current_state = Evaluate;
                            break;
                        }

                        case ' ':
                        {
                            (void)fputc('_', fout);
                            output_byte_count++;
                            line_byte_count++;

                            if (want_line_length) {
                                if (line_byte_count > 65) {
                                    (void)fputc(0x0d, fout);
                                    (void)fputc(0x0a, fout);
                                    output_byte_count += 2;
                                    output_line_count++;
                                    line_byte_count = 0;
                                }
                            }
                            break;
                        }

                        case 0x0a:
                        {
                            input_line_count++;
                            (void)fputc(0x0d, fout);
                            (void)fputc(0x0a, fout);
                            output_byte_count += 2;
                            output_line_count++;
                            line_byte_count = 0;
                            break;
                        }

                        case 0x0d:
                        {
                            break;
                        }

                        default:
                        {
                            (void)fputc(byte, fout);
                            output_byte_count++;
                            line_byte_count++;
                            break;
                        }
                    }
                    break;
                }

                default:
                {
                    (void)printf("\n\n!!!  ERROR: Programming error!\n");
                    current_state = Clear_Text;
                    break;
                }
            }
        }
    }

/*
 * Close the input file
 */

    (void)fclose(fin);
}

/* **********************************************************************
   * Offer all of the references that were found in the file.           *
   *                                                                    *
   ********************************************************************** */

static void offer_references(void)
{
    unsigned short count;
    unsigned char report[101];

/*
 * Start at the top of the linked list
 */

    rl_point = rl_first;
    count = 1;

/*
 * Give us a new line
 */

    (void)fputc(0x0d, fout);
    (void)fputc(0x0a, fout);

/*
 * And include the references
 */


    while (rl_point) {

        (void)sprintf(report, "[ref%03d] %s%c%c",
            count++, rl_point->text, 0x0d, 0x0a);

        (void)fputs(report, fout);

        rl_point = rl_point->next;
    }

/*
 * Put a blank line at the end of the reference list
 */

    (void)fputc(0x0d, fout);
    (void)fputc(0x0a, fout);
}

/* **********************************************************************
   * Offer the final report                                             *
   *                                                                    *
   ********************************************************************** */

static void offer_report(void)
{
    (void)printf("\n-End processing-\n");

    (void)printf("There were %ld bytes read and %ld bytes written\n",
        input_byte_count, output_byte_count);

    (void)printf("There were %ld control words found, %ld were unknown\n",
        control_word_count, unknown_control_word_count);

    (void)printf("There were %ld lines read and %ld lines written\n",
        input_line_count, output_line_count);

    (void)printf("There were %d references\n", reference_count);
}

/* **********************************************************************
   * Process the file                                                   *
   *                                                                    *
   ********************************************************************** */

static void process_this_file(char *fname)
{
    unsigned char new_name[101];
    unsigned char *atpoint;
    unsigned char c_count;

/*
 * Build a new name
 */

    atpoint = fname;
    c_count = 0;

    while (*atpoint && *atpoint != '.') {
        new_name[c_count++] = *atpoint++;
    }

    new_name[c_count] = (unsigned char)NULL;
    (void)strcat(new_name, ".TXT");

    (void)printf("Converting file: [%s] to [%s]\n\n", fname, new_name);

/*
 * Open the input file
 */

    if ((fin = fopen(fname, "rb")) == (FILE *)NULL) {
        (void)printf("I couldn't open HTML file: %s\n", fname);
        fcloseall();
        exit(Cant_Find_Input_File);
    }

/*
 * Create temporary output work file
 */

    if ((fout = fopen("tn-htm.wrk", "wb")) == (FILE *)NULL) {
        (void)printf("I can't create output work file: tn-htm.wrk\n");
        (void)fclose(fin);
        fcloseall();
        exit(Cant_Create_Work_File);
    }

/*
 * Start going through the input file
 */

    process_file();
    offer_references();

/*
 * Now that the file has been processed, offer a report
 */

    offer_report();

/*
 * Close the output file
 */

    (void)fclose(fout);

/*
 * Rename the file
 */

    (void)unlink(fname);
    (void)rename("TN-HTM.WRK", new_name);
}

/* **********************************************************************
   * Process the argument handed to us                                  *
   *                                                                    *
   ********************************************************************** */

static void process_argument(char *fname)
{
    int result;
    struct ffblk direct;

/*
 * See if we have the first one
 */

    result = findfirst(fname, &direct, FA_RDONLY | FA_ARCH);

    if (result != 0) {
        (void)printf("There are no files for: %s\n", fname);
        return;
    }

    process_this_file(direct.ff_name);
    throw_away_accumulated_linked_list();
    initialize();

/*
 * Now continue until they're all done
 */

    while (result == 0) {
        result = findnext(&direct);

        if (result == 0) {
            process_this_file(direct.ff_name);
            throw_away_accumulated_linked_list();
            initialize();
        }
    }
}

/* **********************************************************************
   * The main entry point.                                              *
   *                                                                    *
   ********************************************************************** */

void main(int argc, char *argv[])
{
/*
 * See if we have at least one argument
 */

    if (argc == 1) {
        (void)printf("Syntax:   un-htm {filename}\n");
        fcloseall();
        exit(No_Arguments);
    }

/*
 * Say hello
 */

    say_hello();

/*
 * Initialize this program
 */

    initialize();

/*
 * Go through the process with the argument
 */

    process_argument(argv[1]);

/*
 * Exit this program with success
 */

    exit(No_Problem);
}

