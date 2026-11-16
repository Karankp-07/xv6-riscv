#include "kernel/types.h"
#include "user/user.h"

// Shell command to restore a checkpointed process from a snapshot file.
// Usage: restore <file>
//
// If the restore succeeds, this process is replaced by the saved one
// and nothing below the restore() call ever runs.

int
main(int argc, char *argv[])
{
  if (argc != 2) {
    fprintf(2, "usage: restore <snapshot-file>\n");
    exit(1);
  }

  printf("loading %s ...\n", argv[1]);

  if (restore(argv[1]) < 0) {
    fprintf(2, "restore: failed, is %s a valid snapshot?\n", argv[1]);
    exit(1);
  }

  // never reached on success
  exit(0);
}
