#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <pthread.h>

#define BMP280_IOCTL_BASE 'B'
#define BMP280_GET_TEMP _IOR(BMP280_IOCTL_BASE, 1, int)
#define BMP280_GET_PRESSURE _IOR(BMP280_IOCTL_BASE, 2, int)

// 🔒 Global mutex
pthread_mutex_t lock;

void* read_sensor(void *arg)
{
    int fd = open("/dev/bmp280", O_RDWR);
    int temp, pressure;

    if (fd < 0) {
        perror("open");
        return NULL;
    }

    while (1) {

        // 🔒 LOCK (User-space)
        pthread_mutex_lock(&lock);

        printf("Thread %ld acquired lock\n", pthread_self());

        ioctl(fd, BMP280_GET_TEMP, &temp);
        ioctl(fd, BMP280_GET_PRESSURE, &pressure);

        printf("Thread %ld -> Temp: %d | Pressure: %d\n",
               pthread_self(), temp, pressure);

        // 🔓 UNLOCK
        pthread_mutex_unlock(&lock);

        sleep(1);
    }

    close(fd);
    return NULL;
}

int main()
{
    pthread_t t1, t2;

    // Initialize mutex
    pthread_mutex_init(&lock, NULL);

    // Create 2 threads
    pthread_create(&t1, NULL, read_sensor, NULL);
    pthread_create(&t2, NULL, read_sensor, NULL);

    // Wait for threads
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    pthread_mutex_destroy(&lock);

    return 0;
}
