#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h" // for using MAXARG

static void
run_command(char *xargv[])
{
    int pid = fork();

    if(pid < 0){
        fprintf(2, "xargs: fork failed\n");
        exit(1);
    }

    if(pid == 0){
        exec(xargv[0], xargv);
        fprintf(2, "xargs: exec %s failed\n", xargv[0]);
        exit(1);
    }

    wait(0);
}

int
main(int argc, char *argv[])
{
    if(argc < 2){
        fprintf(2, "Usage: xargs <command>\n");
        exit(1);
    }

    if(argc >= MAXARG){
        fprintf(2, "xargs: too many arguments\n");
        exit(1);
    }
    
    char *xargv[MAXARG]; // Array to hold command and its arguments
    int xargc = 0; // Count of arguments for the command

    for(int i=1; i<argc; i++){ // Start from 1 to skip the command "xargs" itself
        xargv[xargc++] = argv[i]; // Add command and its arguments to xargv
    }

    int base_argc = xargc;
    char buf[512]; // Buffer to read input from stdin
    int buf_idx = 0; // Index to keep track of buffer position
    char c;
    int in_word = 0; // Flag to indicate if we are currently reading a word
    int line_has_arg = 0;
    char *word_start = buf;

    while(read(0, &c, 1) > 0){
        if(c == ' ' || c == '\t' || c == '\r' || c == '\n'){
            if(in_word){
                if(xargc >= MAXARG - 1){
                    fprintf(2, "xargs: too many arguments\n");
                    exit(1);
                }

                buf[buf_idx++] = '\0'; // Null-terminate the word
                xargv[xargc++] = word_start; // Add the word to xargv
                in_word = 0; // Reset the flag
                line_has_arg = 1;
            }

            if(c=='\n'){
                if(line_has_arg){
                    xargv[xargc] = 0; // exec requires a null-terminated argument array
                    run_command(xargv);
                }

                xargc = base_argc; // Reset xargc to the base command and its arguments
                buf_idx = 0; // Reset buffer index for the next line of input
                line_has_arg = 0;
            }
        }else{
            if(buf_idx >= (int)sizeof(buf) - 1){
                fprintf(2, "xargs: input line too long\n");
                exit(1);
            }

            if(!in_word){
                word_start = &buf[buf_idx]; // Mark the start of a new word
                in_word = 1; // Set the flag to indicate we are reading a word
            }
            buf[buf_idx++] = c; // Add character to buffer
        }
    }

    // Process the final line even when it does not end with '\n'.
    if(in_word){
        if(xargc >= MAXARG - 1){
            fprintf(2, "xargs: too many arguments\n");
            exit(1);
        }
        buf[buf_idx] = '\0';
        xargv[xargc++] = word_start;
        line_has_arg = 1;
    }

    if(line_has_arg){
        xargv[xargc] = 0;
        run_command(xargv);
    }

    exit(0);
}
