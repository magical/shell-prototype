
void fork_shell(void);
size_t read_shell(char* buf, size_t size);
size_t write_shell(char* buf, size_t size);

extern int shellfd;
extern int shellpid;
