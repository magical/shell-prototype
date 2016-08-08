#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <pty.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include "shell.h"

struct selfpipe selfpipe;

int pipe2(int pipefd[2], int flags);

static void sigchld();
void* reallocarray(void* v, size_t nmemb, size_t size) {
    // TODO: check for overflow
    return realloc(v, nmemb*size);
}

void cloexec(int fd){
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
        perror("cloexec: fcntl");
        return;
    }
}

int shell_init(Shell *sh) {
    int err;
    int mfd;
    int sfd;

    sh->fd = 0;
    sh->sfd = 0;
    sh->pid = 0;
    sh->jobs = NULL;
    sh->joblen = 0;
    sh->jobcap = 0;

    err = pipe2((int*)&selfpipe, O_CLOEXEC | O_NONBLOCK);
    if (err < 0) {
        perror("shell_init: pipe2");
        return -1;
    }

    // TODO: one pty per command
    err = openpty(&mfd, &sfd, NULL, NULL, NULL);
    if (err < 0) {
        perror("shell_init: openpty");
        return -1;
    }
    cloexec(mfd);
    cloexec(sfd);

    tcgetattr(sfd, &sh->tc);
    sh->tc.c_lflag &= ~ICANON;
    tcsetattr(sfd, 0, &sh->tc);

    signal(SIGCHLD, sigchld);

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

    pid = waitpid(-1, &status, WNOHANG);
    if (pid < 0) {
        perror("shell_reap: waitpid");
        return;
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
    sh->pid = 0;
    sh->fd = 0;
    sh->sfd = 0;
}

bool shell_running(Shell *sh) {
    return sh->pid != 0;
}

pid_t do_exec(int fd, const char *cmd, char **argv) {
    long err;

    switch (err = fork()) {
    case -1:
        perror("fork");
        exit(1); // XXX
        break;

    case 0:
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);

        if (setsid() < 0) {
            perror("do_exec: setsid");
        }

        if (ioctl(fd, TIOCSCTTY, 0) < 0) {
            perror("do_exec: ioctl");
        }

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

        // if we get this far, exec failed
        perror(cmd);
        for (;;) {
            _exit(253);
        }
    default:
        return err;
    }
}

void shell_run(Shell *sh, char *cmdline) {
    Job *job;

    if (sh->joblen == sh->jobcap) {
        void *v;
        int len = 1;
        int newcap;
        newcap = sh->jobcap * 2;
        if (newcap - sh->joblen < len) {
            newcap = sh->joblen + len;
        }
        if (newcap < sh->jobcap) {
            printf("shell_run: overflow\n");
            exit(1);
        }
        v = reallocarray(sh->jobs, newcap, sizeof sh->jobs[0]);
        if (v == NULL) {
            perror("shell_run: realloc");
            exit(1);
        }
        sh->jobs = v;
        sh->jobcap = newcap;
    }

    job = job_create(cmdline);
    job_start(job, sh);
    sh->pid = job->pid;
    sh->jobs[sh->joblen] = job;
    sh->joblen++;
}

Job* job_create(char* cmdline) {
    Job* job = malloc(sizeof(Job));
    if (job == NULL) {
        perror("job_create: malloc");
        return NULL;
    }
    job->cmdline = strdup(cmdline);
    if (job->cmdline == NULL) {
        perror("job_create: strdup");
        free(job);
        return NULL;
    }
    job->dir = "";
    job->pid = 0;
    job->status = 0;
    job->hist = 0;
    job->histlen = 0;
    job->histcap = 0;
    return job;
}

void job_start(Job* job, Shell* sh) {
    static const char* shellcmd = "/bin/sh";
    char *argv[] = {"sh", "-c", "", 0};
    argv[2] = job->cmdline;

    job->ctime = time(NULL);
    job->pid = do_exec(sh->sfd, shellcmd, argv);
}

ssize_t shell_read(Shell *sh, char* buf, size_t size) {
    return read(sh->fd, buf, size);
}

ssize_t shell_write(Shell *sh, char* buf, size_t size) {
    return write(sh->fd, buf, size);
}

int shell_fd(Shell *sh) {
    return sh->fd;
}

void job_appendhist(Job* job, char* buf, size_t len) {
    if (job->histcap - job->histlen < len) {
        void *v;
        int newcap;
        newcap = job->histcap * 2;
        if (newcap - job->histlen < len) {
            newcap = job->histlen + len;
        }
        if (newcap < job->histcap) {
            printf("overflow\n");
            exit(1);
        }
        v = realloc(job->hist, newcap);
        if (v == NULL) {
            perror("realloc");
            exit(1);
        }
        job->hist = v;
        job->histcap = newcap;
    }
    memmove(job->hist+job->histlen, buf, len);
    job->histlen += len;
}
