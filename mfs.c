
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <libgen.h>
#include <sys/stat.h>

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

#define DIR_RECORD_SIZE 16
#define PATH_SEG_MAX (DIR_RECORD_SIZE - 4)

typedef enum {
    MFS_ACTION_MKDIR,
    MFS_ACTION_RMDIR,
    MFS_ACTION_LS,
    MFS_ACTION_TOUCH,
    MFS_ACTION_RM,
    MFS_ACTION_WRITE,
    MFS_ACTION_READ,

    MFS_ACTION_NONE
} MFS_ACTION;

static const struct {
    MFS_ACTION action;
    const char *name;
} MFS_ACTION_MAP[] = {
        { MFS_ACTION_MKDIR, "mkdir"},
        { MFS_ACTION_RMDIR, "rmdir"},
        { MFS_ACTION_LS, "ls"},
        { MFS_ACTION_TOUCH, "touch"},
        { MFS_ACTION_RM, "rm"},
        { MFS_ACTION_WRITE, "write"},
        { MFS_ACTION_READ, "read"}
};

void write16(uint8_t *buf, size_t index, uint16_t data) {
    buf[index] = data & 0xFF;
    buf[index + 1] = (data >> 8) & 0xFF;
}

uint16_t read16(uint8_t *buf, size_t index) {
    return buf[index + 1] << 8 | buf[index];
}

MFS_ACTION parse_action(const char *name) {
    for(int i = 0; i < sizeof(MFS_ACTION_MAP) / sizeof(*MFS_ACTION_MAP); i++) {
        if(strcmp(name, MFS_ACTION_MAP[i].name) == 0) {
            return MFS_ACTION_MAP[i].action;
        }
    }
    return MFS_ACTION_NONE;
}

int mfs_create(char *filename, int optc, char **optv) {
    uint16_t block_size = BLOCK_SIZE;
    uint16_t block_count = BLOCK_COUNT;

    for(int i = 0; i < optc; i++) {
        char *opt = strdup(optv[i]);
        char *name;
        char *value;

        parse_opt(opt, &name, &value);

        if(strcmp(name, "bs") == 0) {
            if(value) {
                block_size = (uint16_t) strtoul(value, NULL, 10);
            }
        } else if(strcmp(name, "bc") == 0) {
            if(value) {
                block_count = (uint16_t) strtoul(value, NULL, 10);
            }
        }

        free(opt);
    }

    if(block_size == 0 || (block_size / DIR_RECORD_SIZE) * DIR_RECORD_SIZE != block_size) {
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
        size_t alloc_table_size = block_count * 2u;

        uint8_t *alloc_table = calloc(alloc_table_size, sizeof(*alloc_table));
        if (alloc_table == NULL) {
            perror("Memory allocation failed");
            fclose(f);
            return EXIT_FAILURE;
        }

        // Reserve first block for root directory
        write16(alloc_table, 0, BLOCK_EOF);

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

int mfs_dump(mfs_t *mfs, int optc, char **optv) {
    printf("Block size: %u\n", mfs->block_size);
    printf("Block count: %u\n", mfs->block_count);

    unsigned int unused = 0;
    unsigned int used = 0;
    for(uint16_t i = 0; i < mfs->block_count; i++) {
        uint16_t value = read16(mfs->alloc_table, i * 2u);

        if(value == BLOCK_UNUSED) {
            unused++;
        } else {
            used++;
        }
    }
    printf("%u blocks (%u bytes) used, %u unused (%u bytes)\n", used, used * mfs->block_size, unused, unused * mfs->block_size);

    return EXIT_SUCCESS;
}

int mfs_block_for_directory_path(mfs_t *mfs, const char *path, uint16_t *block_number_out) {
    size_t alloc_table_base = META_INFO_BLOCK_SIZE;
    size_t blocks_base = alloc_table_base + mfs->block_count * 2;
    uint16_t block_number = 0;

    if(path[0] != '/') {
        fprintf(stderr, "Path has to be absolute\n");
        return -1;
    }

    uint8_t *block = malloc(sizeof(*block) * mfs->block_size);
    if (block == NULL) {
        perror("Memory allocation failed");
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
            free(block);
            return -1;
        }

        // Seek to first block of directory
        fseek(mfs->f, blocks_base + block_number * mfs->block_size, SEEK_SET);

        // Read block into memory
        size_t read = fread(block, sizeof(*block), mfs->block_size, mfs->f);
        if(read != mfs->block_size) {
            if(ferror(mfs->f)) {
                perror("File read error");
            } else if(feof(mfs->f)) {
                fprintf(stderr, "File to short\n");
            }
            free(path_copy_start);
            free(block);
            return -1;
        }

        bool found = false;

        // TODO: search in subsequent blocks (as stated in AllocTable)
        for(uint16_t i = 0; i < mfs->block_size; i += DIR_RECORD_SIZE) {
            uint16_t entry_type = read16(block, i);

            if(entry_type == MFS_TYPE_END) {
                break;
            }

            uint16_t entry_block = read16(block, i + 2);
            char *entry_name = (char *) (&block[i + 4]);

            // Compare the name of the subdirectory with the one we are searching for
            if(strcmp(entry_name, path_seg) == 0) {
                // Only descend to directories
                if(entry_type != MFS_TYPE_DIRECTORY) {
                    fprintf(stderr, "%s is not a directory\n", entry_name);
                    free(path_copy_start);
                    free(block);
                    return -1;
                }

                // We found the subdirectory
                found = true;
                block_number = entry_block;
                // We're done here
                break;
            }
        }

        if(!found) {
            fprintf(stderr, "%s does not exist\n", path_seg);
            free(path_copy_start);
            free(block);
            return -1;
        }
    }

    free(path_copy_start);
    free(block);

    *block_number_out = block_number;

    return 0;
}

typedef struct {
    uint16_t type;
    uint16_t block_number;
    char *name;
} directory_entry_t;

typedef struct {
    mfs_t *mfs;
    uint8_t *block;
    bool stop;
    uint16_t i;
    directory_entry_t *entry;
} directory_iterator_t;

directory_iterator_t *create_directory_iterator(mfs_t *mfs, uint16_t block_number) {
    size_t alloc_table_base = META_INFO_BLOCK_SIZE;
    size_t blocks_base = alloc_table_base + mfs->block_count * 2;

    uint8_t *block = malloc(sizeof(*block) * mfs->block_size);
    if (block == NULL) {
        perror("Memory allocation failed");
        return NULL;
    }

    // Seek to first block of directory
    fseek(mfs->f, blocks_base + block_number * mfs->block_size, SEEK_SET);

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
    it->block = block;
    it->stop = false;
    it->i = 0;
    it->entry = entry;

    return it;
}

directory_entry_t *next_directory_entry(directory_iterator_t *it) {
    if(it->stop) {
        return NULL;
    }

    // TODO: search in subsequent blocks (as stated in AllocTable)
    if(it->i >= it->mfs->block_size) {
        // End reached
        return NULL;
    }

    uint16_t entry_type = read16(it->block, it->i);

    if(entry_type == MFS_TYPE_END) {
        return NULL;
    }

    uint16_t entry_block = read16(it->block, it->i + 2);
    char *entry_name = (char *) (&it->block[it->i + 4]);

    it->entry->type = entry_type;
    it->entry->block_number = entry_block;
    it->entry->name = entry_name;

    it->i += DIR_RECORD_SIZE;

    return it->entry;
}

void free_directory_iterator(directory_iterator_t *it) {
    free(it->block);
    free(it->entry);
    free(it);
}

int mfs_mkdir(mfs_t *mfs, const char *path) {
    size_t alloc_table_base = META_INFO_BLOCK_SIZE;
    size_t blocks_base = alloc_table_base + mfs->block_count * 2;

    char *path_copy1 = strdup(path);
    char *path_copy2 = strdup(path);
    char *dir = dirname(path_copy1);
    char *name = basename(path_copy2);

    if(strcmp(name, "/") == 0) {
        fprintf(stderr, "The root directory can not be modified\n");
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    uint16_t block_number = 0;
    int ret = mfs_block_for_directory_path(mfs, dir, &block_number);
    if(ret) {
        fprintf(stderr, "Directory %s not found\n", path);
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    uint8_t *block = malloc(sizeof(*block) * mfs->block_size);
    if (block == NULL) {
        perror("Memory allocation failed");
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    if(strlen(name) + 1 > PATH_SEG_MAX) {
        fprintf(stderr, "Path segment too long: %s\n", name);
        free(path_copy1);
        free(path_copy2);
        free(block);
        return -1;
    }

    // Seek to first block of directory
    fseek(mfs->f, blocks_base + block_number * mfs->block_size, SEEK_SET);

    // Read block into memory
    size_t read = fread(block, sizeof(*block), mfs->block_size, mfs->f);
    if(read != mfs->block_size) {
        if(ferror(mfs->f)) {
            perror("File read error");
        } else if(feof(mfs->f)) {
            fprintf(stderr, "File to short\n");
        }
        free(path_copy1);
        free(path_copy2);
        free(block);
        return -1;
    }

    bool found = false;
    bool found_empty = false;
    uint16_t empty_addr = 0;

    // TODO: search in subsequent blocks (as stated in AllocTable)
    for(uint16_t i = 0; i < mfs->block_size; i += DIR_RECORD_SIZE) {
        uint16_t entry_type = read16(block, i);

        if(entry_type == MFS_TYPE_END) {
            found_empty = true;
            empty_addr = i;
            break;
        }

        uint16_t entry_block = read16(block, i + 2);
        char *entry_name = (char *) (&block[i + 4]);

        // Compare the name of the subdirectory with the one we are searching for
        if(strcmp(entry_name, name) == 0) {
            found = true;
            break;
        }
    }

    if(found) {
        fprintf(stderr, "%s already exists\n", name);
        free(path_copy1);
        free(path_copy2);
        free(block);
        return -1;
    } else if(!found_empty) {
        fprintf(stderr, "Block is full\n");
        free(path_copy1);
        free(path_copy2);
        free(block);
        return -1;
    } else {
        // Find first free block
        uint16_t new_block_number;
        for(new_block_number = 1; new_block_number < mfs->block_count; new_block_number++) {
            uint16_t value = read16(mfs->alloc_table, new_block_number * 2u);

            if(value == BLOCK_UNUSED) {
                break;
            }
        }

        if(new_block_number == mfs->block_count) {
            fprintf(stderr, "All blocks are used\n");
            free(path_copy1);
            free(path_copy2);
            free(block);
            return -1;
        }

        // Mark the block in AllocTable as used (in mfs->alloc_table AND on disk)
        write16(mfs->alloc_table, block_number * 2u, BLOCK_EOF);

        fseek(mfs->f, alloc_table_base + new_block_number * 2u, SEEK_SET);

        uint8_t tmp[2];
        write16(tmp, 0, BLOCK_EOF);
        size_t written = fwrite(tmp, sizeof(*tmp), 2, mfs->f);
        if (written != 2) {
            perror("Write operation failed");
            free(path_copy1);
            free(path_copy2);
            free(block);
            return -1;
        }

        // Write the entry for the new directory in its parent directory
        fseek(mfs->f, blocks_base + mfs->block_size * block_number + empty_addr, SEEK_SET);

        uint8_t entry[DIR_RECORD_SIZE] = { 0 };

        write16(entry, 0, MFS_TYPE_DIRECTORY);
        write16(entry, 2, new_block_number);
        strcpy((char *) &entry[4], name);

        size_t written2 = fwrite(entry, sizeof(*entry), DIR_RECORD_SIZE, mfs->f);
        if (written2 != DIR_RECORD_SIZE) {
            perror("Write operation failed");
            free(path_copy1);
            free(path_copy2);
            free(block);
            return -1;
        }
    }

    free(path_copy1);
    free(path_copy2);
    free(block);

    return 0;
}

int mfs_rmdir(mfs_t *mfs, const char *path) {
    return -1;
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
    size_t alloc_table_base = META_INFO_BLOCK_SIZE;
    size_t blocks_base = alloc_table_base + mfs->block_count * 2;

    char *path_copy1 = strdup(path);
    char *path_copy2 = strdup(path);
    char *dir = dirname(path_copy1);
    char *name = basename(path_copy2);

    if(strcmp(name, "/") == 0) {
        fprintf(stderr, "The root directory can not be modified\n");
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    uint16_t block_number = 0;
    int ret = mfs_block_for_directory_path(mfs, dir, &block_number);
    if(ret) {
        fprintf(stderr, "Directory %s not found\n", path);
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    uint8_t *block = malloc(sizeof(*block) * mfs->block_size);
    if (block == NULL) {
        perror("Memory allocation failed");
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    if(strlen(name) + 1 > PATH_SEG_MAX) {
        fprintf(stderr, "File name too long: %s\n", name);
        free(path_copy1);
        free(path_copy2);
        free(block);
        return -1;
    }

    // Seek to first block of directory
    fseek(mfs->f, blocks_base + block_number * mfs->block_size, SEEK_SET);

    // Read block into memory
    size_t read = fread(block, sizeof(*block), mfs->block_size, mfs->f);
    if(read != mfs->block_size) {
        if(ferror(mfs->f)) {
            perror("File read error");
        } else if(feof(mfs->f)) {
            fprintf(stderr, "File to short\n");
        }
        free(path_copy1);
        free(path_copy2);
        free(block);
        return -1;
    }

    bool found = false;
    bool found_empty = false;
    uint16_t empty_addr = 0;

    // TODO: search in subsequent blocks (as stated in AllocTable)
    for(uint16_t i = 0; i < mfs->block_size; i += DIR_RECORD_SIZE) {
        uint16_t entry_type = read16(block, i);

        if(entry_type == MFS_TYPE_END) {
            found_empty = true;
            empty_addr = i;
            break;
        }

        uint16_t entry_block = read16(block, i + 2);
        char *entry_name = (char *) (&block[i + 4]);

        // Compare the name of the entry with the one we'd like to use
        if(strcmp(entry_name, name) == 0) {
            found = true;
            break;
        }
    }

    if(found) {
        fprintf(stderr, "%s already exists\n", name);
        free(path_copy1);
        free(path_copy2);
        free(block);
        return -1;
    } else if(!found_empty) {
        fprintf(stderr, "Block is full\n");
        free(path_copy1);
        free(path_copy2);
        free(block);
        return -1;
    } else {
        // Find first free block
        uint16_t new_block_number;
        for(new_block_number = 1; new_block_number < mfs->block_count; new_block_number++) {
            uint16_t value = read16(mfs->alloc_table, new_block_number * 2u);

            if(value == BLOCK_UNUSED) {
                break;
            }
        }

        if(new_block_number == mfs->block_count) {
            fprintf(stderr, "All blocks are used\n");
            free(path_copy1);
            free(path_copy2);
            free(block);
            return -1;
        }

        // Mark the block in AllocTable as used (in mfs->alloc_table AND on disk)
        write16(mfs->alloc_table, block_number * 2u, BLOCK_EOF);

        fseek(mfs->f, alloc_table_base + new_block_number * 2u, SEEK_SET);

        uint8_t tmp[2];
        write16(tmp, 0, BLOCK_EOF);
        size_t written = fwrite(tmp, sizeof(*tmp), 2, mfs->f);
        if (written != 2) {
            perror("Write operation failed");
            free(path_copy1);
            free(path_copy2);
            free(block);
            return -1;
        }

        // Write the entry for the new directory in its parent directory
        fseek(mfs->f, blocks_base + mfs->block_size * block_number + empty_addr, SEEK_SET);

        uint8_t entry[DIR_RECORD_SIZE] = { 0 };

        write16(entry, 0, MFS_TYPE_FILE);
        write16(entry, 2, new_block_number);
        strcpy((char *) &entry[4], name);

        size_t written2 = fwrite(entry, sizeof(*entry), DIR_RECORD_SIZE, mfs->f);
        if (written2 != DIR_RECORD_SIZE) {
            perror("Write operation failed");
            free(path_copy1);
            free(path_copy2);
            free(block);
            return -1;
        }
    }

    free(path_copy1);
    free(path_copy2);
    free(block);

    return 0;
}

int mfs_do(mfs_t *mfs, int optc, char **optv) {
    if(optc < 1) {
        fprintf(stderr, "No action provided\n");
        return EXIT_FAILURE;
    }

    MFS_ACTION action = parse_action(optv[0]);
    if(action == MFS_ACTION_NONE) {
        fprintf(stderr, "Invalid action name\n");
        return EXIT_FAILURE;
    }

    int ret = -1;
    switch(action) {
        case MFS_ACTION_MKDIR:
            if(optc > 1) {
                ret = mfs_mkdir(mfs, optv[1]);
            }
            break;
        case MFS_ACTION_RMDIR:
            if(optc > 1) {
                ret = mfs_rmdir(mfs, optv[1]);
            }
            break;
        case MFS_ACTION_LS:
            if(optc > 1) {
                ret = mfs_ls(mfs, optv[1]);
            }
            break;
        case MFS_ACTION_TOUCH:
            if(optc > 1) {
                ret = mfs_touch(mfs, optv[1]);
            }
            break;
        case MFS_ACTION_RM:
            break;
        case MFS_ACTION_WRITE:
            break;
        case MFS_ACTION_READ:
            break;
    }
    if(ret) {
        fprintf(stderr, "Action failed\n");
    }

    return ret;
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
    size_t alloc_table_size = block_count * 2u;

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

    mfs->block_size = block_size;
    mfs->block_count = block_count;
    mfs->alloc_table = alloc_table;
    mfs->f = f;

    return mfs;
}

void mfs_free(mfs_t *mfs) {
    free(mfs->alloc_table);
    fclose(mfs->f);
    free(mfs);
}

