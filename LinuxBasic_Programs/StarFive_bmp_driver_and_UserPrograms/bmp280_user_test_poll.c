#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <poll.h>

#define BMP280_IOCTL_BASE 'B'
#define BMP280_GET_TEMP _IOR(BMP280_IOCTL_BASE, 1, int)
#define BMP280_GET_PRESSURE _IOR(BMP280_IOCTL_BASE, 2, int)

int main()
{
    int fd = open("/dev/bmp280", O_RDWR);
    struct pollfd pfd;

    int temp, pressure;

    if (fd < 0) {
        perror("open");
        return -1;
    }

    pfd.fd = fd;
    pfd.events = POLLIN;

    while (1) {

        printf("Waiting for data...\n");

        poll(&pfd, 1, -1);   // BLOCK until event

        printf("Data ready!\n");

        ioctl(fd, BMP280_GET_TEMP, &temp);
        ioctl(fd, BMP280_GET_PRESSURE, &pressure);

        printf("Temp: %d | Pressure: %d\n", temp, pressure);
    }

    close(fd);
    return 0;
}
