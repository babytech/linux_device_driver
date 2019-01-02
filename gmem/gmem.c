/*
 * a simple char driver
 *
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define GMEM_SIZE 0x1000
#define MEM_CLEAR 0x1
#define GMEM_MAJOR 231

static int gmem_major = GMEM_MAJOR;
module_param(gmem_major, int, S_IRUGO);

struct gmem_dev {
	struct cdev cdev;
	unsigned char mem[GMEM_SIZE];
};

struct gmem_dev *gmem_devp;

static int gmem_open(struct inode *inode, struct file *filp)
{
	filp->private_data = gmem_devp;
	return 0;
}

static int gmem_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static long gmem_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct gmem_dev *dev = filp->private_data;
	switch (cmd) {
	case MEM_CLEAR:
		memset(dev->mem, 0, GMEM_SIZE);
		printk(KERN_INFO "gmem is set to 0\n");
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static ssize_t gmem_read(struct file *filp, char *buf, size_t size, loff_t *ppos)
{
	unsigned long p = *ppos;
	unsigned int count = size;
	size_t ret = 0;
	struct gmem_dev *dev = filp->private_data;

	if (p >= GMEM_SIZE)
		return 0;
	if (count > GMEM_SIZE - p)
		count = GMEM_SIZE - p;

	if (copy_to_user(buf, dev->mem + p, count)) {
		ret = -EFAULT;
	}else {
		*ppos +=  count;
		ret = count;
		printk (KERN_INFO "read %u bytes from %lu \n", count, p);
	}

	return ret;
}

static ssize_t gmem_write(struct file *filp, const char *buf, size_t size, loff_t *ppos)
{
	unsigned long p = *ppos;
	unsigned int count = size;
	size_t ret = 0;
	struct gmem_dev *dev = filp->private_data;	

	if (p >= GMEM_SIZE)
		return 0;
	if (count > GMEM_SIZE - p)
		count = GMEM_SIZE - p;

	if (copy_from_user(dev->mem + p, buf, count))
		return -EFAULT;
	else {
		*ppos += count;
		ret = count;
		printk(KERN_INFO "write %u bytes from %lu \n", count, p);
	}

	return ret;
}

static loff_t gmem_llseek(struct file *filp, loff_t offset, int orig)
{
	loff_t ret = 0;
	switch (orig) {
	case 0:
		if (offset < 0) {
			ret = -EINVAL;
			break;
		}
		if ((unsigned int)offset > GMEM_SIZE) {
			ret = -EINVAL;
			break;
		}
		filp->f_pos = (unsigned int)offset;
		ret = filp->f_pos;
		break;
	case 1:
		if ((filp->f_pos + offset) > GMEM_SIZE) {
			ret = -EINVAL;
			break;
		}
		if ((filp->f_pos + offset) < 0) {
			ret = -EINVAL;
			break;
		}
		filp->f_pos += offset;
		ret = filp->f_pos;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static struct file_operations gmem_fops = {
	.owner = THIS_MODULE,
	.llseek = gmem_llseek,
	.read = gmem_read,
	.write = gmem_write,
	.unlocked_ioctl = gmem_ioctl,
	.open = gmem_open,
	.release = gmem_release,
};

static void gmem_setup_cdev(struct gmem_dev *dev, int index)
{
	int err, devno = MKDEV(gmem_major, index);

	cdev_init(&dev->cdev, &gmem_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add(&dev->cdev, devno, 1);
	if (err)
		printk(KERN_INFO "Error %d when add gmem%d\n",err, index);
}

static int __init gmem_init(void)
{
	int ret;
	dev_t devno = MKDEV(gmem_major, 0);

	if (gmem_major)
		ret = register_chrdev_region(devno, 1, "gmem");
	else {
		ret = alloc_chrdev_region(&devno, 0, 1, "gmem");
		gmem_major = MAJOR(devno);
	}

	if (ret < 0)
		return ret;

	gmem_devp = kzalloc(sizeof(struct gmem_dev), GFP_KERNEL);
	if (!gmem_devp) {
		ret = -ENOMEM;
		goto fail_malloc;
	}
	
	gmem_setup_cdev(gmem_devp, 0);
	return 0;
fail_malloc:
	unregister_chrdev_region(devno, 1);
	return ret;
}
module_init(gmem_init);

static void __exit gmem_exit(void)
{
	cdev_del(&gmem_devp->cdev);
	kfree(gmem_devp);
	unregister_chrdev_region(MKDEV(gmem_major, 0), 1);
}
module_exit(gmem_exit);

MODULE_AUTHOR("xin.ming@nokia-sbell.com");
MODULE_LICENSE("GPL v2");
