// test_c.c - Part C 구현 종합 테스트
#include "types.h"
#include "user.h"

int
main(void)
{
  uint va, pa, flags;
  int ret;
  char *testaddr;
  struct ipt_entry entries[16];
  int count, i;

  printf(1, "\n========================================\n");
  printf(1, "Part C - 종합 테스트\n");
  printf(1, "========================================\n\n");

  // 테스트 1: 스택 변수에 대한 vtop
  printf(1, "[테스트 1] 스택 변수에 대한 vtop\n");
  va = (uint)&ret;
  ret = vtop((void*)va, &pa, &flags);
  if(ret == 0){
    printf(1, "  VA 0x%x -> PA 0x%x, flags=0x%x [", va, pa, flags);
    if(flags & 0x001) printf(1, "P");
    if(flags & 0x002) printf(1, "W");
    if(flags & 0x004) printf(1, "U");
    printf(1, "]\n");
  } else {
    printf(1, "  VA 0x%x -> NOT MAPPED\n", va);
  }

  // Test 2: vtop on heap (malloc)
  printf(1, "\n[Test 2] vtop on heap (malloc)\n");
  testaddr = malloc(100);
  if(testaddr != 0){
    va = (uint)testaddr;
    ret = vtop((void*)va, &pa, &flags);
    if(ret == 0){
      printf(1, "  Heap VA 0x%x -> PA 0x%x, flags=0x%x [", va, pa, flags);
      if(flags & 0x001) printf(1, "P");
      if(flags & 0x002) printf(1, "W");
      if(flags & 0x004) printf(1, "U");
      printf(1, "]\n");

      // Test 3: phys2virt - reverse lookup
      printf(1, "\n[Test 3] phys2virt reverse lookup\n");
      uint pa_page = pa & ~0xFFF;  // Page-align
      count = phys2virt(pa_page, entries, 16);
      printf(1, "  PA 0x%x (PFN %d) -> %d mapping(s):\n", pa_page, pa_page/4096, count);
      for(i = 0; i < count; i++){
        printf(1, "    [%d] PID=%d VA=0x%x flags=0x%x [",
               i, entries[i].pid, entries[i].va, entries[i].flags);
        if(entries[i].flags & 0x001) printf(1, "P");
        if(entries[i].flags & 0x002) printf(1, "W");
        if(entries[i].flags & 0x004) printf(1, "U");
        printf(1, "]\n");
      }
    }
    free(testaddr);
  }

  // Test 4: vtop on unmapped address
  printf(1, "\n[Test 4] vtop on unmapped address\n");
  va = 0x80000000;  // Likely unmapped
  ret = vtop((void*)va, &pa, &flags);
  if(ret == 0){
    printf(1, "  VA 0x%x -> PA 0x%x (unexpected!)\n", va, pa);
  } else {
    printf(1, "  VA 0x%x -> NOT MAPPED (expected)\n", va);
  }

  // Test 5: Multiple allocations and IPT tracking
  printf(1, "\n[Test 5] Multiple allocations\n");
  char *p1 = sbrk(4096);
  char *p2 = sbrk(4096);
  printf(1, "  Allocated 2 pages via sbrk\n");
  printf(1, "  Page 1: VA 0x%x\n", (uint)p1);
  printf(1, "  Page 2: VA 0x%x\n", (uint)p2);

  ret = vtop(p1, &pa, &flags);
  if(ret == 0){
    printf(1, "  Page 1: VA 0x%x -> PA 0x%x\n", (uint)p1, pa);
  }

  ret = vtop(p2, &pa, &flags);
  if(ret == 0){
    printf(1, "  Page 2: VA 0x%x -> PA 0x%x\n", (uint)p2, pa);
  }

  printf(1, "\n========================================\n");
  printf(1, "Part C Test Complete!\n");
  printf(1, "========================================\n\n");

  exit();
}
