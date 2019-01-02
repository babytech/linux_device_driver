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

#define GFIFO_MAJOR 231
#define GFIFO_SIZE 0x1000
#define MEM_CLEAR 0x1

static int gfifo_major = GFIFO_MAJOR;
module_param(gfifo_major, int, S_IRUGO);

struct gfifo_dev {
	struct cdev cdev;
	unsigned char mem[GFIFO_SIZE];
	struct mutex mutex;
	unsigned long cur_len;
	wait_queue_head_t r_wait;
	wait_queue_head_t w_wait;
};

struct gfifo_dev *gfifo_devp;

static int gfifo_open(struct inode *inode, struct file *filp)
{
	filp->private_data = gfifo_devp;
	return 0;
}

static int gfifo_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static long gfifo_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct gfifo_dev *dev = filp->private_data;
	switch (cmd) {
	case MEM_CLEAR:
		mutex_lock(&dev->mutex);
		memset(dev->mem, 0, GFIFO_SIZE);
		mutex_unlock(&dev->mutex);
		printk(KERN_INFO "gfifo is set to 0\n");
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static ssize_t gfifo_read(struct file *filp, char *buf, size_t size, loff_t *ppos)
{
	unsigned int count = size;
	size_t ret = 0;
	struct gfifo_dev *dev = filp->private_data;
	DECLARE_WAITQUEUE(wait, current);

	mutex_lock(&dev->mutex);
	add_wait_queue(&dev->r_wait, &wait);

	while (dev->cur_len == 0) {
		if (filp->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto out;
		}
		__set_current_state(TASK_INTERRUPTIBLE);
		mutex_unlock(&dev->mutex);
		schedule();
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			goto out2;
		}
		mutex_lock(&dev->mutex);
	}

/*
	if (p >= GFIFO_SIZE)
		return 0;
	if (count > GFIFO_SIZE - p)
		count = GFIFO_SIZE - p;
*/
	if (count > dev->cur_len)
		count = dev->cur_len;

	if (copy_to_user(buf, dev->mem, count)) {
		ret = -EFAULT;
		goto out;
	}else {
		memcpy(dev->mem, dev->mem + count, dev->cur_len - count);
		dev->cur_len -= count;
		printk (KERN_INFO "read %u bytes, cur_len: %lu \n", count, dev->cur_len);
		wake_up_interruptible(&dev->w_wait);
		ret = count;
	}

out:
	mutex_unlock(&dev->mutex);
out2:
	remove_wait_queue(&dev->r_wait, &wait);
	__set_current_state(TASK_RUNNING);
	return ret;
}

static ssize_t gfifo_write(struct file *filp, const char *buf, size_t size, loff_t *ppos)
{
	unsigned int count = size;
	size_t ret = 0;
	struct gfifo_dev *dev = filp->private_data;	
	DECLARE_WAITQUEUE(wait, current);

	mutex_lock(&dev->mutex);
	add_wait_queue(&dev->w_wait, &wait);

	while (dev->cur_len == GFIFO_SIZE) {
		if (filp->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto out;
		}
		__set_current_state(TASK_INTERRUPTIBLE);
		mutex_unlock(&dev->mutex);
		schedule();
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			goto out2;
		}
		mutex_lock(&dev->mutex);
	}

/*
	if (p >= GFIFO_SIZE)
		return 0;
	if (count > GFIFO_SIZE - p)
		count = GFIFO_SIZE - p;
*/
	if (count > GFIFO_SIZE - dev->cur_len)
		count = GFIFO_SIZE - dev->cur_len;

	if (copy_from_user(dev->mem + dev->cur_len, buf, count)) {
		ret = -EFAULT;
		goto out;
	}
	else {
		dev->cur_len += count;
		printk(KERN_INFO "write %u bytes, cur_len:%lu \n", count, dev->cur_len);
		wake_up_interruptible(&dev->r_wait);
		ret = count;
	}

out:
	mutex_unlock(&dev->mutex);
out2:
	remove_wait_queue(&dev->w_wait, &wait);
	__set_current_state(TASK_RUNNING);
	return ret;
}

static loff_t gfifo_llseek(struct file *filp, loff_t offset, int orig)
{
	loff_t ret = 0;
	switch (orig) {
	case 0:
		if (offset < 0) {
			ret = -EINVAL;
			break;
		}
		if ((unsigned int)offset > GFIFO_SIZE) {
			ret = -EINVAL;
			break;
		}
		filp->f_pos = (unsigned int)offset;
		ret = filp->f_pos;
		break;
	case 1:
		if ((filp->f_pos + offset) > GFIFO_SIZE) {
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

static struct file_operations gfifo_fops = {
	.owner = THIS_MODULE,
	.llseek = gfifo_llseek,
	.read = gfifo_read,
	.write = gfifo_write,
	.unlocked_ioctl = gfifo_ioctl,
	.open = gfifo_open,
	.release = gfifo_release,
};

static void gfifo_setup_cdev(struct gfifo_dev *dev, int index)
{
	int err, devno = MKDEV(gfifo_major, index);

	cdev_init(&dev->cdev, &gfifo_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add(&dev->cdev, devno, 1);
	if (err)
		printk(KERN_INFO "Error %d when add gfifo%d\n",err, index);
}

static int __init gfifo_init(void)
{
	int ret;
	dev_t devno = MKDEV(gfifo_major, 0);

	if (gfifo_major)
		ret = register_chrdev_region(devno, 1, "gfifo");
	else {
		ret = alloc_chrdev_region(&devno, 0, 1, "gfifo");
		gfifo_major = MAJOR(devno);
	}

	if (ret < 0)
		return ret;

	gfifo_devp = kzalloc(sizeof(struct gfifo_dev), GFP_KERNEL);
	if (!gfifo_devp) {
		ret = -ENOMEM;
		goto fail_malloc;
	}
	
	mutex_init(&gfifo_devp->mutex);
	init_waitqueue_head(&gfifo_devp->r_wait);
	init_waitqueue_head(&gfifo_devp->w_wait);

	gfifo_setup_cdev(gfifo_devp, 0);
	return 0;
fail_malloc:
	unregister_chrdev_region(devno, 1);
	return ret;
}
module_init(gfifo_init);

static void __exit gfifo_exit(void)
{
	cdev_del(&gfifo_devp->cdev);
	mutex_destroy(&gfifo_devp->mutex);
	kfree(gfifo_devp);
	unregister_chrdev_region(MKDEV(gfifo_major, 0), 1);
}
module_exit(gfifo_exit);

MODULE_AUTHOR("xin.ming@nokia-sbell.com");
MODULE_LICENSE("GPL v2");
