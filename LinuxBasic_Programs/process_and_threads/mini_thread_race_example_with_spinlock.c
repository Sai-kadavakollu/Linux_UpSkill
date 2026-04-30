#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>

atomic_flag locker = ATOMIC_FLAG_INIT;

void spin_lock() {
	while(atomic_flag_test_and_set(&locker));
}

void spin_unlock() {
 	atomic_flag_clear(&locker);
}

int counter = 0;

void* increment(void* arg)
{
        for(int i = 0; i< 1000000; i++){
                spin_lock();
                counter++;
		spin_unlock();
        }
        return NULL;
}

int main() {
        pthread_t t1, t2;

        pthread_create(&t1, NULL, increment, NULL);
        pthread_create(&t2, NULL, increment, NULL);

        pthread_join(t1, NULL);
        pthread_join(t2, NULL);

        printf(" Counter = %d \n", counter);
}

