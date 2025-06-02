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
    syslog(LOG_INFO, "Caught signal %d, exiting", signal);
    run_flag = 0;
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
    if (!fp) {
        syslog(LOG_ERR, "File open error: %s", strerror(errno));
    } else {
        fprintf(fp, "%s", data);
        fclose(fp);
    }
}

void send_file_contents(int fd) {
    FILE *fp = fopen(SOCKET_FILE, "r");
    if (!fp) {
        syslog(LOG_ERR, "File open error: %s", strerror(errno));
    } else {
        char file_buffer[BUFFER_SIZE];
        size_t bytes_read;
        while ((bytes_read = fread(file_buffer, 1, sizeof(file_buffer), fp)) > 0) {
            if (send(fd, file_buffer, bytes_read, 0) == -1) {
                syslog(LOG_ERR, "Send error: %s", strerror(errno));
                break;
            }
        }
        fclose(fp);
    }
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Fork error: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS); // Terminate the parent process
    }

    if (setsid() < 0) {
        syslog(LOG_ERR, "setsid error: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Fork error: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS); // Terminate the parent process
    }

    if (chdir("/") < 0) {
        syslog(LOG_ERR, "chdir error: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // for (int x = sysconf(_SC_OPEN_MAX); x >= 0; x--) {
    //     close(x);
    // }

    int fd = open("/dev/null", O_RDWR);
    if (fd != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    } else {
        syslog(LOG_ERR, "Failed to open /dev/null: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {
    int daemon_mode = 0;
    int opt;

    while ((opt = getopt(argc, argv, "d")) != -1) {
        switch (opt) {
            case 'd':
                daemon_mode = 1;
                break;
            default:
                fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);
    syslog(LOG_INFO, "Program started");

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    FILE *fp = fopen(SOCKET_FILE, "w");
    if (!fp) {
        syslog(LOG_ERR, "File open error: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    fclose(fp);

    int sockfd = -1, fd = -1;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        syslog(LOG_ERR, "Socket creation error: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    int optval = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        syslog(LOG_ERR, "setsockopt error: %s", strerror(errno));
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
        syslog(LOG_ERR, "getaddrinfo error: %s", gai_strerror(res));
        cleanup(sockfd, fd, NULL);
        exit(EXIT_FAILURE);
    }

    res = bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen);
    if (res != 0) {
        syslog(LOG_ERR, "bind error: %s", strerror(errno));
        freeaddrinfo(servinfo);
        cleanup(sockfd, fd, NULL);
        exit(EXIT_FAILURE);
    }
    freeaddrinfo(servinfo);

    if (listen(sockfd, 5) != 0) {
        syslog(LOG_ERR, "listen error: %s", strerror(errno));
        cleanup(sockfd, fd, NULL);
        exit(EXIT_FAILURE);
    }

    if (daemon_mode) {
        daemonize();
    }

    while (run_flag) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        res = select(sockfd + 1, &readfds, NULL, NULL, &timeout);
        if (res == -1) {
            if (errno == EINTR)
                continue;
            syslog(LOG_ERR, "select error: %s", strerror(errno));
            cleanup(sockfd, fd, NULL);
            exit(EXIT_FAILURE);
        }

        if (res == 0)
            continue;

        if (FD_ISSET(sockfd, &readfds)) {
            struct sockaddr addr;
            socklen_t addrlen = sizeof(addr);
            fd = accept(sockfd, &addr, &addrlen);
            if (fd == -1) {
                if (errno == EINTR) continue;
                syslog(LOG_ERR, "accept error: %s", strerror(errno));
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
                        syslog(LOG_ERR, "Memory allocation error: %s", strerror(errno));
                        free(packet);
                        cleanup(sockfd, fd, NULL);
                        exit(EXIT_FAILURE);
                    }
                    packet = new_packet;

                    memcpy(packet + packet_size, buffer, part_size);
                    packet_size += part_size;
                    packet[packet_size] = '\0';

                    syslog(LOG_INFO, "Received message: %s", packet);

                    write_to_file(packet);

                    send_file_contents(fd);

                    free(packet);
                    packet = NULL;
                    packet_size = 0;

                    size_t remaining_size = res - part_size;
                    if (remaining_size > 0) {
                        new_packet = realloc(packet, remaining_size + 1);
                        if (!new_packet) {
                            syslog(LOG_ERR, "Memory allocation error: %s", strerror(errno));
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
                        syslog(LOG_ERR, "Memory allocation error: %s", strerror(errno));
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
                syslog(LOG_ERR, "recv error: %s", strerror(errno));
            }

            syslog(LOG_INFO, "Closed connection from %s", client_ip);
            close(fd);
            fd = -1;
        }
    }

    syslog(LOG_INFO, "Program terminated");
    cleanup(sockfd, fd, NULL);
    return 0;
}