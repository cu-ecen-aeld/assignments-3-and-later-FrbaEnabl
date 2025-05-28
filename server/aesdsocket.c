#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#define BUFFER_SIZE 1024
#define SOCKET_FILE "/var/tmp/aesdsocketdata"

static volatile sig_atomic_t run_flag = 1;

void handle_signal(int signal) {
    syslog(LOG_INFO, "Caught signal, exiting");
    exit(1);    
}

void cleanup(int sockfd, int fd, FILE *fp) {
    if (fp) fclose(fp);
    if (fd != -1) close(fd);
    if (sockfd != -1) close(sockfd);
    remove(SOCKET_FILE);
    closelog();
}

int main() {
    openlog("aesdsocket", LOG_PID, LOG_USER);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    int sockfd = -1, fd = -1;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket creation error");
        exit(EXIT_FAILURE);
    }

    // Set the SO_REUSEADDR option
    int optval = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        perror("setsockopt error");
        cleanup(sockfd, fd, NULL);
        exit(EXIT_FAILURE);
    }

    struct addrinfo hints, *servinfo;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int res = getaddrinfo(NULL, "9000", &hints, &servinfo);
    if (res != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(res));
        cleanup(sockfd, fd, NULL);
        exit(EXIT_FAILURE);
    }

    res = bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen);
    if (res != 0) {
        perror("bind error");
        freeaddrinfo(servinfo);
        cleanup(sockfd, fd, NULL);
        exit(EXIT_FAILURE);
    }
    freeaddrinfo(servinfo);

    if (listen(sockfd, 5) != 0) {
        perror("listen error");
        cleanup(sockfd, fd, NULL);
        exit(EXIT_FAILURE);
    }

    while (run_flag) {
        // printf("RUN");
        struct sockaddr addr;
        socklen_t addrlen = sizeof(addr);
        fd = accept(sockfd, &addr, &addrlen);
        if (fd == -1) {
            if (errno == EINTR) continue; // Interrupted by signal, retry
            perror("accept error");
            cleanup(sockfd, fd, NULL);
            exit(EXIT_FAILURE);
        }

        char client_ip[INET_ADDRSTRLEN];
        struct sockaddr_in *client_addr = (struct sockaddr_in *) &addr;
        inet_ntop(AF_INET, &(client_addr->sin_addr), client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        FILE *fp = fopen(SOCKET_FILE, "a");
        if (!fp) {
            perror("File open error");
            close(fd);
            continue;
        }

        char buffer[BUFFER_SIZE];
        char *packet = NULL;
        size_t packet_size = 0;

        while ((res = recv(fd, buffer, sizeof(buffer) - 1, 0)) > 0) {

            // printf("BLIB");
            buffer[res] = '\0';
            // char *newline = NULL;
            // char *start = buffer;

            // Debugging print to show received data
            printf("Received buffer: %s\n", buffer);

            // while ((newline = strchr(start, '\n')) != NULL) {
            //     printf("BLAM");
            //     size_t len = newline - start + 1;
            //     char *temp = realloc(packet, packet_size + len + 1);
            //     if (!temp) {
            //         fprintf(stderr, "Memory allocation error\n");
            //         free(packet);
            //         fclose(fp);
            //         close(fd);
            //         cleanup(sockfd, -1, NULL);
            //         exit(EXIT_FAILURE);
            //     }
            //     packet = temp;
            //     strncpy(packet + packet_size, start, len);
            //     packet_size += len;
            //     packet[packet_size] = '\0';
            fprintf(fp, "%s", buffer);
            fflush(fp);  // Ensure file is updated

            //     // Debugging print to show processed packet
            //     printf("Packet written to file: %s\n", packet);

            //     // Finished handling this packet, now send the entire file
            //     fclose(fp);

            fp = fopen(SOCKET_FILE, "r");
            if (!fp) {
                perror("File reopen error");
                // free(packet);
                close(fd);
                cleanup(sockfd, -1, NULL);
                exit(EXIT_FAILURE);
            }

            //     // Sending the entire file back to the client
            //     rewind(fp);  // Move to the start of the file
            while ((res = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
                printf("Sending file content: %.*s\n", res, buffer); // Debugging print
                if (send(fd, buffer, res, 0) == -1) {
                    perror("send error");
                    break;
                }
            }

            //     fclose(fp);
            //     fp = fopen(SOCKET_FILE, "a");
            //     if (!fp) {
            //         perror("File reopen error");
            //         free(packet);
            //         close(fd);
            //         cleanup(sockfd, -1, NULL);
            //         exit(EXIT_FAILURE);
            //     }

            //     packet_size = 0;
            //     start = newline + 1;
            // }

            // size_t remain_len = strlen(start);
            // char *temp = realloc(packet, packet_size + remain_len + 1);
            // if (!temp) {
            //     fprintf(stderr, "Memory allocation error\n");
            //     free(packet);
            //     fclose(fp);
            //     close(fd);
            //     cleanup(sockfd, -1, NULL);
            //     exit(EXIT_FAILURE);
            // }
            // packet = temp;
            // strcpy(packet + packet_size, start);
            // packet_size += remain_len;
        }

        if (res == -1) {
            perror("recv error");
        }

        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        free(packet);
        fclose(fp);
        close(fd);
        fd = -1; // Reset fd for the next loop
    }

    cleanup(sockfd, fd, NULL);
    return 0;
}