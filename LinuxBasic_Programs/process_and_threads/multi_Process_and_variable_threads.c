#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/wait.h>

pid_t get_tid() {
  return syscall(SYS_gettid);
}

void* thread_function(void* arg) {
  int thread_num = *(int*)arg;

  printf(" [THread %d] PID=%d | TID = %d \n", thread_num, getpid(), get_tid());
  sleep(1);
  return NULL;
}

void create_threads(int num_threads) {
  pthread_t threads[num_threads];
  int thread_ids[num_threads];

  for(int i = 1; i <= num_threads; i++) { 
    thread_ids[i] = i;
    pthread_create(&threads[i], NULL, thread_function, &thread_ids[i]);
  }
  for(int i = 1; i <= num_threads; i++) {
    pthread_join(threads[i], NULL);
  }
}

int main() {
  printf(" Main PID: %d \n", getpid());
  
  for(int i =1; i <= 4; i++) {
    pid_t pid = fork();
    
    if(pid == 0) {
      printf(" \n === Process P%d === PID=%d | PPID=%d ===\n", i, getpid(), getppid());
      create_threads(i);
      printf(" === Process P%d Done ===\n", i);
      exit(0);
    }
  }
  for (int i = 1; i <= 4; i++) {
    wait(NULL);
  }
  printf(" \n All processes completed. \n");
  return 0;
}
