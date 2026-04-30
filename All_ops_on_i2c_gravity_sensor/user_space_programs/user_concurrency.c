#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#define THREADS 3

pthread_mutex_t print_lock;

void *reader_thread(void *arg)
{
    int id = *(int *)arg;
    int fd;
    char buf[128];

    fd = open("/dev/gravity", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return NULL;
    }

    while (1) {
        int ret = read(fd, buf, sizeof(buf) - 1);

        if (ret > 0) {
            buf[ret] = '\0';

            /* protect stdout */
            pthread_mutex_lock(&print_lock);
            printf("Thread %d → %s", id, buf);
            pthread_mutex_unlock(&print_lock);
        }
        else if (ret < 0) {
            if (errno != EAGAIN) {
                perror("read");
                break;
            }
        }

        usleep(200000); // 200ms
    }

    close(fd);
    return NULL;
}

int main()
{
    pthread_t t[THREADS];
    int ids[THREADS];

    pthread_mutex_init(&print_lock, NULL);

    for (int i = 0; i < THREADS; i++) {
        ids[i] = i;
        pthread_create(&t[i], NULL, reader_thread, &ids[i]);
    }

    for (int i = 0; i < THREADS; i++)
        pthread_join(t[i], NULL);

    return 0;
}
