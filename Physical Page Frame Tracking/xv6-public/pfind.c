// pfind.c - 물리 주소/프레임에 매핑된 가상 주소 찾기
#include "types.h"
#include "user.h"

void
usage(void)
{
  printf(2, "사용법: pfind <물리주소_16진수>\n");
  printf(2, "예제: pfind 0x100000\n");
  exit();
}

// 간단한 16진수 문자열을 uint로 변환
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
      printf(2, "pfind: invalid hex character '%c'\n", s[i]);
      return 0xFFFFFFFF;
    }
  }
  return n;
}

int
main(int argc, char *argv[])
{
  uint pa;
  struct ipt_entry entries[16];
  int count, i;

  if(argc != 2){
    usage();
  }

  pa = hextoi(argv[1]);
  if(pa == 0xFFFFFFFF){
    printf(2, "pfind: invalid physical address\n");
    exit();
  }

  // Page-align the physical address
  pa = pa & ~0xFFF;

  count = phys2virt(pa, entries, 16);

  if(count < 0){
    printf(2, "pfind: phys2virt failed\n");
    exit();
  }

  if(count == 0){
    printf(1, "[pfind] PA 0x%x (PFN %d) -> NO MAPPINGS\n", pa, pa/4096);
  } else {
    printf(1, "[pfind] PA 0x%x (PFN %d) -> %d mapping(s):\n", pa, pa/4096, count);
    for(i = 0; i < count; i++){
      printf(1, "  [%d] PID=%d VA=0x%x flags=0x%x",
             i, entries[i].pid, entries[i].va, entries[i].flags);

      // Decode flags
      printf(1, " [");
      if(entries[i].flags & 0x001) printf(1, "P");
      if(entries[i].flags & 0x002) printf(1, "W");
      if(entries[i].flags & 0x004) printf(1, "U");
      printf(1, "]\n");
    }
  }

  exit();
}
