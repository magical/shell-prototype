//#include <stdbool.h> /* bool */
//#include <unistd.h> /* ssize_t, pid_t */
//#include <sys/resource.h> /* struct rusage */

struct selfpipe {
    int r, w;
};
extern struct selfpipe selfpipe;

typedef struct Shell Shell;
struct Shell {
    int pid;
    int fd;
    int sfd;
};

int shell_init(Shell* sh);
void shell_run(Shell *sh, char *cmdline);
void shell_reap(Shell *sh);
bool shell_running(Shell *sh);
ssize_t shell_read(Shell* sh, char* buf, size_t size);
ssize_t shell_write(Shell* sh, char* buf, size_t size);
