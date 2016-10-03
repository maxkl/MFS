
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <libgen.h>
#include <sys/stat.h>

#include "util.h"
#include "mfs.h"
#include "parse_opts.h"

#define BLOCK_SIZE 128
#define BLOCK_COUNT 128
#define META_INFO_BLOCK_SIZE 4

#define BLOCK_UNUSED 0x0000
#define BLOCK_EOF 0xFFFF

#define MFS_TYPE_END 0
#define MFS_TYPE_DIRECTORY 1
#define MFS_TYPE_FILE 2

#define ALLOC_TABLE_ENTRY_SIZE 4u

#define DIR_ENTRY_SIZE 16
#define PATH_SEG_MAX (DIR_ENTRY_SIZE - 4)

typedef struct {
    uint16_t type;
    uint16_t block_number;
    char *name;
} directory_entry_t;

typedef struct {
    mfs_t *mfs;
    uint16_t block_number;
    uint8_t *block;
    bool reached_eof;
    uint16_t entry_addr;
    directory_entry_t *entry;
} directory_iterator_t;

void write16(uint8_t *buf, size_t index, uint16_t data) {
    buf[index] = data & 0xFF;
    buf[index + 1] = (data >> 8) & 0xFF;
}

uint16_t read16(uint8_t *buf, size_t index) {
    return buf[index + 1] << 8 | buf[index];
}

directory_iterator_t *create_directory_iterator(mfs_t *mfs, uint16_t block_number) {
    uint8_t *block = malloc(sizeof(*block) * mfs->block_size);
    if (block == NULL) {
        perror("Memory allocation failed");
        return NULL;
    }

    // Seek to first block of directory
    fseek(mfs->f, mfs->blocks_base + block_number * mfs->block_size, SEEK_SET);

    // Read block into memory
    size_t read = fread(block, sizeof(*block), mfs->block_size, mfs->f);
    if(read != mfs->block_size) {
        if(ferror(mfs->f)) {
            perror("File read error");
        } else if(feof(mfs->f)) {
            fprintf(stderr, "File to short\n");
        }
        free(block);
        return NULL;
    }

    directory_iterator_t *it = malloc(sizeof(directory_iterator_t));
    if (it == NULL) {
        perror("Memory allocation failed");
        free(block);
        return NULL;
    }

    directory_entry_t *entry = malloc(sizeof(directory_entry_t));
    if (entry == NULL) {
        perror("Memory allocation failed");
        free(it);
        free(block);
        return NULL;
    }

    it->mfs = mfs;
    it->block_number = block_number;
    it->block = block;
    it->reached_eof = false;
    it->entry_addr = 0;
    it->entry = entry;

    return it;
}

uint16_t get_block_next(mfs_t *mfs, uint16_t block_number) {
    return read16(mfs->alloc_table, block_number * ALLOC_TABLE_ENTRY_SIZE);
}

uint16_t get_block_previous(mfs_t *mfs, uint16_t block_number) {
    return read16(mfs->alloc_table, block_number * ALLOC_TABLE_ENTRY_SIZE + 2);
}

int set_block(mfs_t *mfs, uint16_t block, uint16_t previous, uint16_t next) {
    size_t offset = block * ALLOC_TABLE_ENTRY_SIZE;

    write16(mfs->alloc_table, offset, next);
    write16(mfs->alloc_table, offset + 2, previous);

    // Save to disk
    fseek(mfs->f, mfs->alloc_table_base + offset, SEEK_SET);
    size_t written = fwrite(mfs->alloc_table + offset, sizeof(*mfs->alloc_table), 4, mfs->f);
    if (written != 4) {
        perror("Write operation failed");
        return -1;
    }

    return 0;
}

int set_block_next(mfs_t *mfs, uint16_t block, uint16_t next) {
    return set_block(mfs, block, get_block_previous(mfs, block), next);
}

int set_block_previous(mfs_t *mfs, uint16_t block, uint16_t previous) {
    return set_block(mfs, block, previous, get_block_next(mfs, block));
}

uint16_t find_free_block(mfs_t *mfs) {
    for(uint16_t block_number = 1; block_number < mfs->block_count; block_number++) {
        uint16_t value = get_block_next(mfs, block_number);

        if(value == BLOCK_UNUSED) {
            return block_number;
        }
    }

    return 0;
}

uint16_t alloc_free_block(mfs_t *mfs, uint16_t previous, uint16_t next) {
    uint16_t free_block = find_free_block(mfs);
    if(free_block == 0) {
        fprintf(stderr, "All blocks are used\n");
        return 0;
    }

    if(set_block(mfs, free_block, previous, next)) {
        return 0;
    }

    return free_block;
}

directory_entry_t *next_directory_entry(directory_iterator_t *it) {
    if(it->entry_addr >= it->mfs->block_size) {
        // End of block reached
        it->entry_addr = 0;

        // Read next block
        uint16_t next_block_number = get_block_next(it->mfs, it->block_number);
        if(next_block_number == BLOCK_EOF) {
            // The directory contains no more entries
            it->reached_eof = true;
            return NULL;
        }
        if(next_block_number == BLOCK_UNUSED) {
            fprintf(stderr, "Block 0x%04x is unused\n", it->block_number);
            return NULL;
        }

        it->block_number = next_block_number;

        // Seek to first block of directory
        fseek(it->mfs->f, it->mfs->blocks_base + next_block_number * it->mfs->block_size, SEEK_SET);

        // Read block into memory
        size_t read = fread(it->block, sizeof(*it->block), it->mfs->block_size, it->mfs->f);
        if(read != it->mfs->block_size) {
            if(ferror(it->mfs->f)) {
                perror("File read error");
            } else if(feof(it->mfs->f)) {
                fprintf(stderr, "File to short\n");
            }
            return NULL;
        }
    }

    uint16_t entry_type = read16(it->block, it->entry_addr);

    if(entry_type == MFS_TYPE_END) {
        return NULL;
    }

    uint16_t entry_block = read16(it->block, it->entry_addr + 2);
    char *entry_name = (char *) (&it->block[it->entry_addr + 4]);

    it->entry->type = entry_type;
    it->entry->block_number = entry_block;
    it->entry->name = entry_name;

    it->entry_addr += DIR_ENTRY_SIZE;

    return it->entry;
}

void free_directory_iterator(directory_iterator_t *it) {
    free(it->block);
    free(it->entry);
    free(it);
}

int mfs_block_for_directory_path(mfs_t *mfs, const char *path, uint16_t *block_number_out) {
    uint16_t block_number = 0;

    if(path[0] != '/') {
        fprintf(stderr, "Path has to be absolute\n");
        return -1;
    }

    char *path_copy = strdup(path), *path_copy_start = path_copy;
    char *path_seg;
    while((path_seg = strsep(&path_copy, "/"))) {
        // Skip empty segments
        if(path_seg[0] == '\0') {
            continue;
        }

        if(strlen(path_seg) + 1 > PATH_SEG_MAX) {
            fprintf(stderr, "Path segment too long: %s\n", path_seg);
            free(path_copy_start);
            return -1;
        }

        directory_iterator_t *it = create_directory_iterator(mfs, block_number);
        if(it == NULL) {
            fprintf(stderr, "Failed to iterate directory\n");
            return -1;
        }

        bool found = false;

        while(next_directory_entry(it)) {
            // Compare the name of the subdirectory with the one we are searching for
            if(strequals(it->entry->name, path_seg)) {
                // Only descend to directories
                if(it->entry->type != MFS_TYPE_DIRECTORY) {
                    fprintf(stderr, "%s is not a directory\n", it->entry->name);
                    free_directory_iterator(it);
                    free(path_copy_start);
                    return -1;
                }

                // We found the subdirectory
                found = true;
                block_number = it->entry->block_number;
                break;
            }
        }

        free_directory_iterator(it);

        if(!found) {
            fprintf(stderr, "%s does not exist\n", path_seg);
            free(path_copy_start);
            return -1;
        }
    }

    free(path_copy_start);

    *block_number_out = block_number;

    return 0;
}

int mfs_create(char *filename, int optc, char **optv) {
    uint16_t block_size = BLOCK_SIZE;
    uint16_t block_count = BLOCK_COUNT;

    for(int i = 0; i < optc; i++) {
        char *opt = strdup(optv[i]);
        char *name;
        char *value;

        parse_opt(opt, &name, &value);

        if(strequals(name, "bs")) {
            if(value) {
                block_size = (uint16_t) strtoul(value, NULL, 10);
            }
        } else if(strequals(name, "bc")) {
            if(value) {
                block_count = (uint16_t) strtoul(value, NULL, 10);
            }
        }

        free(opt);
    }

    if(block_size == 0 || (block_size / DIR_ENTRY_SIZE) * DIR_ENTRY_SIZE != block_size) {
        fprintf(stderr, "Invalid block size\n");
        return -1;
    } else if(block_count == 0) {
        fprintf(stderr, "Invalid block count\n");
        return -1;
    }

#ifdef DEBUG
    printf("Block size: %u\n", block_size);
    printf("Block count: %u\n", block_count);
    printf("Expected file size: %lu\n", (unsigned long) (META_INFO_BLOCK_SIZE + block_count * 2u + block_count * block_size));
#endif

    FILE *f = fopen(filename, "wb");

    if(f == NULL) {
        perror("Failed to open file");
        return EXIT_FAILURE;
    }

    {
        uint8_t *meta_info_block = malloc(sizeof(*meta_info_block) * META_INFO_BLOCK_SIZE);
        if (meta_info_block == NULL) {
            perror("Memory allocation failed");
            fclose(f);
            return EXIT_FAILURE;
        }

        write16(meta_info_block, 0, block_size);
        write16(meta_info_block, 2, block_count);

        size_t written = fwrite(meta_info_block, sizeof(*meta_info_block), META_INFO_BLOCK_SIZE, f);
        if (written != META_INFO_BLOCK_SIZE) {
            perror("Write operation failed");
            fclose(f);
            return EXIT_FAILURE;
        }

        free(meta_info_block);
    }

    {
        // AllocTable contains 16 bit addresses
        size_t alloc_table_size = block_count * ALLOC_TABLE_ENTRY_SIZE;

        uint8_t *alloc_table = calloc(alloc_table_size, sizeof(*alloc_table));
        if (alloc_table == NULL) {
            perror("Memory allocation failed");
            fclose(f);
            return EXIT_FAILURE;
        }

        // Reserve first block for root directory
        write16(alloc_table, 0, BLOCK_EOF);
        write16(alloc_table, 2, BLOCK_EOF);

        size_t written = fwrite(alloc_table, sizeof(*alloc_table), alloc_table_size, f);
        if (written != alloc_table_size) {
            perror("Write operation failed");
            free(alloc_table);
            fclose(f);
            return EXIT_FAILURE;
        }

        free(alloc_table);
    }

    {
        uint8_t *block = calloc(block_size, sizeof(*block));
        if (block == NULL) {
            perror("Memory allocation failed");
            fclose(f);
            return EXIT_FAILURE;
        }

        for(int i = 0; i < block_count; i++) {
            size_t written = fwrite(block, sizeof(*block), block_size, f);
            if (written != block_size) {
                perror("Write operation failed");
                free(block);
                fclose(f);
                return EXIT_FAILURE;
            }
        }

        free(block);
    }

#ifdef DEBUG
    struct stat st;
    fflush(f);
    if(fstat(fileno(f), &st) == 0) {
        printf("Actual size: %lu\n", st.st_size);
    } else {
        perror("fstat() failed");
    }
#endif

    fclose(f);

    return EXIT_SUCCESS;
}

mfs_t *mfs_open(char *filename) {
    size_t read;

    FILE *f = fopen(filename, "r+b");

    if (f == NULL) {
        perror("Failed to open file");
        return NULL;
    }

    uint8_t *meta_info_block = (uint8_t *) malloc(sizeof(*meta_info_block) * META_INFO_BLOCK_SIZE);
    if (meta_info_block == NULL) {
        perror("Memory allocation failed");
        fclose(f);
        return NULL;
    }

    read = fread(meta_info_block, sizeof(uint8_t), META_INFO_BLOCK_SIZE, f);
    if (read != META_INFO_BLOCK_SIZE) {
        if (ferror(f)) {
            perror("File read error");
        } else if (feof(f)) {
            fprintf(stderr, "File to short\n");
        }
        fclose(f);
        return NULL;
    }

    uint16_t block_size = read16(meta_info_block, 0);
    uint16_t block_count = read16(meta_info_block, 2);

    free(meta_info_block);

    // AllocTable contains 16 bit addresses
    size_t alloc_table_size = block_count * ALLOC_TABLE_ENTRY_SIZE;

    uint8_t *alloc_table = malloc(sizeof(*alloc_table) * alloc_table_size);
    if (alloc_table == NULL) {
        perror("Memory allocation failed");
        fclose(f);
        return NULL;
    }

    read = fread(alloc_table, sizeof(uint8_t), alloc_table_size, f);
    if (read != alloc_table_size) {
        if (ferror(f)) {
            perror("File read error");
        } else if (feof(f)) {
            fprintf(stderr, "File to short\n");
        }
        fclose(f);
        return NULL;
    }

    mfs_t *mfs = malloc(sizeof(mfs_t));
    if (mfs == NULL) {
        perror("Memory allocation failed");
        fclose(f);
        return NULL;
    }

    size_t alloc_table_base = META_INFO_BLOCK_SIZE;
    size_t blocks_base = alloc_table_base + block_count * ALLOC_TABLE_ENTRY_SIZE;

    mfs->f = f;
    mfs->block_size = block_size;
    mfs->block_count = block_count;
    mfs->alloc_table_base = alloc_table_base;
    mfs->blocks_base = blocks_base;
    mfs->alloc_table = alloc_table;
    mfs->file_open = false;
    mfs->file_start_block_number = 0;
    mfs->file_block_number = 0;
    mfs->file_offset = 0;

    return mfs;
}

void mfs_free(mfs_t *mfs) {
    free(mfs->alloc_table);
    fclose(mfs->f);
    free(mfs);
}

int mfs_info(mfs_t *mfs) {
    printf("Block size: %u\n", mfs->block_size);
    printf("Block count: %u\n", mfs->block_count);

    unsigned int unused = 0;
    unsigned int used = 0;
    for(uint16_t i = 0; i < mfs->block_count; i++) {
        uint16_t value = get_block_next(mfs, i);

        if(value == BLOCK_UNUSED) {
            unused++;
        } else {
            used++;
        }
    }
    printf("%u blocks (%u bytes) used, %u unused (%u bytes)\n", used, used * mfs->block_size, unused, unused * mfs->block_size);

    return 0;
}

int mfs_mkdir(mfs_t *mfs, const char *path) {
    char *path_copy1 = strdup(path);
    char *path_copy2 = strdup(path);
    char *dir = dirname(path_copy1);
    char *name = basename(path_copy2);

    if(strequals(name, "/")) {
        fprintf(stderr, "The root directory can not be modified\n");
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    if(strlen(name) + 1 > PATH_SEG_MAX) {
        fprintf(stderr, "Directory name too long: %s\n", name);
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    uint16_t block_number = 0;
    int ret = mfs_block_for_directory_path(mfs, dir, &block_number);
    if(ret) {
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    directory_iterator_t *it = create_directory_iterator(mfs, block_number);
    if(it == NULL) {
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    bool exists = false;
    while(next_directory_entry(it)) {
        // Compare the name of the subdirectory with the one we are searching for
        if(strequals(it->entry->name, name)) {
            exists = true;
            break;
        }
    }

    uint16_t dir_block_number = it->block_number;
    uint16_t empty_addr = it->entry_addr;
    bool reached_eof = it->reached_eof;

    free_directory_iterator(it);

    if(exists) {
        fprintf(stderr, "%s already exists\n", name);
        free(path_copy1);
        free(path_copy2);
        return -1;
    } else {
        // Find first free block
        uint16_t new_block_number = alloc_free_block(mfs, BLOCK_EOF, BLOCK_EOF);
        if(new_block_number == 0) {
            free(path_copy1);
            free(path_copy2);
            return -1;
        }

        if(reached_eof) {
            block_number = alloc_free_block(mfs, dir_block_number, BLOCK_EOF);
            if(block_number == 0) {
                free(path_copy1);
                free(path_copy2);
                return -1;
            }

            if(set_block_next(mfs, dir_block_number, block_number)) {
                free(path_copy1);
                free(path_copy2);
                return -1;
            }
        } else {
            block_number = dir_block_number;
        }

        // Write the entry for the new directory in its parent directory
        fseek(mfs->f, mfs->blocks_base + mfs->block_size * block_number + empty_addr, SEEK_SET);

        uint8_t entry[DIR_ENTRY_SIZE] = { 0 };

        write16(entry, 0, MFS_TYPE_DIRECTORY);
        write16(entry, 2, new_block_number);
        strcpy((char *) &entry[4], name);

        size_t written2 = fwrite(entry, sizeof(*entry), DIR_ENTRY_SIZE, mfs->f);
        if (written2 != DIR_ENTRY_SIZE) {
            perror("Write operation failed");
            free(path_copy1);
            free(path_copy2);
            return -1;
        }
    }

    free(path_copy1);
    free(path_copy2);

    return 0;
}

int mfs_rmdir(mfs_t *mfs, const char *path) {
    return mfs_rm(mfs, path);
}

int mfs_ls(mfs_t *mfs, const char *path) {
    uint16_t block_number = 0;
    int ret = mfs_block_for_directory_path(mfs, path, &block_number);
    if(ret) {
        fprintf(stderr, "Directory %s not found\n", path);
        return -1;
    }

    directory_iterator_t *it = create_directory_iterator(mfs, block_number);
    if(it == NULL) {
        fprintf(stderr, "Failed to iterate directory\n");
        return -1;
    }

    while(next_directory_entry(it)) {
        printf("%-4s 0x%04x %-*s\n", it->entry->type == MFS_TYPE_DIRECTORY ? "dir" : it->entry->type == MFS_TYPE_FILE ? "file" : "unkn", it->entry->block_number, PATH_SEG_MAX, it->entry->name);
    }

    free_directory_iterator(it);

    return 0;
}

int mfs_touch(mfs_t *mfs, const char *path) {
    char *path_copy1 = strdup(path);
    char *path_copy2 = strdup(path);
    char *dir = dirname(path_copy1);
    char *name = basename(path_copy2);

    if(strequals(name, "/")) {
        fprintf(stderr, "The root directory can not be modified\n");
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    if(strlen(name) + 1 > PATH_SEG_MAX) {
        fprintf(stderr, "File name too long: %s\n", name);
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    uint16_t block_number = 0;
    int ret = mfs_block_for_directory_path(mfs, dir, &block_number);
    if(ret) {
        fprintf(stderr, "Failed to open directory\n");
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    directory_iterator_t *it = create_directory_iterator(mfs, block_number);
    if(it == NULL) {
        fprintf(stderr, "Failed to iterate directory\n");
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    bool exists = false;

    while(next_directory_entry(it)) {
        // Compare the name of the entry with the one we'd like to use
        if(strequals(it->entry->name, name)) {
            exists = true;
            break;
        }
    }

    uint16_t dir_block_number = it->block_number;
    uint16_t empty_addr = it->entry_addr;
    bool reached_eof = it->reached_eof;

    free_directory_iterator(it);

    if(exists) {
        fprintf(stderr, "%s already exists\n", name);
        free(path_copy1);
        free(path_copy2);
        return -1;
    } else {
        uint16_t new_block_number = alloc_free_block(mfs, BLOCK_EOF, BLOCK_EOF);
        if(new_block_number == 0) {
            fprintf(stderr, "All blocks are used\n");
            free(path_copy1);
            free(path_copy2);
            return -1;
        }

        if(reached_eof) {
            block_number = alloc_free_block(mfs, dir_block_number, BLOCK_EOF);
            if(block_number == 0) {
                fprintf(stderr, "All blocks are used\n");
                free(path_copy1);
                free(path_copy2);
                return -1;
            }

            if(set_block_next(mfs, dir_block_number, block_number)) {
                free(path_copy1);
                free(path_copy2);
                return -1;
            }
        } else {
            block_number = dir_block_number;
        }

        // Write the entry for the new directory in its parent directory
        fseek(mfs->f, mfs->blocks_base + mfs->block_size * block_number + empty_addr, SEEK_SET);

        uint8_t entry[DIR_ENTRY_SIZE] = { 0 };

        write16(entry, 0, MFS_TYPE_FILE);
        write16(entry, 2, new_block_number);
        strcpy((char *) &entry[4], name);

        size_t written2 = fwrite(entry, sizeof(*entry), DIR_ENTRY_SIZE, mfs->f);
        if (written2 != DIR_ENTRY_SIZE) {
            perror("Write operation failed");
            free(path_copy1);
            free(path_copy2);
            return -1;
        }
    }

    free(path_copy1);
    free(path_copy2);

    return 0;
}

int mfs_rm(mfs_t *mfs, const char *path) {
    char *path_copy1 = strdup(path);
    char *path_copy2 = strdup(path);
    char *dir = dirname(path_copy1);
    char *name = basename(path_copy2);

    if(strequals(name, "/")) {
        fprintf(stderr, "The root directory can not be modified\n");
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    if(strlen(name) + 1 > PATH_SEG_MAX) {
        fprintf(stderr, "File name too long: %s\n", name);
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    uint16_t block_number = 0;
    int ret = mfs_block_for_directory_path(mfs, dir, &block_number);
    if(ret) {
        fprintf(stderr, "Directory %s not found\n", dir);
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    directory_iterator_t *it = create_directory_iterator(mfs, block_number);
    if(it == NULL) {
        fprintf(stderr, "Failed to iterate directory\n");
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    bool found = false;
    uint16_t last_entry_addr = 0;
    uint16_t last_entry_block = 0;
    uint16_t file_block_number = 0;
    uint16_t file_entry_addr = 0;
    uint16_t file_entry_block = 0;

    while(next_directory_entry(it)) {
        last_entry_addr = it->entry_addr;
        last_entry_block = it->block_number;
        if(!found && strequals(it->entry->name, name)) {
            found = true;
            file_block_number = it->entry->block_number;
            // FIXME: it->entry_addr is incremented after entry is read, so it refers do the next entry
            file_entry_addr = it->entry_addr;
            file_entry_block = it->block_number;
        }
    }

    free_directory_iterator(it);
    free(path_copy1);
    free(path_copy2);

    if(found) {
        while(file_block_number != BLOCK_EOF) {
            uint16_t next_block_number = get_block_next(mfs, file_block_number);
            set_block(mfs, file_block_number, BLOCK_UNUSED, BLOCK_UNUSED);
            file_block_number = next_block_number;
        }
        uint8_t *entry = malloc(sizeof(*entry) * DIR_ENTRY_SIZE);
        if(entry == NULL) {
            perror("No memory for entry");
            return -1;
        }
        fseek(mfs->f, mfs->blocks_base + mfs->block_size * last_entry_block + last_entry_addr, SEEK_SET);
        size_t read = fread(entry, sizeof(*entry), DIR_ENTRY_SIZE, mfs->f);
        if(read != DIR_ENTRY_SIZE) {
            perror("Failed to read entry");
            free(entry);
            return -1;
        }
        fseek(mfs->f, mfs->blocks_base + mfs->block_size * file_entry_block + file_entry_addr, SEEK_SET);
        size_t written = fwrite(entry, sizeof(*entry), DIR_ENTRY_SIZE, mfs->f);
        if(written != DIR_ENTRY_SIZE) {
            perror("Failed to write entry");
            free(entry);
            return -1;
        }
        free(entry);
    } else {
        fprintf(stderr, "File not found\n");
        return -1;
    }

    return 0;
}

int mfs_fopen(mfs_t *mfs, const char *path) {
    if(mfs->file_open) {
        fprintf(stderr, "Only one file can be open at a time\n");
        return -1;
    }

    char *path_copy1 = strdup(path);
    char *path_copy2 = strdup(path);
    char *dir = dirname(path_copy1);
    char *name = basename(path_copy2);

    if(strequals(name, "/")) {
        fprintf(stderr, "The root directory can not be modified\n");
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    if(strlen(name) + 1 > PATH_SEG_MAX) {
        fprintf(stderr, "Directory name too long: %s\n", name);
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    uint16_t block_number = 0;
    int ret = mfs_block_for_directory_path(mfs, dir, &block_number);
    if(ret) {
        fprintf(stderr, "Directory %s not found\n", dir);
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    directory_iterator_t *it = create_directory_iterator(mfs, block_number);
    if(it == NULL) {
        fprintf(stderr, "Failed to iterate directory\n");
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    bool found = false;
    uint16_t file_block_number = 0;

    while(next_directory_entry(it)) {
        if(strequals(it->entry->name, name)) {
            if(it->entry->type != MFS_TYPE_FILE) {
                fprintf(stderr, "Not a file\n");
                free(path_copy1);
                free(path_copy2);
                free_directory_iterator(it);
                return -1;
            }

            found = true;
            file_block_number = it->entry->block_number;
        }
    }

    free_directory_iterator(it);
    free(path_copy1);
    free(path_copy2);

    if(found) {
        mfs->file_open = true;
        mfs->file_start_block_number = file_block_number;
        mfs->file_block_number = file_block_number;
        mfs->file_block_index = 0;
        mfs->file_offset = 0;
    } else {
        fprintf(stderr, "File not found\n");
        return -1;
    }

    return 0;
}

int mfs_fclose(mfs_t *mfs) {
    if(!mfs->file_open) {
        fprintf(stderr, "No open file\n");
        return -1;
    }

    mfs->file_open = false;

    return 0;
}

int mfs_finfo(mfs_t *mfs) {
    printf("Open:           %s\n", mfs->file_open ? "yes" : "no");
    if(mfs->file_open) {
        printf("Start block:    0x%04x\n", mfs->file_start_block_number);
        printf("Current block:  0x%04x\n", mfs->file_block_number);
        printf("Current offset: %u\n", mfs->file_offset);
    }
    return 0;
}

int mfs_fseek(mfs_t *mfs, uint16_t pos) {
    if(!mfs->file_open) {
        fprintf(stderr, "No open file\n");
        return -1;
    }

    uint16_t block_index = pos / mfs->block_size;
    uint16_t offset = pos % mfs->block_size;

    if(block_index < mfs->file_block_index) {
        while(block_index < mfs->file_block_index) {
            uint16_t previous = get_block_previous(mfs, mfs->file_block_number);
            if(previous == BLOCK_EOF) {
                fprintf(stderr, "Block 0x%04x has no previous block\n", mfs->file_block_number);
                return -1;
            }
            mfs->file_block_number = previous;
            mfs->file_block_index--;
        }
    } else if(block_index > mfs->file_block_index) {
        while(block_index > mfs->file_block_index) {
            uint16_t next = get_block_next(mfs, mfs->file_block_number);
            if(next == BLOCK_EOF) {
                fprintf(stderr, "Block 0x%04x has no next block\n", mfs->file_block_number);
                return -1;
            }
            mfs->file_block_number = next;
            mfs->file_block_index++;
        }
    }

    mfs->file_offset = offset;

    return 0;
}

int mfs_fwrite(mfs_t *mfs, uint16_t len, uint8_t *buf) {
    if(!mfs->file_open) {
        fprintf(stderr, "No open file\n");
        return -1;
    }

    uint16_t buf_offset = 0;
    uint16_t remaining = len;

    while(remaining > 0) {
        fseek(mfs->f, mfs->blocks_base + mfs->file_block_number * mfs->block_size + mfs->file_offset, SEEK_SET);

        uint16_t to_write = mfs->block_size - mfs->file_offset;
        if(to_write > remaining) to_write = remaining;

        size_t written = fwrite(buf + buf_offset, sizeof(uint8_t), to_write, mfs->f);
        if (written != to_write) {
            perror("Failed to write buffer to file");
            return -1;
        }

        buf_offset += to_write;
        remaining -= to_write;

        if(remaining > 0) {
            uint16_t next_block_number = get_block_next(mfs, mfs->file_block_number);
            if(next_block_number == BLOCK_EOF) {
                next_block_number = alloc_free_block(mfs, mfs->file_block_number, BLOCK_EOF);
                if(next_block_number == 0) {
                    return -1;
                }
                if(set_block_next(mfs, mfs->file_block_number, next_block_number)) {
                    return -1;
                }
            }
            mfs->file_block_number = next_block_number;
            mfs->file_block_index++;
            mfs->file_offset = 0;
        } else {
            mfs->file_offset += to_write;
        }
    }

    return 0;
}

int mfs_fread(mfs_t *mfs, uint16_t len, uint8_t *buf) {
    if(!mfs->file_open) {
        fprintf(stderr, "No open file\n");
        return -1;
    }

    uint16_t buf_offset = 0;
    uint16_t remaining = len;

    while(remaining > 0) {
        fseek(mfs->f, mfs->blocks_base + mfs->file_block_number * mfs->block_size + mfs->file_offset, SEEK_SET);

        uint16_t to_read = mfs->block_size - mfs->file_offset;
        if(to_read > remaining) to_read = remaining;

        size_t read = fread(buf + buf_offset, sizeof(uint8_t), to_read, mfs->f);
        if (read != to_read) {
            perror("Failed to read file into buffer");
            return -1;
        }

        buf_offset += to_read;
        remaining -= to_read;

        if(remaining > 0) {
            uint16_t next_block_number = get_block_next(mfs, mfs->file_block_number);
            if (next_block_number == BLOCK_EOF) {
                fprintf(stderr, "Reached EOF\n");
                return -1;
            }
            mfs->file_block_number = next_block_number;
            mfs->file_block_index++;
            mfs->file_offset = 0;
        } else {
            mfs->file_offset += to_read;
        }
    }

    return 0;
}
