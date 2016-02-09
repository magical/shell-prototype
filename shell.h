//#include <unistd.h> /* ssize_t */

typedef struct Shell Shell;
struct Shell {
    int pid;
    int fd;
};

void shell_init(Shell* sh);
void shell_fork(Shell* sh);
ssize_t shell_read(Shell* sh, char* buf, size_t size);
ssize_t shell_write(Shell* sh, char* buf, size_t size);
