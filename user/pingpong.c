#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main() {
    int p1[2], p2[2]; // p1: parent->child, p2: child->parent
    char buf[1];

    pipe(p1);
    pipe(p2);

    if (fork() == 0) { // child process
        read(p1[0], buf, 1);
        printf("%d: received ping\n", getpid());
        write(p2[1], buf, 1);
        exit(0);
    } else { // parent process
        write(p1[1], "a", 1);
        read(p2[0], buf, 1);
        printf("%d: received pong\n", getpid());
        wait(0);
    }
    exit(0);
}