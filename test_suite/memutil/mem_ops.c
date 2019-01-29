#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "mem_util.h"
#include "work_paras.h"
#include "mem_ops.h"

static int map_memory_fixed(unsigned long long phy_addr, unsigned long long len, void **result, int want_write)
{

	int ret = -1;
	void *vaddr;
	int open_flags = (want_write? O_RDWR :O_RDONLY);
	int prot = (want_write? (PROT_READ | PROT_WRITE) :PROT_READ);
	int fd;

	VERBOSE_PRINT("do fixed mmap (%s): physical addr=0x%llx(%llu); length=0x%llx(%llu)\n"
		, want_write?"read_and_write":"read_only"
		, phy_addr, phy_addr, len, len);

	fd = open("/dev/mem", open_flags);
	if (fd<0) {
		SYS_ERR_PRINT("open file /dev/mem failed\n");
		goto EXIT;
	}

	/* for 32-bit system to mmap large physical addr, use mmap64 */
#ifdef __LP64__
	vaddr = mmap(NULL, (size_t)len, prot, MAP_SHARED, fd, (off_t)phy_addr);
#else
	vaddr = mmap64(NULL, (size_t)len, prot, MAP_SHARED, fd, (off_t)phy_addr);
#endif
	if (MAP_FAILED == vaddr) {
		SYS_ERR_PRINT("mmap failed\n");
		goto CLOSE_FILE;
	}

	ret = 0;
	VERBOSE_PRINT("mmap succeed: virtual addr=0x%lx(%lu)\n"
		, (unsigned long)vaddr, (unsigned long)vaddr);
	*result = vaddr;

CLOSE_FILE:
	close(fd);
EXIT:
	return ret;

}

static int map_memory(struct working_params *work_para, int want_write)
{
	int	ret = -1;
	void *vaddr_base = NULL, *vaddr = NULL;
	int page_size = getpagesize();
	unsigned long long phy_addr_for_map = work_para->start_phy_addr;
	unsigned long long length_for_map = work_para->length;

	VERBOSE_PRINT("map memory: physical addr=0x%llx(%llu); length=0x%llx(%llu)\n"
		, work_para->start_phy_addr, work_para->start_phy_addr
		, work_para->length, work_para->length);

	/* adjust start addr to a page aligned place */
	phy_addr_for_map &= (~((unsigned long long)page_size-1));

	/* length start from phy_addr_for_map, and is multiple of page_size */
	length_for_map += (work_para->start_phy_addr - phy_addr_for_map);
	length_for_map = ((length_for_map + page_size-1)/page_size)*page_size;

	VERBOSE_PRINT("after adjusted for fixed mmap: physical addr=0x%llx(%llu); length=0x%llx(%llu)\n"
		, phy_addr_for_map, phy_addr_for_map
		, length_for_map, length_for_map);

	ret = map_memory_fixed(phy_addr_for_map, length_for_map, &vaddr_base, want_write);
	if (ret)
		goto EXIT;

	ret = 0;
	vaddr = vaddr_base + (int)(work_para->start_phy_addr - phy_addr_for_map);
	work_para->vaddr_base = vaddr_base;
	work_para->map_length = length_for_map;
	work_para->vaddr = vaddr;

	VERBOSE_PRINT("vaddr_base=0x%lx(%lu); vaddr=0x%lx(%lu)\n"
		, (unsigned long)work_para->vaddr_base, (unsigned long)work_para->vaddr_base
		, (unsigned long)work_para->vaddr, (unsigned long)work_para->vaddr);
EXIT:
	return ret;
}

static int open_new_file_for_write(const char *file)
{
	int fd =open(file
	    , O_CREAT|O_WRONLY|O_APPEND|O_TRUNC
	    , S_IRUSR | S_IWUSR | S_IRGRP);

	if (fd < 0)
	{
		SYS_ERR_PRINT("open file %s failed\n", file);
		return -1;
	}

	VERBOSE_PRINT("open file %s for write succeed, fd=%d\n", file, fd);
	return fd;
}

static int open_file_for_read(const char *file)
{
	int fd =open(file, O_RDONLY);

	if (fd < 0)
	{
		SYS_ERR_PRINT("open file %s failed\n", file);
		return -1;
	}

	VERBOSE_PRINT("open file %s for read succeed, fd=%d\n", file, fd);
	return fd;
}

static ssize_t safe_read(int fd, void *buf, size_t count)
{
    ssize_t ret = 0;
    ssize_t len = count;

    while (len != 0 && (ret = read (fd, buf, len)) != 0) {

        if (ret == -1) {
            if (errno == EINTR)
                /* just restart */
                continue;

            /* error */
            break;
        }

        len -= ret;
        buf += ret;
    }

    if (ret == -1)
        return ret;
    else
        return count - len;
}

ssize_t safe_write(int fd, const void *buf, size_t count)
{
    ssize_t ret = 0;
    ssize_t len = count;

    while (len != 0 && (ret = write (fd, buf, len)) != 0) {

        if (ret == -1) {
            if (errno == EINTR)
                /* just restart */
                continue;

            /* error */
            break;
        }

        len -= ret;
        buf += ret;
    }

    if (ret == -1)
        return ret;
    else
        return count - len;
}

int save_mem_region_to_file(struct working_params *work_para)
{
	int ret = -1;
	int fd = -1;
	ssize_t wr_cnt;

	if (map_memory(work_para, 0))
		goto EXIT;

	if ((fd=open_new_file_for_write(work_para->file_path))<0)
		goto EXIT;

	wr_cnt = safe_write(fd, work_para->vaddr, (size_t)work_para->length);
	if (wr_cnt < 0) {
		SYS_ERR_PRINT("write memory region to file failed\n");
		goto CLOSE_FILE;
	} else if ((size_t)wr_cnt != (size_t)work_para->length) {
		ERR_PRINT("want write %zu bytes, but actually write %zu bytes"
			,(size_t)work_para->length ,(size_t)wr_cnt);
		goto CLOSE_FILE;
	}

	ret = 0;
	VERBOSE_PRINT("save memory region to file succeed.\n");

	if (munmap(work_para->vaddr_base, (size_t)work_para->map_length)) {
		SYS_ERR_PRINT("munmap failed\n");
	}

CLOSE_FILE:
	close(fd);
EXIT:
	return ret;
}

int load_mem_region_from_file(struct working_params *work_para)
{
	int ret = -1;
	int fd = -1;
	ssize_t rd_cnt;

	if (map_memory(work_para, 1))
		goto EXIT;

	if ((fd=open_file_for_read(work_para->file_path))<0)
		goto EXIT;

	rd_cnt = safe_read(fd, work_para->vaddr, (size_t)work_para->length);
	if (rd_cnt < 0) {
		SYS_ERR_PRINT("read file to memory region failed\n");
		goto CLOSE_FILE;
	} else if ((size_t)rd_cnt != (size_t)work_para->length) {
		ERR_PRINT("want read %zu bytes, but actually read %zu bytes"
			,(size_t)work_para->length ,(size_t)rd_cnt);
		goto CLOSE_FILE;
	}

	ret = 0;
	VERBOSE_PRINT("load file to memory region succeed.\n");

	if (munmap(work_para->vaddr_base, (size_t)work_para->map_length)) {
		SYS_ERR_PRINT("munmap failed\n");
	}

CLOSE_FILE:
	close(fd);
EXIT:
	return ret;
}
