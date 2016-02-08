#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <pty.h>

static const char* shell = "/usr/lib/plan9/bin/rc";
static char* shellargs[] = {"rc", 0};

int shellfd;
int shellpid;

void exec_shell(void) {
    unsetenv("COLUMNS");
    unsetenv("LINES");
    unsetenv("TERMCAP");
    setenv("TERM", "magicalterm", 1);

    signal(SIGCHLD, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGALRM, SIG_DFL);
    execv(shell, shellargs);
    perror("execvp");
    _exit(1);
}

void fork_shell(void) {
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
        exec_shell();
        break;
    default:
        close(sfd);
        shellpid = err;
        shellfd = mfd;
        //signal(SIGCHLD, sigchld);
        break;
    }
}

ssize_t read_shell(char* buf, size_t size) {
    return read(shellfd, buf, size);
}

ssize_t write_shell(char* buf, size_t size) {
    return write(shellfd, buf, size);
}
