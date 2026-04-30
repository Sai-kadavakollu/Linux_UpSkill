#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

int main()
{
    int fd;
    char buf[128];

    /* 🔥 OPEN WITH NONBLOCK */
    fd = open("/dev/gravity", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    while (1) {

        int ret = read(fd, buf, sizeof(buf)-1);

        if (ret > 0) {
            buf[ret] = '\0';
            printf("Data: %s", buf);
        }
        else if (ret < 0) {
            if (errno == EAGAIN) {
                printf("No data...\n");
            } else {
                perror("read");
                break;
            }
        }

        sleep(1);
    }

    close(fd);
    return 0;
}
