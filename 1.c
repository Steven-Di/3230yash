#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

int main() {
    char line[1024];
    char *fields[30];
    int field_count;

    // read multiple lines from stdin
    while (fgets(line, sizeof(line), stdin)) {

        // field processing and storage
        line[strcspn(line, "\n")] = '\0';
        field_count = 0;
        char *token = strtok(line, " \t");
        while (token != NULL && field_count < MAX_FIELDS) {
            fields[field_count++] = token;
            token = strtok(NULL, " \t");
        }

        
    }

    return 0;
}