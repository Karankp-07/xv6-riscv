#include "kernel/types.h"
#include "user/user.h"

int main(void) {
    printf("Hello from xv6!\n");
    printf("My PID: %d\n", getpid());
    exit(0);
}