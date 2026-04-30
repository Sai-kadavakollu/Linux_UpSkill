#include <fcntl.h>
#include <unistd.h> //for file
#include <string.h>
#include <stdio.h>

void writeFile()
{
	int fd = open("exmpl.txt",  O_CREAT | O_RDWR);
	char *msg = "Today's ->->->->-> Linux System programming \n";
	write(fd, msg, strlen(msg));

	close(fd);

	printf(" Writing File.....\n");
}	
