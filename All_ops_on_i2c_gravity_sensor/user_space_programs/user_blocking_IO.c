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

    printf("Reading... (will block)\n");
	
    	read(fd, buf, sizeof(buf));
        printf("   after sleep Received:     %s", buf);

    close(fd);
    return 0;
}
