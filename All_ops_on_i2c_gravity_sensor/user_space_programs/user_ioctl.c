#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define GRAVITY_MAGIC 'g'

struct gravity_data {
    int x;
    int y;
    int z;
};

#define GET_XYZ _IOR(GRAVITY_MAGIC, 1, struct gravity_data)
#define SET_ODR _IOW(GRAVITY_MAGIC, 2, int)

int main()
{
    int fd = open("/dev/gravity", O_RDWR);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    struct gravity_data data;

    /* GET XYZ */
    if (ioctl(fd, GET_XYZ, &data) == 0) {
        printf("XYZ: x=%d y=%d z=%d\n", data.x, data.y, data.z);
    }

    /* SET ODR */
    int odr = 100;
    ioctl(fd, SET_ODR, &odr);

    close(fd);
    return 0;
}
