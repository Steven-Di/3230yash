#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#define MAX_FIELDS 30
#define MAX_LINE_LENGTH 1024
#define MAX_PIPES 4
#define MAX_CMDS 5

int main() {
    char line[MAX_LINE_LENGTH];
    char *fields[MAX_FIELDS +1]; // assuming a maximum of 30 fields + 1 for NULL
    int field_count, pipe_count, bound = 0;

    printf("## 3230yash >> ");

    // read (multiple) lines from stdin

    while (fgets(line, sizeof(line), stdin)) {
        
        // parse
        line[strcspn(line, "\n")] = '\0';
        field_count = 0;
        char *token = strtok(line, " \n");
        while (token) {
            if (field_count < MAX_FIELDS) {
                fields[field_count++] = token;
                token = strtok(NULL, " \n");
            }else {
                printf("3230yash: Too many arguments\n");// handle too many arguments
                bound = 1;
                break;
            }
        }
        
        // execute command

        if (field_count > 0) {
            if (bound == 0) {
                fields[field_count] = NULL; // NULL-terminate the array
                pid_t pid = fork();
                if (pid == 0) {
                // child process

                    execvp(fields[0], fields);
                    fprintf(stderr, "3230yash: \'%s\': %s\n", fields[0], strerror(errno));
                    exit(1);
                } else if (pid > 0) {
                    // parent process

                    int status;
                    waitpid(pid, &status, 0);
                } else {
                    perror("fork failed");
                }
            }
            bound = 0;
        }
        printf("## 3230yash >> ");
    }
    return 0;
}