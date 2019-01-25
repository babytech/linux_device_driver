/*
 * a simple platform driver for support miscdevice for gfifo
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/sched/signal.h>

#define GFIFO_SIZE 0x100
#define MEM_CLEAR 0x1

struct gfifo_dev {
	struct cdev cdev;
	unsigned char mem[GFIFO_SIZE];
	struct mutex mutex;
	unsigned long cur_len;
	wait_queue_head_t r_wait;
	wait_queue_head_t w_wait;
	struct fasync_struct *async_queue;
	struct miscdevice miscdev;
};

static int gfifo_fasync(int fd, struct file *filp, int mode)
{
	struct gfifo_dev *dev = container_of(filp->private_data, struct gfifo_dev, miscdev);
	return fasync_helper(fd, filp, mode, &dev->async_queue);
}

static int gfifo_release(struct inode *inode, struct file *filp)
{
	gfifo_fasync(-1, filp, 0);
	return 0;
}

static long gfifo_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct gfifo_dev *dev = container_of(filp->private_data, struct gfifo_dev, miscdev);
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
	struct gfifo_dev *dev = container_of(filp->private_data, struct gfifo_dev, miscdev);
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
		if (dev->async_queue) {
			kill_fasync(&dev->async_queue, SIGIO, POLL_OUT);
			printk(KERN_DEBUG "%s kill SIGIO\n", __func__);
		}
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
	struct gfifo_dev *dev = container_of(filp->private_data, struct gfifo_dev, miscdev);
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
		if (dev->async_queue) {
			kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
			printk(KERN_DEBUG "%s kill SIGIO\n", __func__);
		}
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

static unsigned int gfifo_poll(struct file *filp, poll_table *p) 
{
	unsigned int mask = 0;
	struct gfifo_dev *dev = container_of(filp->private_data, struct gfifo_dev, miscdev);

	mutex_lock(&dev->mutex);

	poll_wait(filp, &dev->r_wait, p);
	poll_wait(filp, &dev->w_wait, p);

	if (dev->cur_len != 0)
		mask |= POLLIN|POLLRDNORM;
	if (dev->cur_len != GFIFO_SIZE)
		mask |= POLLOUT|POLLWRNORM;

	mutex_unlock(&dev->mutex);
	return mask;
}

static struct file_operations gfifo_fops = {
	.owner = THIS_MODULE,
	.llseek = gfifo_llseek,
	.read = gfifo_read,
	.write = gfifo_write,
	.unlocked_ioctl = gfifo_ioctl,
	.poll = gfifo_poll,
	.fasync = gfifo_fasync,
	.release = gfifo_release,
};

static int gfifo_probe(struct platform_device *pdev)
{
	int ret;
	struct gfifo_dev *dev;

	dev = kzalloc(sizeof(struct gfifo_dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	mutex_init(&dev->mutex);
	init_waitqueue_head(&dev->r_wait);
	init_waitqueue_head(&dev->w_wait);

	dev->miscdev.minor = MISC_DYNAMIC_MINOR;
	dev->miscdev.name = "gfifo";
	dev->miscdev.fops = &gfifo_fops;
	platform_set_drvdata(pdev,dev);

	ret = misc_register(&dev->miscdev);
	if (ret) {
		printk(KERN_ERR "%s: register misc device failed\n", __func__);
		return ret;
	}

	return 0;
}

static int gfifo_remove(struct platform_device *pdev)
{
	struct gfifo_dev *dev = platform_get_drvdata(pdev);

	misc_deregister(&dev->miscdev);
	mutex_destroy(&dev->mutex);
	kfree(dev);
	return 0;
}

static struct platform_driver gfifo_driver = {
	.driver = {
		.name = "gfifo",
		.owner = THIS_MODULE,
	},
	.probe = gfifo_probe,
	.remove = gfifo_remove,
};
module_platform_driver(gfifo_driver);

MODULE_AUTHOR("babytech@126.com");
MODULE_LICENSE("GPL v2");
