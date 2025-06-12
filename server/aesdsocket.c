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
#include <pthread.h>

#define BUFFER_SIZE 1024
#define SOCKET_FILE "/var/tmp/aesdsocketdata"

static volatile sig_atomic_t run_flag = 1;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int client_socket;
    struct sockaddr addr;
} client_info_t;

typedef struct thread_node {
    pthread_t thread_id;
    client_info_t *cinfo;
    struct thread_node *next;
} thread_node_t;

thread_node_t *head = NULL;
pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;

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
    pthread_mutex_destroy(&file_mutex);
    pthread_mutex_destroy(&list_mutex);
}

void add_thread(thread_node_t *node) {
    pthread_mutex_lock(&list_mutex);
    node->next = head;
    head = node;
    pthread_mutex_unlock(&list_mutex);
}

void join_and_clean_threads() {
    pthread_mutex_lock(&list_mutex);
    thread_node_t *current = head;
    while (current != NULL) {
        pthread_join(current->thread_id, NULL);
        free(current->cinfo);
        thread_node_t *temp = current;
        current = current->next;
        free(temp);
    }
    head = NULL;
    pthread_mutex_unlock(&list_mutex);
}

void write_to_file(const char *data) {
    pthread_mutex_lock(&file_mutex);
    FILE *fp = fopen(SOCKET_FILE, "a");
    if (!fp) {
        syslog(LOG_ERR, "File open error: %s", strerror(errno));
    } else {
        fprintf(fp, "%s", data);
        fclose(fp);
    }
    pthread_mutex_unlock(&file_mutex);
}

void send_file_contents(int fd) {
    pthread_mutex_lock(&file_mutex);
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
    pthread_mutex_unlock(&file_mutex);
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Fork error: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
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
        exit(EXIT_SUCCESS);
    }

    if (chdir("/") < 0) {
        syslog(LOG_ERR, "chdir error: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

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

void *handle_client(void *arg) {
    client_info_t *cinfo = (client_info_t *)arg;
    int fd = cinfo->client_socket;
    struct sockaddr addr = cinfo->addr;
    char client_ip[INET_ADDRSTRLEN];
    struct sockaddr_in *client_addr = (struct sockaddr_in *)&addr;

    inet_ntop(AF_INET, &(client_addr->sin_addr), client_ip, INET_ADDRSTRLEN);
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);

    char buffer[BUFFER_SIZE];
    char *packet = NULL;
    size_t packet_size = 0;

    ssize_t res;
    while ((res = recv(fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[res] = '\0';
        char *newline_pos = strchr(buffer, '\n');

        if (newline_pos) {
            size_t part_size = newline_pos - buffer + 1;
            char *new_packet = realloc(packet, packet_size + part_size + 1);
            if (!new_packet) {
                syslog(LOG_ERR, "Memory allocation error: %s", strerror(errno));
                free(packet);
                close(fd);
                pthread_exit(NULL);
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
                    close(fd);
                    pthread_exit(NULL);
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
                close(fd);
                pthread_exit(NULL);
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
    free(cinfo);
    pthread_exit(NULL);
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

    pthread_mutex_init(&file_mutex, NULL);

    FILE *fp = fopen(SOCKET_FILE, "w");
    if (!fp) {
        syslog(LOG_ERR, "File open error: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    fclose(fp);

    int sockfd = -1;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        syslog(LOG_ERR, "Socket creation error: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    int optval = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        syslog(LOG_ERR, "setsockopt error: %s", strerror(errno));
        cleanup(sockfd, -1, NULL);
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
        cleanup(sockfd, -1, NULL);
        exit(EXIT_FAILURE);
    }

    res = bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen);
    if (res != 0) {
        syslog(LOG_ERR, "bind error: %s", strerror(errno));
        freeaddrinfo(servinfo);
        cleanup(sockfd, -1, NULL);
        exit(EXIT_FAILURE);
    }
    freeaddrinfo(servinfo);

    if (listen(sockfd, 5) != 0) {
        syslog(LOG_ERR, "listen error: %s", strerror(errno));
        cleanup(sockfd, -1, NULL);
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
            cleanup(sockfd, -1, NULL);
            exit(EXIT_FAILURE);
        }

        if (res == 0)
            continue;

        if (FD_ISSET(sockfd, &readfds)) {
            struct sockaddr addr;
            socklen_t addrlen = sizeof(addr);
            int fd = accept(sockfd, &addr, &addrlen);
            if (fd == -1) {
                if (errno == EINTR) continue;
                syslog(LOG_ERR, "accept error: %s", strerror(errno));
                cleanup(sockfd, -1, NULL);
                exit(EXIT_FAILURE);
            }

            client_info_t *cinfo = malloc(sizeof(client_info_t));
            if (!cinfo) {
                syslog(LOG_ERR, "Memory allocation error: %s", strerror(errno));
                close(fd);
                continue;
            }
            cinfo->client_socket = fd;
            memcpy(&cinfo->addr, &addr, sizeof(struct sockaddr));

            thread_node_t *new_node = malloc(sizeof(thread_node_t));
            if (!new_node) {
                syslog(LOG_ERR, "Memory allocation error: %s", strerror(errno));
                free(cinfo);
                close(fd);
                continue;
            }
            new_node->cinfo = cinfo;

            if (pthread_create(&new_node->thread_id, NULL, handle_client, cinfo) != 0) {
                syslog(LOG_ERR, "pthread_create error: %s", strerror(errno));
                free(cinfo);
                free(new_node);
                close(fd);
            } else {
                add_thread(new_node);
            }
        }
    }

    join_and_clean_threads();  // Wait for all threads to finish

    syslog(LOG_INFO, "Program terminated");
    cleanup(sockfd, -1, NULL);
    return 0;
}