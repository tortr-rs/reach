/* reach: ssh by name on the local network. "reach start" answers to its name, "reach NAME" finds it, puts your
 * key there the first time (ssh asks the password once) and logs in. ssh does the connecting, reach only finds. */
#if (defined(__x86_64__) || defined(__i386__)) && !defined(LIBC)
#include "c.h"
#else
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define PORT 7177
#define ASK "reach?"

static void die(const char *m)
{
    fprintf(stderr, "reach: %s\n", m);
    exit(1);
}

static const char *home(void)
{
    const char *h = getenv("HOME");
    struct passwd *p;
    if (h && *h) return h;
    p = getpwuid(getuid());
    return p ? p->pw_dir : "/";
}

static const char *me(void)
{
    struct passwd *p = getpwuid(getuid());
    const char *u = getenv("USER");
    return p ? p->pw_name : u ? u : "root";
}

static int run(char *const av[], int in)
{
    int st;
    pid_t p = fork();
    if (p < 0) return -1;
    if (!p) {
        if (in >= 0) { dup2(in, 0); close(in); }
        execvp(av[0], av);
        _exit(127);
    }
    while (waitpid(p, &st, 0) < 0) if (errno != EINTR) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int udp(int port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0), on = 1;
    struct sockaddr_in a;
    if (fd < 0) die("no socket");
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) return -1;
    return fd;
}

/* sshd: up already if something takes a connection on 22, else started through doas */
static int listening(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0), r;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(22);
    a.sin_addr.s_addr = htonl(0x7f000001);
    r = connect(fd, (struct sockaddr *)&a, sizeof a);
    close(fd);
    return r == 0;
}

static void sshd(void)
{
    char *root[] = { "sh", "-c", "ssh-keygen -A >/dev/null && /usr/sbin/sshd", NULL };
    char *doas[] = { "doas", "sh", "-c", "ssh-keygen -A >/dev/null && /usr/sbin/sshd", NULL };
    if (listening()) return;
    if (access("/usr/sbin/sshd", X_OK)) die("there is no sshd: install openssh-server");
    if (run(getuid() ? doas : root, -1)) die("sshd did not start");
}

static char *pidfile(void)
{
    static char f[512];
    snprintf(f, sizeof f, "%s/.cache/reach.pid", home());
    return f;
}

static void start(const char *name)
{
    char host[256], reply[600], buf[64];
    struct sockaddr_in from;
    socklen_t fl;
    FILE *f;
    int fd;
    pid_t p;
    if (!name) {
        if (gethostname(host, sizeof host)) die("no host name");
        name = host;
    }
    sshd();
    if ((fd = udp(PORT)) < 0) die("port 7177 is taken: is reach running already? (reach stop)");
    snprintf(reply, sizeof reply, "reach %s %s", name, me());
    if ((p = fork()) < 0) die("fork failed");
    if (p) {
        snprintf(buf, sizeof buf, "%s/.cache", home());
        mkdir(buf, 0755);
        if ((f = fopen(pidfile(), "w"))) { fprintf(f, "%d\n", (int)p); fclose(f); }
        printf("reach: answering as %s\n", name);
        return;
    }
    setsid();
    if ((p = open("/dev/null", O_RDWR)) >= 0) { dup2(p, 0); dup2(p, 1); dup2(p, 2); if (p > 2) close(p); }
    for (;;) {
        ssize_t n;
        fl = sizeof from;
        n = recvfrom(fd, buf, sizeof buf - 1, 0, (struct sockaddr *)&from, &fl);
        if (n < 0) continue;
        buf[n] = 0;
        if (!strcmp(buf, ASK)) sendto(fd, reply, strlen(reply), 0, (struct sockaddr *)&from, fl);
    }
}

static void stop(void)
{
    FILE *f = fopen(pidfile(), "r");
    int p = 0;
    if (!f || fscanf(f, "%d", &p) != 1 || p <= 0) die("reach is not running here");
    fclose(f);
    if (kill(p, SIGTERM)) die("reach is not running here");
    unlink(pidfile());
    puts("reach: stopped");
}

struct peer { char name[256], user[64], ip[16]; };

/* asks the network, gives what answered within a second */
static int ask(struct peer *pe, int max)
{
    int fd = udp(0), n = 0;
    struct sockaddr_in a, from;
    socklen_t fl;
    struct pollfd pf;
    char buf[600];
    if (fd < 0) die("no socket");
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(PORT);
    a.sin_addr.s_addr = htonl(0xffffffff);
    sendto(fd, ASK, strlen(ASK), 0, (struct sockaddr *)&a, sizeof a);
    pf.fd = fd;
    pf.events = POLLIN;
    while (n < max && poll(&pf, 1, 1000) > 0) {
        ssize_t r;
        fl = sizeof from;
        r = recvfrom(fd, buf, sizeof buf - 1, 0, (struct sockaddr *)&from, &fl);
        if (r <= 0) continue;
        buf[r] = 0;
        if (sscanf(buf, "reach %255s %63s", pe[n].name, pe[n].user) != 2) continue;
        snprintf(pe[n].ip, sizeof pe[n].ip, "%s", inet_ntoa(from.sin_addr));
        n++;
    }
    close(fd);
    return n;
}

static void list(void)
{
    struct peer pe[64];
    int i, n = ask(pe, 64);
    if (!n) die("nothing answered: run reach start on the other machine");
    for (i = 0; i < n; i++) printf("%-20s %-15s %s\n", pe[i].name, pe[i].ip, pe[i].user);
}

static void go(const char *name, int argc, char **argv)
{
    struct peer pe[64];
    char key[512], pub[520], to[340], *av[64];
    int i, n = ask(pe, 64), k, fd;
    for (i = 0; i < n && strcasecmp(pe[i].name, name); i++) {}
    if (i == n) {
        fprintf(stderr, "reach: %s did not answer%s\n", name, n ? ", these did:" : "");
        for (k = 0; k < n; k++) fprintf(stderr, "  %s\n", pe[k].name);
        exit(1);
    }
    snprintf(to, sizeof to, "%s@%s", pe[i].user, pe[i].ip);
    snprintf(key, sizeof key, "%s/.ssh/id_ed25519", home());
    snprintf(pub, sizeof pub, "%s.pub", key);
    if (access(pub, R_OK)) {
        char *gen[] = { "ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", key, NULL };
        snprintf(to, sizeof to, "%s/.ssh", home());
        mkdir(to, 0700);
        snprintf(to, sizeof to, "%s@%s", pe[i].user, pe[i].ip);
        if (run(gen, -1)) die("ssh-keygen failed");
    }
    {
        char *test[] = { "ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "-o", "StrictHostKeyChecking=accept-new", to, "true", NULL };
        char *copy[] = { "ssh", "-o", "StrictHostKeyChecking=accept-new", to,
            "umask 077; mkdir -p .ssh && cat >> .ssh/authorized_keys", NULL };
        if (run(test, -1)) {
            printf("reach: first time on %s, its password puts your key there\n", pe[i].name);
            if ((fd = open(pub, O_RDONLY)) < 0) die("cannot read the key");
            if (run(copy, fd)) die("the key did not get there");
            close(fd);
        }
    }
    k = 0;
    av[k++] = "ssh";
    av[k++] = to;
    for (i = 0; i < argc && k < 63; i++) av[k++] = argv[i];
    av[k] = NULL;
    execvp("ssh", av);
    die("there is no ssh: install openssh-client");
}

static void usage(void)
{
    fputs("usage: reach start [NAME]   answer to NAME (the host name) on this network, sshd started if needed\n"
          "       reach stop           stop answering\n"
          "       reach list           what answers on this network\n"
          "       reach NAME [CMD]     log in to NAME, or run CMD there\n", stderr);
    exit(2);
}

int main(int argc, char **argv)
{
    if (argc < 2) usage();
    if (!strcmp(argv[1], "start")) start(argc > 2 ? argv[2] : NULL);
    else if (!strcmp(argv[1], "stop")) stop();
    else if (!strcmp(argv[1], "list")) list();
    else if (argv[1][0] == '-') usage();
    else go(argv[1], argc - 2, argv + 2);
    return 0;
}
