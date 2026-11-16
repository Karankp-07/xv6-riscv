#include "kernel/types.h"
#include "user/user.h"

// A demo program to test the checkpoint/restore feature.
// Run it once to count up to 10 and save a snapshot at count 5.
// Then run `restore snap.chk` and watch it pick up from count 5 again.

#define SNAPSHOT "snap.chk"

int
main(void)
{
  int i;

  printf("\ncptest running (pid %d)\n", getpid());

  for (i = 1; i <= 10; i++) {
    printf("  count = %d  (uptime %d)\n", i, uptime());

    if (i == 5) {
      printf("saving snapshot...\n");
      if (checkpoint(SNAPSHOT) < 0) {
        fprintf(2, "checkpoint failed\n");
        exit(1);
      }
      // when restored, execution resumes here with i == 5
      printf("snapshot ok, continuing from %d\n", i);
    }

    pause(20);
  }

  printf("done. reached 10.\n\n");
  exit(0);
}
