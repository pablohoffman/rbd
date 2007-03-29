#include "proto.h"

#define STORAGE_TOKEN "RBDS"
#define STORAGE_VERSION 1
#define STORAGE_OFFSET 4096
#define STORAGE_SECSIZE 512

struct storage_metadata_struct {
    char token[5];                 /* to check for valid storage files */
    unsigned int version;
    unsigned int data_offset;      /* data start offset (in bytes) */
    unsigned long size;            /* device size (in bytes) */
};
typedef struct storage_metadata_struct storage_metadata_t;

struct storage_struct {
    storage_metadata_t *metadata;
    char fpath[1024];
    FILE *file;
};
typedef struct storage_struct storage_t;

int storage_init(storage_t *, const char *, unsigned long);
int storage_close(storage_t *);
