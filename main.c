
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "util.h"
#include "mfs.h"

int main_repl(mfs_t *mfs, int optc, char **optv);

int main(int argc, char **argv) {
    if(argc < 2) {
        fprintf(stderr, "No command specified\n");
        exit(EXIT_FAILURE);
    } else if(argc < 3) {
        fprintf(stderr, "Missing file name\n");
        exit(EXIT_FAILURE);
    }

    char *filename = argv[1];
    char *cmd = argv[2];

    char **optv;
    int optc;
    if(argc > 3) {
        optv = &argv[3];
        optc = argc - 3;
    } else {
        optv = NULL;
        optc = 0;
    }

    int ret;
    if(strequals("create", cmd)) {
        ret = mfs_create(filename, optc, optv);
    } else if(strequals("repl", cmd)) {
        mfs_t *mfs = mfs_open(filename);
        if(mfs == NULL) {
            fprintf(stderr, "Failed to open MFS file\n");
            return EXIT_FAILURE;
        }

        ret = main_repl(mfs, optc, optv);

        mfs_free(mfs);
    } else {
        fprintf(stderr, "Unknown command %s\n", cmd);
        ret = EXIT_FAILURE;
    }

    return ret;
}

#define LINE_MAXLEN 1024
#define ARGS_MAX 2

int main_repl(mfs_t *mfs, int optc, char **optv) {
    int c;
    bool exit = false;
    char line[LINE_MAXLEN + 1];
    char *args[ARGS_MAX];
    int arg_count = 0;
    int i = 0;
    while(1) {
        printf("> ");

        while((c = getchar()) != '\n') {
            if(c == EOF) {
                exit = true;
                break;
            }

            if(i < LINE_MAXLEN) {
                line[i] = (char) c;
                i++;
            }
        }

        if(exit) {
            putchar('\n');
            break;
        }

        line[i] = '\0';
        i = 0;

        arg_count = 0;
        char *token = strtok(line, " ");
        while(token != NULL) {
            if(arg_count < ARGS_MAX) {
                args[arg_count] = token;
                arg_count++;
            } else {
                fprintf(stderr, "Too many arguments\n");
            }
            token = strtok(NULL, " ");
        }

        if(arg_count < 1) {
            continue;
        }

        char *cmd = args[0];

        if(strequals(cmd, "exit")) {
            printf("Bye\n");
            break;
        } else if(strequals(cmd, "sync")) {
            fflush(mfs->f);
        } else if(strequals(cmd, "info")) {
            mfs_info(mfs);
        } else if(strequals(cmd, "mkdir")) {
            if(arg_count >= 2) {
                mfs_mkdir(mfs, args[1]);
            } else {
                fprintf(stderr, "Missing path\n");
            }
        } else if(strequals(cmd, "rmdir")) {
            if(arg_count >= 2) {
                mfs_rmdir(mfs, args[1]);
            } else {
                fprintf(stderr, "Missing path\n");
            }
        } else if(strequals(cmd, "ls")) {
            if(arg_count >= 2) {
                mfs_ls(mfs, args[1]);
            } else {
                fprintf(stderr, "Missing path\n");
            }
        } else if(strequals(cmd, "touch")) {
            if(arg_count >= 2) {
                mfs_touch(mfs, args[1]);
            } else {
                fprintf(stderr, "Missing file name\n");
            }
        } else {
            fprintf(stderr, "Unknown command\n");
        }
    }
    return 0;
}
