#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

int main()
{
    int fd = open("/dev/gravity", O_RDONLY);
    struct pollfd pfd;
    char buf[128];

    pfd.fd = fd;
    pfd.events = POLLIN;

    while (1) {
        poll(&pfd, 1, -1);

        read(fd, buf, sizeof(buf));
        printf("Data: %s", buf);
    }
}
