#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h" // for using MAXARG

int main(int argc, char* argv[]){
    if(argc < 2){
        fprintf(2, "Usage: xargs <command>\n");
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
    char *word_start = buf;

    while(read(0, &c, 1) > 0){
        if(c==' ' || c == '\n'){
            buf[buf_idx++] = '\0'; // Null-terminate the word
            
            if(in_word){
                xargv[xargc++] = word_start; // Add the word to xargv
                in_word = 0; // Reset the flag
            }

            if(c=='\n'){
                xargv[xargc] = 0; // execv requires a null-terminated array of arguments
                
                if(fork() == 0){
                    exec(xargv[0], xargv);
                    fprintf(2, "xargs: exec %s failed\n", xargv[0]);
                    exit(1);
                } else {
                    wait(0); // Wait for the child process to finish
                }

                xargc = base_argc; // Reset xargc to the base command and its arguments
                buf_idx = 0; // Reset buffer index for the next line of input
            }
        }else{
            if(!in_word){
                word_start = &buf[buf_idx]; // Mark the start of a new word
                in_word = 1; // Set the flag to indicate we are reading a word
            }
            buf[buf_idx++] = c; // Add character to buffer
        }
    }
    exit(0);
}