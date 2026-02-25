#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

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
