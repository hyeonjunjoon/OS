#include "types.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int id = snapshot_create();
  if(id < 0){
    printf(2, "snapshot_create failed\n");
  } else {
    printf(1, "snapshot created with id %d\n", id);
  }
  exit();
}
