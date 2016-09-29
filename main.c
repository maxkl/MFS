#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define DEBUG

#define MAGIC 0x3053464d // MFS0 in ASCII
#define META_INFO_BLOCK_SIZE 4 //

int main_create(char *filename, int optc, char **optv) ;
int main_dump(char *filename, int optc, char **optv) ;

void write32(uint8_t *buf, uint32_t index, uint32_t data);

int main(int argc, char **argv) {
    if(argc < 2) {
        fprintf(stderr, "No command specified\n");
        exit(EXIT_FAILURE);
    } else if(argc < 3) {
        fprintf(stderr, "Missing file name\n");
        exit(EXIT_FAILURE);
    }

    char *cmd = argv[1];

    char **optv;
    int optc;
    if(argc > 3) {
        optv = &argv[2];
        optc = argc - 3;
    } else {
        optv = NULL;
        optc = 0;
    }

    char *filename = argv[argc - 1];

#ifdef DEBUG
    printf("%s %s\n", cmd, filename);
    for(int i = 0; i < optc; i++) {
        printf("opt %s\n", optv[i]);
    }
#endif

    int ret;
    if(strcmp("create", cmd) == 0) {
        ret = main_create(filename, optc, optv);
    } else if(strcmp("dump", cmd) == 0) {
        ret = main_dump(filename, optc, optv);
    } else {
        fprintf(stderr, "Unknown command %s\n", cmd);
        exit(EXIT_FAILURE);
    }

    return ret;
}

int main_create(char *filename, int optc, char **optv) {
    FILE *f = fopen(filename, "wb");

    if(f == NULL) {
        perror("Failed to open file");
        return EXIT_FAILURE;
    }

    uint8_t *meta_info_block = (uint8_t *) malloc(META_INFO_BLOCK_SIZE * sizeof(uint8_t));
    if(meta_info_block == NULL) {
        perror("Memory allocation failed");
        fclose(f);
        return EXIT_FAILURE;
    }

    write32(meta_info_block, 0, MAGIC);

    size_t to_write = META_INFO_BLOCK_SIZE;
    size_t written = fwrite(meta_info_block, sizeof(uint8_t), to_write, f);
    if(written != to_write) {
        perror("Write operation failed");
        fclose(f);
        return EXIT_FAILURE;
    }

    // TODO: structure/layout plan, opt parsing,

    free(meta_info_block);

    fclose(f);

    return EXIT_SUCCESS;
}

void write32(uint8_t *buf, uint32_t index, uint32_t data) {
    buf[index] = data & 0xFF;
    buf[index + 1] = (data >> 8) & 0xFF;
    buf[index + 2] = (data >> 16) & 0xFF;
    buf[index + 3] = (data >> 24) & 0xFF;
}

uint32_t read32(uint8_t *buf, uint32_t index) {
    return buf[index + 3] << 24 | buf[index + 2] << 16 | buf[index + 1] << 8 | buf[index];
}

int main_dump(char *filename, int optc, char **optv) {
    FILE *f = fopen(filename, "rb");

    if(f == NULL) {
        perror("Failed to open file");
        return EXIT_FAILURE;
    }

    uint8_t *meta_info_block = (uint8_t *) malloc(META_INFO_BLOCK_SIZE * sizeof(uint8_t));
    if(meta_info_block == NULL) {
        perror("Memory allocation failed");
        fclose(f);
        return EXIT_FAILURE;
    }

    size_t to_read = META_INFO_BLOCK_SIZE;
    size_t read = fread(meta_info_block, sizeof(uint8_t), to_read, f);
    if(read != to_read) {
        if(ferror(f)) {
            perror("File read error");
        } else if(feof(f)) {
            fprintf(stderr, "File to short\n");
        }
        fclose(f);
        return EXIT_FAILURE;
    }

    uint32_t magic = read32(meta_info_block, 0);
    printf("Magic number: 0x%08x\n", magic);
    if(magic != MAGIC) {
        fprintf(stderr, "Invalid magic number\n");
    }

    free(meta_info_block);

    fclose(f);

    return EXIT_SUCCESS;
}
