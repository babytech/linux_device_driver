/*
 * a simple platform device for gfifo
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#define DEVICE_NUM 0x10

static struct platform_device *gfifo_pdev[DEVICE_NUM];

static int __init gfifo_device_init(void)
{
	int ret, i;

	for (i = 0; i < DEVICE_NUM; i++) {
		gfifo_pdev[i] = platform_device_alloc("gfifo", i);
		if (!gfifo_pdev[i]) {
			printk(KERN_ERR "Failed to alloc gfifo%d!\n", i);
			return -ENOMEM;
		}

		ret = platform_device_add(gfifo_pdev[i]);
		if (ret) {
			platform_device_put(gfifo_pdev[i]);
			printk(KERN_ERR "Failed to add gfifo%d!\n", i);
			return ret;
		}
	}
	return 0;
}
module_init(gfifo_device_init);

static void __exit gfifo_device_exit(void)
{
	int i;
	for (i = 0; i < DEVICE_NUM; i++)
		platform_device_unregister(gfifo_pdev[i]);
}
module_exit(gfifo_device_exit);

MODULE_AUTHOR("babytech@126.com");
MODULE_LICENSE("GPL v2");
