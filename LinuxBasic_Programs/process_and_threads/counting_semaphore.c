#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <sys/mman.h>

int main() {
    sem_t *sem = mmap(NULL, sizeof(sem_t),
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_ANONYMOUS,
                      -1, 0);

    sem_init(sem, 1, 2); //  COUNT = 2

    for (int i = 0; i < 5; i++) {
        if (fork() == 0) {

            printf("Process %d waiting...\n", getpid());

            sem_wait(sem);  //  enter

            printf("Process %d ENTERED critical section\n", getpid());
            sleep(2);

            printf("Process %d EXITING\n", getpid());

            sem_post(sem);  //  leave

            exit(0);
        }
    }

    for (int i = 0; i < 5; i++) {
        wait(NULL);
    }

    return 0;
}
