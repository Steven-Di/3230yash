#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

int main() {
    char line[1024];
    char *fields[31]; // assuming a maximum of 30 fields + 1 for NULL
    int field_count;

    printf("## 3230yash >> ");

    // read multiple lines from stdin
    while (fgets(line, sizeof(line), stdin)) {
        
        // field processing and storage
        line[strcspn(line, "\n")] = '\0';
        field_count = 0;
        char *token = strtok(line, " \n");
        while (token) {
            if (field_count < 30) {
                fields[field_count++] = token;
                token = strtok(NULL, " \n");
            }else {
                printf("3230yash: Too many arguments\n");
                break;
            }
        }
        
        // execute command
        if (field_count > 0) {
            fields[field_count] = NULL; // NULL-terminate the array
            pid_t pid = fork();
            if (pid == 0) {
                // child process
                execvp(fields[0], fields);
                perror("execvp failed");
                exit(1);
            } else if (pid > 0) {
                // parent process
                int status;
                waitpid(pid, &status, 0);
            } else {
                perror("fork failed");
            }
        }
        printf("## 3230yash >> ");
    }
    return 0;
}