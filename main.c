
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "mfs.h"

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
    if(strcmp("create", cmd) == 0) {
        ret = mfs_create(filename, optc, optv);
    } else if(strcmp("dump", cmd) == 0) {
        ret = mfs_dump(filename, optc, optv);
    } else if(strcmp("do", cmd) == 0) {
        ret = mfs_do(filename, optc, optv);
    } else {
        fprintf(stderr, "Unknown command %s\n", cmd);
        exit(EXIT_FAILURE);
    }

    return ret;
}
