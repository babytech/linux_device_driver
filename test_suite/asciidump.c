#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <ctype.h>

#define __PAGE_SIZE 4096
#define __PAGE_MASK ((__PAGE_SIZE)-1)

void print_hex_ascii_line(const unsigned long long *base, const unsigned char *buf, int len, int offset)
{
	int i;
	int gap;
	const unsigned char *ch;

	if (base)
		fprintf(stderr, "0x%x_%08x ", (unsigned int) ((*base + offset) >> 32), (unsigned int) (*base + offset));

	/* offset */
	fprintf(stderr, "0x%08x  ", offset);

	/* hex */
	ch = buf;
	for (i = 0; i < len; i++) {
		fprintf(stderr, "%02x ", *ch);
		ch++;
		/* print extra space after 8th byte for visual aid */
		if (i == 7)
			fprintf(stderr, " ");
	}
	/* print space to handle line less than 8 bytes */
	if (len < 8)
		fprintf(stderr, " ");

	/* fill hex gap with spaces if not full line */
	if (len < 16) {
		gap = 16 - len;
		for (i = 0; i < gap; i++) {
			fprintf(stderr, "   ");
		}
	}
	fprintf(stderr, "   ");

	/* ascii (if printable) */
	ch = buf;
	for (i = 0; i < len; i++) {
		if (isprint(*ch))
			fprintf(stderr, "%c", *ch);
		else
			fprintf(stderr, ".");
		ch++;
	}

	fprintf(stderr, "\n");

	return;
}

/*
 * print buf data (avoid printing binary data)
 */
void print_buf(const unsigned long long *base, const unsigned char *buf, int len)
{

	int len_rem = len;
	int line_width = 16;		/* number of bytes per line */
	int line_len;
	int offset = 0;				/* zero-based offset counter */
	const unsigned char *ch = buf;

	if (len <= 0)
		return;

	/* data fits on one line */
	if (len <= line_width) {
		print_hex_ascii_line(base, ch, len, offset);
		return;
	}

	/* data spans multiple lines */
	for (;;) {
		/* compute current line length */
		line_len = line_width % len_rem;
		/* print line */
		print_hex_ascii_line(base, ch, line_len, offset);
		/* compute total remaining */
		len_rem = len_rem - line_len;
		/* shift pointer to remaining bytes to print */
		ch = ch + line_len;
		/* add offset */
		offset = offset + line_width;
		/* check if we have line width chars or less */
		if (len_rem <= line_width) {
			/* print last line and get out */
			print_hex_ascii_line(base, ch, len_rem, offset);
			break;
		}
	}

	return;
}

void show(unsigned long long paddr, unsigned long size)
{
	int fd;
	void *vaddr;
	unsigned char *p;
	int i;
	unsigned long long _addr = paddr & ~__PAGE_MASK;
	unsigned long _offset = paddr & __PAGE_MASK;

	if ((fd = open("/dev/mem", O_RDWR)) < 0) {
		fprintf(stderr, "open file /dev/mem fail, errno %d(%s)\n", errno, strerror(errno));
		exit(-1);
	}

	if ((vaddr = mmap(NULL, size + _offset, PROT_WRITE | PROT_READ, MAP_SHARED, fd, _addr)) == MAP_FAILED) {
		fprintf(stderr, "Error while /dev/mem mmap. errno %d(%s)\n", errno, strerror(errno));
		exit(-1);
	}

	p = ((unsigned char *) vaddr) + _offset;

	print_buf(&paddr, p, size);
}

int main(int argc, char *argv[])
{
	unsigned long long paddr;
	unsigned long size;

	if (argc < 3) {
		fprintf(stderr, "usage: %s <addr> <size>\n", argv[0]);
		return -1;
	}

	paddr = strtoull(argv[1], NULL, 16);
	size = strtoul(argv[2], NULL, 10);

	show(paddr, size);

	return 0;
}
