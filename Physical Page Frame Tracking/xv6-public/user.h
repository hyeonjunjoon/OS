struct stat;
struct rtcdate;

// Physical frame information structure (shared with kernel)
struct physframe_info {
  uint frame_index;     // Physical frame number (index)
  int allocated;        // 1: in use, 0: free
  int pid;              // Owner PID (-1 or 0 for kernel)
  uint start_tick;      // Tick when this PID started using frame
};

// C-2: IPT entry structure (for phys2virt)
struct ipt_entry {
  unsigned int pfn;           // Physical frame number
  unsigned int pid;           // Owner process PID
  unsigned int va;            // Mapped virtual address (page-aligned)
  unsigned short flags;       // PTE flags (P/W/U etc.)
  unsigned short refcnt;      // Reference count
  struct ipt_entry *next;     // Hash chain (kernel only)
};

// system calls
int fork(void);
int exit(void) __attribute__((noreturn));
int wait(void);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);
int dump_physmem_info(void *addr, int max_entries);
int vtop(void *va, unsigned int *pa_out, unsigned int *flags_out);
int phys2virt(unsigned int pa_page, struct ipt_entry *out, int max);

// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void printf(int, const char*, ...);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);
