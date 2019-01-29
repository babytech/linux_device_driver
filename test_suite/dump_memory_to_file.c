/*
 * File:   dump_memory_to_file.c
 *
 * This program allows to dump the bytes from a given memory range to a regular
 * file. One use case is where a file has been placed in memory using a JTAG
 * probe, but the tool could also be used to save an interesting memory region
 * for later analysis.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <unistd.h>

#include <sys/mman.h>

unsigned long long readNumFromStr(char *str, int base);


int main(int argc, char** argv)
{
	if (argc != 4) {
		fprintf(stderr, "Wrong number of arguments. Usage:\n");
		fprintf(stderr, "\tdump_memory_to_file addrInMemory size outfile\n");
		return EXIT_FAILURE;
	}

	unsigned long long fromPhyMem;
	unsigned long long size;
	char *outFile;

	fromPhyMem = readNumFromStr(argv[1], 0);
	size = readNumFromStr(argv[2], 0);
	outFile = argv[3];

	if ((long long)size < 0) {
		fprintf(stderr, "Negative size given. %lld\n", (long long)size);
		return (EXIT_FAILURE);
	}

	int fdout;

	if ((fdout = creat(outFile, S_IRWXU)) == -1) {
		fprintf(stderr, "Error while creating file %s. Errno=%d (%s)\n", outFile, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	int fddevmem;

	if ((fddevmem = open("/dev/mem", O_RDONLY)) == -1) {
		fprintf(stderr, "Error while opening /dev/mem. Errno=%d (%s)\n", errno, strerror(errno));
		return EXIT_FAILURE;
	}

	void *srcAddr;

	if ((srcAddr = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fddevmem, fromPhyMem)) == MAP_FAILED) {
		fprintf(stderr, "Error while mmap. Errno=%d (%s)\n", errno, strerror(errno));
		return EXIT_FAILURE;
	}

	printf("Starting copying byte from address %#llx into %s\n", fromPhyMem, outFile);

	size_t nbByteCopied = -1;

	while ((nbByteCopied = write(fdout, srcAddr, size)) == -1) {
		switch (errno) {
		case EAGAIN: //or EWOULDBLOCK
			continue;

		default:
			fprintf(stderr, "Error while writing into file %s. Errno=%d (%s)\n", outFile, errno, strerror(errno));
			return EXIT_FAILURE;
		}
	}

	close(fdout);


	return (EXIT_SUCCESS);
}

unsigned long long readNumFromStr(char *str, int base)
{
	char *endptr;
	unsigned long long returnIntVal;

	errno = 0; /* To distinguish success/failure after call */
	returnIntVal = strtoull(str, &endptr, base);

	/* Check for various possible errors */

	if ((errno == ERANGE && (returnIntVal == ULLONG_MAX))
	    || (errno != 0 && returnIntVal == 0)) {
		fprintf(stderr, "Error while reading %s: errno:%d (%s)\n", str, errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (endptr == str) {
		fprintf(stderr, "No digits were found in %s\n", str);
		exit(EXIT_FAILURE);
	}

	return returnIntVal;
}
