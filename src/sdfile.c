/*
 * Remote Block Device - TSO 2006 - Pablo Hoffman, Javier Regusci
 *
 * Programa para generar los archivos del Storage Daemon
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "sd.h"

storage_t sd_storage;

void usage(void) {
    printf("Usage: sdfile -s SIZE FILE\n\n");
    printf("SIZE - block device capacity (in megabytes)\n");
    printf("FILE - storage device filename\n");
    exit(2);
}

int main(int argc, char **argv)
{
    long size;
    char fn[1024];
    int c;

    while ((c = getopt(argc, argv, "s:")) != -1) 
        switch (c) {
            case 's':
                size = 1024*1024*atoi(optarg);
                break;
            default:
                return 2;
        }

    if (argc == optind) 
        usage();

    if (storage_init(&sd_storage, argv[optind], size))
        perror("Unable to create SD File\n");
    else
        printf("SD File created succesfully: %s\n", argv[optind]);
    return 0;
}
