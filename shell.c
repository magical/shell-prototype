#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <pty.h>
#include <sys/select.h>

const char* shell = "/usr/lib/plan9/bin/cat";
char* shellargs[] = {"cat", 0};

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
    int fd[2];
    int err;

    err = openpty(&fd[1], &fd[0], NULL, NULL, NULL);
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
        setsid();
        dup2(fd[1], 0);
        dup2(fd[1], 1);
        dup2(fd[1], 2);
        close(fd[0]);
        exec_shell();
        break;
    default:
        close(fd[1]);
        shellpid = err;
        shellfd = fd[0];
        //signal(SIGCHLD, sigchld);
        break;
    }
}

size_t read_shell(char* buf, size_t size) {
    struct timeval tv = {0, 1000};
    fd_set rfd;
    fd_set errfd;
    FD_ZERO(&rfd);
    FD_SET(shellfd, &rfd);
    FD_SET(shellfd, &errfd);
    if (!select(shellfd+1, &rfd, NULL, &errfd, &tv)) {
        return 0;
    }
    return read(shellfd, buf, size);
}
