/*
 * Remote Block Device - Storage Daemon test
 *
 */

#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "sd.h"
#include "proto.h"

int msg_id = 0;
char test_str1[100] = "first test of storage daemon";
char test_str2[100] = "test connecting and disconnecting the storage daemon";
char test_str3[100] = "testing opening new connections without closing previous ones";

union sock
{
    struct sockaddr s;
    struct sockaddr_in i;
};

union sock sd_sock;

int test_connect() {
    int sd;

    printf(">>> test_connect:\n");
    sd_sock.i.sin_family = AF_INET;
    sd_sock.i.sin_port = htons(SDPORT);
    sd_sock.i.sin_addr.s_addr = inet_addr("127.0.0.1");

    sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd == 0) exit(1);
    if (connect(sd, &(sd_sock.s), sizeof(struct sockaddr_in)) == -1) {
        perror("Connection failed");
        exit(1);
    }
    printf("OK\n");
    return sd;
}

int test_write(int sd, char *str)
{
    int nrv;
    struct rbdmsg_hdr msg, rsp;
    char buf[1024];
    
    printf(">>> test_write: %s\n", str);
    msg.version = PROTO_VERSION;
    msg.type = CMD;
    msg.code = CMD_WRITE;
    msg.id = ++msg_id;
    msg.payload_size = 100;
    msg.fsop_offset_sectors = 50;
    msg.fsop_size = 100;

    write(sd, &msg, sizeof(struct rbdmsg_hdr));
    bzero(buf, 100);
    strcpy(buf, str);
    write(sd, buf, 100);
    nrv = read(sd, &rsp, sizeof(struct rbdmsg_hdr));

    assert(rsp.id == msg.id);
    assert(rsp.type == REP);
    assert(rsp.payload_size == 0);
    printf("OK\n");

    return 0;
}

int test_read(int sd, char *expected) 
{
    int nrv, i;
    struct rbdmsg_hdr msg, rsp;
    char buf[1024];

    printf(">>> test_read: %s\n", expected);
    msg.version = PROTO_VERSION;
    msg.type = CMD;
    msg.code = CMD_READ;
    msg.id = ++msg_id;
    msg.payload_size = 0;
    msg.fsop_offset_sectors = 50;
    msg.fsop_size = 20;

    write(sd, &msg, sizeof(struct rbdmsg_hdr));
    nrv = read(sd, &rsp, sizeof(struct rbdmsg_hdr));
    nrv = read(sd, buf, rsp.payload_size);

    assert(rsp.id == msg.id);
    assert(rsp.type == REP);
    assert(rsp.payload_size == msg.fsop_size);
    assert(strncmp(buf, expected, rsp.payload_size) == 0);
    printf("OK\n");

    return 0;
}

int test_getsz(int sd) 
{
    int nrv;
    struct rbdmsg_hdr msg, rsp;
    unsigned long size;
    
    msg.version = PROTO_VERSION;
    msg.type = CMD;
    msg.code = CMD_GETSZ;
    msg.id = ++msg_id;
    msg.payload_size = 0;

    printf(">>> test_getsz:\n");
    write(sd, &msg, sizeof(struct rbdmsg_hdr));
    nrv = read(sd, &rsp, sizeof(struct rbdmsg_hdr));
    nrv = read(sd, &size, rsp.payload_size);

    assert(rsp.payload_size == sizeof(size));
    printf("OK - %lu\n", size);

    return 0;
}

int test_close(int sd) 
{
    int nrv;
    struct rbdmsg_hdr msg, rsp;
    unsigned long size;
    
    msg.version = PROTO_VERSION;
    msg.type = CMD;
    msg.code = CMD_CLOSE;
    msg.id = ++msg_id;
    msg.payload_size = 0;

    printf(">>> test_close:\n");
    write(sd, &msg, sizeof(struct rbdmsg_hdr));
    nrv = read(sd, &rsp, sizeof(struct rbdmsg_hdr));

    printf("OK\n");

    return 0;
}

int main(int argc,char *argv[])
{
    int sd;

    /* only one connection */
    sd = test_connect();
    test_write(sd, test_str1);
    test_read(sd, test_str1);
    test_getsz(sd);
    test_close(sd);
    close(sd);

    /* several connections */
    sd = test_connect();
    test_write(sd, test_str2);
    test_close(sd);
    close(sd);

    sd = test_connect();
    test_read(sd, test_str2);
    test_close(sd);
    close(sd);

    sd = test_connect();
    test_getsz(sd);
    test_close(sd);
    close(sd);

    sd = test_connect();
    sd = test_connect();
    test_write(sd, test_str3);
    test_read(sd, test_str3);
    close(sd);

	return 0;
}

