// init: The initial user-level program

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "user/user.h"
#include "kernel/fcntl.h"

char *argv[] = {"sh", 0};

int
main(void)
{
  int pid, wpid;

  if (open("console", O_RDWR) < 0) {
    mknod("console", CONSOLE, 0);
    open("console", O_RDWR);
  }
  dup(0); // stdout
  dup(0); // stderr

  // Check if an active process snapshot exists from a previous session
  int chk_fd = open("snap.chk", O_RDONLY);
  if (chk_fd >= 0) {
    close(chk_fd);
    printf("init: found saved snapshot 'snap.chk', auto-resuming process\n");
    pid = fork();
    if (pid < 0) {
      printf("init: fork failed during auto-restore\n");
    } else if (pid == 0) {
      char *r_argv[] = {"restore", "snap.chk", 0};
      exec("restore", r_argv);
      printf("init: exec restore failed\n");
      exit(1);
    } else {
      for (;;) {
        wpid = wait((int *)0);
        if (wpid == pid || wpid < 0)
          break;
      }
    }
  }

  for (;;) {
    printf("init: starting sh\n");
    pid = fork();
    if (pid < 0) {
      printf("init: fork failed\n");
      exit(1);
    }
    if (pid == 0) {
      exec("sh", argv);
      printf("init: exec sh failed\n");
      exit(1);
    }

    for (;;) {
      // this call to wait() returns if the shell exits,
      // or if a parentless process exits.
      wpid = wait((int *)0);
      if (wpid == pid) {
        // the shell exited; restart it.
        break;
      } else if (wpid < 0) {
        printf("init: wait returned an error\n");
        exit(1);
      } else {
        // it was a parentless process; do nothing.
      }
    }
  }
}
