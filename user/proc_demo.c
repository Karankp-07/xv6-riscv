#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void) {
    int pid = getpid();
    printf("Parent PID: %d\n", pid);

    int child_pid = fork();
    if (child_pid < 0) {
        printf("Fork failed!\n");
        exit(1);
    } else if (child_pid == 0) {
        // Child process
        printf("Child PID: %d\n", getpid());
        printf("Uptime: %d ticks\n", uptime());
        exit(0);
    } else {
        // Parent process
        wait(0); // Wait for the child to finish
        printf("Child process has exited. Parent PID: %d\n", pid);
    }

    exit(0);
}