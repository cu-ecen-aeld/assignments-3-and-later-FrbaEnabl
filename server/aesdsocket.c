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

    // Open the file with write mode to clear its contents at the start
    FILE *fp = fopen(SOCKET_FILE, "w");
    if (!fp) {
        perror("File open error");
        exit(EXIT_FAILURE);
    }
    fclose(fp);

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
        printf("RUN\n");
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



        char buffer[BUFFER_SIZE];
        // char wrt_buffer[BUFFER_SIZE];
        char *packet = NULL;
        size_t packet_size = 0;

        while ((res = recv(fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
            FILE *fp = fopen(SOCKET_FILE, "a");
            if (!fp) {
                perror("File open error");
                close(fd);
                continue;
            }
            printf("BLIB\n");
            buffer[res] = '\0';

            // Debugging print to show received data
            printf("Received buffer: %s\n", buffer);

            int f_res = fprintf(fp, "%s", buffer);
            printf("Bytes written: %d\n", f_res);
            fclose(fp);
            fp = fopen(SOCKET_FILE, "r");
            if (!fp) {
                printf("OPENING ERROR\n");
                perror("File reopen error");
                // free(packet);
                close(fd);
                cleanup(sockfd, -1, NULL);
                exit(EXIT_FAILURE);
            }
            // res = fread(wrt_buffer, 1, sizeof(wrt_buffer), fp);
            // printf("Sending file content: %.*s", res, wrt_buffer); // Debugging print
            // if (send(fd, wrt_buffer, res, 0) == -1) {
            //     perror("send error");
            //     break;
            // }
             
            // Read the file in chunks of BUFFER_SIZE
            size_t bytesRead;
            while ((bytesRead = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
                // Null-terminate the buffer for safe printing
                buffer[bytesRead] = '\0';
                // Print the buffer content
                printf("%s", buffer);
            }
                // Check for reading errors
            if (ferror(file)) {
                perror("Error reading file");
                fclose(file);
                return EXIT_FAILURE;
            }
            fclose(fp);

        }
            
        if (res == -1) {
            perror("recv error");
        }

        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        free(packet);
        // fclose(fp);
        close(fd);
        fd = -1; // Reset fd for the next loop
    }

    cleanup(sockfd, fd, NULL);
    return 0;
}