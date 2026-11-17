#include "kernel/types.h"
#include "user/user.h"

// Periodic checkpointing workload.
// It performs a long-running counting task up to 20 ticks.
// Every 5 counts, it saves its state to snap.chk.
// If the system loses power or QEMU is killed with Ctrl-A X,
// the process can be resumed from the most recent checkpoint.
// When the entire computation finishes, snap.chk is unlinked.

#define SNAPSHOT "snap.chk"
#define TOTAL_TICKS 20
#define CHK_INTERVAL 5

int
main(void)
{
  int i;

  printf("cptest running (pid %d)\n", getpid());

  for (i = 1; i <= TOTAL_TICKS; i++) {
    printf("  count = %d  (uptime %d)\n", i, uptime());

    if (i % CHK_INTERVAL == 0) {
      int ret = checkpoint(SNAPSHOT);
      if (ret < 0) {
        fprintf(2, "checkpoint failed\n");
        exit(1);
      }
      if (ret == 0) {
        printf("checkpoint saved at count %d (safe to power off anytime)\n", i);
      } else if (ret == 1) {
        printf("resumed from checkpoint at count %d\n", i);
      }
    }

    pause(25);
  }

  // Workload finished, clean up the snapshot file so future boots start clean
  unlink(SNAPSHOT);
  printf("cptest finished successfully. snapshot removed.\n");
  exit(0);
}
