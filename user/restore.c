#include "kernel/types.h"
#include "user/user.h"

// restore.c – xv6 shell command to resurrect a checkpointed process.
//
// Usage:  restore <snapshot-file>
//
// Calls sys_restore() which:
//   1. Reads the snapshot file from disk.
//   2. Validates the XV6C magic header.
//   3. Frees this process's current address space.
//   4. Maps in the saved memory pages.
//   5. Restores all CPU registers from the saved trapframe.
//   6. Returns to userspace at the saved program counter (epc).
//
// If sys_restore() succeeds, execution is transferred to the restored
// program immediately — the lines below the restore() call are NEVER
// reached.  If restore() returns, something went wrong.

int
main(int argc, char *argv[])
{
  if (argc != 2) {
    fprintf(2, "Usage: restore <snapshot-file>\n");
    fprintf(2, "Example: restore snap.chk\n");
    exit(1);
  }

  printf("restore: loading snapshot '%s' ...\n", argv[1]);

  // sys_restore() replaces our entire address space on success.
  // Control never returns here if it works.
  int ret = restore(argv[1]);

  // If we reach this point, restore failed.
  fprintf(2, "restore: failed (ret=%d). Is the file a valid XV6C snapshot?\n",
          ret);
  exit(1);
}
