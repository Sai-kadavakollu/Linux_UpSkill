#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>

void readFile(){
	char buf[100];
	int fd = open("writeTest.txt", O_CREAT | O_RDONLY);
	if(fd < 0) printf(" Error opening the file..........");
	int n = read(fd, buf, sizeof(buf));
	write(1, buf, n);

	int fd1 = open("exmpl.txt", O_RDWR);
	dup2(fd1, 1);

	close(fd);

	printf("FileRead..... \n");

}
