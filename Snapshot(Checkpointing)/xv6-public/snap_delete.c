#include "types.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    printf(2, "Usage: snap_delete id\n");
    exit();
  }
  int id = atoi(argv[1]);
  if(snapshot_delete(id) < 0){
    printf(2, "snapshot_delete failed\n");
  } else {
    printf(1, "snapshot deleted\n");
  }
  exit();
}
