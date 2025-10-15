#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

int main() {
    char line[1024];
    char *fields[31]; // assuming a maximum of 30 fields + 1 for NULL
    int field_count;

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
        
        // output the whole line
        for (int i = 0; i < field_count; i++) {
            printf("%s ", fields[i]);
        }
        printf("\n");
        
    }

    return 0;
}