#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

static void
usage(void) {
  printf(1, "usage: memstress [-n pages] [-t ticks] [-w]\n");
  exit();
}

int
main(int argc, char *argv[])
{
  int pages = 10;
  int hold_ticks = 200;
  int do_write = 0;
  int i, p;

  // Parse options
  for(i = 1; i < argc; i++){
    if(argv[i][0] == '-'){
      if(argv[i][1] == 'n'){
        if(i + 1 < argc){
          pages = atoi(argv[i + 1]);
          i++;
        }
        else{
          usage();
        }
      }
      else if(argv[i][1] == 't'){
        if(i + 1 < argc){
          hold_ticks = atoi(argv[i + 1]);
          i++;
        }
        else{
          usage();
        }
      }
      else if(argv[i][1] == 'w'){
        do_write = 1;
      }
      else{
        usage();
      }
    }
    else{
      usage();
    }
  }

  int pid = getpid();
  printf(1, "[memstress] pid=%d pages=%d hold=%d ticks write=%d\n",
         pid, pages, hold_ticks, do_write);

  int inc = pages * 4096;
  char *base = sbrk(inc);
  if (base == (char*)-1) {
    printf(1, "[memstress] sbrk failed\n");
    exit();
  }

  if (do_write) {
    for (p = 0; p < pages; p++) {
      base[p*4096] = (char)(p & 0xff);
    }
  }

  sleep(hold_ticks);

  printf(1, "[memstress] pid=%d done\n", pid);
  exit();
}
