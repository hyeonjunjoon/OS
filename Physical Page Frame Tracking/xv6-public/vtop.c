// vtop.c - Virtual to Physical address translation utility
#include "types.h"
#include "user.h"

void
usage(void)
{
  printf(2, "Usage: vtop <virtual_address_hex>\n");
  printf(2, "Example: vtop 0x1000\n");
  exit();
}

// Simple hex string to uint converter
uint
hextoi(char *s)
{
  uint n = 0;
  int i;

  // Skip "0x" or "0X" prefix if present
  if(s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    s += 2;

  for(i = 0; s[i] != '\0'; i++){
    n *= 16;
    if(s[i] >= '0' && s[i] <= '9')
      n += s[i] - '0';
    else if(s[i] >= 'a' && s[i] <= 'f')
      n += s[i] - 'a' + 10;
    else if(s[i] >= 'A' && s[i] <= 'F')
      n += s[i] - 'A' + 10;
    else {
      printf(2, "vtop: invalid hex character '%c'\n", s[i]);
      return 0xFFFFFFFF;
    }
  }
  return n;
}

int
main(int argc, char *argv[])
{
  uint va, pa, flags;
  int ret;

  if(argc != 2){
    usage();
  }

  va = hextoi(argv[1]);
  if(va == 0xFFFFFFFF){
    printf(2, "vtop: invalid virtual address\n");
    exit();
  }

  ret = vtop((void*)va, &pa, &flags);

  if(ret < 0){
    printf(1, "[vtop] VA 0x%x -> NOT MAPPED\n", va);
  } else {
    printf(1, "[vtop] VA 0x%x -> PA 0x%x (flags=0x%x", va, pa, flags);

    // Decode flags
    printf(1, " [");
    if(flags & 0x001) printf(1, "P");  // Present
    if(flags & 0x002) printf(1, "W");  // Writable
    if(flags & 0x004) printf(1, "U");  // User
    printf(1, "])\n");
  }

  exit();
}
