#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

void readFile();
void writeFile();
int serialLogger();

int main()
{
	writeFile();
	readFile();

	serialLogger();
}

int serialLogger()
{
	int fd_read, fd_temp, fd_write;
	char buf[100];
	int n;

	fd_read = open("input.txt",O_CREAT | O_RDWR);
	if(fd_read < 0)
	{
		perror("[Open Serial_Log File] : ");
		return 1;
	}

	char *x = "Reading from file: ";
	write(1, x, 19);
	fd_temp = open("output.txt", O_CREAT | O_RDWR | O_TRUNC, 0644);

	while((n = read(fd_read, buf, sizeof(buf))) > 0)
	{
		write(1, buf, n);
		write(fd_temp, buf, n);
	}

	close(fd_read);
	close(fd_temp);

	fd_write = open("Serial_Log.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if(fd_write < 0)
	{
		perror("[Open Serial_Log File] : ");

		return 1;
	}

	write(1, "\n Enter text (Ctrl + D  to stop): \n", 31);
	printf("\n File Descriptor of the Serial_Log file: %d \n ", fd_write);

	while((n = read(0, buf, sizeof(buf))) > 0)
	{
		write(fd_write, buf, n);
	}
	close(fd_write);

	return 0;

}
