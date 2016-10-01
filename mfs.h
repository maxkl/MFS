
#pragma once

#include <stdint.h>

typedef struct {
    uint16_t block_size;
    uint16_t block_count;
    size_t alloc_table_base;
    size_t blocks_base;
    uint8_t *alloc_table;
    FILE *f;
} mfs_t;

mfs_t *mfs_open(char *filename);
void mfs_free(mfs_t *mfs);

int mfs_create(char *filename, int optc, char **optv);

int mfs_info(mfs_t *mfs);
int mfs_mkdir(mfs_t *mfs, const char *path);
int mfs_rmdir(mfs_t *mfs, const char *path);
int mfs_ls(mfs_t *mfs, const char *path);
int mfs_touch(mfs_t *mfs, const char *path);
int mfs_rm(mfs_t *mfs, const char *path);
int mfs_write(mfs_t *mfs, const char *path);
int mfs_read(mfs_t *mfs, const char *path);
