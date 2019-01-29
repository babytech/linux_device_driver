#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char *argv[])
{
	void *ptr = NULL;
	unsigned long long start;
	unsigned long long size;
	unsigned long pattern = 0x0a;
	unsigned long check;
	int fd;

	printf("Version 1.1\n");

	if ( argc < 3 ) {
		printf("Usage: %s <startaddress> <sizeinbytes> [pattern]\n", argv[0]);
		exit(0);
	}

	start = strtoll(argv[1], NULL, 0);
	size = strtoll(argv[2], NULL, 0);
	if ( argc >= 4 )
		pattern = (unsigned char)strtol(argv[3], NULL, 0);

	printf("Clearing memory 0x%8.8llX->0x%8.8llX (size 0x%8.8llX) with pattern 0x%2.2lX ... ", 
		start, start + size, size, pattern);

	fd = open("/dev/mem", O_RDWR);
	if ( fd < 0 ) {
		perror("\nFailed to open /dev/mem");
		exit(EXIT_FAILURE);
	}

	ptr = mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, start);
	memset(ptr, pattern, size);
	memset(&check, pattern, sizeof(check));
	if ( ((unsigned long *)ptr)[0] != check )
		printf("\nMemory not cleared!\n");
	else
		printf("done\n");
	munmap(ptr, size);
	close(fd);
	return 0;
}
