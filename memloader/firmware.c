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

#define DRIVER_VERSION			"1.6"

#include <linux/module.h>   /* Needed by all modules */
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/firmware.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include "kernel_compat.h"
#include "firmware.h"

/******************************************************************************
 * Defaults
 ******************************************************************************/
#define DEF_FIRMWARE_NAME	"fpga.bit"
#define DEF_BLOCKREAD		640 /* bytes */
#define DEF_BLOCKSIZE		20  /* bytes */
#define DEF_BLOCKDELAY		10  /* us */

/******************************************************************************
 * Firmware states
 ******************************************************************************/
#define FW_EMPTY		0
#define FW_LOADING		1
#define FW_LOADED		2
#define FW_FAILED		3
#define FW_DELETED		4

/******************************************************************************/
static char version_firmware[] = DRIVER_VERSION;
module_param_string(version_firmware, version_firmware, sizeof(version_firmware), 0444);
MODULE_PARM_DESC(version_firmware, "Firmware driver version");

static DEFINE_MUTEX(serial_lock);

/******************************************************************************/
int fw_load_device_firmware(struct fw_context *ctx, int load)
{
	const struct firmware	*fw;
	int			prev_delay_count, prev_read_count, curr_count;
	int			ret;
	unsigned long		jiffies_begin = 0, jiffies_end = 0;
	unsigned int		msec;

	if (!load) {
		/* If the firmware is loading, it will be cancelled here */
		ctx->state = FW_EMPTY;
		return 0; /* no error */
	}
	/* Userspace wants to load the firmware */
	dev_dbg(ctx->dev, "Requesting firmware '%s'\n", ctx->filename);

	mutex_lock(&serial_lock);

	switch (ctx->state) {
	case FW_LOADING:
	case FW_LOADED:
		mutex_unlock(&serial_lock);
		dev_err(ctx->dev, "Firmware '%s' loading or already loaded\n", ctx->filename);
		return -EINVAL;

	case FW_DELETED:
		mutex_unlock(&serial_lock);
		dev_err(ctx->dev, "Firmware '%s' loading disabled\n", ctx->filename);
		return -EINVAL;
	}

	ctx->state = FW_LOADING;
	ctx->current_count = 0;
	ctx->total_count = 0;
	prev_delay_count = 0;
	prev_read_count = 0;
	curr_count = 0;

	/* Request firmware from userspace */
	ret = request_firmware(&fw, ctx->filename, ctx->dev);
	if (ret) {
		ctx->state = FW_FAILED;
		mutex_unlock(&serial_lock);
		dev_err(ctx->dev, "Failed to request firmware '%s': err %d\n", ctx->filename, ret);
		return ret;
	}

	ctx->fw   = fw;
	ctx->data = fw->data;
	ctx->total_count = (int)fw->size;

	dev_dbg(ctx->dev, "Loading firmware '%s' with size %lu, devicemask 0x%2.2x, blockread %d, blocksize %d, blockdelay %d us\n",
		ctx->filename, (long unsigned int)fw->size, ctx->devicemask, ctx->blockread, ctx->blocksize, ctx->blockdelay);

	/* Load firmware */
	if (ctx->start != NULL) {
		ret = ctx->start(ctx);
	}

	jiffies_begin = jiffies;
	if ((ctx->write != NULL) && (ret >= 0)) {
		for (ctx->current_count = 0; (ctx->current_count < ctx->total_count) && (ctx->state == FW_LOADING);) {
			/* Call the write operation. 
			 * More then one byte can be written in one call.
			 * If zero is returned this indicates an EOF by the hardware.
			 */
			ret = ctx->write(ctx);
			if (ret <= 0)
				break;

			ctx->current_count += ret;
			curr_count += ret;

			if ((ctx->read != NULL) && (ctx->blockread > 0) && (curr_count - prev_read_count >= ctx->blockread)) {
				/* It's time to do some reading */
				ret = ctx->read(ctx);
				if (ret < 0)
					break;
				prev_read_count = curr_count;
			}

			if ((ctx->blocksize > 0) && (curr_count - prev_delay_count >= ctx->blocksize)) {
				/* It's time to have a nap */
				usleep_range(ctx->blockdelay, ctx->blockdelay);
				prev_delay_count = curr_count;
			}
		}
	}
	jiffies_end = jiffies;

	/* At this point the firmware load could have been cancelled from another thread.
	 * So we must check the state next to ret (state will be FW_EMPTY if cancelled, FW_LOADING if not cancelled).
	 */
	if (ret < 0)
		ctx->state = FW_FAILED; /* start or write failed */

	if (ctx->end != NULL) {
		ret = ctx->end(ctx, ctx->state != FW_LOADING);
		if (ret < 0)
			ctx->state = FW_FAILED; /* end failed */
	}

	/* Release firmware */
	release_firmware(fw);

	switch (ctx->state) {
	case FW_LOADING:
		msec = jiffies_to_msecs(jiffies_end - jiffies_begin);
		ctx->state = FW_LOADED;
		dev_dbg(ctx->dev, "Firmware '%s' loading successfull: %d bytes transferred in %d ms (%d kB/s)\n",
			ctx->filename, ctx->current_count, msec, msec ? (ctx->current_count / msec) : 0);
		ret = 0;
		break;

	case FW_EMPTY:
	case FW_DELETED:
		dev_dbg(ctx->dev, "Firmware '%s' loading cancelled: %d/%d bytes transferred\n", ctx->filename, ctx->current_count, ctx->total_count);
		ret = -EIO;
		break;

	case FW_FAILED:
		dev_err(ctx->dev, "Firmware '%s' loading failed: %d/%d bytes transferred, err %d\n", ctx->filename, ctx->current_count, ctx->total_count, ret);
		break;

	case FW_LOADED: /* should never occur */
	default:
		ctx->state = FW_EMPTY;
		dev_err(ctx->dev, "Firmware '%s' loading error: err %d\n", ctx->filename, ret);
		break;
	}

	mutex_unlock(&serial_lock);

	return ret;
}

/******************************************************************************/
int fw_load_firmware(struct fw_context *ctx, const char *buf, size_t count)
{
	long val = simple_strtol(buf, NULL, 10);

	return fw_load_device_firmware(ctx, (int)val);
}

/******************************************************************************/
int fw_set_filename(struct fw_context *ctx, const char *buf, size_t count)
{
	int	n;

	/* Strip trailing spaces, ... */
	for (n = (int)count - 1; n >= 0; n--) {
		if ((buf[n] != '\r') && (buf[n] != '\n') && (buf[n] != '\t') && (buf[n] != ' '))
			break;
	}

	if (n < 0) {
		dev_err(ctx->dev, "Empty filename given\n");
		return -EINVAL;
	}

	count = n + 1;

	if (count >= sizeof(ctx->filename)) {
		dev_err(ctx->dev, "Filename too long (max=%d)\n", (int)sizeof(ctx->filename) - 1);
		return -EINVAL;
	}

	memcpy(ctx->filename, buf, count);
	ctx->filename[count] = '\0';

	return 0;
}

/******************************************************************************/
int fw_set_blockread(struct fw_context *ctx, const char *buf, size_t count)
{
	ctx->blockread = (int)simple_strtol(buf, NULL, 10);

	return 0;
}

/******************************************************************************/
int fw_set_blocksize(struct fw_context *ctx, const char *buf, size_t count)
{
	ctx->blocksize = (int)simple_strtol(buf, NULL, 10);

	return 0;
}

/******************************************************************************/
int fw_set_delay(struct fw_context *ctx, const char *buf, size_t count)
{
	ctx->blockdelay = (int)simple_strtol(buf, NULL, 10);

	return 0;
}

/******************************************************************************/
int fw_set_devicemask(struct fw_context *ctx, const char *buf, size_t count)
{
	ctx->devicemask = (int)simple_strtol(buf, NULL, 10);

	return 0;
}

/******************************************************************************/
ssize_t fw_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct fw_context	*ctx = container_of(kobj, struct fw_context, kobj);
	ssize_t			ret = -EIO;

	buf[0] = '\0';

	if (strcmp(attr->name, "state") == 0) {
		return sprintf(buf, "%d\n", ctx->state);
	}
	else if (strcmp(attr->name, "filename") == 0) {
		return sprintf(buf, "%s\n", ctx->filename);
	}
	else if (strcmp(attr->name, "blockread") == 0) {
		return sprintf(buf, "%d\n", ctx->blockread);
	}
	else if (strcmp(attr->name, "blocksize") == 0) {
		return sprintf(buf, "%d\n", ctx->blocksize);
	}
	else if (strcmp(attr->name, "blockdelay") == 0) {
		return sprintf(buf, "%d\n", ctx->blockdelay);
	}
	else if (strcmp(attr->name, "devicemask") == 0) {
		return sprintf(buf, "%d\n", ctx->devicemask);
	}
	else if (strcmp(attr->name, "progress_pct") == 0) {
		int	cur, tot;
		/* Do not use mutex lock here otherwise we would block until firmware is loaded. */
		cur = ctx->current_count;
		tot = ctx->total_count;
		return sprintf(buf, "%d\n", tot ? (100 * cur / tot) : 0);
	}
	else if (strcmp(attr->name, "progress_size") == 0) {
		return sprintf(buf, "%d\n", ctx->current_count);
	}
	else if (strcmp(attr->name, "total_size") == 0) {
		return sprintf(buf, "%d\n", ctx->total_count);
	}

	return ret;
}

/******************************************************************************/
ssize_t fw_store(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count)
{
	struct fw_context	*ctx = container_of(kobj, struct fw_context, kobj);
	ssize_t			ret = -EIO;

	if (strcmp(attr->name, "state") == 0) {
		ret = fw_load_firmware(ctx, buf, count);
	}
	else if (strcmp(attr->name, "filename") == 0) {
		ret = fw_set_filename(ctx, buf, count);
	}
	else if (strcmp(attr->name, "blockread") == 0) {
		ret = fw_set_blockread(ctx, buf, count);
	}
	else if (strcmp(attr->name, "blocksize") == 0) {
		ret = fw_set_blocksize(ctx, buf, count);
	}
	else if (strcmp(attr->name, "blockdelay") == 0) {
		ret = fw_set_delay(ctx, buf, count);
	}
	else if (strcmp(attr->name, "devicemask") == 0) {
		ret = fw_set_devicemask(ctx, buf, count);
	}

	return (ret < 0) ? ret : count;
}

/******************************************************************************/
static void fw_release(struct kobject *kobj)
{
}

/******************************************************************************/
static struct sysfs_ops fw_ops = {
	.show		= fw_show,
	.store		= fw_store,
};

static struct attribute state_attr		= SYSFS_ATTR(state,		S_IRUGO | S_IWUGO);
static struct attribute filename_attr		= SYSFS_ATTR(filename,		S_IRUGO | S_IWUGO);
static struct attribute blockread_attr		= SYSFS_ATTR(blockread,		S_IRUGO | S_IWUGO);
static struct attribute blocksize_attr		= SYSFS_ATTR(blocksize,		S_IRUGO | S_IWUGO);
static struct attribute delay_attr		= SYSFS_ATTR(blockdelay,	S_IRUGO | S_IWUGO);
static struct attribute devicemask_attr		= SYSFS_ATTR(devicemask,	S_IRUGO | S_IWUGO);
static struct attribute progress_pct_attr	= SYSFS_ATTR(progress_pct,	S_IRUGO);
static struct attribute progress_size_attr	= SYSFS_ATTR(progress_size,	S_IRUGO);
static struct attribute total_size_attr		= SYSFS_ATTR(total_size,	S_IRUGO);

static struct attribute *fw_attrs[] = {
	&state_attr,
	&filename_attr,
	&blockread_attr,
	&blocksize_attr,
	&delay_attr,
	&devicemask_attr,
	&progress_pct_attr,
	&progress_size_attr,
	&total_size_attr,
	NULL
};

static struct kobj_type fw_kobj_type = {
	.release	= fw_release,
	.sysfs_ops	= &fw_ops,
	.default_attrs	= fw_attrs,
};

/******************************************************************************/
int fw_get_parameters(struct fw_context *ctx, struct device *dev)
{
	const __be32		*of_p;
	struct device_node	*of_target;
	struct platform_device	*pdev_target;
	int			ret, len;

	if ((ctx == NULL) || (dev == NULL))
		return -EINVAL;

	/* Check if firmware needs to be loaded */
	of_p = of_get_property(dev->of_node, "firmware-load", NULL);
	if (of_p == NULL) {
		/* No need to load firmware. This not an error so return zero. */
		return 0;
	}

	/* Get firmware target */
	of_p = of_get_property(dev->of_node, "firmware-target", NULL);
	if (of_p != NULL) {
		/* Get referenced target */
		of_target = of_find_node_by_phandle(be32_to_cpup(of_p));
		if (of_target == NULL) {
			dev_err(dev, "target node referenced by node %s does not exist\n", dev->of_node->full_name);
			return -ENODEV;
		}

		/* Get corresponding platform device */
		pdev_target = of_find_device_by_node(of_target);
		if (pdev_target == NULL) {
			dev_err(dev, "target device of node %s does not exist\n", of_target->full_name);
			return -ENODEV;
		}

		/* Override or set target device */
		ctx->dev = &pdev_target->dev;
	}

	of_p = of_get_property(dev->of_node, "firmware-blockread", NULL);
	if (of_p != NULL)
		ctx->blockread = be32_to_cpup(of_p);

	of_p = of_get_property(dev->of_node, "firmware-blocksize", NULL);
	if (of_p != NULL)
		ctx->blocksize = be32_to_cpup(of_p);

	of_p = of_get_property(dev->of_node, "firmware-blockdelay", NULL);
	if (of_p != NULL)
		ctx->blockdelay = be32_to_cpup(of_p);

	of_p = of_get_property(dev->of_node, "firmware-filename", &len);
	if (of_p != NULL) {
		ret = fw_set_filename(ctx, (char *)of_p, len);
		if (ret < 0)
			return ret;
	}

	/* Firmware needs to be loaded so return a positive value. */
	return 1;
}

/******************************************************************************/
int fw_initialize(struct fw_context *ctx, struct device *dev_configuration, struct device *dev_target)
{
	int	ret;

	if ((ctx == NULL) || (dev_configuration == NULL))
		return -EINVAL;

	/* Set hardcoded defaults */
	strcpy(ctx->filename, DEF_FIRMWARE_NAME);
	ctx->blockread = DEF_BLOCKREAD;
	ctx->blocksize = DEF_BLOCKSIZE;
	ctx->blockdelay = DEF_BLOCKDELAY;
	ctx->devicemask = 0xff;
	ctx->dev = dev_target; /* can be NULL */
	ctx->state = FW_EMPTY;

	/* Override defaults with the values from device tree */
	ret = fw_get_parameters(ctx, dev_configuration);
	if (ret <= 0)
		return ret; /* error or no firmware loading */

	/* Check device */
	if (ctx->dev == NULL) {
		dev_err(dev_configuration, "no target device given or configured\n");
		return -ENODEV;
	}

	/* Initialize kobject if not yet initialized */
	if (!ctx->kobj_init) {
		mutex_init(&serial_lock);
		kobject_init(&ctx->kobj, &fw_kobj_type);
		ctx->kobj_init = 1;
	}

	/* Add kobject if not yet added (will create entry in sysfs) */
	if (!ctx->kobj_added) {
		ret = kobject_add(&ctx->kobj, &ctx->dev->kobj, "fw");
		if (ret) {
			dev_err(dev_configuration, "cannot add kobject resource (ret %d)\n", ret);
			return ret;
		}
		ctx->kobj_added = 1;
	}

	dev_dbg(dev_configuration, "firmware loading initialized\n");

	return 0;
}

/******************************************************************************/
void fw_cleanup(struct fw_context *ctx)
{
	if ((ctx != NULL) && ctx->kobj_added) {
		dev_dbg(ctx->dev, "cleanup firmware loading\n");

		/* Cancel possible firmware loading */
		ctx->state = FW_DELETED;

		ctx->kobj_added = 0;
		kobject_del(&ctx->kobj);
	}
}
