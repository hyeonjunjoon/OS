#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "fs.h"

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))

int
main(int argc, char *argv[])
{
  if(argc != 2){
    printf(2, "Usage: print_addr filename\n");
    exit();
  }
  
  int fd = open(argv[1], O_RDONLY);
  if(fd < 0){
    printf(2, "cannot open %s\n", argv[1]);
    exit();
  }
  
  
  for(int i=0; i<NDIRECT; i++){
    int addr = get_block_addr(fd, i);
    if(addr != 0){
      printf(1, "addr[%d] : %x\n", i, addr);
    }
  }
  
  
  int indirect_addr = get_block_addr(fd, -1);
  if(indirect_addr != 0){
    printf(1, "addr[%d] : %x (INDIRECT POINTER)\n", NDIRECT, indirect_addr);
    
    
    for(int j=0; j<NINDIRECT; j++){
      int addr = get_block_addr(fd, NDIRECT + j);
      if(addr != 0){
        printf(1, "addr[%d] -> [%d] (bn : %d) : %x\n", NDIRECT, j, NDIRECT+j, addr);
      }
    }
  }
  
  close(fd);
  exit();
}
