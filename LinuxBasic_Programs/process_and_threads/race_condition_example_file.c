#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#define FILE_NAME "test.txt"

void write_to_file(const char *label) {
    int fd = open(FILE_NAME, O_WRONLY | O_CREAT | O_APPEND, 0644);

    if (fd < 0) {
        perror("open");
        exit(1);
    }

    for (int i = 0; i < 5; i++) {
        char buffer[100];
        sprintf(buffer, "%s writing line %d (PID: %d)\n", label, i, getpid());

 	for (int j = 0; j < strlen(buffer); j++) {
    		write(fd, &buffer[j], 1);  // write 1 byte at a time
    		usleep(1000);
	}

        // Force scheduling mix
        usleep(100000);
    }

    close(fd);
}

int main() {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        write_to_file("CHILD");
    } else {
        write_to_file("PARENT");
    }

    return 0;
}
