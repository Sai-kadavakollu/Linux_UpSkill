#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <stdlib.h>

#define NUM_THREADS 2

// Function to get Thread ID (TID)
pid_t get_tid() {
    return syscall(SYS_gettid);
}

// Thread function
void* thread_function(void* arg) {
    int thread_num = *(int*)arg;

    printf("[THREAD %d] PID: %d | PPID: %d | TID: %d\n",
           thread_num, getpid(), getppid(), get_tid());

    sleep(1);
    printf("[THREAD %d] Exiting...\n", thread_num);

    return NULL;
}

int main() {
    printf("=== MAIN PROCESS START ===\n");
    printf("[MAIN] PID: %d | PPID: %d | TID: %d\n",
           getpid(), getppid(), get_tid());

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork failed");
        exit(1);
    }

    if (pid == 0) {
        // CHILD PROCESS
        printf("\n=== CHILD PROCESS ===\n");
        printf("[CHILD] PID: %d | PPID: %d | TID: %d\n",
               getpid(), getppid(), get_tid());

        pthread_t threads[NUM_THREADS];
        int thread_ids[NUM_THREADS];

        for (int i = 0; i < NUM_THREADS; i++) {
            thread_ids[i] = i;
            pthread_create(&threads[i], NULL, thread_function, &thread_ids[i]);
        }

        for (int i = 0; i < NUM_THREADS; i++) {
            pthread_join(threads[i], NULL);
        }

        printf("[CHILD] Done with threads\n");
    } else {
        // PARENT PROCESS
        printf("\n=== PARENT PROCESS ===\n");
        printf("[PARENT] PID: %d | CHILD PID: %d\n", getpid(), pid);

        pthread_t threads[NUM_THREADS];
        int thread_ids[NUM_THREADS];

        for (int i = 0; i < NUM_THREADS; i++) {
            thread_ids[i] = i;
            pthread_create(&threads[i], NULL, thread_function, &thread_ids[i]);
        }

        for (int i = 0; i < NUM_THREADS; i++) {
            pthread_join(threads[i], NULL);
        }

        printf("[PARENT] Done with threads\n");
    }

    printf("=== PROCESS END ===\n");
    return 0;
}
