
#pragma once

#include <stdint.h>

typedef struct {
    FILE *f;
    uint16_t block_size;
    uint16_t block_count;
    size_t alloc_table_base;
    size_t blocks_base;
    uint8_t *alloc_table;
    bool file_open;
    uint16_t file_start_block_number;
    uint16_t file_block_number;
    uint16_t file_block_index;
    uint16_t file_offset;
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
int mfs_fopen(mfs_t *mfs, const char *path);
int mfs_fclose(mfs_t *mfs);
int mfs_finfo(mfs_t *mfs);
int mfs_fseek(mfs_t *mfs, uint16_t pos);
int mfs_fwrite(mfs_t *mfs, uint16_t len, uint8_t *buf);
int mfs_fread(mfs_t *mfs, uint16_t len, uint8_t *buf);
