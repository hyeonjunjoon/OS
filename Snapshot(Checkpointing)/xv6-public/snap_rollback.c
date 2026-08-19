#include "types.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    printf(2, "Usage: snap_rollback id\n");
    exit();
  }
  int id = atoi(argv[1]);
  if(snapshot_rollback(id) < 0){
    printf(2, "snapshot_rollback failed\n");
  } else {
    printf(1, "rollback successful\n");
  }
  exit();
}
