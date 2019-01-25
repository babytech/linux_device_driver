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
*******************************************************************************/

#define DRIVER_VERSION      "0.0.4"

#include <linux/module.h>		/* Needed by all modules */
#include <linux/version.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/highmem.h>
#include <asm/io.h>
#include "kernel_compat.h"
#include "firmware.h"

struct memloader_info {
	struct kobject kobj;
	struct platform_device *pdev;
	struct fw_context fwctx;
	__be64 load_base_address;
	int current_count;
};

/******************************************************************************
 * Module parameters
 ******************************************************************************/
static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Enable debug logging");

/******************************************************************************
 * Function interface
 ******************************************************************************/
static int memloader_load(__be64 phy_address, const unsigned char *data, int count)
{
	__be32 __iomem *base;

	if (!data || count <= 0)
		return -EINVAL;

	base = ioremap(phy_address, count);
	if (!base)
		return -EBUSY;

	memcpy(base, data, count);
	iounmap(base);

	return count;
}

int memloader_start(const struct fw_context *ctx)
{
	struct memloader_info *info = container_of(ctx, struct memloader_info, fwctx);

	if (debug)
		dev_dbg(&info->pdev->dev, "filename %s, size %d\n", ctx->filename, ctx->total_count);

	info->current_count = 0;

	return 0;
}

int memloader_write(const struct fw_context *ctx)
{
	struct memloader_info *info = container_of(ctx, struct memloader_info, fwctx);
	int remaint = ctx->total_count - ctx->current_count;
	int count;
	int ret;
	int total_copy  = 0;
	void *page_data = NULL;
	int page_nr	 = 0;

	count = remaint;
	if (ctx->blocksize)
		count = ctx->blocksize;

	if (count > remaint)
		count = remaint;

	if( ctx->data ) {
		/* if 'vmalloc()' or 'vmap()' OK in 'request_firmware()->...' */
		ret = memloader_load(info->load_base_address + ctx->current_count, ctx->data + ctx->current_count, count);
		if (ret > 0) {
			info->current_count += ret;
			if (debug)
				dev_dbg(&info->pdev->dev, "write bytes %d\n", ret);
		} else {
			dev_info(&info->pdev->dev, "memloader_load failed with error %d\n", ret);
		}
		return ret;
	}
	else if ( ctx->fw->pages != NULL ) {
		/*  if 'vmalloc()' and 'vmap()' KO, but phy pages OK :
			fw_load_device_firmware()->request_firmware()->_request_firmware() {
				if( FAILED(fw_get_filesystem_firmware()) ) {
					fw_load_from_user_helper()
				}
			}
			fw_get_filesystem_firmware() will alloc large virtual address space, if failed,
			fw_load_from_user_helper() will be called and uevent sent to user space.
			User sapce callback of /proc/hotplug( such as mdev ) will write the firmware file to kernel,
			kernel stores the data in phycial pages, but will not return virtual address.
			We need handle this scenario.
		*/
		while (count) {
			page_nr = info->current_count >> PAGE_SHIFT;
			page_data = kmap(ctx->fw->pages[page_nr]);
			ret = memloader_load(info->load_base_address + info->current_count, page_data, min_t(size_t, PAGE_SIZE, count));
			if (ret > 0) {
				info->current_count += ret;
				count -= ret;
				total_copy += ret;
			}
			else {
				dev_info(&info->pdev->dev, "write pages[%d] to '0x%llx + %d' failed, ret: %d\n",
					page_nr, info->load_base_address, info->current_count, ret );
				return -ENOMEM;
			}

			kunmap(ctx->fw->pages[page_nr]);
		}
		dev_dbg(&info->pdev->dev, "write bytes %d\n", total_copy);
		return total_copy;
	}

	return -EINVAL;
}

int memloader_end(const struct fw_context *ctx, int error)
{
	struct memloader_info *info = container_of(ctx, struct memloader_info, fwctx);

	if (debug)
		dev_dbg(&info->pdev->dev, "load data size %d,%d\n", info->current_count, ctx->current_count);

	if (info->current_count != ctx->current_count)
		return -EIO;

	return 0;
}

/******************************************************************************/
void memloader_release(struct kobject *kobj)
{
}

/******************************************************************************/
static int memloader_set_load_address(struct memloader_info *info, const char *buf, size_t count)
{
	long val = simple_strtol(buf, NULL, 16);
	info->load_base_address = (__be64) val;
	return 0;
}

/******************************************************************************/
ssize_t memloader_show(struct kobject * kobj, struct attribute * attr, char *buf)
{
	struct memloader_info *info = container_of(kobj, struct memloader_info, kobj);

	if (strcmp(attr->name, "load-address") == 0)
		return sprintf(buf, "0x%llx\n", info->load_base_address);
	else if (strcmp(attr->name, "load-size") == 0)
		return sprintf(buf, "0x%x\n", info->current_count);
	return -EIO;
}

/******************************************************************************/
ssize_t memloader_store(struct kobject * kobj, struct attribute * attr, const char *buf, size_t count)
{
	struct memloader_info *info = container_of(kobj, struct memloader_info, kobj);
	int ret = -EINVAL;

	if (strcmp(attr->name, "load-address") == 0)
		ret = memloader_set_load_address(info, buf, count);

	return ret < 0 ? -EINVAL : count;
}

/******************************************************************************/
static struct attribute load_base_address_attr = SYSFS_ATTR(load-address, S_IRUGO | S_IWUGO);
static struct attribute load_size_attr = SYSFS_ATTR(load-size, S_IRUGO);

static struct attribute *memloader_attrs[] = {
	&load_base_address_attr,	/* load address info */
	&load_size_attr,		/* load address size */
	NULL
};

static struct sysfs_ops memloader_sysfs_ops = {
	.show = memloader_show,
	.store = memloader_store
};

static struct kobj_type memloader_kobj_type = {
	.release = memloader_release,
	.sysfs_ops = &memloader_sysfs_ops,
	.default_attrs = memloader_attrs,
};

/******************************************************************************
 * Device tree remove function.
 ******************************************************************************/
int memloader_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct memloader_info *memloader = platform_get_drvdata(pdev);

	kobject_put(&memloader->kobj);
	fw_cleanup(&memloader->fwctx);
	devm_kfree(dev, memloader);

	return 0;
}

/******************************************************************************
 * Device tree probing function.
 ******************************************************************************/
int memloader_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct memloader_info *memloader;
	const __be64 *of_p;
	int ret = 0;

	/* Allocate memloader data structure */
	memloader = devm_kzalloc(dev, sizeof(*memloader), GFP_KERNEL);
	if (memloader == NULL) {
		dev_err(dev, "cannot allocate memloader resource\n");
		return -ENOMEM;
	}

	of_p = of_get_property(dev->of_node, "load-address", NULL);
	if (of_p == NULL) {
		dev_err(dev, "cannot get the load address\n");
		ret = -EINVAL;
		goto free_mem;
	}

	memloader->load_base_address = be64_to_cpup(of_p);
	memloader->pdev = pdev;
	platform_set_drvdata(pdev, memloader);

	memloader->fwctx.start = memloader_start;
	memloader->fwctx.write = memloader_write;
	memloader->fwctx.end = memloader_end;

	ret = fw_initialize(&memloader->fwctx, dev, dev);
	if (ret < 0) {
		dev_err(dev, "cannot initialize driver firmware loading\n");
		goto free_mem;
	}

	ret = kobject_init_and_add(&memloader->kobj, &memloader_kobj_type, &dev->kobj, "memloader");
	if (ret) {
		dev_err(dev, "cannot add kobject resource\n");
		goto fw_clean;
	}

	printk(KERN_INFO "%s: has been run OK\n", __func__);
	return 0;

  fw_clean:
	fw_cleanup(&memloader->fwctx);
  free_mem:
	devm_kfree(dev, memloader);

	return ret;
}

/******************************************************************************/
static const struct of_device_id memloader_ids[] = {
	{
		.compatible = "memloader",
	},
	{},
};

static struct platform_driver memloader_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "memloader",
		.of_match_table = memloader_ids,
	},
	.probe = memloader_probe,
	.remove = memloader_remove,
};

static int __init memloader_init(void)
{
	return platform_driver_register(&memloader_driver);
}

static void __exit memloader_exit(void)
{
	platform_driver_unregister(&memloader_driver);
}

module_init(memloader_init);
module_exit(memloader_exit);

/******************************************************************************/
MODULE_AUTHOR("PAN Guolin <Guolin.Pan@nokia-sbell.com>");
MODULE_DESCRIPTION("Memory Loader");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);
