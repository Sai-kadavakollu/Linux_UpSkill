#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main()
{
    int fd = open("/dev/bmp280", O_RDWR);
    int data[2];

    while (1) {
        read(fd, data, sizeof(data));

        printf("Temp: %d | Pressure: %d\n", data[0], data[1]);
    }

    return 0;
}
