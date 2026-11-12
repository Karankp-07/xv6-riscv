#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  uint64 before = getmemsize();
  printf("Memory size before sbrk: %ld bytes\n", before);

  char *p = sbrk(4096);
  if (p == (char *)-1) {
    printf("sbrk(4096) failed\n");
    exit(1);
  }

  uint64 after = getmemsize();
  printf("Memory size after sbrk: %ld bytes\n", after);

  if (after - before == 4096) {
    printf("SUCCESS: Memory grew by 4096 bytes (from %ld to %ld)\n", before, after);
    exit(0);
  } else {
    printf("FAILURE: Unexpected memory difference: %ld bytes\n", after - before);
    exit(1);
  }
}
