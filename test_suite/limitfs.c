#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <alloca.h>
#include <stdlib.h>

#define MAX_PATH 1024
static char *src = NULL;
static unsigned long max_size = 0;
static unsigned long cur_size = 0;
static char path_max_size[MAX_PATH] = "/max_size";
static char path_cur_size[MAX_PATH] = "/cur_size";

static int get_file_size(char *filename, unsigned long *filesize)
{
	struct stat f_stat;
	memset(&f_stat, 0, sizeof(struct stat));
	if (stat(filename, &f_stat) == -1)
		return -1;
	*filesize = f_stat.st_size;
	return 0;	
}
static int is_dir(char *path, int *dir)
{
	struct stat f_stat;
	memset(&f_stat, 0, sizeof(struct stat));
	if (stat(path, &f_stat) == -1)
		return -1;
	if (S_ISDIR(f_stat.st_mode))
		*dir = 1;
	else
		*dir = 0;
	return 0;	
}
static int __get_dir_size(char *dir_name, unsigned long *filesize)
{
	DIR *p_dir;
	struct dirent *cur_dir;
	int path_is_dir;
	char path[MAX_PATH];
	unsigned long cur_filesize = 0;
	p_dir = opendir(dir_name);
	if (!p_dir)
		return -1;
	while ((cur_dir = readdir(p_dir)) != NULL) {
		if (strcmp(cur_dir->d_name, ".") == 0 || strcmp(cur_dir->d_name, "..") == 0)
			continue;
		sprintf(path, "%s/%s", dir_name, cur_dir->d_name);
		if (is_dir(path, &path_is_dir))
			return -1;
		if (path_is_dir) {
			if (__get_dir_size(path, &cur_filesize))
				return -1;
		} else {
			if (get_file_size(path, &cur_filesize))
				return -1;
		}

		*filesize += cur_filesize;
	}
	closedir(p_dir);
	return 0;
}

static int get_dir_size(char *dir_name, unsigned long *filesize)
{
	*filesize = 0;
	return __get_dir_size(dir_name, filesize);
}

static int limitfs_getattr(const char *path, struct stat *stbuf)
{
	int res;
	char real_path[MAX_PATH];
	sprintf(real_path, "%s/%s", src, path);
	if ((strcmp(path, path_cur_size)== 0) || (strcmp(path, path_max_size) == 0))
	{
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = 128;
		return 0;
	} else {
		res = lstat(real_path, stbuf);
		if (res == -1)
			return -errno;
		return 0;
	}
}

static int limitfs_access(const char *path, int mask)
{
	int res;
	char real_path[MAX_PATH];
	sprintf(real_path, "%s/%s", src, path);
	if (strcmp(path, path_cur_size) == 0 || strcmp(path, path_max_size) == 0)
		return 0;
	res = access(real_path, mask);
	if (res == -1)
		return -errno;

	return 0;
}


static int limitfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		off_t offset, struct fuse_file_info *fi)
{
	DIR *dp;
	struct dirent *de;

	char real_path[MAX_PATH];
	sprintf(real_path, "%s/%s", src, path);
	(void) offset;
	(void) fi;

	if (strcmp(path, "/") == 0) {
		filler(buf, path_cur_size + 1, NULL, 0);
		filler(buf, path_max_size + 1, NULL, 0);
	}

	dp = opendir(real_path);
	if (dp == NULL)
		return -errno;

	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0))
			break;
	}

	closedir(dp);
	return 0;
}

static int limitfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;
	char real_path[MAX_PATH];
	sprintf(real_path, "%s/%s", src, path);
	if (cur_size > max_size)
		return -ENOBUFS;

	if (strcmp(path, path_cur_size) == 0 || strcmp(path, path_max_size) == 0)
		return 0;
	if (S_ISREG(mode)) {
		res = open(real_path, O_CREAT | O_EXCL | O_WRONLY, mode);
		if (res >= 0)
			res = close(res);
	} else if (S_ISFIFO(mode))
		res = mkfifo(real_path, mode);
	else
		res = mknod(real_path, mode, rdev);
	if (res == -1)
		return -errno;
	
	return 0;
}

static int limitfs_open(const char *path, struct fuse_file_info *fi)
{
	int res;
	char real_path[MAX_PATH];
	sprintf(real_path, "%s/%s", src, path);
	if (strcmp(path, path_cur_size) == 0 || strcmp(path, path_max_size) == 0)
		return 0;
	res = open(real_path, fi->flags);
	if (res == -1)
		return -errno;
	fi->fh = res;
	return 0;
}

static int limitfs_read(const char *path, char *buf, size_t size, off_t offset,
		struct fuse_file_info *fi)
{
	int res;
	
	if ((strcmp(path, path_cur_size)== 0)) {
		return sprintf(buf, "%lu\n", cur_size);
	} else if((strcmp(path, path_max_size) == 0)) {
		return sprintf(buf, "%lu\n", max_size);
	}
	res = pread(fi->fh, buf, size, offset);
	if (res == -1) {
		res = -errno;
		close(fi->fh);
	}

	return res;
}

static int limitfs_write(const char *path, const char *buf, size_t size,
		off_t offset, struct fuse_file_info *fi)
{
	int res;
	unsigned long old_size, new_size;
	char real_path[MAX_PATH];
	if (strcmp(path, path_cur_size) == 0 || strcmp(path, path_max_size) == 0)
		return 0;
	if (cur_size > max_size) {
		close(fi->fh);
		return -ENOBUFS;
	}
	sprintf(real_path, "%s/%s", src, path);

	if (get_file_size(real_path, &old_size))
		return -1;

	res = pwrite(fi->fh, buf, size, offset);
	if (res == -1) {
		res = -errno;
		close(fi->fh);
	}

	if (get_file_size(real_path, &new_size))
		return -1;
	cur_size = cur_size >= old_size ? cur_size - old_size + new_size : new_size;
	return res;
}

static int limitfs_mkdir(const char *path, mode_t mode)
{
	int res;
	char real_path[MAX_PATH];
	if (strcmp(path, path_cur_size) == 0 || strcmp(path, path_max_size) == 0)
		return -1;
	sprintf(real_path, "%s/%s", src, path);
	res = mkdir(real_path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int limitfs_truncate(const char *path, off_t size)
{
	int res;
	char real_path[MAX_PATH];
	if (strcmp(path, path_cur_size) == 0 || strcmp(path, path_max_size) == 0)
		return -1;
	sprintf(real_path, "%s/%s", src, path);
	res = truncate(real_path, size);

	if (get_dir_size(src, &cur_size))
		printf("Warning: failed to update cur_size when truncate\n");
	if (res == -1)
		return -errno;

	return 0;
}

static int limitfs_chmod(const char *path, mode_t mode)
{
	int res;
	char real_path[MAX_PATH];
	if (strcmp(path, path_cur_size) == 0 || strcmp(path, path_max_size) == 0)
		return -1;
	sprintf(real_path, "%s/%s", src, path);

	res = chmod(real_path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int limitfs_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;
	char real_path[MAX_PATH];
	if (strcmp(path, path_cur_size) == 0 || strcmp(path, path_max_size) == 0)
		return -1;
	sprintf(real_path, "%s/%s", src, path);

	res = lchown(real_path, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

static int limitfs_rmdir(const char *path)
{
	int res;
	char real_path[MAX_PATH];

	if (strcmp(path, path_cur_size) == 0 || strcmp(path, path_max_size) == 0)
		return -1;
	sprintf(real_path, "%s/%s", src, path);

	res = rmdir(real_path);
	if (get_dir_size(src, &cur_size))
		printf("Warning: failed to update cur_size when rmdir\n");
	if (res == -1)
		return -errno;

	return 0;
}

static int limitfs_rename(const char *from, const char *to)
{
	int res;
	char real_from_path[MAX_PATH];
	char real_to_path[MAX_PATH];

	if (strcmp(from, path_cur_size) == 0 || strcmp(from, path_max_size) == 0)
		return -1;
	if (strcmp(to, path_cur_size) == 0 || strcmp(to, path_max_size) == 0)
		return -1;

	sprintf(real_from_path, "%s/%s", src, from);
	sprintf(real_to_path, "%s/%s", src, to);

	res = rename(real_from_path, real_to_path);
	if (res == -1)
		return -errno;

	return 0;
}

static int limitfs_unlink(const char *path)
{
	int res;
	char real_path[MAX_PATH];

	if (strcmp(path, path_cur_size) == 0 || strcmp(path, path_max_size) == 0)
		return -1;
	sprintf(real_path, "%s/%s", src, path);


	res = unlink(real_path);

	if (get_dir_size(src, &cur_size))
		printf("Warning: failed to update cur_size when unlink\n");
	if (res == -1)
		return -errno;

	return 0;
}

static int limitfs_release(const char *path, struct fuse_file_info *fi)
{
    int res = close(fi->fh);
    if (res == -1)
		return (-errno);
	return 0;
}

static struct fuse_operations limitfs_oper = {
	.getattr	= limitfs_getattr,
	.access		= limitfs_access,
	.readdir	= limitfs_readdir,
	.mknod		= limitfs_mknod,
	.open		= limitfs_open,
	.read		= limitfs_read,
	.write		= limitfs_write,
	.mkdir		= limitfs_mkdir,
	.chmod		= limitfs_chmod,
	.truncate	= limitfs_truncate,
	.chown		= limitfs_chown,
	.rename		= limitfs_rename,
	.rmdir		= limitfs_rmdir,
	.unlink		= limitfs_unlink,
	.release	= limitfs_release,
};

int main(int argc, char *argv[])
{
	int skip_args = 0, i = 0, pos = 0;
	char** fuse_args = alloca(sizeof(char*) * argc);
	while (i < argc)
	{
		if (strcmp(argv[i], "--size") == 0) {
			max_size = (unsigned long)atol(argv[i + 1]);
			i += 2;
			skip_args += 2;
		} else if (strcmp(argv[i], "--src") == 0) {
			src = argv[i + 1];
			i +=2;
			skip_args += 2;
		} else {
			fuse_args[pos++] = argv[i++];
		}
	}
	if (src == NULL || max_size == 0) {
		fprintf(stderr, "Source dirtory (--src) or max file system size (--size) are not specific!\n");
		return 1;
	}

	umask(0);
	if (get_dir_size(src, &cur_size))
		return -1;
	if (cur_size >= max_size) {
		printf("Warning: Current size is overload\n");
	}
	return fuse_main(pos, fuse_args, &limitfs_oper, NULL);
}
