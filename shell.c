#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <pty.h>
#include <sys/wait.h>
#include "shell.h"

struct selfpipe selfpipe;

int pipe2(int pipefd[2], int flags);

int shell_init(Shell *sh) {
    int err;
    int mfd;
    int sfd;

    sh->fd = 0;
    sh->pid = 0;

    err = pipe2((int*)&selfpipe, O_CLOEXEC | O_NONBLOCK);
    if (err < 0) {
        perror("pipe2");
        return -1;
    }

    // TODO: one pty per command
    err = openpty(&mfd, &sfd, NULL, NULL, NULL);
    if (err < 0) {
        perror("openpty");
        return -1;
    }

    sh->fd = mfd;
    sh->sfd = sfd;
    return 0;
}

static void sigchld() {
    static const char byte[1] = {0};
    write(selfpipe.w, byte, sizeof byte);
}

void shell_reap(Shell *sh) {
    int status;
    pid_t pid;

    pid = waitpid(WAIT_ANY, &status, WNOHANG);
    if (pid < 0) {
        perror("waitpid");
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
        printf("command exited with status %d\n", WEXITSTATUS(status));
    }

    if (pid != sh->pid) {
        printf("unknown pid exited: %d\n", pid);
    } else {
        sh->pid = 0;
    }
}

void shell_exit(Shell *sh) {
    if (sh->pid != 0) {
        kill(-sh->pid, SIGTERM);
    }
    close(sh->fd);
    close(sh->sfd);
}

bool shell_running(Shell *sh) {
    return sh->pid != 0;
}

void do_exec(Shell *sh, const char *cmd, char **argv) {
    int mfd = sh->fd;
    int sfd = sh->sfd;
    int err;

    switch (err = fork()) {
    case -1:
        perror("fork");
        exit(1);
        break;
    case 0:
        close(mfd);
        setsid();
        dup2(sfd, 0);
        dup2(sfd, 1);
        dup2(sfd, 2);
        ioctl(sfd, TIOCSCTTY, NULL);

        unsetenv("COLUMNS");
        unsetenv("LINES");
        unsetenv("TERMCAP");
        setenv("TERM", "magicalterm", 1);

        // Do we really need to reset all these signals?
        signal(SIGCHLD, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGALRM, SIG_DFL);

        execv(cmd, argv);
        perror("execv");
        _exit(1);
        break;
    default:
        sh->pid = err;
        signal(SIGCHLD, sigchld);
        break;
    }
}

void shell_run(Shell *sh, char *cmdline) {
    static const char* shellcmd = "/usr/lib/plan9/bin/rc";
    char *argv[] = {"rc", "-c", "", 0};
    argv[2] = cmdline;
    do_exec(sh, shellcmd, argv);
}

ssize_t shell_read(Shell *sh, char* buf, size_t size) {
    return read(sh->fd, buf, size);
}

ssize_t shell_write(Shell *sh, char* buf, size_t size) {
    return write(sh->fd, buf, size);
}
