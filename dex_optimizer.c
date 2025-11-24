#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <linux/netlink.h>
#include <poll.h>

#define LOG_FILE "/storage/emulated/0/dexopt.log"
#define BATTERY_CAPACITY_PATH "/sys/class/power_supply/battery/capacity"
#define BATTERY_STATUS_PATH "/sys/class/power_supply/battery/status"
#define CMD_DEXOPT "cmd package bg-dexopt-job"

static pid_t dexopt_pid = -1;
static int dexopt_run_this_cycle = 0;

void write_log(const char *message) {
    FILE *fp = fopen(LOG_FILE, "a");
    if (fp) {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);
        fprintf(fp, "%s %s\n", time_str, message);
        fclose(fp);
    }
}

int read_int_from_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    int value = -1;
    fscanf(fp, "%d", &value);
    fclose(fp);
    return value;
}

void read_str_from_file(const char *path, char *buf, size_t size) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        if (size > 0) buf[0] = '\0';
        return;
    }
    if (fgets(buf, size, fp)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
        }
    } else {
        if (size > 0) buf[0] = '\0';
    }
    fclose(fp);
}

void start_dexopt() {
    if (dexopt_pid != -1) return;

    write_log("Dexopt started");
    pid_t pid = fork();
    if (pid == 0) {
        char *args[] = {"su", "-c", CMD_DEXOPT, NULL};
        execvp("su", args);
        exit(1);
    } else if (pid > 0) {
        dexopt_pid = pid;
    }
}

void stop_dexopt() {
    if (dexopt_pid != -1) {
        kill(dexopt_pid, SIGKILL);
        waitpid(dexopt_pid, NULL, 0);
        dexopt_pid = -1;
        write_log("Dexopt Interrupted");
    }
}

void handle_sigchld(int sig) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (pid == dexopt_pid) {
            dexopt_pid = -1;
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                write_log("Dexopt runned Successfully");
                dexopt_run_this_cycle = 1;
            }
        }
    }
}

void check_state() {
    int capacity = read_int_from_file(BATTERY_CAPACITY_PATH);
    char status[32];
    read_str_from_file(BATTERY_STATUS_PATH, status, sizeof(status));

    int is_charging = (strcasecmp(status, "Charging") == 0 || strcasecmp(status, "Full") == 0);

    if (capacity <= 99) {
        dexopt_run_this_cycle = 0;
    }

    if (is_charging) {
        if (capacity == 100 && !dexopt_run_this_cycle && dexopt_pid == -1) {
            start_dexopt();
        }
    } else {
        if (dexopt_pid != -1) {
            stop_dexopt();
        }
    }
}

int main() {
    struct sockaddr_nl sa;
    int sock;
    struct pollfd pfd;
    char buf[4096];

    signal(SIGCHLD, handle_sigchld);

    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    sa.nl_groups = 1;

    sock = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
    if (sock < 0) return 1;

    if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(sock);
        return 1;
    }

    pfd.fd = sock;
    pfd.events = POLLIN;

    check_state();

    while (1) {
        if (poll(&pfd, 1, -1) > 0) {
            ssize_t len = recv(sock, buf, sizeof(buf), 0);
            if (len > 0) {
                if (strstr(buf, "power_supply")) {
                    check_state();
                }
            }
        }
    }

    close(sock);
    return 0;
}
