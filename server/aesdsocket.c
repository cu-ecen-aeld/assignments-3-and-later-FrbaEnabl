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
    run_flag = 0; 
    printf("Signal %d received!");
}

void cleanup(int sockfd, int fd, FILE *fp) {
    if (fp) fclose(fp);
    if (fd != -1) close(fd);
    if (sockfd != -1) close(sockfd);
    remove(SOCKET_FILE);
    closelog();
}

void write_to_file(const char *data) {
    FILE *fp = fopen(SOCKET_FILE, "a");
    if (fp) {
        fprintf(fp, "%s", data);
        fclose(fp);
    } else {
        perror("File open error");
    }
}

void send_file_contents(int fd) {
    FILE *fp = fopen(SOCKET_FILE, "r");
    if (fp) {
        char file_buffer[BUFFER_SIZE];
        size_t bytes_read;
        while ((bytes_read = fread(file_buffer, 1, sizeof(file_buffer), fp)) > 0) {
            if (send(fd, file_buffer, bytes_read, 0) == -1) {
                perror("send error");
                break;
            }
        }
        fclose(fp);
    } else {
        perror("File open error");
    }
}

int main() {
    openlog("aesdsocket", LOG_PID, LOG_USER);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

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
            if (errno == EINTR) continue;
            perror("accept error");
            cleanup(sockfd, fd, NULL);
            exit(EXIT_FAILURE);
        }

        char client_ip[INET_ADDRSTRLEN];
        struct sockaddr_in *client_addr = (struct sockaddr_in *)&addr;
        inet_ntop(AF_INET, &(client_addr->sin_addr), client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        char buffer[BUFFER_SIZE];
        char *packet = NULL;
        size_t packet_size = 0;

        while ((res = recv(fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
            buffer[res] = '\0';
            char *newline_pos = strchr(buffer, '\n');

            if (newline_pos) {
                size_t part_size = newline_pos - buffer + 1;
                char *new_packet = realloc(packet, packet_size + part_size + 1);
                if (!new_packet) {
                    perror("Memory allocation error");
                    free(packet);
                    cleanup(sockfd, fd, NULL);
                    exit(EXIT_FAILURE);
                }
                packet = new_packet;

                memcpy(packet + packet_size, buffer, part_size);
                packet_size += part_size;
                packet[packet_size] = '\0';

                printf("Received message: %s", packet);

                write_to_file(packet);

                send_file_contents(fd);

                free(packet);
                packet = NULL;
                packet_size = 0;

                size_t remaining_size = res - part_size;
                if (remaining_size > 0) {
                    new_packet = realloc(packet, remaining_size + 1);
                    if (!new_packet) {
                        perror("Memory allocation error");
                        free(packet);
                        cleanup(sockfd, fd, NULL);
                        exit(EXIT_FAILURE);
                    }
                    packet = new_packet;

                    memcpy(packet, buffer + part_size, remaining_size);
                    packet_size = remaining_size;
                    packet[packet_size] = '\0';
                }
            } else {
                char *new_packet = realloc(packet, packet_size + res + 1);
                if (!new_packet) {
                    perror("Memory allocation error");
                    free(packet);
                    cleanup(sockfd, fd, NULL);
                    exit(EXIT_FAILURE);
                }
                packet = new_packet;

                memcpy(packet + packet_size, buffer, res);
                packet_size += res;
                packet[packet_size] = '\0';
            }
        }

        free(packet);

        if (res == -1) {
            perror("recv error");
        }

        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        close(fd);
        fd = -1;
    }

    cleanup(sockfd, fd, NULL);
    return 0;
}