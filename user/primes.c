#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

/*
Pipeline architecture (xv6 primes):

P0 (main)
  create initial_pipe
  fork -> P1
  parent(P0): write 2..35 -> close write end -> wait -> exit
  child (P1): sieve(initial_pipe)

Each sieve stage S(k):
  1) read first int from left pipe as prime p, print "prime p"
  2) create right_pipe
  3) fork -> next stage S(k+1)
     - child:  close right_pipe[1], recurse sieve(right_pipe), exit
     - parent: close right_pipe[0], filter input:
               for each num from left pipe:
                 if (num % p != 0) write to right_pipe[1]
               close fds, wait child, exit

Dataflow example:
  initial: 2 3 4 5 6 7 8 9 ...
  S1(p=2):   3 5 7 9 11 ...
  S2(p=3):   5 7 11 13 ...
  S3(p=5):   7 11 13 17 ...
*/

void sieve(int left_pipe[2]){
    int prime;

    if(read(left_pipe[0], &prime, sizeof(prime)) == 0){
        close(left_pipe[0]);
        return;
    }

    printf("prime %d\n", prime);

    int right_pipe[2]; //for next stage of sieve
    pipe(right_pipe);

    int pid = fork();
    if(pid == 0){ //child
        close(right_pipe[1]);//close write end of pipe
        sieve(right_pipe);
        close(right_pipe[0]);
        exit(0);
    }else{ //parent
        close(right_pipe[0]);//close read end of pipe
        int num;

        //read numbers from left pipe, filter them and write to right pipe
        while(read(left_pipe[0], &num, sizeof(num)) > 0){
            if(num % prime != 0){ //if num is not divisible by prime
                write(right_pipe[1], &num, sizeof(num));
            }
        }

        close(left_pipe[0]);//close read end of left pipe
        close(right_pipe[1]);//close write end of right pipe
        wait(0);//wait for child to finish
        exit(0);
    }
}

int main() {
    int initial_pipe[2];
    pipe(initial_pipe);

    int pid = fork();

    if(pid == 0){ //child
        close(initial_pipe[1]);//close write end of pipe
        sieve(initial_pipe);
    }else{ //parent
        close(initial_pipe[0]);//close read end of pipe

        for(int i = 2; i <= 35; i++){
            write(initial_pipe[1], &i, sizeof(i));
        }

        close(initial_pipe[1]);//close write end of pipe
        wait(0);//wait for child to finish
        exit(0);
    }
}
