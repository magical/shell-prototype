//#include <termios.h> /* struct termios */
//#include <sys/resource.h> /* struct rusage */
//#include <stdbool.h> /* bool */
//#include <unistd.h> /* ssize_t, pid_t */

// Shell:
//  runs jobs
//  owns the pty
//  stores scrollback

struct selfpipe {
    int r, w;
};
extern struct selfpipe selfpipe;

typedef struct Shell Shell;
typedef struct Job Job;

struct Shell {
    pid_t pid; // current job
    int fd;  // pty master
    int sfd; // pty slave

    struct termios tc;

    Job **jobs;
    int joblen;
    int jobcap;
};

struct Job {
    char *cmdline;
    char *dir;
    // TODO: env?
    struct rusage rusage;

    pid_t pid; // process id
    int status; // exit status
    time_t ctime; // start time

    // scrollback buffer
    char *hist;
    int histlen;
    int histcap;
};

int shell_init(Shell* sh);
void shell_run(Shell *sh, char *cmdline);
void shell_exit(Shell *sh);
void shell_reap(Shell *sh);
int shell_fd(Shell *sh);
bool shell_running(Shell *sh);
ssize_t shell_read(Shell* sh, char* buf, size_t size);
ssize_t shell_write(Shell* sh, char* buf, size_t size);

Job* job_create(char* cmdline);
void job_start(Job* job, Shell* sh);
void job_appendhist(Job* job, char* buf, size_t len);
