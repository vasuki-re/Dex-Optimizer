#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <linux/netlink.h>
#include <poll.h>

#define BAT_CAP "/sys/class/power_supply/battery/capacity"
#define BAT_STAT "/sys/class/power_supply/battery/status"
#define LOG_PATH "/storage/emulated/0/dexopt.log"
#define CMD_OPT "cmd package bg-dexopt-job"

static pid_t child_pid = -1;
static int cycle_done = 0;

static void write_log(const char *msg) {
    int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        char buf[128];
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        int len = snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d %s\n",
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec, msg);
        if (len > 0) write(fd, buf, len);
        close(fd);
    }
}

static int get_capacity() {
    char b[4];
    int fd = open(BAT_CAP, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, b, sizeof(b) - 1);
    close(fd);
    if (n > 0) {
        b[n] = 0;
        return atoi(b);
    }
    return -1;
}

static int is_charging_or_full() {
    char c;
    int fd = open(BAT_STAT, O_RDONLY);
    if (fd < 0) return 0;
    if (read(fd, &c, 1) == 1) {
        close(fd);
        return (c == 'C' || c == 'F');
    }
    close(fd);
    return 0;
}

static void trigger_opt() {
    if (child_pid != -1) return;
    write_log("Dexopt started");
    if ((child_pid = fork()) == 0) {
        char *argv[] = {"su", "-c", CMD_OPT, NULL};
        execvp("su", argv);
        _exit(1);
    }
}

static void reaper(int sig) {
    int s;
    pid_t p;
    while ((p = waitpid(-1, &s, WNOHANG)) > 0) {
        if (p == child_pid) {
            child_pid = -1;
            if (WIFEXITED(s) && WEXITSTATUS(s) == 0) {
                cycle_done = 1;
                write_log("Dexopt runned Successfully");
            }
        }
    }
}

static void eval_state() {
    int cap = get_capacity();
    if (cap <= 99) {
        cycle_done = 0;
    } else if (cap == 100 && !cycle_done && child_pid == -1) {
        if (is_charging_or_full()) trigger_opt();
    }
}

int main() {
    struct sockaddr_nl sa;
    struct pollfd pfd;
    char buf[2048];
    int sock;

    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = reaper;
    act.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &act, 0);

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

    eval_state();

    while (1) {
        if (poll(&pfd, 1, -1) > 0) {
            ssize_t len = recv(sock, buf, sizeof(buf), 0);
            if (len > 0) {
                int found = 0;
                for (ssize_t i = 0; i < len; i += strlen(buf + i) + 1) {
                    if (strstr(buf + i, "power_supply")) {
                        found = 1;
                        break;
                    }
                }
                if (found) eval_state();
            }
        }
    }
    close(sock);
    return 0;
}
