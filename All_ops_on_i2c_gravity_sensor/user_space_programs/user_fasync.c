#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

int fd;

void signal_handler(int sig)
{
    char buf[128];
    int ret = read(fd, buf, sizeof(buf)-1);

    if (ret > 0) {
        buf[ret] = '\0';
        printf("Async Data: %s", buf);
    }
}

int main()
{
    fd = open("/dev/gravity", O_RDONLY);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    /* register handler */
    signal(SIGIO, signal_handler);

    /* set owner */
    fcntl(fd, F_SETOWN, getpid());

    /* enable async */
    fcntl(fd, F_SETFL, O_ASYNC);

    printf("Waiting for async notifications...\n");

    while (1) {
        pause();  // wait for signal
    }

    close(fd);
    return 0;
}
