/******************************************************************************
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
*******************************************************************************/
#define DRIVER_VERSION			"1.3"

#include <linux/module.h>   /* Needed by all modules */
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/workqueue.h>
#include "generic_access.h"

/* Redefine to even lower level atomic accesses (protected by a spinlock) */
#define dev_read(addr)					ga_reg_read8(addr, 0xff)
#define dev_read8(addr)					ga_reg_read8(addr, 0xff)
#define dev_read16(addr)				ga_reg_read16(addr, 0xffff)
#define dev_write(addr, val)				ga_reg_write8(addr, 0xff, val, GA_WRITE)
#define dev_write8(addr, val)				ga_reg_write8(addr, 0xff, val, GA_WRITE)
#define dev_write16(addr, val)				ga_reg_write16(addr, 0xffff, val, GA_WRITE)
/* Read from addr, disable bits in mask, update bits in upd and write to addr */
#define dev_read_modify_write(addr, mask, upd)		ga_reg_write8(addr, mask, upd, GA_READ_WRITE_ALWAYS) /* forced write */
#define dev_read_modify_write_cond(addr, mask, upd)	ga_reg_write8(addr, mask, upd, GA_READ_WRITE_CONDITIONAL) 

/******************************************************************************/
#define DEV_WATCHDOG_KICK_INTERVAL	1000 /* ms */

/******************************************************************************/
#define WD_PROP_KICK_OFFSET	0
#define WD_PROP_KICK_VALUE	1
#define WD_PROP_ENABLE_OFFSET	2
#define WD_PROP_ENABLE_VALUE	3
#define WD_PROP_COUNT		4

#define WD_PROP_NONE		-1

#define WD_STATE_UNKNOWN	0 /* state register not configured */
#define WD_STATE_DISABLED	1
#define WD_STATE_ENABLED	2

struct dev_watchdog_prop {
	int		mandatory;
	const char	*property;
};

/******************************************************************************/
struct dev_watchdog {
	struct timer_list	timer;
	struct workqueue_struct	*workqueue;
	struct work_struct	work;
	struct device		*dev;
	void __iomem		*reg_base;
	int			use_workqueue;
	int			props[WD_PROP_COUNT];
	int			misc_registered:2;
	int			timer_running:2;
	int			wd_state:4;
};
static struct dev_watchdog priv;

/******************************************************************************/
static char version[] = DRIVER_VERSION;
module_param_string(version, version, sizeof(version), 0444);
MODULE_PARM_DESC(version, "Module version");

static int kick_interval = DEV_WATCHDOG_KICK_INTERVAL;
module_param(kick_interval, int, 0644);
MODULE_PARM_DESC(kick_interval, "DEV watchdog kick interval in ms");

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Enable debug logging");

/******************************************************************************/
static const char *wd_state_str[] = {
	[WD_STATE_UNKNOWN]  = "unknown",
	[WD_STATE_DISABLED] = "disabled",
	[WD_STATE_ENABLED]  = "enabled",
};
static const struct dev_watchdog_prop watchdog_props[WD_PROP_COUNT] = {
	[WD_PROP_KICK_OFFSET]	= {1,	"reg-kick-offset"},
	[WD_PROP_KICK_VALUE]	= {1,	"reg-kick-value"},
	[WD_PROP_ENABLE_OFFSET]	= {0,	"reg-enable-offset"},
	[WD_PROP_ENABLE_VALUE]	= {0,	"reg-enable-value"},
};

/******************************************************************************/
static void dev_watchdog_kick(void)
{
	if (debug)
		dev_dbg(priv.dev, "dev_watchdog: kick\n");

	dev_write(priv.reg_base + priv.props[WD_PROP_KICK_OFFSET], priv.props[WD_PROP_KICK_VALUE]);
}

/******************************************************************************/
static void dev_watchdog_enable(int enable)
{
	if ((priv.props[WD_PROP_ENABLE_OFFSET] != WD_PROP_NONE) && (priv.props[WD_PROP_ENABLE_VALUE] != WD_PROP_NONE)) {
		int reg;

		/* Set the watchdog state. If no GICI is attached, this will have no effect. */
		dev_read_modify_write(priv.reg_base + priv.props[WD_PROP_ENABLE_OFFSET],
			priv.props[WD_PROP_ENABLE_VALUE], enable ? priv.props[WD_PROP_ENABLE_VALUE] : 0x00);

		/* Read back the actual state to handle the case where no GICI is attached. */
		reg = dev_read(priv.reg_base + priv.props[WD_PROP_ENABLE_OFFSET]);
		priv.wd_state = (reg & priv.props[WD_PROP_ENABLE_VALUE]) ? WD_STATE_ENABLED : WD_STATE_DISABLED;
	}

	if (debug)
		dev_dbg(priv.dev, "dev_watchdog: state: %s\n", wd_state_str[priv.wd_state]);
}

/******************************************************************************/
static void dev_watchdog_read_enable(void)
{
	priv.wd_state = WD_STATE_UNKNOWN;

	if ((priv.props[WD_PROP_ENABLE_OFFSET] != WD_PROP_NONE) && (priv.props[WD_PROP_ENABLE_VALUE] != WD_PROP_NONE)) {
		int reg = dev_read(priv.reg_base + priv.props[WD_PROP_ENABLE_OFFSET]);
		priv.wd_state = (reg & priv.props[WD_PROP_ENABLE_VALUE]) ? WD_STATE_ENABLED : WD_STATE_DISABLED;
	}

	dev_dbg(priv.dev, "dev_watchdog: initial state: %s\n", wd_state_str[priv.wd_state]);
}

/******************************************************************************/
static void dev_watchdog_start(void)
{
	if (debug)
		dev_dbg(priv.dev, "dev_watchdog: start kicking\n");

	priv.timer_running = 1;
	mod_timer(&priv.timer, jiffies + msecs_to_jiffies(kick_interval));
}

/******************************************************************************/
static void dev_watchdog_stop(void)
{
	if (debug)
		dev_dbg(priv.dev, "dev_watchdog: stop kicking\n");

	priv.timer_running = 0;
	del_timer_sync(&priv.timer);
}

/******************************************************************************/
static void dev_watchdog_work(struct work_struct *work)
{
	dev_watchdog_kick();
}

/******************************************************************************/
static void dev_watchdog_timeout(unsigned long data)
{
	if (!priv.timer_running)
		return;

	if (priv.use_workqueue != 0)
		queue_work(priv.workqueue, &priv.work);
	else
		dev_watchdog_kick();

	dev_watchdog_start();
}

/******************************************************************************/
int dev_watchdog_open(struct inode *inode, struct file *file)
{
	file->private_data = &priv;
	return nonseekable_open(inode, file);
}

/******************************************************************************/
int dev_watchdog_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

/******************************************************************************/
ssize_t dev_watchdog_write(struct file *file, const char __user *data, size_t len, loff_t *ppos)
{
	char	c;

	if (get_user(c, data)) {
		dev_err(priv.dev, "dev_watchdog: cannot copy from user\n");
		return -EFAULT;
	}

	if (debug)
		dev_dbg(priv.dev, "dev_watchdog: execute command '%c' (0x%x)\n", c, (int)c);

	switch (c) {
	case '\0':
	case '0':
	case 'q':
	case 'Q': /* Quit */
		/* Stop the timer which automatically kicks the watchdog */
		dev_watchdog_stop();
		break;

	case '\1':
	case '1':
	case 'k':
	case 'K': /* Kick */
		/* Kick the watchdog once */
		dev_watchdog_kick();
		break;

	case '\2':
	case '2':
	case 's':
	case 'S': /* Start */
		/* Start the timer which automatically kicks the watchdog */
		dev_watchdog_start();
		break;

	case '\3':
	case '3':
	case 'e':
	case 'E': /* Enable */
		/* Enable the watchdog */
		dev_watchdog_enable(1);
		break;

	case '\4':
	case '4':
	case 'd':
	case 'D': /* Disable */
		/* Disable the watchdog */
		dev_watchdog_enable(0);
		break;

	default:
		dev_err(priv.dev, "dev_watchdog: invalid command '%c' (0x%x)\n", c, (int)c);
		return -EINVAL;
	}

	return len;
}

/******************************************************************************/
ssize_t dev_watchdog_read(struct file *file, char __user *data, size_t len, loff_t *ppos)
{
	char	str[16];

	if (file->private_data == NULL)
		return 0; /* return EOF */
	file->private_data = NULL; /* return EOF with next read */

	len = sprintf(str, "t=%d s=%c\n", priv.timer_running, wd_state_str[priv.wd_state][0]);

	if (debug)
		dev_dbg(priv.dev, "dev_watchdog: read state: %s\n", str);

	if (copy_to_user(data, str, len)) {
		dev_err(priv.dev, "dev_watchdog: cannot copy to user (len %d)\n", (int)len);
		return -EFAULT;
	}

	return len;
}

/******************************************************************************/
static const struct file_operations dev_watchdog_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.open		= dev_watchdog_open,
	.release	= dev_watchdog_release,
	.write		= dev_watchdog_write,
	.read		= dev_watchdog_read,
};

static struct miscdevice dev_watchdog_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &dev_watchdog_fops,
};

/******************************************************************************/
int dev_watchdog_remove(struct platform_device *pdev)
{
	struct device	*dev = &pdev->dev;

	dev_dbg(dev, "dev_watchdog: driver remove version %s\n", DRIVER_VERSION);

	dev_watchdog_stop();

	if (priv.workqueue != NULL)
		destroy_workqueue(priv.workqueue);

	if (priv.misc_registered) {
		misc_deregister(&dev_watchdog_miscdev);
		priv.misc_registered = 0;
	}

	if (priv.reg_base != NULL) {
		iounmap(priv.reg_base);
		priv.reg_base = NULL;
	}

	return 0;
}

/******************************************************************************/
int dev_watchdog_probe(struct platform_device *pdev)
{
	struct device		*dev = &pdev->dev;
	const __be32		*of_p;
	char			str[256];
	int			i, len, ret = 0;

	dev_dbg(dev, "dev_watchdog: driver probe version %s\n", DRIVER_VERSION);

	if (priv.reg_base != NULL) {
		dev_err(dev, "dev_watchdog: already probed\n");
		return -EIO;
	}
	priv.dev = dev;

	/* Map memory */
	priv.reg_base = of_iomap(dev->of_node, 0);
	if (priv.reg_base == NULL) {
		dev_err(dev, "dev_watchdog: cannot map memory\n");
		ret = -EIO;
		goto err;
	}

	/* Get properties */
	for (i = 0; i < WD_PROP_COUNT; i++) {
		of_p = of_get_property(dev->of_node, watchdog_props[i].property, &len);
		if (of_p != NULL) {
			if (len != sizeof(int)) {
				dev_err(dev, "dev_watchdog: invalid %s property (len %d)\n", watchdog_props[i].property, len);
				ret = -EINVAL;
				goto err;
			}
			priv.props[i] = be32_to_cpup(of_p);
		}
		else if (watchdog_props[i].mandatory) {
			dev_err(dev, "dev_watchdog: missing %s property\n", watchdog_props[i].property);
			ret = -EINVAL;
			goto err;
		}
		else {
			priv.props[i] = WD_PROP_NONE;
		}
	}

	/* Register misc device */
	dev_watchdog_miscdev.parent = dev;
	ret = misc_register(&dev_watchdog_miscdev);
	if (ret < 0) {
		dev_err(dev, "dev_watchdog: failed to register misc device\n");
		goto err;
	}
	priv.misc_registered = 1;

	of_p = of_get_property(dev->of_node, "use-workqueue", NULL);
	if (of_p != NULL)
		priv.use_workqueue = 1;

	/*
	 * In order for the priority of the created thread to be correct,
	 * its name should start with "kworker", cfr. kernel/kthread.c.
	 */
	priv.workqueue = create_singlethread_workqueue("kworker_wd");
	if (!priv.workqueue) {
		dev_err(dev, "dev_watchdog: failed to allocate workqueue\n");
		goto err;
	}

	INIT_WORK(&priv.work, dev_watchdog_work);

	for (len = 0, i = 0; i < WD_PROP_COUNT; i++) {
		if (priv.props[i] != WD_PROP_NONE)
			len += snprintf(&str[len], sizeof(str)-len-1, " %s=0x%x", watchdog_props[i].property, priv.props[i]);
	}
	dev_dbg(dev, "dev_watchdog: registered:%s\n", str);

	/* Read initial state */
	dev_watchdog_read_enable();

	return 0;

err:
	dev_watchdog_remove(pdev);
	return ret;
}

/******************************************************************************/
static const struct of_device_id dev_watchdog_match[] = {
	{.compatible = "dev-watchdog"},
	{},
};
MODULE_DEVICE_TABLE(of, dev_watchdog_match);

static struct platform_driver dev_watchdog_driver = {
	.probe      = dev_watchdog_probe,
	.remove     = dev_watchdog_remove,
	.driver     = {
		.name   = "dev_watchdog",
		.owner  = THIS_MODULE,
		.of_match_table = dev_watchdog_match,
	},
};

/******************************************************************************
 * Module initializations
 ******************************************************************************/
int __init dev_watchdog_init(void)
{
	printk(KERN_INFO "dev_watchdog: module init version %s\n", DRIVER_VERSION);

	memset(&priv, 0, sizeof(priv));

	setup_timer(&priv.timer, dev_watchdog_timeout, 0);

	return platform_driver_register(&dev_watchdog_driver);
}

/******************************************************************************/
void __exit dev_watchdog_exit(void)
{
	printk(KERN_INFO "dev_watchdog: module exit version %s\n", DRIVER_VERSION);

	platform_driver_unregister(&dev_watchdog_driver);
}

/******************************************************************************/
module_init(dev_watchdog_init);
module_exit(dev_watchdog_exit);

/******************************************************************************/
MODULE_AUTHOR("babytech@126.com");
MODULE_DESCRIPTION("Device watchdog driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("dev_watchdog");
MODULE_VERSION(DRIVER_VERSION);
