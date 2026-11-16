#include "kernel/types.h"
#include "user/user.h"

// cptest.c – Checkpoint/Restore demonstration for xv6.
//
// Scenario:
//   1. Run `cptest` in the xv6 shell.
//      The program counts from 1 to 10, sleeping briefly between ticks.
//      At count == 5 it calls checkpoint("snap.chk") to freeze its state.
//      It then continues counting to 10 and exits normally.
//
//   2. Run `restore snap.chk` in a *new* xv6 shell session (or just after
//      cptest exits).  The restore utility calls sys_restore(), which tears
//      down the restore process's address space, maps in the saved pages, and
//      resumes execution right after the checkpoint() call with counter == 5.
//      You will see the counter pick up from 5 and reach 10 again — proving
//      that memory state and the program counter were fully preserved.

#define SNAPSHOT "snap.chk"
#define COUNT_TO  10
#define CHK_AT     5
#define TICK_SLEEP 20   // ticks to sleep between counts (~0.2 s at 100 Hz)

int
main(void)
{
  int i;

  printf("\n=== cptest: Process Checkpoint Demo ===\n");
  printf("PID: %d\n\n", getpid());

  for (i = 1; i <= COUNT_TO; i++) {
    printf("  [tick %2d] uptime = %d ticks\n", i, uptime());

    if (i == CHK_AT) {
      printf("\n>> Reached count %d – saving snapshot to '%s' ...\n",
             CHK_AT, SNAPSHOT);

      if (checkpoint(SNAPSHOT) < 0) {
        fprintf(2, "cptest: checkpoint() failed!\n");
        exit(1);
      }

      // Both the original run AND any restore of this snapshot resume here.
      // The counter variable `i` in the saved stack frame holds CHK_AT,
      // so the loop continues from CHK_AT+1 in both cases.
      printf(">> Snapshot saved (or just restored). Resuming from count %d.\n\n",
             i);
    }

    pause(TICK_SLEEP);
  }

  printf("\n=== cptest: Done! Counter reached %d. ===\n\n", COUNT_TO);
  exit(0);
}
