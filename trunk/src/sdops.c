/*
 * Remote Block Device - Storage Daemon operations
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>

#include "sd.h"
#include "proto.h"

/* return storage size (in sectors) */
unsigned long storage_size(storage_t *st)
{
    return st->metadata->size / STORAGE_SECSIZE;
}

/* return storage size (in bytes) */
unsigned long storage_size_bytes(storage_t *st)
{
    return st->metadata->size;
}

/* initialize storage file
 *
 * size: size in bytes
 * path: file path
 */
int storage_init(storage_t *st, const char *path, unsigned long size)
{
    storage_metadata_t *stmd;

    strcpy(st->fpath, path);
    st->file = fopen(path, "w+");
    if (!st->file) return -1;

    stmd = malloc(sizeof(storage_metadata_t));
    strcpy(stmd->token, STORAGE_TOKEN);
    stmd->version = STORAGE_VERSION;
    stmd->data_offset = STORAGE_OFFSET;
    stmd->size = size;

    st->metadata = stmd;
    st->file = st->file;

    fwrite(stmd, sizeof(storage_metadata_t), 1, st->file);
    fseek(st->file, storage_size_bytes(st)-1, SEEK_SET);
    fwrite("\0", 1, 1, st->file);
    storage_close(st);

    return 0;
}

int storage_load(storage_t *st, char *path)
{
    storage_metadata_t *stmd;
    
    strcpy(st->fpath, path);
    st->file = fopen(st->fpath, "r+");
    if (!st->file) return -1;

    stmd = malloc(sizeof(storage_metadata_t));
    fread(stmd, sizeof(storage_metadata_t), 1, st->file);
    if (strcmp(stmd->token, STORAGE_TOKEN)) return -1; /* invalid file */
    
    st->metadata = stmd;

    st->file = st->file;
    storage_close(st);

    return 0;
}

int storage_open(storage_t *st)
{
    st->file = fopen(st->fpath, "r+");
}

int storage_close(storage_t *st)
{
    fclose(st->file);
}

int storage_free(storage_t *st)
{
    storage_close(st);
    free(st->metadata);
    return 0;
}

int storage_read(storage_t *st, void *buf, unsigned long offset, unsigned long size)
{
    int ret;
    storage_open(st);
    printf("SD: storage_read | offset: %ld | size: %ld\n", offset, size);
    offset += st->metadata->data_offset;
    fseek(st->file, offset, SEEK_SET);
    ret = fread(buf, size, 1, st->file);
    storage_close(st);
    return ret;
}

int storage_write(storage_t *st, const void *buf, unsigned long offset, unsigned long size)
{
    storage_open(st);
    printf("SD: storage_write | offset: %ld | size: %ld\n", offset, size);
    offset += st->metadata->data_offset;
    fseek(st->file, offset, SEEK_SET);
    fwrite(buf, size, 1, st->file);
    storage_close(st);
    return 0;
}

/* receive message from socket and process it
 *
 * st     - storage 
 * sockfd - socket where to extract message from
 */
int storage_process(storage_t *st, int sockfd)
{
    struct rbdmsg_hdr msg;
    int offs, rv;
    void *buf;
    unsigned long size;

    /* TODO: error check missing. for example: that request doesn't 
     * extend over disk limits, etc */

    rv = recv(sockfd, &msg, sizeof(msg), 0);
    if (rv <= 0)
        return -1;

    printf("SD: storage_process rv=%d | msg.id=%u | msg.code=%u\n", rv, msg.id, msg.code);
    msg.type = REP;

    switch(msg.code) {
        case CMD_READ:
            offs = msg.fsop_offset_sectors * STORAGE_SECSIZE;
            msg.payload_size = msg.fsop_size;
            if (send(sockfd, &msg, sizeof(msg), 0) <= 0) {
                perror("storage_process: send-msghdr");
                return -1;
            }
            buf = malloc(msg.fsop_size);
            storage_read(st, buf, offs, msg.fsop_size);
            if (send(sockfd, buf, msg.fsop_size, 0) <= 0){
                perror("CMD_READ: send-payload");
                free(buf);
                return -1;
            }
            free(buf);
            return 0;
            
        case CMD_WRITE:
            buf = malloc(msg.payload_size);
            if (recv(sockfd, buf, msg.payload_size, MSG_WAITALL) <= 0) {
                perror("CMD_WRITE: recv-payload");    
                free(buf);
                return -1;
            }
            offs = msg.fsop_offset_sectors * STORAGE_SECSIZE;
            storage_write(st, buf, offs, msg.payload_size);
            msg.payload_size = 0;
            if (send(sockfd, &msg, sizeof(msg), 0) <= 0) {
                perror("storage_process: send-msghdr");
                free(buf);
                return -1;
            }
            free(buf);
            return 0;

        case CMD_GETSZ:
            printf("SD: storage_process | CMD_GETSZ\n");
            size = storage_size(st);
            msg.payload_size = sizeof(size);
            if (send(sockfd, &msg, sizeof(msg), 0) <= 0) {
                perror("storage_process: send-msghdr");
                return -1;
            }
            if (send(sockfd, &size, sizeof(size), 0) <= 0) {
                perror("CMD_GETSZ: send-payload");
                return -1;
            }
            return 0;

        case CMD_CLOSE:
            printf("SD: storage_process | CMD_CLOSE\n");
            return -1;

        default:
            return -1;
    }
}

