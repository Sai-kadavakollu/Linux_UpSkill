#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main()
{
    int fd;
    char buf[128];

    fd = open("/dev/gravity", O_RDONLY);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    printf("Waiting for GPIO interrupt...\n");

    while (1) {
        int ret = read(fd, buf, sizeof(buf) - 1);

        if (ret > 0) {
            buf[ret] = '\0';
            printf("Data: %s", buf);
        }
    }

    close(fd);
    return 0;
}
