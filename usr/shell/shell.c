/*
 * Copyright (C) 2013 F4OS Authors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <linker_array.h>

#include "shell.h"
#include "app.h"

LINKER_ARRAY_DECLARE(shell_apps)

static char *cmd_hist[SHELL_HISTORY]; // Command history buffer
static int   cmd_index;               // Next buffer index to fill

static void free_argv(int argc, char ***argv);
static void parse_command(char *command, int *argc, char ***argv);
static void run_command(char *command, int argc, char **argv);
static int start_match(char *buf, char *cmd, int n);
static int fill_next_match(char *buf, int n);

void shell(void) {
    int n;
    int argc;
    char **argv;
    char *command;

    // Allocate enough command buffers for history
    for (int i = 0; i < SHELL_HISTORY; i++) {
        cmd_hist[i] = malloc(SHELL_BUF_MAX);
        if (cmd_hist[i] == NULL) {
            printf("Shell unable to allocate buffer for command %d. You are on"
                    "your own. Goodbye.\r\n", i);
            return;
        }
        memset(cmd_hist[i], 0, SHELL_BUF_MAX);
    }
    cmd_index = 0;

    /* Wait for stdin/stdout - getc returns a negative error when it is not connected. */
    while (getc() < 0);


    while (1) {
        printf("%s", SHELL_PROMPT);
        command = cmd_hist[cmd_index];
        memset(command, 0, SHELL_BUF_MAX);
        n = 0;

        // Parse a single line into a command to run
        int done = 0;
        while (!done && n < SHELL_BUF_MAX) {
            int direction;
            int c = getc();
            switch (c) {
            case '\n':
            case '\r':
                done = 1;
                break;
            case '\b':
            case 0x7F: // Backspace
                if (n == 0) break;
                memmove(&command[n-1], &command[n], SHELL_BUF_MAX - n);
                command[SHELL_BUF_MAX-1] = '\0';
                n--;
                printf(LEFT CLEARLINE "%s", &command[n]);
                printf("\r\e[%dC", SHELL_PROMPT_LEN + n);
                break;
            case '\e': // Escape Character
                switch (getc()) {
                case '[': // Arrow keys
                    direction = 1;
                    switch (getc()) {
                    case 'A': // Up
                        direction = SHELL_HISTORY - 1;
                    case 'B': // Down
                        cmd_index = (cmd_index + direction) % SHELL_HISTORY;
                        command = cmd_hist[cmd_index];
                        printf("\r" CLEARLINE "%s%s", SHELL_PROMPT, command);
                        n = strnlen(command, SHELL_BUF_MAX);
                        break;
                    case 'C': // Right
                        if (command[n] != '\0') {
                            puts(RIGHT);
                            n++;
                        }
                        break;
                    case 'D': // Left
                        if (n > 0) {
                            puts(LEFT);
                            n--;
                        }
                    }
                }
                break;
            case '\t':
                n = fill_next_match(command, n);
                printf("\r" CLEARLINE "%s%s", SHELL_PROMPT, command);
                break;
            default:
                if (printable(c)) {
                    putc(c);
                    command[n] = c;
                    n++;
                }
            }
        }
        puts("\r\n");

        if (n >= SHELL_BUF_MAX) {
            printf("Line too long, max length is %d.\r\n", SHELL_BUF_MAX);
            continue;
        }

        parse_command(command, &argc, &argv);
        if (argc < 0) {
            /* Something bad happened, argv has already been freed */
            printf("%s: could not parse input\r\n", command);
        }
        else {
            run_command(command, argc, argv);
            free_argv(argc, &argv);
        }

        // Insert command into history, unless blank
        if (argc > 0) {
            cmd_index = (cmd_index + 1) % SHELL_HISTORY;
            command = cmd_hist[cmd_index];
        }
    }

    for (int i = 0; i < SHELL_HISTORY; i++) {
        free(cmd_hist[i]);
    }
}

void free_argv(int argc, char ***argv) {
    while (argc) {
        free((*argv)[argc-1]);
        argc--;
    }
    if (*argv) {
        free(*argv);
    }
}

static int start_match(char *buf, char *cmd, int n) {
    for(int i = 0; i < n; i++) {
        if(buf[i] != cmd[i])
            return 0;
    }
    return n;
}

static int fill_next_match(char *buf, int n) {
    struct command *command;

    LINKER_ARRAY_FOR_EACH(shell_apps, command) {
        int index = start_match(buf, command->name, n);
        if(index) {
           strncpy(&buf[index], &command->name[index], command->len - index);
           return command->len;
        }
    }

    return n;
}

void parse_command(char *command, int *argc, char ***argv) {
    char *begin = command;
    uint32_t n = 0;
    *argc = 0;
    *argv = NULL;

    /* skip leading whitespace */
    while (*command == ' ' || *command == '\t') {
        if (!*command) {
            break;
        }
        command++;
    }

    begin = command;

    while (*command) {
        *argc += 1;

        while (*command != ' ' && *command != '\t') {
            if (!*command || n >= SHELL_ARG_BUF_MAX) {
                break;
            }
            command++;
        }

        /* Skip whitespace */
        while (*command == ' ' || *command == '\t') {
            if (!*command) {
                break;
            }
            command++;
        }
    }

    if (!*argc) {
        return;
    }

    *argv = malloc(sizeof(char*) * *argc);
    if (!*argv) {
        printf("Memory error: unable to allocate space for argv table\r\n");
        return;
    }

    command = begin;
    *argc = 0;

    while (*command) {
        uint32_t i = 0;
        n = 0;
        *argc += 1;
        (*argv)[*argc-1] = malloc(SHELL_ARG_BUF_MAX);

        if ((*argv)[*argc-1] == NULL) {
            printf("Memory error: unable to allocate space for arguement\r\n");
            (*argc)--;
            while (*argc) {
                free((*argv)[*argc-1]);
                (*argc)--;
            }
            free(*argv);
            *argc = -1;
            argv = NULL;
            return;
        }

        while (*command != ' ' && *command != '\t') {
            if (!*command || n >= (SHELL_ARG_BUF_MAX-1)) {
                break;
            }
            (*argv)[*argc-1][i++] = *command++;
        }
        (*argv)[*argc-1][i] = '\0';

        /* Skip whitespace */
        while (*command == ' ' || *command == '\t') {
            if (!*command) {
                break;
            }
            command++;
        }
    }
}

void run_command(char *command, int argc, char **argv) {
    struct command *current_command;

    if (!argc) {
        return;
    }

    LINKER_ARRAY_FOR_EACH(shell_apps, current_command) {
        if (!strncmp(argv[0], current_command->name, SHELL_BUF_MAX)) {
            current_command->fptr(argc, argv);
            return;
        }
    }

    printf("%s: command not found\r\n", argv[0]);
}

void help(int argc, char **argv) {
    struct command *command;

    puts("Available commands:\r\n");

    LINKER_ARRAY_FOR_EACH(shell_apps, command) {
        printf("%s\r\n", command->name);
    }
}
DEFINE_APP(help)

void history(int argc, char **argv) {
    int count = 0; // printed counter, skip blank lines

    printf("History:\r\n");
    // iterate through history list, printing nonempty items
    for (int i = 1; i < SHELL_HISTORY + 1; i++) {
        int j = (cmd_index + i) % SHELL_HISTORY;
        if (cmd_hist[j][0] == '\0')
            continue;
        printf("\t%d: %s\r\n", count, cmd_hist[j]);
        count++;
    }
}
DEFINE_APP(history)
