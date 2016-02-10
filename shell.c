#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <pty.h>
#include <sys/wait.h>
#include "shell.h"

static const char* shellcmd = "/usr/lib/plan9/bin/rc";
static       char* shellargs[] = {"rc", 0};

void shell_init(Shell *sh) {
    sh->fd = 0;
    sh->pid = 0;
}

static void sigchld() {
    int status;
    pid_t pid;
    pid = waitpid(WAIT_ANY, &status, WNOHANG);
    if (pid < 0) {
        perror("waitpid");
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
        printf("shell exited with status %d\n", WEXITSTATUS(status));
    }
    exit(0);
}

static void shell_exec(Shell *sh) {
    unsetenv("COLUMNS");
    unsetenv("LINES");
    unsetenv("TERMCAP");
    setenv("TERM", "magicalterm", 1);

    // Do we need to reset all these signals?
    signal(SIGCHLD, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGALRM, SIG_DFL);

    execv(shellcmd, shellargs);
    perror("execvp");
    _exit(1);
}

void shell_fork(Shell *sh) {
    int mfd;
    int sfd;
    int err;

    err = openpty(&mfd, &sfd, NULL, NULL, NULL);
    if (err < 0) {
        perror("openpty");
        exit(1);
    }
    
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
        shell_exec(sh);
        break;
    default:
        close(sfd);
        sh->pid = err;
        sh->fd = mfd;
        signal(SIGCHLD, sigchld);
        break;
    }
}

ssize_t shell_read(Shell *sh, char* buf, size_t size) {
    return read(sh->fd, buf, size);
}

ssize_t shell_write(Shell *sh, char* buf, size_t size) {
    return write(sh->fd, buf, size);
}
