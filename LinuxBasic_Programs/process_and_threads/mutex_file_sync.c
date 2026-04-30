#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

pthread_mutex_t lock;
pthread_cond_t cond;

int data_ready = 0;  // shared flag

void* writer(void* arg) {
    FILE *fp;

    pthread_mutex_lock(&lock);

    printf("Writer: Writing to file...\n");

    fp = fopen("data.txt", "w");
    fprintf(fp, "Hello from writer thread!\n");
    fclose(fp);

    data_ready = 1;

    printf("Writer: Data written, signaling reader...\n");

    pthread_cond_signal(&cond);  // wake reader

    pthread_mutex_unlock(&lock);

    return NULL;
}

void* reader(void* arg) {
    FILE *fp;
    char buffer[100];

    pthread_mutex_lock(&lock);

    while (data_ready == 0) {
        printf("Reader: Waiting for data...\n");
        pthread_cond_wait(&cond, &lock);  // sleep
    }

    printf("Reader: Reading file...\n");

    fp = fopen("data.txt", "r");
    fgets(buffer, sizeof(buffer), fp);
    fclose(fp);

    printf("Reader got: %s", buffer);

    pthread_mutex_unlock(&lock);

    return NULL;
}

int main() {
    pthread_t t1, t2;

    pthread_mutex_init(&lock, NULL);
    pthread_cond_init(&cond, NULL);

    pthread_create(&t2, NULL, reader, NULL);
    sleep(3);  // ensure reader starts first
    pthread_create(&t1, NULL, writer, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    pthread_mutex_destroy(&lock);
    pthread_cond_destroy(&cond);

    return 0;
}
