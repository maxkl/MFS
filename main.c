
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "mfs.h"

#define strequal(a, b) (strcmp((a), (b)) == 0)

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

    mfs_t *mfs = mfs_open(filename);
    if(mfs == NULL) {
        fprintf(stderr, "Failed to open MFS file\n");
        return EXIT_FAILURE;
    }

    int ret;
    if(strcmp("repl", cmd) == 0) {
        ret = main_repl(mfs, optc, optv);
    } else if(strcmp("create", cmd) == 0) {
        ret = mfs_create(filename, optc, optv);
    } else if(strcmp("dump", cmd) == 0) {
        ret = mfs_dump(mfs, optc, optv);
    } else if(strcmp("do", cmd) == 0) {
        ret = mfs_do(mfs, optc, optv);
    } else {
        fprintf(stderr, "Unknown command %s\n", cmd);
        ret = EXIT_FAILURE;
    }

    mfs_free(mfs);

    return ret;
}

#define LINE_MAXLEN 10

int main_repl(mfs_t *mfs, int optc, char **argv) {
    int c;
    bool exit = false;
    char line[LINE_MAXLEN];
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
            }

            i++;
        }

        if(exit) break;

        line[i] = '\0';

        if(strequal(line, "lsroot")) {
            mfs_ls(mfs, "/");
        } else {
            fprintf(stderr, "Unknown command\n");
        }

        i = 0;
    }
    return 0;
}
