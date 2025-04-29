#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        const char *error_msg = "2 arguments must be specified! Exiting...\n";
        syslog(LOG_ERR, "%s", error_msg);

        return 1;
    }

    const char *writefile = argv[1];
    const char *writestr = argv[2];
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Writing %s to %s\n", writestr, writefile);
    syslog(LOG_DEBUG, "%s", log_msg);

    int fd = open(writefile, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "Error writing %s to %s\n", writestr, writefile);
        syslog(LOG_ERR, "%s", log_msg);
        close(fd);
        return 1;
    }
    ssize_t bytes_written = write(fd, writestr, strlen(writestr));
    if (bytes_written == -1) {
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "Error writing %s to %s\n", writestr, writefile);
        syslog(LOG_ERR, "%s", log_msg);
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}