/*
 * Remote Block Device - Storage Daemon
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include "proto.h"
#include "sd.h"

storage_t sd_storage;
pid_t childpid;

void usage(void) {
    printf("Usage: sd [-p PORT] FILE\n\n");
    printf("PORT - device TCP listening port. default: %d\n", SDPORT);
    printf("FILE - storage daemon file\n");
    exit(2);
}

void sigchld_handler(int s)
{
}


int main(int argc, char **argv)
{
    int sockfd, new_fd;  
    struct sockaddr_in locaddr;    
    struct sockaddr_in remaddr; 
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    int sd_port = SDPORT;
    int c;
    int rv = 0;
    int status;
    pid_t pid;

    if ((sockfd = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
        perror("SD: error creating socket");
        exit(1);
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        perror("SD: error stting socket as reusable");
        exit(1);
    }

    while ((c = getopt(argc, argv, "p:")) != -1) 
        switch (c) {
            case 'p':
                sd_port = atoi(optarg);
                break;
            default:
                return 2;
        }

    if (argc == optind) 
        usage();

    if (storage_load(&sd_storage, argv[optind])) {
        perror("SD: error loading SD file");
        exit(1);
    } else
        printf("SD: storage file loaded succesfully: %s\n", argv[optind]);

    locaddr.sin_family = AF_INET;         
    locaddr.sin_port = htons(sd_port);     
    locaddr.sin_addr.s_addr = INADDR_ANY; 
    memset(&(locaddr.sin_zero), '\0', 8); 

    if (bind(sockfd, (struct sockaddr *)&locaddr, sizeof(struct sockaddr))== -1) {
        perror("SD: bind error");
        exit(1);
    }

    if (listen(sockfd, 1) == -1) {
        perror("SD: listen error");
        exit(1);
    }

    sa.sa_handler = sigchld_handler; 
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("SD: error setting handler for SIGCHLD");
        exit(1);
    }

    while(1) {  
        sin_size = sizeof(struct sockaddr_in);
        printf("SD: accept | port=%d\n", sd_port);
        if ((new_fd = accept(sockfd, (struct sockaddr *)&remaddr, &sin_size)) == -1) {
            perror("SD: unable to accept connections");
            continue;
        }
        printf("SD: new connection from: %s\n", inet_ntoa(remaddr.sin_addr));

        pid = waitpid(WAIT_ANY, &status, WNOHANG);
        if (errno != ECHILD)
            kill(childpid, SIGTERM);

        childpid = fork();
        if (!childpid) {
            close(sockfd);
            while(!rv) {
                rv = storage_process(&sd_storage, new_fd);
            }
            printf("SD: closing connection from: %s\n", inet_ntoa(remaddr.sin_addr));
            close(new_fd);
            exit(0);
        }
        close(new_fd);

    }

    storage_free(&sd_storage);
    return 0;
} 
