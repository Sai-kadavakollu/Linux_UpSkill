#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define BMP280_IOCTL_BASE 'B'
#define BMP280_GET_TEMP _IOR(BMP280_IOCTL_BASE, 1, int)
#define BMP280_GET_PRESSURE _IOR(BMP280_IOCTL_BASE, 2, int)

int main()
{
    int fd = open("/dev/bmp280", O_RDWR);
    int temp, pressure;

    if (fd < 0) {
        perror("open");
        return -1;
    }

    ioctl(fd, BMP280_GET_TEMP, &temp);
    ioctl(fd, BMP280_GET_PRESSURE, &pressure);

    printf("Temp: %d\n", temp);
    printf("Pressure: %d\n", pressure);

    close(fd);
    return 0;
}
